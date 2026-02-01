#ifndef GLOBAL_SPACE_H
#define GLOBAL_SPACE_H

#include <algorithm>
#include <deque>
#include <forward_list>
#include <map>
#include <unordered_map>
#include <vector>

#include <cassert>
#include <iostream>
#include <ostream>
#include <memory>
#include <type_traits>

#ifdef CONFIG_PARALLEL
#include <atomic>
#include <mutex>
#include <tbb/concurrent_unordered_map.h>
#include <tbb/concurrent_queue.h>
#include <tbb/spin_rw_mutex.h>
#include <tbb/parallel_for.h>
#include <tbb/task_arena.h>
#endif

#include "config.h"
#include "problem.hpp"
#include "global/state_space_data.hpp"
#include "clock.hpp"
#include "mem.hpp"

#include "global/node.hpp"
#include "global/state.hpp"
#include "object_pool.hpp"
#include "global/exploration_statistics.hpp"
#include "global/resource_monitor.hpp"
#include "global/states_manager.hpp"
#include "logger.hpp"

#ifdef CONFIG_PRUNING
#include "global/secateur.hpp"
#endif

#ifdef CONFIG_ANALYSIS_EXTENSIONS
#include "global/extension/mk-firm/mk_extension.hpp"
#include "global/extension/taskchains/taskchains_extension.hpp"
#endif // CONFIG_ANALYSIS_EXTENSIONS

namespace NP {

	namespace Global {

		template<class Time> class State_space
		{
		public:

			typedef Scheduling_problem<Time> Problem;
			using Workload = typename Scheduling_problem<Time>::Workload;
			using Precedence_constraints = typename Scheduling_problem<Time>::Precedence_constraints;
			using Mutex_constraints = typename Scheduling_problem<Time>::Mutex_constraints;
			using Abort_actions = typename Scheduling_problem<Time>::Abort_actions;
			typedef typename std::vector<Interval<Time>> CoreAvailability;

			typedef Schedule_node<Time> Node;
			typedef std::shared_ptr<Node> Node_ref;
			typedef Schedule_state<Time> State;
			typedef std::shared_ptr<State> State_ref;

			/**
			 * @brief Explore the state space for the given problem with the given analysis options and logging options.
			 */
			static std::unique_ptr<State_space> explore(
				const Problem& prob,
				const Analysis_options& opts
#ifdef CONFIG_COLLECT_SCHEDULE_GRAPH
				, const Log_options<Time>& log_opts = Log_options<Time>()
#endif
			)
			{
				if (opts.verbose)
					std::cout << "Starting" << std::endl;

				Merge_options merge_opts{opts.merge_opts.conservative, opts.merge_opts.use_finish_times, opts.merge_opts.budget};
				auto s = std::unique_ptr<State_space>(new State_space(prob.jobs, prob.prec, prob.aborts, prob.mutexes, prob.processors_initial_state,
#ifdef CONFIG_ANALYSIS_EXTENSIONS
					prob.problem_extensions,
#endif
					merge_opts, opts.timeout, opts.max_memory, opts.max_depth, opts.early_exit, opts.verbose
#ifdef CONFIG_COLLECT_SCHEDULE_GRAPH
					, log_opts
#endif
#ifdef CONFIG_PARALLEL
					, opts.parallel_enabled, opts.num_threads
#endif
#ifdef CONFIG_PRUNING
					, opts.pruning_active
					, opts.pruning_cond
#endif
				));
				s->config.be_naive = opts.be_naive;
				if (opts.verbose)
					std::cout << "Analysing" << std::endl;
				s->resource_monitor.start_timing();
				s->explore();
				s->resource_monitor.stop_timing();
				return s;
			}

			// convenience interface for tests
			static std::unique_ptr<State_space> explore_naively(
				const Workload& jobs,
				unsigned int num_cpus = 1)
			{
				Problem p{ jobs, num_cpus };
				Analysis_options o;
				o.be_naive = true;
				return explore(p, o);
			}

			// convenience interface for tests
			static std::unique_ptr<State_space> explore(
				const Workload& jobs,
				unsigned int num_cpus = 1)
			{
				Problem p{ jobs, num_cpus };
				Analysis_options o;
				return explore(p, o);
			}

			/** @brief Return the best-case completion time and worst-case completion time of job j */
			Interval<Time> get_finish_times(const Job<Time>& j) const
			{
				return get_finish_times(j.get_job_index());
			}

			/** @brief Return the best-case completion time and worst-case completion time of job with index j */
			Interval<Time> get_finish_times(Job_index j) const
			{
#ifdef CONFIG_PARALLEL
				if (parallel_enabled) {
					std::lock_guard<std::mutex> lock(*rta_mutexes[j]);
					if (completion_times[j].valid) {
						return completion_times[j].ct;
					}
					else {
						return Interval<Time>{0, Time_model::constants<Time>::infinity()};
					}
				}
#endif
				if (completion_times[j].valid) {
					return completion_times[j].ct;
				}
				else {
					return Interval<Time>{0, Time_model::constants<Time>::infinity()};
				}
			}

			/** @brief Return whether the analyzed scheduling problem is schedulable */
			bool is_schedulable() const
			{
				return !aborted && !observed_deadline_miss;
			}

			/** @brief Return whether the analysis timed out */
			bool was_timed_out() const
			{
				return resource_monitor.is_timed_out();
			}

			/** @brief Return whether the analysis ran out of memory */
			bool out_of_memory() const
			{
				return resource_monitor.is_out_of_memory();
			}

			/** @brief Return whether a deadline miss was observed during the analysis */
			bool deadline_miss_observed() const
			{
				return observed_deadline_miss;
			}

			/** @brief Return the node and state where a deadline miss was observed if one was observed. Return a pair of nullptr pointers if no deadline miss was observed.
			 */
			const std::pair<Node_ref, State_ref> get_deadline_miss_state()
			{
				return {deadline_miss_node, deadline_miss_state};
			}

			/** @brief Return the number of nodes explored during the analysis */
			unsigned long long number_of_nodes() const
			{
				return statistics.get_num_nodes();
			}

			/** @brief Return the number of states explored during the analysis */
			unsigned long long number_of_states() const
			{
				return statistics.get_num_states();
			}

			/** @brief Return the number of edges explored during the analysis */
			unsigned long long number_of_edges() const
			{
				return statistics.get_num_edges();
			}

			/** @brief Return the maximum exploration front width observed during the analysis */
			unsigned long long max_exploration_front_width() const
			{
				return statistics.get_max_width();
			}

			/** @brief Return the evolution of exploration front width over time */
			const std::vector<std::pair<unsigned long long, unsigned long long>>& evolution_exploration_front_width() const
			{
				return statistics.get_width_evolution();
			}

			/** @brief Return the CPU time used for the analysis in seconds */
			double get_cpu_time() const
			{
				return resource_monitor.get_cpu_time();
			}

			/** @brief Return the memory usage during the analysis in KiB */
			long get_memory_usage() const
			{
				return resource_monitor.get_memory_usage();
			}
			
			/** @brief Return the total number of jobs in the workload */
			unsigned long long get_total_number_of_jobs() const
			{
				return (unsigned long long)(sp_data.jobs.size());
			}

			/** @brief Return the number of jobs that have been analyzed, i.e., until which SAG layer the analysis has progressed */
			unsigned long long get_number_of_jobs_analyzed() const
			{
				return (unsigned long long)(current_job_count);
			}

#ifdef CONFIG_ANALYSIS_EXTENSIONS
			/** @brief Return the results of a specific analysis extension in a string stream */
			template <typename Extension_type>
			std::ostringstream export_results()
			{
				Extension_type* mk_data = sp_data.get_extensions().template get<Extension_type>();
				if (mk_data == nullptr) {
					std::cerr << "Error: analysis extension is not available." << std::endl;
					return std::ostringstream();
				}
				return mk_data->export_results(sp_data);
			}

			/** @brief Return the results of a specific analysis extension in an extension-specific format */
			template <typename Extension_type>
			auto get_results() const -> typename std::remove_reference<decltype(std::declval<Extension_type>().get_results())>::type
			{
				Extension_type* data = sp_data.get_extensions().template get<Extension_type>();
				if (data == nullptr) {
					std::cerr << "Error: analysis extension is not available." << std::endl;
					// construct and return an empty value of the deduced (non-reference) return type
					using result_t = typename std::remove_reference<decltype(data->get_results())>::type;
					return result_t();
				}
				return data->get_results();
			}
#endif // CONFIG_ANALYSIS_EXTENSIONS

		private:
			typedef const Job<Time>* Job_ref;
			using Nodes = typename States_manager<Time>::Node_refs;

			/** Structure to hold completion time information for a job */
			struct Completion_time_item {				
				/** Indicates whether the completion time value saved in 'ct' is valid */
				bool valid;
				/** Best-case and worst-case completion time of the job */
				Interval<Time> ct;

				Completion_time_item()
					: valid(false)
					, ct(0, 0)
				{
				}
			};
			/** Analysis results: vector of completion times of all jobs analysed */
			typedef std::vector<Completion_time_item> Completion_times;
			Completion_times completion_times;

			/** Flag recording whether the analysis was aborted (due to timeout, memory exhaustion, or early exit) */
			bool aborted;
			
			/** Flag recording whether a deadline miss was observed */
			bool observed_deadline_miss;
			/** Node and state where a deadline miss was observed */
			Node_ref deadline_miss_node;
			State_ref deadline_miss_state;

			/** Configuration options for the analysis */
			Analysis_options config;

			/** Manager that maintains the set of nodes and states being explored */
			States_manager<Time> states_mgr;

			/** Resource monitoring and statistics */
			Exploration_statistics statistics;
			Resource_monitor resource_monitor;

#ifdef CONFIG_PARALLEL
			/** Per-job mutexes for protecting RTA updates */
			mutable std::vector<std::unique_ptr<std::mutex>> rta_mutexes;
			/** Whether parallel execution is enabled */
			bool parallel_enabled;
			/** Number of threads to use in parallel execution */
			unsigned int num_threads;			
			/** TBB task arena for thread control */
			std::unique_ptr<tbb::task_arena> task_arena_;
#endif
			/** Number of jobs analyzed so far. 
			 * Updated only by main thread - no protection needed */
			unsigned long long current_job_count;
			// Initial state of the cores
			const std::vector<Interval<Time>> cores_initial_state;
			/** State space data structure */
			State_space_data<Time> sp_data;

			/** Constructor */
			State_space(const Workload& jobs,
				const Precedence_constraints& edges,
				const Abort_actions& aborts,
				const Mutex_constraints& mutexes,
				const std::vector<Interval<Time>>& cores_initial_state,
#ifdef CONFIG_ANALYSIS_EXTENSIONS
				const Problem_extensions& problem_extensions,
#endif
				Merge_options merge_options,
				unsigned long long max_cpu_time = 0,
				unsigned long long max_memory = 0,
				unsigned long long max_depth = 0,
				bool early_exit = true,
				bool verbose = false
#ifdef CONFIG_COLLECT_SCHEDULE_GRAPH
				, Log_options<Time> log_opts = Log_options<Time>()
#endif
#ifdef CONFIG_PARALLEL
				, bool parallel_enabled = false
				, unsigned int num_threads = 0
#endif
#ifdef CONFIG_PRUNING
				, bool pruning_active = false
				, const Pruning_condition& pruning_cond = Pruning_condition()
#endif
			)	: sp_data(jobs, edges, aborts, mutexes, (unsigned int)cores_initial_state.size())
				, aborted(false)
				, observed_deadline_miss(false)
				, deadline_miss_state(nullptr)
				, deadline_miss_node(nullptr)
				, statistics(jobs.size())
				, resource_monitor(max_cpu_time, max_memory, max_depth)
#ifdef CONFIG_PARALLEL
				, parallel_enabled(parallel_enabled)
				, num_threads(num_threads)
#endif
				, completion_times(jobs.size())
#ifdef CONFIG_PARALLEL
				, rta_mutexes(jobs.size())
#endif
				, current_job_count(0)
				, cores_initial_state(cores_initial_state)
#ifdef CONFIG_COLLECT_SCHEDULE_GRAPH
				, log(log_opts.log)
				, logger(log_opts.log_cond)
#endif
#ifdef CONFIG_PRUNING
				, pruning_active(pruning_active)
				, secateur(pruning_cond)
#endif
			{
				// Initialize configuration
				config.merge_opts = merge_options;
				config.timeout = max_cpu_time;
				config.max_memory = max_memory;
				config.max_depth = max_depth;
				config.early_exit = early_exit;
				config.be_naive = false;
				config.verbose = verbose;
#ifdef CONFIG_PARALLEL
				config.parallel_enabled = parallel_enabled;
				config.num_threads = num_threads;

				// Initialize TBB task arena with specified thread count 
				if (parallel_enabled) {
					if (num_threads > 0) {
						task_arena_ = std::make_unique<tbb::task_arena>(num_threads);
					} else {
						task_arena_ = std::make_unique<tbb::task_arena>(tbb::task_arena::automatic);
					}
					task_arena_->initialize();
				}
				// Initialize per-job mutexes
				for (size_t i = 0; i < rta_mutexes.size(); ++i) {
					rta_mutexes[i] = std::make_unique<std::mutex>();
				}
#endif
#ifdef CONFIG_PRUNING
				config.pruning_active = pruning_active;
#endif
				// Initialize the number of layers of the SAG that must be kept track of by the states manager
				size_t layers = 1;
				const auto& cond_constr = sp_data.conditional_dispatch_constraints;
				for (Job_index j = 0; j < jobs.size(); ++j)
					layers = std::max(layers, cond_constr.get_incompatible_jobs(j).size());
				states_mgr.set_size(layers+1);
				
#ifdef CONFIG_ANALYSIS_EXTENSIONS
				// check if the MK analysis extension is registered
				auto mk_ext = problem_extensions.template get<MK_analysis::MK_problem_extension>();
				if (mk_ext)
				{
					// If yes, activate MK analysis
					MK_analysis::MK_extension<Time>::activate(sp_data, jobs.size(), mk_ext->get_mk_constraints());
				}
				// check if the Task Chains analysis extension is registered
				using TC_problem_ext = Taskchains_analysis::Taskchains_problem_extension<Time>;
				auto tc_ext = problem_extensions.template get<TC_problem_ext>();
				if (tc_ext)
				{
					// If yes, activate task chains analysis
					using TC_ext = Taskchains_analysis::Taskchains_analysis_extension<Time>;
					TC_ext::activate(sp_data, jobs, tc_ext->get_task_chains());
				}
#endif // CONFIG_ANALYSIS_EXTENSIONS
			}

		private:
#ifdef CONFIG_COLLECT_SCHEDULE_GRAPH
			bool log = false; // whether to log the schedule graph
			SAG_logger<Time> logger; 
#endif
#ifdef CONFIG_PRUNING
			bool pruning_active = false; // whether to use pruning
			Secateur<Time> secateur; // pruning tool
#endif

			/** @brief Create a new node at the specified depth with the given arguments */
			template <typename... Args>
			Node_ref new_node(const unsigned int depth, Args&&... args)
			{
				statistics.count_node();
				return states_mgr.new_node(current_job_count + depth, std::forward<Args>(args)...);
			}

			/** @brief Create a new state with the given arguments */
			template <typename... Args>
			State_ref new_state(Args&&... args)
			{
				return states_mgr.new_state(std::forward<Args>(args)...);
			}

			/** @brief Release a node back to the states manager */
			void release_node(const std::shared_ptr<Node>& n)
			{
				states_mgr.release_node(n);
			}

			/** @brief Release a state back to the states manager */
			void release_state(const std::shared_ptr<State>& s)
			{
				states_mgr.release_state(s);
			}

			/** 
			 * @brief Create a new state and try to merge it with existing states in node n.
			 * If merging is successful, the new state is released.
			 * @param n node to add/merge the new state into
			 * @param args arguments to create the new state
			*/
			template <typename... Args>
			void new_or_merge_state(Node& n, Args&&... args)
			{
				// create a new state.
				State_ref new_s = states_mgr.new_state(std::forward<Args>(args)...);
#ifndef CONFIG_PARALLEL
				if (observed_deadline_miss)
					deadline_miss_state = new_s;
#endif
				// try to merge the new state with existing states in node n.
				if (!(n.get_states()->empty())) {
					int n_states_merged = n.merge_states(*new_s, 
						config.merge_opts.conservative, 
						config.merge_opts.use_finish_times, 
						config.merge_opts.budget);
					if (n_states_merged > 0) {
						states_mgr.release_state(new_s); // if we could merge no need to keep track of the new state anymore
						statistics.remove_states(n_states_merged - 1);
					}
					else
					{
						n.add_state(new_s); // else add the new state to the node
						statistics.count_state();
					}
				}
				else
				{
					n.add_state(new_s); // else add the new state to the node
					statistics.count_state();

				}
			}

			/** 
			 * @brief Get the set of nodes at the current exploration depth plus an optional offset
			 * @param depth optional depth offset
			 * @return set of nodes at the specified depth
			*/
			Nodes& nodes(const int depth = 0)
			{
				return states_mgr.get_nodes_at_depth(current_job_count + depth);
			}

			/** 
			 * @brief Check whether the CPU time limit has been exceeded and set the aborted flag if so
			 * @return current CPU time in seconds
			*/
			double check_cpu_time()
			{
				double cpu_time;
				if (resource_monitor.check_timeout(cpu_time)) {
					aborted = true;
				}
				return cpu_time;
			}

			/** 
			 * @brief Check whether the memory limit has been exceeded and set the aborted flag if so
			 * @return current memory usage in KiB
			*/
			long check_memory_usage()
			{
				long mem;
				if (resource_monitor.check_out_of_memory(mem)) {
					aborted = true;
				}
				return mem;
			}

			/** 
			 * @brief Check whether the maximum exploration depth has been exceeded and set the aborted flag if so
			*/
			void check_depth_abort()
			{
				if (resource_monitor.check_depth(current_job_count))
					aborted = true;
			}

			/** 
			 * @brief Check whether any job is guaranteed to miss its deadline in any state in node new_n,
			 * given that we have progressed from old_n to new_n.
			 * @param old_n previous node
			 * @param new_n new node
			*/
			void check_for_deadline_misses(const Node& old_n, const Node& new_n)
			{
				auto check_from = old_n.get_first_state()->core_availability().min();

				// check if we skipped any jobs that are now guaranteed
				// to miss their deadline
				for (auto it = sp_data.jobs_by_deadline.lower_bound(check_from);
					it != sp_data.jobs_by_deadline.end(); it++) {
					const Job<Time>& j = *(it->second);
					auto pmin = j.get_min_parallelism();
					auto earliest = new_n.get_last_state()->core_availability(pmin).min();
					if (j.get_deadline() < earliest) {
						if (!new_n.job_dispatched(j.get_job_index())) {
							DM("deadline miss: " << new_n << " -> " << j << std::endl);
							// This job is still incomplete but has no chance
							// of being scheduled before its deadline anymore.
							observed_deadline_miss = true;
							// if we stop at the first deadline miss, abort and create node in the graph for explanation purposes
							if (config.early_exit)
							{
								aborted = true;
								// create a dummy node for explanation purposes
								auto frange = new_n.get_last_state()->core_availability(pmin) + j.get_cost(pmin);
								unsigned int n_inc_jobs = (unsigned int) sp_data.conditional_dispatch_constraints.get_incompatible_jobs(j.get_job_index()).size();
								Node_ref next =	new_node(n_inc_jobs, new_n, j, j.get_job_index(), sp_data);
								//const CoreAvailability empty_cav = {};
								State_ref next_s = new_state(
										*new_n.get_last_state(), j.get_job_index(), frange, frange, new_n.get_scheduled_jobs(),
										new_n.get_jobs_with_pending_start_successors(), new_n.get_jobs_with_pending_finish_successors(),
										new_n.get_ready_successor_jobs(), sp_data, new_n.get_next_certain_source_job_release(), pmin
								);
								next->add_state(next_s);
								statistics.count_state();
								deadline_miss_state = next_s;
								deadline_miss_node = next;
								// update response times
								update_finish_times(j, frange);
								statistics.count_edge();
							}
							break;
						}
					}
					else
						// deadlines now after the next earliest finish time
						break;
				}
			}

			/** 
			 * @brief Check whether all jobs have been dispatched when reaching node n
			 * @param n node to check
			 * @return true if all jobs have been dispatched, false otherwise
			*/
			bool all_jobs_scheduled(const Node& n)
			{
				return (n.number_of_scheduled_jobs() == sp_data.num_jobs());
			}

			/** 
			 * @brief Update the completion time information for job with index id given a new completion time range.
			 * NOTE: this function is not thread-safe. The caller must ensure thread safety when parallel execution is active.
			 * @param r completion times data structure to update
			 * @param id index of the job to update
			 * @param range new completion time range
			*/
			void update_finish_times(Completion_times& r, const Job_index id,
				Interval<Time> range)
			{
				if (!r[id].valid) {
					r[id].valid = true;
					r[id].ct = range;
				}
				else {
					r[id].ct |= range;
				}
				DM("RTA " << id << ": " << r[id].ct << std::endl);
			}

			/** 
			 * @brief Update the completion time information for job j given a new completion time range.
			 * It also checks whether the job exceeds its deadline with the new completion time range and updates the deadline miss flag accordingly.
			 * NOTE: This function handles thread safety when parallel execution is active.
			 * @param j job to update
			 * @param range new completion time range
			*/
			void update_finish_times(const Job<Time>& j, Interval<Time> range)
			{
#ifdef CONFIG_PARALLEL
				Job_index j_idx = j.get_job_index();
				if (parallel_enabled) {
					std::lock_guard<std::mutex> lock(*rta_mutexes[j_idx]);
					update_finish_times(completion_times, j_idx, range);
				} else {
					update_finish_times(completion_times, j_idx, range);
				}
#else
				update_finish_times(completion_times, j.get_job_index(), range);
#endif
				// check for deadline miss
				if (j.exceeds_deadline(range.upto())) {
					observed_deadline_miss = true;
					if (config.early_exit)
						aborted = true;
				}
			}

			// find next time by which a job is certainly ready in system state 's'
			// return min { min_{ j \in SeqIndependentJobs \ Omega(s) } { r^max(j) },
			//				min_{j \in GangIndependentJobs \ Omega(s)} { max{ r^max(j), A^max_p^min(j)(s)) } },
			//				min_{ j \in MutxSourceJobs \ Omega(s) } { max{	r^max(j), 
			//											 	  				A^max_p^min(j)(s),
	  		//					 						 	  				max_{i \in Mutx^s(j) \cup Omega(s)} { ST^max(i) + delay^max(i,j) }, 
	  		//					 						 	  				max_{i \in Mutx^f(j) \cup Omega(s)} { FT^max(i) + delay^max(i,j) } } },
			//			  	min_{ j \in R^pot(s)} { max_{sib \in CondSibs(j)} {
			//											 max{ r^max(j), 
			//											 	  A^max_p^min(j)(s),
	  		//					 						 	  max_{i \in Pred^s(j)} { ST^max(i) + delay^max(i,j) }, 
	  		//					 						 	  max_{i \in Pred^f(j)} { FT^max(i) + delay^max(i,j) }, 
	  		//					 						 	  max_{i \in Mutx^s(j) \cup Omega(s)} { ST^max(i) + delay^max(i,j) }, 
	  		//					 						 	  max_{i \in Mutx^f(j) \cup Omega(s)} { FT^max(i) + delay^max(i,j) } } } } }
			// where SeqIndependentJobs = { j | Pred(j) = emptyset and Mutx(j) = emptyset and p^min(j) = 1 } and GangIndependentJobs = { j | Pred(j) = emptyset and Mutx(j) = emptyset and p^min(j) > 1 }
			Time next_certain_job_ready_time(const Node& n, const State& s) const
			{
				// calculate min_{ j \in SeqIndependentJobs \ Omega(s) } { r^max(j) }
				Time t_seq = n.get_next_certain_sequential_independent_job_release();
				// calculate min_{j \in GangIndependentJobs \ Omega(s)} { max{ r^max(j), A^max_p^min(j)(s)) } }
				Time t_gang = s.next_certain_gang_independent_job_dispatch();
				// calculate min_{ j \in MutxSourceJobs \ Omega(s) } { ... }
				Time t_mtx = s.next_certain_mutex_source_job_dispatch();
				// calculate min_{ j \in R^pot(s)} { ... }
				Time t_succ = s.next_certain_successor_jobs_dispatch();
				// take the minimum over all
				return std::min(t_seq, std::min(t_mtx, std::min(t_gang, t_succ)));
			}

			/**
			 * @brief Calculate the possible abort time interval for job j given its start and finish time intervals.
			 * @param j job to calculate the abort time for
			 * @param est earliest start time of job j
			 * @param lst latest start time of job j
			 * @param eft earliest finish time of job j
			 * @param lft latest finish time of job j
			 * @return interval representing the possible abort times for job j
			 */
			Interval<Time> calculate_abort_time(const Job<Time>& j, Time est, Time lst, Time eft, Time lft)
			{
				auto j_idx = j.get_job_index();
				auto abort_action = sp_data.abort_action_of(j_idx);
				if (abort_action) {
					auto lt = abort_action->latest_trigger_time();
					// Rule: if we're certainly past the trigger, the job is
					//       completely skipped.
					if (est >= lt) {
						// job doesn't even start, it is skipped immediately
						return Interval<Time>{ est, lst };
					}
					else {
						// The job can start its execution but we check
						// if the job must be aborted before it finishes
						auto eat = abort_action->earliest_abort_time();
						auto lat = abort_action->latest_abort_time();
						return Interval<Time>{ std::min(eft, eat), std::min(lft, lat) };
					}
				}
				else {
					// compute range of possible finish times
					return Interval<Time>{ eft, lft };
				}
			}

			/** 
			 * @brief Try to dispatch job j in all states of node n.
			 * For each state in which the job can be dispatched, a new state (or merged state) will be created in the next node.
			 * @param n node containing the states to try to dispatch the job in
			 * @param j job to dispatch
			 * @param t_high_wos bound on when a higher priority sequential source job will certainly be ready
			 * @return true if the job was dispatched in at least one state, false otherwise
			*/
			bool dispatch(const Node_ref& n, const Job<Time>& j, Time t_high_wos)
			{
				// All states in node 'n' for which the job 'j' is eligible will 
				// be added to that same node. 
				// If such a node already exists, we keep a reference to it
				Node_ref next = nullptr;
				DM("--- global:dispatch() " << n << ", " << j << ", " << t_wc_wos << ", " << t_high_wos << std::endl);

				// flag to indicate whether we dispatched the job in at least one state
				bool dispatched_one = false;

				// loop over all states in the node n
				const auto* n_states = n->get_states();
				for (const State_ref& s : *n_states)
				{
					// if the job priority is lower than than the minimum priority of the next dispatched job, it will not be dispatched next
					Job_ref next_dispatch_min_prio = s->get_next_dispatched_job_min_priority();
					if (next_dispatch_min_prio != NULL && next_dispatch_min_prio->higher_priority_than(j))
						continue;

					// calculate when will the job be ready at the earliest
					auto rt = sp_data.ready_times(*n, *s, j);
					Time rt_min = rt.min();
					if (t_high_wos <= rt_min)   
						continue; // a higher priority source job will be ready before j can start, j will not be dispatched next
					
					// calculate t_wc: earliest time by which a job is ready and enough cores are available to execute it
					// t_wc(s) = max(A^max_1(s), min_{j' \notin Omega(s)}(max{R^max(j',s), A^max_p^min(j')(s)}))
					Time t_wc = std::max(s->core_availability().max(), next_certain_job_ready_time(*n, *s));
					if (t_wc < rt_min)
						continue; // another job will be dispatched before j is ready, j will not be dispatched next

					// check for all possible parallelism levels of the moldable gang job j 
					// (if j is not gang or not moldable than min_paralellism = max_parallelism and 
					// costs only constains a single element).
					const auto& costs = j.get_all_costs();
					for (auto it = costs.begin(); it != costs.end(); it++)
					{
						unsigned int p = it->first;
						// calculate earliest time j may start executing in state s on p cores
						auto at = s->core_availability(p);
						Time at_min = at.min();
						if (t_wc < at_min)
							break; // another job will be dispatched before enough cores are free to dispatch j, j will not be dispatched next on p or more cores
						if (t_high_wos <= at_min)   
							break; // a higher priority source job will be ready before enough cores are free to dispatch j, j will not be dispatched next
						// EST = max{ r^min(j), A^max_p(s) }
						Time est = std::max(rt_min, at_min);
						
						// Calculate t_high
						Time t_high_gang = sp_data.next_certain_higher_priority_gang_independent_job_ready_time(*n, *s, j, p, est, t_wc + 1);
						if (t_high_gang <= est)
							continue; // a higher priority gang source job will be dispatched before j can start
						Time t_high_mutx = sp_data.next_certain_higher_priority_mutex_job_ready_time(*n, *s, j, p, est, t_wc + 1);
						if (t_high_mutx <= est)
							continue; // a higher priority mutex-source job will be dispatched before j can start
						Time t_high_succ = sp_data.next_certain_higher_priority_successor_job_ready_time(*n, *s, j, p, est);
						if (t_high_succ <= est)
							continue; // a higher priority successor job will be dispatched before j can start
						Time t_high = std::min(t_high_wos, std::min(t_high_gang,  std::min(t_high_mutx, t_high_succ)));
						// If j can execute on ncores+k cores, then 
						// the scheduler will start j on ncores only if 
						// there isn't ncores+k cores available
						Time t_avail = (p == j.get_max_parallelism()) ? Time_model::constants<Time>::infinity() : s->core_availability(std::next(it)->first).max();
						if (t_avail <= est)
							continue; // j will be started with higher parallelism than p
						DM("=== t_high = " << t_high << ", t_wc = " << t_wc << ", t_avail = " << t_avail << std::endl);
						
						// latest time j may start executing in state s on p cores
						// LST = min{ max{r^max(j), A^max_p(s)}, t_wc(s), t_high(j,p,s)-eps, t_avail(p,s)-eps }
						// where eps is 1 for discrete time and the smallest representable time value for dense time
						Time lst = std::min(std::max(rt.max(), at.max()), std::min(t_wc,
									std::min(t_high, t_avail) - Time_model::constants<Time>::epsilon()));
						Interval<Time> stimes(est, lst);

						//calculate the job finish time interval
						auto exec_time = it->second;
						// EFT = EST + C^min(j,p)
						Time eft = est + exec_time.min();
						// LFT = LST + C^max(j,p)
						Time lft = lst + exec_time.max();

						// check for possible abort actions
						Interval<Time> ftimes = calculate_abort_time(j, est, lst, eft, lft);

						// job j is a feasible successor in state s
						dispatched_one = true;

						// update finish-time estimates
						update_finish_times(j, ftimes);

						// If be_naive, a new node and a new state should be created for each new job dispatch.
						if (config.be_naive) {
							unsigned int n_inc_jobs = (unsigned int) sp_data.conditional_dispatch_constraints.get_incompatible_jobs(j.get_job_index()).size();
							next = new_node(n_inc_jobs, *n, j, j.get_job_index(), sp_data);
						}

						// if we do not have a pointer to a node with the same set of scheduled job yet,
						// try to find an existing node with the same set of scheduled jobs. Otherwise, create one.
						if (next == nullptr)
						{
							unsigned int n_inc_jobs = (unsigned int) sp_data.conditional_dispatch_constraints.get_incompatible_jobs(j.get_job_index()).size();
							next = states_mgr.find_node(n->next_key(j.get_job_index(), sp_data), current_job_count + n_inc_jobs, n->get_scheduled_jobs(), j.get_job_index());
							// If there is no node yet, create one.
							if (next == nullptr) {
								next = new_node(n_inc_jobs, *n, j, j.get_job_index(), sp_data);
							}
						}

						// next should always exist at this point, possibly without states in it
						// create a new state resulting from scheduling j in state s on p cores and try to merge it with an existing state in node 'next'.							
						new_or_merge_state(*next, *s, j.get_job_index(),
							stimes, ftimes, next->get_scheduled_jobs(),
							next->get_jobs_with_pending_start_successors(), next->get_jobs_with_pending_finish_successors(),
							next->get_ready_successor_jobs(), sp_data, next->get_next_certain_source_job_release(), p
						);
#ifdef CONFIG_COLLECT_SCHEDULE_GRAPH
						if(log)
							logger.log_job_dispatched(n, j, stimes, ftimes, p, next, current_job_count);
#endif
						// make sure we didn't skip any jobs which would then certainly miss its deadline
						// only do that if we stop the analysis when a deadline miss is found 
						if (config.be_naive && config.early_exit) {
							check_for_deadline_misses(*n, *next);
						}

						statistics.count_edge();

						if (observed_deadline_miss) {
	#ifndef CONFIG_PARALLEL
							deadline_miss_node = next;
	#endif
							return dispatched_one;
						}
					}
				}				
				
				// if we stop the analysis when a deadline miss is found, then check whether a job will certainly miss 
				// its deadline because of when the processors become free next.
				// if we are not using the naive exploration, we check for deadline misses only once per job dispatched
				if (config.early_exit && !config.be_naive && next != nullptr) {
					check_for_deadline_misses(*n, *next);
				}
				return dispatched_one;
			}

			/** 
			 * @brief Explore all possible job dispatching options in states of node n.
			 * For each eligible job in each state of node n, try to dispatch the job 
			 * and create new states/nodes accordingly.
			 * If no job can be dispatched in any state of node n, and not all jobs have 
			 * been scheduled yet, then a deadline miss is recorded.
			 * @param n node to explore
			*/
			void explore(const Node_ref& n)
			{
				// flag to record whether we could dispatch at least one job in at least 
				// one state of node n
				bool found_one = false;
				// flag to record whether at least one job dispatching option was pruned
				bool pruned = false;

				DM("---- global:explore(node)" << n->finish_range() << std::endl);

				// latest time some unfinished job is certainly ready
				auto nxt_ready_job = n->next_certain_job_ready_time();
				// latest time all cores are certainly available
				auto avail_max = n->latest_core_availability();
				// upper bound on the latest time by which a work-conserving scheduler
				// certainly schedules some job
				// upbnd_t_wc = max_{v \in node} { max( A^max_m(v), next_certainly_ready_job(v) ) }
				auto upbnd_t_wc = std::max(avail_max, nxt_ready_job);

				DM(n << std::endl);
				DM("t_min: " << t_min << std::endl
					<< "nxt_ready_job: " << nxt_ready_job << std::endl
					<< "avail_max: " << avail_max << std::endl
					<< "upbnd_t_wc: " << upbnd_t_wc << std::endl);

				//check all jobs that may be eligible to be dispatched next
				// part 1: check source jobs (i.e., jobs without precedence constraints) that are potentially eligible
				// 		   i.e., for all j | j \notin Omega(v) and r^min(j) <= upbnd_t_wc and Pred(j) = emptyset
				for (auto it = sp_data.jobs_by_earliest_arrival.lower_bound(n->earliest_job_release());
					it != sp_data.jobs_by_earliest_arrival.end();
					it++)
				{
					const Job<Time>& j = *it->second;
					DM(j << " (" << j.get_job_index() << ")" << std::endl);
					// stop looking once we've left the window of interest
					if (j.earliest_arrival() > upbnd_t_wc)
						break;
					// if the job has already been dispatched, it is certainly not eligible to be dispatched again
					if (n->job_dispatched(j.get_job_index()))
						continue;
					// if there is a higher priority job that is certainly ready before job j is released at the earliest, 
					// then j will never be the next job dispached by the scheduler
					Time t_high_wos = sp_data.next_certain_higher_priority_seq_independent_job_release(*n, j, upbnd_t_wc + 1);
					if (t_high_wos <= j.earliest_arrival())
						continue;

#ifdef CONFIG_PRUNING
					// if pruning is active, check whether the job is eligible for dispatching
					if (pruning_active && secateur.prune_branch(j, *n)) {
						pruned = true;
						continue;
					}
#endif
					// try to dispatch job j in all states of node n
					found_one |= dispatch(n, j, t_high_wos);
				}
				// part 2: check ready successor jobs (i.e., jobs with all precedence constraints fulfilled) that are potentially eligible
				//	   	   i.e., for all j | j \in R^pot(v) and r^min(j) <= upbnd_t_wc
				for (const auto pj : n->get_ready_successor_jobs())
				{
					const Job<Time>& j = *pj;
					DM(j << " (" << j.get_job_index() << ")" << std::endl);
					// don't look outside the window of interest
					if (j.earliest_arrival() > upbnd_t_wc)
						continue;
					// Since this job is is recorded as ready in the state, it better
					// be incomplete...
					assert(not n->job_dispatched(j.get_job_index()));

					// if there is a higher priority job that is certainly ready before job j is released at the earliest, 
					// then j will never be the next job dispached by the scheduler
					Time t_high_wos = sp_data.next_certain_higher_priority_seq_independent_job_release(*n, j, upbnd_t_wc + 1);
					if (t_high_wos <= j.earliest_arrival())
						continue;
#ifdef CONFIG_PRUNING
					// if pruning is active, check whether the job is eligible for dispatching
					if (pruning_active && secateur.prune_branch(j, *n)) {
						pruned = true;
						continue;
					}
#endif
					// try to dispatch job j in all states of node n
					found_one |= dispatch(n, j, t_high_wos);
				}
				// check for a dead end
				if (!found_one && !pruned && !all_jobs_scheduled(*n)) {
					// out of options and we didn't schedule all jobs
					observed_deadline_miss = true;
					deadline_miss_node = n;
					deadline_miss_state = n->get_first_state();
					aborted = true;
				}
			}

			/** 
			 * @brief Create the initial node and state in the state space
			*/
			void make_initial_node() {
				// construct initial state
				Node_ref n = new_node(0, cores_initial_state, sp_data);
				State_ref s = new_state(cores_initial_state, sp_data);
				n->add_state(s);
				statistics.count_state();
			}

			/** 
			 * @brief Main state space exploration loop 
			*/
			void explore()
			{
				long long last_time = (long long) check_cpu_time();
				unsigned long long target_depth;
				
				if (config.verbose) {
					std::cout << "0%; 0s";
					target_depth = std::max((unsigned long long)sp_data.num_jobs(), config.max_depth);
				}
				
				unsigned long long last_num_states = statistics.get_num_states();
				make_initial_node();

				while (current_job_count < sp_data.num_jobs()) {
					Nodes& exploration_front = nodes();
#ifdef CONFIG_PARALLEL
					unsigned long long n = exploration_front.unsafe_size();
#else
					unsigned long long n = exploration_front.size();
#endif
					if (n == 0) {
						// exploration front is empty, nothing more to explore at that depth
						// clean up the state cache if necessary
						if (!config.be_naive)
							states_mgr.clear_cache_at_depth(current_job_count);
						// advance to next depth
						current_job_count++;
						continue;
					}

					// keep track of exploration front width (main thread only - no protection needed)
					unsigned long long current_states = statistics.get_num_states();
					statistics.record_width(current_job_count, n, current_states - last_num_states);
					last_num_states = current_states;

					long long time = (long long) check_cpu_time();
					if (time > last_time + 4) {
						// check memory usage
						long mem = check_memory_usage();
						if (config.verbose) {
							// update progress information approxmately every 4 seconds of runtime
#ifdef __APPLE__
							std::cout << "\r" << (int)(((double)current_job_count / target_depth) * 100) << "% (" << current_job_count << "/" << target_depth << "); " << time << "s; " << mem / 1024 << "KiB";
#else
							std::cout << "\r" << (int)(((double)current_job_count / target_depth) * 100) << "% (" << current_job_count << "/" << target_depth << "); " << time << "s; " << mem / 1024 << "MiB";
#endif
							last_time = time;
						}
					}
					check_depth_abort();
					if (aborted)
						break;

#ifdef CONFIG_PARALLEL
					// Parallel processing of exploration front within task arena
					if (parallel_enabled && n >= 4) {  // Parallel threshold
						task_arena_->execute([&]() {
							tbb::parallel_for(tbb::blocked_range<size_t>(0, exploration_front.unsafe_size()),
								[&](const tbb::blocked_range<size_t>& range) {
									Node_ref node;
									while (exploration_front.try_pop(node)) {
										if (aborted) break;
										
										explore(node);
										
										if (aborted) break;

										// Clean up nodes that are no longer referenced
										if (node.use_count() == 1) {
											states_mgr.release_states_of(node);
											release_node(node);
										}
									}
								});
						});
					} else {
						// Sequential processing for small fronts or when parallel is disabled
						Node_ref node;
						while (exploration_front.try_pop(node)) {
							explore(node);
							check_cpu_time();
							if (aborted)
								break;

							if (node.use_count() == 1) {
								states_mgr.release_states_of(node);
								release_node(node);
							}
						}
					}
#else
					// Original sequential processing
					for (const Node_ref& node : exploration_front) {
						explore(node);
						check_cpu_time();
						if (aborted)
							break;

						// If the node is not refered to anymore, we can reuse the node and state objects for other states.
						if (node.use_count() == 1) {
							states_mgr.release_states_of(node);
							release_node(node);
						}
					}
#endif

					// clean up the state cache if necessary
					if (!config.be_naive)
						states_mgr.clear_cache_at_depth(current_job_count);

					nodes().clear();
					current_job_count++;
				}
				if (config.verbose) {
#if __APPLE__
					std::cout << "\r100%; " << get_cpu_time() << "s; " << resource_monitor.get_memory_usage() / 1024 << "KiB" << std::endl << "Terminating" << std::endl;
#else
					std::cout << "\r100%; " << get_cpu_time() << "s; " << resource_monitor.get_memory_usage() / 1024 << "MiB" << std::endl << "Terminating" << std::endl;
#endif
				}
				// clean out any remaining nodes
				states_mgr.clear();
			}

			/** naive state space exploration: no state merging */
			void explore_naively()
			{
				config.be_naive = true;
				explore();
			}

#ifdef CONFIG_COLLECT_SCHEDULE_GRAPH
		public:
			/** @brief Print the SAG in a DOT file format */
			void print_dot_file(std::ostream& o, Dot_file_config config) {
				logger.print_dot_file(o, sp_data.jobs, config);
			}
#endif
		};

	}
}

namespace std
{
	template<class Time> struct hash<NP::Global::Schedule_state<Time>>
	{
		std::size_t operator()(NP::Global::Schedule_state<Time> const& s) const
		{
			return s.get_key();
		}
	};
}

#endif
