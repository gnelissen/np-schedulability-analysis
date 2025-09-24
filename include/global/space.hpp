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

#include "global/state.hpp"
#include "object_pool.hpp"

#include "logger.hpp"

#ifdef CONFIG_PRUNING
#include "global/secateur.hpp"
#endif

#ifdef CONFIG_ANALYSIS_EXTENSIONS
#include "global/extension/taskchains/taskchains_extension.hpp"
#include "global/extension/taskchains/taskchains_problem_extension.hpp"
#endif

namespace NP {

	namespace Global {

		template<class Time> class State_space
		{
		public:

			typedef Scheduling_problem<Time> Problem;
			typedef typename Scheduling_problem<Time>::Workload Workload;
			typedef typename Scheduling_problem<Time>::Precedence_constraints Precedence_constraints;
			typedef typename Scheduling_problem<Time>::Mutex_constraints Mutex_constraints;
			typedef typename Scheduling_problem<Time>::Abort_actions Abort_actions;
			typedef typename std::vector<Interval<Time>> CoreAvailability;

			typedef Schedule_node<Time> Node;
			typedef std::shared_ptr<Node> Node_ref;
			typedef Schedule_state<Time> State;
			typedef std::shared_ptr<State> State_ref;

#ifdef CONFIG_COLLECT_SCHEDULE_GRAPH
			static std::unique_ptr<State_space> explore(
				const Problem& prob,
				const Analysis_options& opts,
				const Log_options<Time>& log_opts)
			{
				if (opts.verbose)
					std::cout << "Starting" << std::endl;

				Merge_options merge_opts{opts.merge_conservative, opts.merge_use_job_finish_times, opts.merge_depth};
				auto s = std::unique_ptr<State_space>(new State_space(prob.jobs, prob.prec, prob.aborts, prob.mutexes, prob.processors_initial_state,
#ifdef CONFIG_ANALYSIS_EXTENSIONS
					prob.problem_extensions,
#endif
					merge_opts, opts.timeout, opts.max_memory_usage, opts.max_depth, opts.early_exit, opts.verbose, log_opts
#ifdef CONFIG_PARALLEL
					, opts.parallel_enabled, opts.num_threads
#endif
				));
				s->be_naive = opts.be_naive;
				if (opts.verbose)
					std::cout << "Analysing" << std::endl;
				s->cpu_time.start();
				s->explore();
				s->cpu_time.stop();
				return s;
			}
#endif
			static std::unique_ptr<State_space> explore(
				const Problem& prob,
				const Analysis_options& opts)
			{
				if (opts.verbose)
					std::cout << "Starting" << std::endl;

				Merge_options merge_opts{opts.merge_conservative, opts.merge_use_job_finish_times, opts.merge_depth};
				auto s = std::unique_ptr<State_space>(new State_space(prob.jobs, prob.prec, prob.aborts, prob.mutexes, prob.processors_initial_state,
#ifdef CONFIG_ANALYSIS_EXTENSIONS
					prob.problem_extensions,
#endif
					merge_opts, opts.timeout, opts.max_memory_usage, opts.max_depth, opts.early_exit, opts.verbose
#ifdef CONFIG_PARALLEL
					, opts.parallel_enabled, opts.num_threads
#endif
#ifdef CONFIG_PRUNING
					, opts.pruning_active
					, opts.pruning_cond
#endif
				));
				s->be_naive = opts.be_naive;
				if (opts.verbose)
					std::cout << "Analysing" << std::endl;
				s->cpu_time.start();
				s->explore();
				s->cpu_time.stop();
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

			// return the BCRT and WCRT of job j 
			Interval<Time> get_finish_times(const Job<Time>& j) const
			{
				return get_finish_times(j.get_job_index());
			}

			Interval<Time> get_finish_times(Job_index j) const
			{
#ifdef CONFIG_PARALLEL
				if (parallel_enabled) {
					std::lock_guard<std::mutex> lock(*rta_mutexes[j]);
					if (rta[j].valid) {
						return rta[j].rt;
					}
					else {
						return Interval<Time>{0, Time_model::constants<Time>::infinity()};
					}
				}
#endif
				if (rta[j].valid) {
					return rta[j].rt;
				}
				else {
					return Interval<Time>{0, Time_model::constants<Time>::infinity()};
				}
			}

#ifdef CONFIG_ANALYSIS_EXTENSIONS
			// return the maximum data age of taskchain `tc_id`
			Time get_max_data_age(unsigned long tc_id) const
			{
				auto tc_data = state_space_data.get_extensions().get<Taskchains_analysis::Taskchains_sp_data_extension<Time>>();
				if (tc_data == nullptr) {
					std::cerr << "Error: Task chain data extension is not available." << std::endl;
					return 0;
				}
				if (tc_id < tc_data->get_task_chains().size()) {
					return tc_data->get_max_data_age(tc_id);
				}
				else {
					std::cerr << "Error: Task chain ID " << tc_id << " is out of bounds." << std::endl;
					return 0;
				}
			}

			// return the maximum reaction time of taskchain `tc_id`
			Time get_max_reaction_time(unsigned long tc_id) const
			{
				auto tc_data = state_space_data.get_extensions().get<Taskchains_analysis::Taskchains_sp_data_extension<Time>>();
				if (tc_data == nullptr) {
					std::cerr << "Error: Task chain data extension is not available." << std::endl;
					return 0;
				}
				if (tc_id < tc_data->get_task_chains().size()) {

					return tc_data->get_max_reaction_time(tc_id);
				}
				else {
					std::cerr << "Error: Task chain ID " << tc_id << " is out of bounds." << std::endl;
					return 0;
				}
			}
#endif // CONFIG_ANALYSIS_EXTENSIONS

			bool is_schedulable() const
			{
				return !aborted && !observed_deadline_miss;
			}

			bool was_timed_out() const
			{
				return timed_out;
			}

			bool out_of_memory() const
			{
				return mem_out;
			}

			const std::pair<Node_ref, State_ref> get_deadline_miss_state()
			{
				return {deadline_miss_node, deadline_miss_state};
			}

			//currently unused, only required to compile nptest.cpp correctly
			unsigned long number_of_nodes() const
			{
				return num_nodes;
			}

			unsigned long number_of_states() const
			{
				return num_states;
			}

			unsigned long number_of_edges() const
			{
				return num_edges;
			}

			unsigned long max_exploration_front_width() const
			{
				return max_width;
			}

			const std::vector<std::pair<unsigned long, unsigned long>>& evolution_exploration_front_width() const
			{
				return width;
			}

			double get_cpu_time() const
			{
				return cpu_time;
			}

#ifdef CONFIG_PARALLEL
			// Thread-safe concurrent queue for storing nodes during parallel execution
			typedef tbb::concurrent_queue<Node_ref> Nodes;
#else
			typedef std::deque<Node_ref> Nodes;
#endif
			typedef std::vector<Nodes> Nodes_storage;

		private:

			typedef typename std::forward_list<Node_ref> Node_refs;
			typedef typename std::forward_list<State_ref> State_refs;
			
#ifdef CONFIG_PARALLEL
			typedef tbb::concurrent_unordered_map<hash_value_t, Node_refs> Nodes_map;
#else
			typedef std::unordered_map<hash_value_t, Node_refs> Nodes_map;
#endif
			typedef const Job<Time>* Job_ref;

			// Similar to uni/space.hpp, make Response_times a vector of intervals.

			// typedef std::unordered_map<Job_index, Interval<Time> > Response_times;
			struct Response_time_item {
				bool valid;
				Interval<Time> rt;

				Response_time_item()
					: valid(false)
					, rt(0, 0)
				{
				}
			};
			typedef std::vector<Response_time_item> Response_times;
			Response_times rta;

			bool aborted;
			bool timed_out;
			bool mem_out;
			bool observed_deadline_miss;
			bool early_exit;

			// node and state where a deadline miss was observed
			Node_ref deadline_miss_node;
			State_ref deadline_miss_state;

			const unsigned int max_depth;

			bool be_naive;

			struct Merge_options {
				bool conservative; 
				bool use_finish_times; 
				int budget;
			};
			const Merge_options merge_opts;
			Nodes_storage nodes_storage;
			Nodes_map nodes_by_key;
			Object_pool<Node> node_pool;
			Object_pool<State> state_pool;

#ifdef CONFIG_PARALLEL
			// Thread-safe statistics and control
			std::atomic<unsigned long> num_nodes, num_states, num_edges;
			mutable std::vector<std::unique_ptr<std::mutex>> rta_mutexes;
			bool parallel_enabled;
			unsigned int num_threads;
			
			// TBB task arena for thread control 
			std::unique_ptr<tbb::task_arena> task_arena_;
#else
			unsigned long num_nodes, num_states, num_edges;
#endif

			// updated only by main thread - no protection needed
			unsigned long long current_job_count, max_width;
			std::vector<std::pair<unsigned long, unsigned long>> width;

			Processor_clock cpu_time;
			Memory_monitor mem_consumption;
			const double timeout;
			const long max_mem; // in kiB
			const unsigned int num_cpus;
			const std::vector<Interval<Time>> cores_initial_state;

			State_space_data<Time> state_space_data;

			State_space(const Workload& jobs,
				const Precedence_constraints& edges,
				const Abort_actions& aborts,
				const Mutex_constraints& mutexes,
				const std::vector<Interval<Time>>& cores_initial_state,
#ifdef CONFIG_ANALYSIS_EXTENSIONS
				const Problem_extensions& problem_extensions,
#endif
				Merge_options merge_options,
				double max_cpu_time = 0,
				long max_memory = 0,
				unsigned int max_depth = 0,
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
			)	: state_space_data(jobs, edges, aborts, mutexes, num_cpus)
				, aborted(false)
				, timed_out(false)
				, observed_deadline_miss(false)
				, deadline_miss_state(nullptr)
				, deadline_miss_node(nullptr)
				, be_naive(false)		
				, timeout(max_cpu_time)
				, mem_out(false)
				, max_mem(max_memory)
				, max_depth(max_depth)
				, merge_opts(merge_options)
				, verbose(verbose)
#ifdef CONFIG_PARALLEL
				, parallel_enabled(parallel_enabled)
				, num_threads(num_threads)
#endif
				, num_nodes(0)
				, num_states(0)
				, num_edges(0)
				, max_width(0)
				, width(jobs.size(), { 0,0 })
				, rta(jobs.size())
#ifdef CONFIG_PARALLEL
				, rta_mutexes(jobs.size())
#endif
				, current_job_count(0)
				, num_cpus(cores_initial_state.size())
				, cores_initial_state(cores_initial_state)
				, early_exit(early_exit)
				, nodes_storage(2)
#ifdef CONFIG_COLLECT_SCHEDULE_GRAPH
				, log(log_opts.log)
				, logger(log_opts.log_cond)
#endif
#ifdef CONFIG_PRUNING
				, pruning_active(pruning_active)
				, secateur(pruning_cond)
#endif
			{
#ifdef CONFIG_PARALLEL
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
#endif // CONFIG_PARALLEL
#ifdef CONFIG_ANALYSIS_EXTENSIONS
				// check if the taskchains extension is registered
				auto tc_ext = problem_extensions.get<Taskchains_analysis::Taskchains_problem_extension<Time>>();
				if (tc_ext)
				{
					// If yes, activate task chains analysis
					Taskchains_analysis::Taskchains_analysis_extension<Time>::activate(jobs, tc_ext->taskchains);
				}
#endif // CONFIG_ANALYSIS_EXTENSIONS
			}

		private:
			bool verbose;

#ifdef CONFIG_COLLECT_SCHEDULE_GRAPH
			bool log = false; // whether to log the schedule graph
			SAG_logger<Time> logger; 
#endif
#ifdef CONFIG_PRUNING
			bool pruning_active = false; // whether to use pruning
			Secateur<Time> secateur; // pruning tool
#endif

#ifdef CONFIG_PARALLEL
			// Thread-safe helper methods
			void count_edge_safe()
			{
				if (parallel_enabled) {
					num_edges.fetch_add(1, std::memory_order_relaxed);
				} else {
					num_edges++;
				}
			}
			
			void increment_nodes_safe()
			{
				if (parallel_enabled) {
					num_nodes.fetch_add(1, std::memory_order_relaxed);
				} else {
					num_nodes++;
				}
			}
			
			void increment_states_safe()
			{
				if (parallel_enabled) {
					num_states.fetch_add(1, std::memory_order_relaxed);
				} else {
					num_states++;
				}
			}
			
			void decrement_states_safe(int count)
			{
				if (parallel_enabled) {
					num_states.fetch_sub(count, std::memory_order_relaxed);
				} else {
					num_states -= count;
				}
			}
#else
			void count_edge_safe()
			{
				num_edges++;
			}
			
			void increment_nodes_safe()
			{
				num_nodes++;
			}
			
			void increment_states_safe()
			{
				num_states++;
			}
			
			void decrement_states_safe(int count)
			{
				num_states -= count;
			}
#endif

			void count_edge()
			{
				count_edge_safe();
			}

			void update_finish_times(Response_times& r, const Job_index id,
				Interval<Time> range)
			{
				if (!r[id].valid) {
					r[id].valid = true;
					r[id].rt = range;
				}
				else {
					r[id].rt |= range;
				}
				DM("RTA " << id << ": " << r[id].rt << std::endl);
			}

			void update_finish_times(
				Response_times& r, const Job<Time>& j, Interval<Time> range)
			{
				update_finish_times(r, j.get_job_index(), range);
				if (j.exceeds_deadline(range.upto())) {
					observed_deadline_miss = true;

					if (early_exit)
						aborted = true;
				}
			}

			void update_finish_times(const Job<Time>& j, Interval<Time> range)
			{
#ifdef CONFIG_PARALLEL
				if (parallel_enabled) {
					Job_index j_idx = j.get_job_index();
					std::lock_guard<std::mutex> lock(*rta_mutexes[j_idx]);
					update_finish_times(rta, j, range);
				} else {
					update_finish_times(rta, j, range);
				}
#else
				update_finish_times(rta, j, range);
#endif
			}

			void make_initial_node() {
				// construct initial state
				Node_ref n = new_node(0, cores_initial_state, state_space_data);
				State_ref s = new_state(cores_initial_state, state_space_data);
				n->add_state(s);
				increment_states_safe();
			}

			Nodes& nodes(const int depth = 0)
			{
				return nodes_storage[(current_job_count+ depth)% nodes_storage.size()];
			}

			template <typename... Args>
			Node_ref alloc_node(const int depth, Args&&... args)
			{
				Node_ref n = node_pool.acquire(std::forward<Args>(args)...);
#ifdef CONFIG_PARALLEL
				if (parallel_enabled) {
					nodes_storage[(current_job_count + depth) % nodes_storage.size()].push(n);
				} else {
					nodes_storage[(current_job_count + depth) % nodes_storage.size()].push(n);
				}
#else
				nodes_storage[(current_job_count + depth) % nodes_storage.size()].push_back(n);
#endif
				return n;
			}

			template <typename... Args>
			State_ref new_state(Args&&... args)
			{
				return state_pool.acquire(std::forward<Args>(args)...);
			}

			template <typename... Args>
			void new_or_merge_state(Node& n, Args&&... args)
			{
				// create a new state.
				State_ref new_s = new_state(std::forward<Args>(args)...);
#ifndef CONFIG_PARALLEL
				if (observed_deadline_miss)
					deadline_miss_state = new_s;
#endif
				// try to merge the new state with existing states in node n.
				if (!(n.get_states()->empty())) {
					int n_states_merged = n.merge_states(*new_s, merge_opts.conservative, merge_opts.use_finish_times, merge_opts.budget);
					if (n_states_merged > 0) {
						release_state(new_s); // if we could merge no need to keep track of the new state anymore
						decrement_states_safe(n_states_merged - 1);
					}
					else
					{
						n.add_state(new_s); // else add the new state to the node
						increment_states_safe();
					}
				}
				else
				{
					n.add_state(new_s); // else add the new state to the node
					increment_states_safe();

				}
			}

			void release_state(const std::shared_ptr<State>& s)
			{
				state_pool.release(s);
			}

			void release_node(const std::shared_ptr<Node>& n)
			{
				node_pool.release(n);
			}

			void cache_node(Node_ref n)
			{
				// create a new list if needed, or lookup if already existing
				auto res = nodes_by_key.emplace(
					std::make_pair(n->get_key(), Node_refs()));

				auto pair_it = res.first;
				Node_refs& list = pair_it->second;

				list.push_front(n);
			}

			template <typename... Args>
			Node_ref new_node(const int depth, Args&&... args)
			{
				Node_ref n = alloc_node(depth, std::forward<Args>(args)...);
				DM("new node - global " << n << std::endl);
				// add node to nodes_by_key map.
				cache_node(n);
				num_nodes++;
				return n;
			}

			void check_cpu_timeout()
			{
				if (timeout && get_cpu_time() > timeout) {
					aborted = true;
					timed_out = true;
				}
			}

			long check_memory_abort()
			{
				long mem = mem_consumption;
				if (max_mem && mem > max_mem) {
					aborted = true;
					mem_out = true;
				}
				return mem;
			}

			void check_depth_abort()
			{
				if (max_depth && current_job_count > max_depth)
					aborted = true;
			}

			bool unfinished(const Node& n, const Job<Time>& j) const
			{
				return n.job_not_dispatched(j.get_job_index());
			}

			// Check if any job is guaranteed to miss its deadline in any state in node new_n
			void check_for_deadline_misses(const Node& old_n, const Node& new_n)
			{
				auto check_from = old_n.get_first_state()->core_availability().min();

				// check if we skipped any jobs that are now guaranteed
				// to miss their deadline
				for (auto it = state_space_data.jobs_by_deadline.lower_bound(check_from);
					it != state_space_data.jobs_by_deadline.end(); it++) {
					const Job<Time>& j = *(it->second);
					auto pmin = j.get_min_parallelism();
					auto earliest = new_n.get_last_state()->core_availability(pmin).min();
					if (j.get_deadline() < earliest) {
						if (unfinished(new_n, j)) {
							DM("deadline miss: " << new_n << " -> " << j << std::endl);
							// This job is still incomplete but has no chance
							// of being scheduled before its deadline anymore.
							observed_deadline_miss = true;
							// if we stop at the first deadline miss, abort and create node in the graph for explanation purposes
							if (early_exit)
							{
								aborted = true;
								// create a dummy node for explanation purposes
								auto frange = new_n.get_last_state()->core_availability(pmin) + j.get_cost(pmin);
								Node_ref next =
									new_node(1, new_n, j, j.get_job_index(), state_space_data, 0, 0, 0);
								//const CoreAvailability empty_cav = {};
								State_ref next_s = new_state(
										*new_n.get_last_state(), j.get_job_index(), frange, frange, new_n.get_scheduled_jobs(),
										new_n.get_jobs_with_pending_start_successors(), new_n.get_jobs_with_pending_finish_successors(),
										new_n.get_ready_successor_jobs(), state_space_data, new_n.get_next_certain_source_job_release(), pmin
								);
								next->add_state(next_s);
								increment_states_safe();
								deadline_miss_state = next_s;
								deadline_miss_node = next;
								// update response times
								update_finish_times(j, frange);
								count_edge();
							}
							break;
						}
					}
					else
						// deadlines now after the next earliest finish time
						break;
				}
			}

			bool all_jobs_scheduled(const Node& n)
			{
				return (n.number_of_scheduled_jobs() == state_space_data.num_jobs());
			}

			// find next time by which a job is certainly ready in system state 's'
			Time next_certain_job_ready_time(const Node& n, const State& s) const
			{
				Time t_ws = std::min(s.next_certain_gang_source_job_dispatch(), s.next_certain_successor_jobs_dispatch());
				Time t_wos = n.get_next_certain_sequential_source_job_release();
				return std::min(t_wos, t_ws);
			}

			// assumes all predecessors of j have been dispatched
			// NOTE: we don't use Interval<Time> here because the
			//       Interval c'tor sorts its arguments.
			std::pair<Time, Time> start_times( const Node& n, const State& s, 
				const Job<Time>& j, const Time t_wc, const Time t_high,
				const Time t_avail, const unsigned int ncores = 1) const
			{
				auto rt = state_space_data.earliest_ready_time(n, s, j);
				auto at = s.core_availability(ncores).min();
				Time est = std::max(rt, at);

				DM("rt: " << rt << std::endl
					<< "at: " << at << std::endl);

				Time lst = std::min(t_wc,
					std::min(t_high, t_avail) - Time_model::constants<Time>::epsilon());

				DM("est: " << est << std::endl);
				DM("lst: " << lst << std::endl);

				return { est, lst };
			}

			Time earliest_job_abortion(const Abort_action<Time>& a)
			{
				return a.earliest_trigger_time() + a.least_cleanup_cost();
			}

			Time latest_job_abortion(const Abort_action<Time>& a)
			{
				return a.latest_trigger_time() + a.maximum_cleanup_cost();
			}

			Interval<Time> calculate_abort_time(const Job<Time>& j, Time est, Time lst, Time eft, Time lft)
			{
				auto j_idx = j.get_job_index();
				auto abort_action = state_space_data.abort_action_of(j_idx);
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
						auto eat = earliest_job_abortion(*abort_action);
						auto lat = latest_job_abortion(*abort_action);
						return Interval<Time>{ std::min(eft, eat), std::min(lft, lat) };
					}
				}
				else {
					// compute range of possible finish times
					return Interval<Time>{ eft, lft };
				}
			}

			bool dispatch(const Node_ref& n, const Job<Time>& j, Time t_wc_wos, Time t_high_wos)
			{
				// All states in node 'n' for which the job 'j' is eligible will 
				// be added to that same node. 
				// If such a node already exists, we keep a reference to it
				Node_ref next = nullptr;
				DM("--- global:dispatch() " << n << ", " << j << ", " << t_wc_wos << ", " << t_high_wos << std::endl);

				bool dispatched_one = false;

				// loop over all states in the node n
				const auto* n_states = n->get_states();

				for (const State_ref& s : *n_states)
				{
					// if the job priority is lower than than the minimum priority of the next dispatched job, it will not be dispatched next
					// (remember that lower number means higher priority)
					Job_ref next_dispatch_min_prio = s->get_next_dispatched_job_min_priority();
					if (next_dispatch_min_prio != NULL && next_dispatch_min_prio->higher_priority_than(j))
						continue;

					const auto& costs = j.get_all_costs();
					// check for all possible parallelism levels of the moldable gang job j (if j is not gang or not moldable than min_paralellism = max_parallelism and costs only constains a single element).
					//for (unsigned int p = j.get_max_parallelism(); p >= j.get_min_parallelism(); p--)
					for (auto it = costs.rbegin(); it != costs.rend(); it++)
					{
						unsigned int p = it->first;
						// Calculate t_wc and t_high
						Time t_wc = std::max(s->core_availability().max(), next_certain_job_ready_time(*n, *s));

						Time t_high_succ = state_space_data.next_certain_higher_priority_successor_job_ready_time(*n, *s, j, p);
						Time t_high_gang = state_space_data.next_certain_higher_priority_gang_source_job_ready_time(*n, *s, j, p, t_wc + 1);
						Time t_high = std::min(t_high_wos, std::min(t_high_gang, t_high_succ));

						// If j can execute on ncores+k cores, then 
						// the scheduler will start j on ncores only if 
						// there isn't ncores+k cores available
						Time t_avail = Time_model::constants<Time>::infinity();
						if (p < j.get_max_parallelism())
							t_avail = s->core_availability(std::prev(it)->first).max();

						DM("=== t_high = " << t_high << ", t_wc = " << t_wc << std::endl);
						auto _st = start_times(*n, *s, j, t_wc, t_high, t_avail, p);
						if (_st.first > t_wc || _st.first >= t_high || _st.first >= t_avail)
							continue; // nope, not next job that can be dispatched in state s, try the next state.

						Interval<Time> stimes(_st);
						//calculate the job finish time interval
						auto exec_time = it->second;
						Time eft = stimes.min() + exec_time.min();
						Time lft = stimes.max() + exec_time.max();

						// check for possible abort actions
						Interval<Time> ftimes = calculate_abort_time(j, _st.first, _st.second, eft, lft);

						// yep, job j is a feasible successor in state s
						dispatched_one = true;

						// update finish-time estimates
						update_finish_times(j, ftimes);

						// If be_naive, a new node and a new state should be created for each new job dispatch.
						if (be_naive)
							next = new_node(1, *n, j, j.get_job_index(), state_space_data, state_space_data.earliest_possible_job_release(*n, j), state_space_data.earliest_certain_source_job_release(*n, j), state_space_data.earliest_certain_sequential_source_job_release(*n, j));

						// if we do not have a pointer to a node with the same set of scheduled job yet,
						// try to find an existing node with the same set of scheduled jobs. Otherwise, create one.
						if (next == nullptr)
						{
							const auto pair_it = nodes_by_key.find(n->next_key(j));
							if (pair_it != nodes_by_key.end()) {
								Job_set new_sched_jobs{ n->get_scheduled_jobs(), j.get_job_index() };
								for (Node_ref other : pair_it->second) {
									if (other->get_scheduled_jobs() == new_sched_jobs)
									{
										next = other;
										DM("=== dispatch: next exists." << std::endl);
										break;
									}
								}
							}
							// If there is no node yet, create one.
							if (next == nullptr)
								next = new_node(1, *n, j, j.get_job_index(), state_space_data, state_space_data.earliest_possible_job_release(*n, j), state_space_data.earliest_certain_source_job_release(*n, j), state_space_data.earliest_certain_sequential_source_job_release(*n, j));
						}

						// next should always exist at this point, possibly without states in it
						// create a new state resulting from scheduling j in state s on p cores and try to merge it with an existing state in node 'next'.							
						new_or_merge_state(*next, *s, j.get_job_index(),
							stimes, ftimes, next->get_scheduled_jobs(),
							next->get_jobs_with_pending_start_successors(), next->get_jobs_with_pending_finish_successors(),
							next->get_ready_successor_jobs(), state_space_data, next->get_next_certain_source_job_release(), p
						);

#ifdef CONFIG_COLLECT_SCHEDULE_GRAPH
						if(log)
							logger.log_job_dispatched(n, j, stimes, ftimes, p, next, current_job_count);
#endif

						// make sure we didn't skip any jobs which would then certainly miss its deadline
						// only do that if we stop the analysis when a deadline miss is found 
						if (be_naive && early_exit) {
							check_for_deadline_misses(*n, *next);
						}

						count_edge();

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
				if (early_exit && !be_naive && next != nullptr)
					check_for_deadline_misses(*n, *next);

				return dispatched_one;
			}

			void explore(const Node_ref& n)
			{
				bool found_one = false;
				bool pruned = false;

				DM("---- global:explore(node)" << n->finish_range() << std::endl);

				// (0) define the window of interest
				auto t_min = n->earliest_job_release();
				// latest time some unfinished job is certainly ready
				auto nxt_ready_job = n->next_certain_job_ready_time();
				// latest time all cores are certainly available
				auto avail_max = n->latest_core_availability();
				// latest time by which a work-conserving scheduler
				// certainly schedules some job
				auto upbnd_t_wc = std::max(avail_max, nxt_ready_job);

				DM(n << std::endl);
				DM("t_min: " << t_min << std::endl
					<< "nxt_ready_job: " << nxt_ready_job << std::endl
					<< "avail_max: " << avail_max << std::endl
					<< "upbnd_t_wc: " << upbnd_t_wc << std::endl);

				//check all jobs that may be eligible to be dispatched next
				// part 1: check source jobs (i.e., jobs without precedence constraints) that are potentially eligible
				for (auto it = state_space_data.jobs_by_earliest_arrival.lower_bound(t_min);
					it != state_space_data.jobs_by_earliest_arrival.end();
					it++)
				{
					const Job<Time>& j = *it->second;
					DM(j << " (" << j.get_job_index() << ")" << std::endl);
					// stop looking once we've left the window of interest
					if (j.earliest_arrival() > upbnd_t_wc)
						break;

					if (!unfinished(*n, j))
						continue;

					Time t_high_wos = state_space_data.next_certain_higher_priority_seq_source_job_release(*n, j, upbnd_t_wc + 1);
					// if there is a higher priority job that is certainly ready before job j is released at the earliest, 
					// then j will never be the next job dispached by the scheduler
					if (t_high_wos <= j.earliest_arrival())
						continue;

#ifdef CONFIG_PRUNING
					// if pruning is active, check whether the job is eligible for dispatching
					if (pruning_active && secateur.prune_branch(j, *n)) {
						pruned = true;
						continue;
					}
#endif
					found_one |= dispatch(n, j, upbnd_t_wc, t_high_wos);
				}
				// part 2: check ready successor jobs (i.e., jobs with precedence constraints that are completed) that are potentially eligible
				for (auto it = n->get_ready_successor_jobs().begin();
					it != n->get_ready_successor_jobs().end();
					it++)
				{
					const Job<Time>& j = **it;
					DM(j << " (" << j.get_job_index() << ")" << std::endl);

					// don't look outside the window of interest
					if (j.earliest_arrival() > upbnd_t_wc)
						continue;

					// Since this job is is recorded as ready in the state, it better
					// be incomplete...
					assert(unfinished(*n, j));

					Time t_high_wos = state_space_data.next_certain_higher_priority_seq_source_job_release(*n, j, upbnd_t_wc + 1);
					// if there is a higher priority job that is certainly ready before job j is released at the earliest, 
					// then j will never be the next job dispached by the scheduler
					if (t_high_wos <= j.earliest_arrival())
						continue;

#ifdef CONFIG_PRUNING
					// if pruning is active, check whether the job is eligible for dispatching
					if (pruning_active && secateur.prune_branch(j, *n)) {
						pruned = true;
						continue;
					}
#endif

					found_one |= dispatch(n, j, upbnd_t_wc, t_high_wos);
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

			// naive: no state merging
			void explore_naively()
			{
				be_naive = true;
				explore();
			}

			void explore()
			{
				long long last_time = get_cpu_time();
				unsigned int target_depth;
				
				if (verbose) {
					std::cout << "0%; 0s";
					target_depth = std::max((unsigned int)state_space_data.num_jobs(), max_depth);
				}
				
				int last_num_states = num_states;
				make_initial_node();

				while (current_job_count < state_space_data.num_jobs()) {
					Nodes& exploration_front = nodes();
#ifdef CONFIG_PARALLEL
					unsigned long long n = exploration_front.unsafe_size();
#else
					unsigned long long n = exploration_front.size();
#endif
					if (n == 0)
					{
						aborted = true;
						break;
					}

					// keep track of exploration front width (main thread only - no protection needed)
					max_width = std::max(max_width, n);
					int current_states = num_states;
					width[current_job_count] = { n, current_states - last_num_states };
					last_num_states = current_states;

					long long time = get_cpu_time();
					if (time > last_time + 4) {
						// check memory usage
						long mem = check_memory_abort();
						if (verbose) {
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
					check_cpu_timeout();
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
										if (node.unique()) {
											auto states = node->get_states();
											for (auto s = states->begin(); s != states->end(); s++) {
												release_state(*s);
											}
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
							check_cpu_timeout();
							if (aborted)
								break;

							if (node.unique()) {
								auto states = node->get_states();
								for (auto s = states->begin(); s != states->end(); s++) {
									release_state(*s);
								}
								release_node(node);
							}
						}
					}
#else
					// Original sequential processing
					for (const Node_ref& node : exploration_front) {
						explore(node);
						check_cpu_timeout();
						if (aborted)
							break;

						// If the node is not refered to anymore, we can reuse the node and state objects for other states.
						if (node.unique()) {
							auto states = node->get_states();
							for (auto s = states->begin(); s != states->end(); s++) {
								release_state(*s);
							}
							release_node(node);
						}
					}
#endif

					// clean up the state cache if necessary
					if (!be_naive)
						nodes_by_key.clear();

					nodes().clear();
					current_job_count++;
				}
				if (verbose) {
#if __APPLE__
					std::cout << "\r100%; " << get_cpu_time() << "s; " << mem_consumption / 1024 << "KiB" << std::endl << "Terminating" << std::endl;
#else
					std::cout << "\r100%; " << get_cpu_time() << "s; " << mem_consumption / 1024 << "MiB" << std::endl << "Terminating" << std::endl;
#endif
				}
				// clean out any remaining nodes
				nodes_storage.clear();
			}

#ifdef CONFIG_COLLECT_SCHEDULE_GRAPH
		public:
			void print_dot_file(std::ostream& o, Dot_file_config config) {
				logger.print_dot_file(o, state_space_data.jobs, config);
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
