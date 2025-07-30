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

#include "config.h"

#ifdef CONFIG_PARALLEL
#include "tbb/concurrent_hash_map.h"
#include "tbb/enumerable_thread_specific.h"
#include "tbb/parallel_for.h"
#include <atomic>
#endif

#include "problem.hpp"
#include "global/state_space_data.hpp"
#include "clock.hpp"

#include "global/node.hpp"
#include "global/state.hpp"
#include "global/cluster.hpp"

namespace NP {

	namespace Global {

		template<class Time> class State_space
		{
		public:

			typedef Scheduling_problem<Time> Problem;
			typedef typename Scheduling_problem<Time>::Workload Workload;
			typedef typename Scheduling_problem<Time>::Precedence_constraints Precedence_constraints;
			typedef typename Scheduling_problem<Time>::Abort_actions Abort_actions;
			typedef Schedule_state<Time> State;
			typedef Cluster_state<Time> Clstr_state;
			typedef typename std::vector<Interval<Time>> CoreAvailability;

			typedef Schedule_node<Time> Node;

			static State_space* explore(
				const Problem& prob,
				const Analysis_options& opts)
			{
				if (opts.verbose)
					std::cout << "Starting" << std::endl;

				State_space* s = new State_space(prob.jobs, prob.prec, prob.aborts, prob.num_processors, 
					{ opts.merge_conservative, opts.merge_use_job_finish_times, opts.merge_depth }, opts.timeout, opts.max_depth, opts.early_exit, opts.verbose);
				s->be_naive = opts.be_naive;
				if (opts.verbose)
					std::cout << "Analysing" << std::endl;
				s->cpu_time.start();
				s->explore();
				s->cpu_time.stop();
				return s;
			}

			// convenience interface for tests
			static State_space* explore_naively(
				const Workload& jobs,
				const std::vector<unsigned int>& num_cpus = { 1 })
			{
				Problem p{ jobs, num_cpus };
				Analysis_options o;
				o.be_naive = true;
				return explore(p, o);
			}

			// convenience interface for tests
			static State_space* explore_naively(
				const Workload& jobs,
				const unsigned int num_cpus)
			{
				Problem p{ jobs, {num_cpus} };
				Analysis_options o;
				o.be_naive = true;
				return explore(p, o);
			}

			// convenience interface for tests
			static State_space* explore(
				const Workload& jobs,
				const std::vector<unsigned int>& num_cpus = { 1 })
			{
				Problem p{ jobs, num_cpus };
				Analysis_options o;
				return explore(p, o);
			}

			// convenience interface for tests
			static State_space* explore(
				const Workload& jobs,
				const unsigned int num_cpus)
			{
				Problem p{ jobs, {num_cpus} };
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
				if (rta[j].valid) {
					return rta[j].rt;
				}
				else {
					return Interval<Time>{0, Time_model::constants<Time>::infinity()};
				}
			}

			bool is_schedulable() const
			{
				return !aborted && !observed_deadline_miss;
			}

			bool was_timed_out() const
			{
				return timed_out;
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

			typedef std::deque<Node> Nodes;
			typedef std::deque<State> States;

#ifdef CONFIG_PARALLEL
			typedef tbb::enumerable_thread_specific< Nodes > Split_nodes;
			typedef std::deque<Split_nodes> Nodes_storage;
#else
			typedef std::vector<Nodes> Nodes_storage;
#endif

#ifdef CONFIG_COLLECT_SCHEDULE_GRAPH

			struct Edge {
				const std::vector<const Job<Time>*> scheduled;
				const Node* source;
				const Node* target;
				const std::vector<Interval<Time>>  finish_range;
				const std::vector<unsigned int> parallelism;

				Edge(const std::vector<const Job<Time>*>& s, const Node* src, const Node* tgt,
					const std::vector<Interval<Time>>& fr, const std::vector<unsigned int> parallelism)
					: scheduled(s)
					, source(src)
					, target(tgt)
					, finish_range(fr)
					, parallelism(parallelism)
				{
				}

				bool deadline_miss_possible() const
				{
					bool deadline_miss = false;
					for (int i = 0; i < scheduled.size(); i++) {
						if(scheduled[i] != NULL)
							deadline_miss = deadline_miss || scheduled[i]->exceeds_deadline(finish_range[i].upto());
					}
					return deadline_miss;
				}

				Time earliest_finish_time(int i) const
				{
					return finish_range[i].from();
				}

				Time latest_finish_time(int i) const
				{
					return finish_range[i].upto();
				}

				Time earliest_start_time(int i) const
				{
					return finish_range[i].from() - scheduled[i]->least_exec_time();
				}

				Time latest_start_time(int i) const
				{
					return finish_range[i].upto() - scheduled[i]->maximal_exec_time();
				}

				unsigned int parallelism_level(int i) const
				{
					return parallelism[i];
				}
			};

			const std::deque<Edge>& get_edges() const
			{
				return edges;
			}

			const Nodes_storage& get_nodes() const
			{
				return nodes_storage;
			}


#endif
		private:

			typedef Node* Node_ref;
			typedef typename std::forward_list<Node_ref> Node_refs;
			typedef State* State_ref;
			typedef typename std::forward_list<State_ref> State_refs;

#ifdef CONFIG_PARALLEL
			typedef tbb::concurrent_hash_map<hash_value_t, Node_refs> Nodes_map;
			typedef typename Nodes_map::accessor Nodes_map_accessor;
#else
			typedef std::unordered_map<std::pair<unsigned int, hash_value_t>, Node_refs> Nodes_map;
#endif
			typedef const Job<Time>* Job_ref;
			struct Time_bounds {
				Time t_high_upbnd;
				Time earliest_next_release;
				Time latest_next_source_job_release;
				Time latest_next_seq_source_job_release;
			};
			typedef std::pair<Job_ref, Time_bounds> Job_with_time_bounds;
			typedef std::deque<Job_with_time_bounds> Set_of_jobs_and_bounds; // set of jobs with associated timing bounds 

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

#ifdef CONFIG_COLLECT_SCHEDULE_GRAPH
			std::deque<Edge> edges;
#endif
			// Similar to uni/space.hpp, make rta a 

			Response_times rta;

#ifdef CONFIG_PARALLEL
			tbb::enumerable_thread_specific<Response_times> partial_rta;
#endif
			bool verbose;
			bool aborted;
			bool timed_out;
			bool observed_deadline_miss;
			bool early_exit;			
			bool be_naive;
			const unsigned int max_depth;
			struct Merge_options {
				bool conservative; 
				bool use_finish_times; 
				int budget;
			};
			const Merge_options merge_opts;

			Nodes_storage nodes_storage;
			Nodes_map nodes_by_key;

#ifdef CONFIG_PARALLEL
			std::atomic_ulong num_nodes, num_states, num_edges;
#else
			unsigned long num_nodes, num_states, num_edges;
#endif
			// updated only by main thread
			unsigned long current_job_count, max_width;
			std::vector<std::pair<unsigned long, unsigned long>> width;

#ifdef CONFIG_PARALLEL
			tbb::enumerable_thread_specific<unsigned long> edge_counter;
#endif
			Processor_clock cpu_time;
			const double timeout;
			const unsigned int num_clusters;
			const std::vector<unsigned int> num_cpus;

			State_space_data<Time> state_space_data;

			State_space(const Workload& jobs,
				const Precedence_constraints& edges,
				const Abort_actions& aborts,
				const std::vector<unsigned int>& num_cpus,
				Merge_options merge_options,
				double max_cpu_time = 0,
				unsigned int max_depth = 0,
				bool early_exit = true,
				bool verbose = false,
				bool use_supernodes = true)
				: state_space_data(jobs, edges, aborts, num_cpus)
				, aborted(false)
				, timed_out(false)
				, observed_deadline_miss(false)
				, be_naive(false)		
				, timeout(max_cpu_time)
				, max_depth(max_depth)
				, merge_opts(merge_options)
				, verbose(verbose)
				, num_nodes(0)
				, num_states(0)
				, num_edges(0)
				, max_width(0)
				, width(jobs.size(), { 0,0 })
				, rta(jobs.size())
				, current_job_count(0)
				, num_clusters(num_cpus.size())
				, num_cpus(num_cpus)
				, early_exit(early_exit)
#ifdef CONFIG_COLLECT_SCHEDULE_GRAPH
				, nodes_storage(jobs.size() + 1)
#else
				, nodes_storage(num_cpus.size() + 1)
#endif
#ifdef CONFIG_PARALLEL
				, partial_rta(jobs.size())
#endif
			{
			}

		private:

			void count_edge()
			{
#ifdef CONFIG_PARALLEL
				edge_counter.local()++;
#else
				num_edges++;
#endif
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
				Response_times& r =
#ifdef CONFIG_PARALLEL
					partial_rta.local();
#else
					rta;
#endif
				update_finish_times(r, j, range);
			}

			// Check if any job is guaranteed to miss its deadline in any state in the new node
			void check_for_deadline_misses(const Node& old_n, const Node& new_n)
			{
				for(int i=0; i<num_clusters; i++) {
					auto check_from = old_n.finish_range(i).min();

					// check if we skipped any jobs that are now guaranteed
					// to miss their deadline
					for (auto it = state_space_data.jobs_by_deadline[i].lower_bound(check_from);
						it != state_space_data.jobs_by_deadline[i].end(); it++)
					{
						const Job<Time>& j = *(it->second);
						auto pmin = j.get_min_parallelism();
						auto latest = new_n.finish_range(i).max();
						if (j.get_deadline() < latest) {
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
									auto frange = new_n.finish_range(i) + j.get_cost(pmin);
									Node& next =
										new_node(1, new_n, j, j.get_job_index(), state_space_data.predecessors_suspensions, state_space_data.successors_suspensions, 0, 0, 0);
									//const CoreAvailability empty_cav = {};
									State& next_s = new_state(*(new_n.get_states()->front()), j, frange, frange, new_n.get_scheduled_jobs(), new_n.get_ready_successor_jobs(), state_space_data, 0, pmin);
									next.add_state(&next_s);
									num_states++;

									// update response times
									update_finish_times(j, frange);
	#ifdef CONFIG_COLLECT_SCHEDULE_GRAPH
									std::vector<Job_ref> missed_job(num_clusters, NULL);
									std::vector<Interval<Time>> ftimes(num_clusters, { 0,0 });
									std::vector<unsigned int> p(num_clusters, 0);
									missed_job[j.get_affinity()] = &j;
									ftimes[j.get_affinity()] = frange;
									p[j.get_affinity()] = pmin;
									edges.emplace_back(missed_job, &new_n, &next, ftimes, p);
	#endif
									count_edge();
								}
								break;
							}
						}
						else
							// deadlines now after the next latest finish time
							break;
					}
				}
			}

			// construct initial state
			void make_initial_node()
			{				
				Node& n = new_node(0, num_cpus, state_space_data);
				State& s = new_state(num_cpus, state_space_data);
				n.add_state(&s);
				num_states++;
			}

			Nodes& nodes(const int depth=0)
			{
#ifdef CONFIG_PARALLEL
				return nodes_storage.back().local();
#else
				size_t layer = (current_job_count + depth) % nodes_storage.size();
				return nodes_storage[layer];
#endif
			}

			template <typename... Args>
			Node_ref alloc_node(const int depth, Args&&... args)
			{
				Nodes& n_storage = nodes(depth);
				n_storage.emplace_back(std::forward<Args>(args)...);
				Node_ref n = &(*(--n_storage.end()));

				// make sure we didn't screw up...
				assert(
					(n->number_of_scheduled_jobs() ==0 && num_states == 0) // initial state
					|| (n->number_of_scheduled_jobs() > current_job_count && num_states > 0) // normal State
				);

				return n;
			}

			template <typename... Args>
			State& new_state(Args&&... args)
			{
				return *(new State(std::forward<Args>(args)...));
			}


			template <typename... Args>
			void new_or_merge_state(Node& n, Args&&... args)
			{
				// create a new state.
				State& new_s = new_state(std::forward<Args>(args)...);

				// try to merge the new state with existing states in node n.
				if (!(n.get_states()->empty())) {
					int n_states_merged = n.merge_states(new_s, merge_opts.conservative, merge_opts.use_finish_times, merge_opts.budget);
					if (n_states_merged > 0) {
						delete& new_s; // if we could merge no need to keep track of the new state anymore
						num_states -= (n_states_merged - 1);
					}
					else
					{
						n.add_state(&new_s); // else add the new state to the node
						num_states++;
					}
				}
				else
				{
					n.add_state(&new_s); // else add the new state to the node
					num_states++;
				}
			}


#ifdef CONFIG_PARALLEL
			#warning  "Parallel code is not updated for clusters."

			// make node available for fast lookup
			void insert_cache_node(Nodes_map_accessor& acc, Node_ref n)
			{
				assert(!acc.empty());

				Node_refs& list = acc->second;
				list.push_front(n);
			}

			template <typename... Args>
			Node& new_node_at(Nodes_map_accessor& acc, Args&&... args)
			{
				assert(!acc.empty());
				Node_ref n = alloc_node(std::forward<Args>(args)...);
				DM("new node - global " << n << std::endl);
				// add node to nodes_by_key map.
				insert_cache_node(acc, n);
				num_nodes++;
				return *n;
			}

			template <typename... Args>
			Node& new_node(Args&&... args)
			{
				Nodes_map_accessor acc;
				Node_ref n = alloc_node(std::forward<Args>(args)...);
				while (true) {
					if (nodes_by_key.find(acc, n->get_key()) || nodes_by_key.insert(acc, n->get_key())) {
						DM("new node - global " << n << std::endl);
						// add node to nodes_by_key map.
						insert_cache_node(acc, n);
						num_nodes++;
						return *n;
					}
				}
			}

#else
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
			Node& new_node(const int n_jobs_dispatched, Args&&... args)
			{
				Node_ref n = alloc_node(n_jobs_dispatched, std::forward<Args>(args)...);
				DM("new node - global " << n << std::endl);
				// add node to nodes_by_key map.
				cache_node(n);
				num_nodes++;
				return *n;
			}
#endif

			void check_cpu_timeout()
			{
				if (timeout && get_cpu_time() > timeout) {
					aborted = true;
					timed_out = true;
				}
			}

			void check_depth_abort()
			{
				if (max_depth && current_job_count > max_depth)
					aborted = true;
			}

			bool unfinished(const Node& n, const Job<Time>& j) const
			{
				return n.job_incomplete(j.get_job_index());
			}

			// Check wether a job is ready (not dspatched yet and all its predecessors are completed).
			bool ready(const Node& n, const Job<Time>& j) const
			{
				return n.job_incomplete(j.get_job_index()) && n.job_ready(state_space_data.predecessors_of(j));
			}

			bool all_jobs_scheduled(const Node& n)
			{
				return (n.number_of_scheduled_jobs() == state_space_data.num_jobs());
			}

			// find next time by which a job is certainly ready in system state 's' on cluster 'cluster_id'
			Time next_certain_job_ready_time(const Node& n, const State& s, const unsigned int cluster_id) const
			{
				const auto& cs = s.cluster(cluster_id);
				Time t_ws = std::min(cs.next_certain_gang_source_job_disptach(), cs.next_certain_successor_jobs_disptach());
				Time t_wos = n.get_next_certain_sequential_source_job_release(cluster_id);
				return std::min(t_wos, t_ws);
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

			// assumes j is ready
			// NOTE: we don't use Interval<Time> here because the
			//       Interval c'tor sorts its arguments.
			std::pair<Time, Time> start_times(
				const State& s, const Job<Time>& j, const Time t_wc, const Time t_high,
				const Time t_avail, const unsigned int ncores = 1) const
			{
				const auto& cs = s.cluster(j.get_affinity());
				auto rt = state_space_data.earliest_ready_time(s, j);
				auto at = cs.core_availability(ncores).min();
				Time est = std::max(rt, at);

				DM("rt: " << rt << std::endl
					<< "at: " << at << std::endl);

				Time lst = std::min(t_wc,
					std::min(t_high, t_avail) - Time_model::constants<Time>::epsilon());

				DM("est: " << est << std::endl);
				DM("lst: " << lst << std::endl);

				return { est, lst };
			}

			bool dispatch(const Node& n, const Job_with_time_bounds& disp_j, const unsigned int affinity)
			{
				// All states in node 'n' for which the job 'j' is eligible will 
				// be added to that same node. 
				// If such a node already exists, we keep a reference to it
				Node_ref next = nullptr;
				bool dispatched_one = false;

				const Job_ref j = disp_j.first;
				Time t_high_wos = disp_j.second.t_high_upbnd;

				// loop over all states in the node n
				const auto* n_states = n.get_states();
				for (const State_ref& s : *n_states)
				{
					const auto& cs = s->cluster(affinity);
					
					// check for all possible parallelism levels of the moldable gang job j (if j is not gang or not moldable than min_paralellism = max_parallelism and costs only constains a single element).
					const auto& costs = j->get_all_costs();
					for (auto it = costs.rbegin(); it != costs.rend(); it++)
					{
						unsigned int p = it->first;
						// Calculate t_wc and t_high
						Time t_wc = std::max(cs.core_availability().max(), next_certain_job_ready_time(n, *s, affinity));

						Time t_high_succ = state_space_data.next_certain_higher_priority_successor_job_ready_time(n, *s, *j, p, t_wc + 1);
						Time t_high_gang = state_space_data.next_certain_higher_priority_gang_source_job_ready_time(n, *s, *j, p, t_wc + 1);
						Time t_high = std::min(t_high_wos, std::min(t_high_gang, t_high_succ));

						// If j can execute on ncores+k cores, then 
						// the scheduler will start j on ncores only if 
						// there isn't ncores+k cores available
						Time t_avail = Time_model::constants<Time>::infinity();
						if (p < j->get_max_parallelism())
							t_avail = cs.core_availability(std::prev(it)->first).max();

						DM("=== t_high = " << t_high << ", t_wc = " << t_wc << std::endl);
						auto _st = start_times(*s, *j, t_wc, t_high, t_avail, p);
						if (_st.first > t_wc || _st.first >= t_high || _st.first >= t_avail)
							continue; // nope, not next job that can be dispatched in state s, try the next state.

						Interval<Time> stimes(_st);
						//calculate the job finish time interval
						auto exec_time = it->second;
						Time eft = stimes.min() + exec_time.min();
						Time lft = stimes.max() + exec_time.max();

						// check for possible abort actions
						Interval<Time> ftimes = calculate_abort_time(*j, _st.first, _st.second, eft, lft);

						// yep, job j is a feasible successor in state s
						dispatched_one = true;

						// update finish-time estimates
						update_finish_times(*j, ftimes);

						// If be_naive, a new node and a new state should be created for each new job dispatch.
						if (be_naive)
							next = &(new_node(1, n, *j, j->get_job_index(), state_space_data.predecessors_suspensions, state_space_data.successors_suspensions, disp_j.second.earliest_next_release, disp_j.second.latest_next_source_job_release, disp_j.second.latest_next_seq_source_job_release));

						// if we do not have a pointer to a node with the same set of scheduled job yet,
						// try to find an existing node with the same set of scheduled jobs. Otherwise, create one.
						if (next == nullptr)
						{
							const auto pair_it = nodes_by_key.find(n.next_key(*j));
							if (pair_it != nodes_by_key.end()) {
								Dispatched_job_set new_sched_jobs{ n.get_scheduled_jobs(), j->get_job_index() };
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
								next = &(new_node(1, n, *j, j->get_job_index(), state_space_data.predecessors_suspensions, state_space_data.successors_suspensions, disp_j.second.earliest_next_release, disp_j.second.latest_next_source_job_release, disp_j.second.latest_next_seq_source_job_release));
						}

						// next should always exist at this point, possibly without states in it
						// create a new state resulting from scheduling j in state s on p cores and try to merge it with an existing state in node 'next'.							
						new_or_merge_state(*next, *s, *j,
							stimes, ftimes, next->get_scheduled_jobs(), /*next->get_jobs_with_pending_successors(),*/ next->get_ready_successor_jobs(), state_space_data, next->get_next_certain_source_job_release(affinity), p);

#ifdef CONFIG_COLLECT_SCHEDULE_GRAPH
						//if (log)
						//	logger.log_job_dispatched(n, *j, stimes, ftimes, p, next, current_job_count);
						edges.emplace_back(
							std::vector<Job_ref>({j}),
							&n, next,
							std::vector<Interval<Time>>({stimes}),
							std::vector<unsigned int>({p}));
#endif

						// make sure we didn't skip any jobs which would then certainly miss its deadline
						// only do that if we stop the analysis when a deadline miss is found 
						if (be_naive && early_exit) {
							check_for_deadline_misses(n, *next);
						}

						count_edge();

						if (observed_deadline_miss) {
/*#ifndef CONFIG_PARALLEL
							deadline_miss_node = next;
#endif*/
							return dispatched_one;
						}
					}
				}

				// if we stop the analysis when a deadline miss is found, then check whether a job will certainly miss 
				// its deadline because of when the processors become free next.
				// if we are not using the naive exploration, we check for deadline misses only once per job dispatched
				if (early_exit && !be_naive && next != nullptr)
					check_for_deadline_misses(n, *next);

				return dispatched_one;
			}

			void explore(const Node& n)
			{
				bool found_one = false;

				DM("---- global:explore(node)" << n.finish_range() << std::endl);

				// (0) define the time window of interest
				Time upbnd_t_wc_any = Time_model::constants<Time>::infinity();
				std::vector<Time> upbnd_t_wc_per_cluster;
				upbnd_t_wc_per_cluster.reserve(num_clusters);
				std::vector<Time> t_min;
				t_min.reserve(num_clusters);

				for (int i = 0; i < num_clusters; i++) {
					t_min.push_back(n.earliest_job_release(i));
					// latest time some unfinished job is certainly ready
					auto nxt_ready_job = n.next_certain_job_ready_time(i);
					// latest time all cores are certainly available
					auto avail_max = n.get_latest_core_availability(i);
					// latest time by which a work-conserving scheduler
					// certainly schedules some job
					upbnd_t_wc_per_cluster.push_back(std::max(avail_max, nxt_ready_job));
					upbnd_t_wc_any = std::min(upbnd_t_wc_any, upbnd_t_wc_per_cluster[i]);
				}

				std::vector<Set_of_jobs_and_bounds> eligible_jobs_per_cluster(num_clusters);
				
				int independent_cluster = -1;
				for (int cluster_id = 0; cluster_id < num_clusters; cluster_id++) {
					bool is_independent = true;
					bool found_one_on_c = false;
					//check all jobs that may be eligible to be dispatched next
					for (auto it = state_space_data.jobs_by_earliest_arrival_by_cluster[cluster_id].lower_bound(t_min[cluster_id]);
						it != state_space_data.jobs_by_earliest_arrival_by_cluster[cluster_id].end();
						it++)
					{
						const Job<Time>& j = *it->second;
						DM(j << " (" << index_of(j) << ")" << std::endl);
						// stop looking once we've left the window of interest
						if (j.earliest_arrival() > upbnd_t_wc_per_cluster[cluster_id])
							break;

						// Job could be not ready due to precedence constraints
						if (ready(n, j)) {
							// Since this job is released in the future, it better
							// be incomplete...
							assert(unfinished(n, j));

							Time t_high_wos = state_space_data.next_certain_higher_priority_seq_source_job_release(n, j, upbnd_t_wc_per_cluster[cluster_id] + 1);
							// if there is a higher priority job that is certainly ready before job j is released at the earliest, 
							// then j will never be the next job dispached by the scheduler
							if (t_high_wos <= j.earliest_arrival())
								continue;

							// calculate lower and upper bounds on the next job releases if j gets dispatched
							Time earliest_next_job_rel = state_space_data.earliest_possible_job_release(n, j);
							Time latest_next_source_job_rel = state_space_data.earliest_certain_source_job_release(n, j);
							Time latest_next_seq_source_job_rel = state_space_data.earliest_certain_sequential_source_job_release(n, j);

							// add j to the list of eligible jobs together with relevant timing information
							eligible_jobs_per_cluster[cluster_id].emplace_back(&j, Time_bounds{ t_high_wos, earliest_next_job_rel, latest_next_source_job_rel, latest_next_seq_source_job_rel });
							found_one = true;
							found_one_on_c = true;
						}
						else if (is_independent && n.job_dependent_on_other_cluster(j, state_space_data.predecessors_of(j), state_space_data.jobs, upbnd_t_wc_per_cluster[cluster_id])) {
							is_independent = false;
						}
					}
					if (found_one_on_c && is_independent) {
						independent_cluster = cluster_id;
						break;
					}
				}
				// check for a dead end
				if (!found_one && !all_jobs_scheduled(n)) {
					// out of options and we didn't schedule all jobs
					observed_deadline_miss = true;
					aborted = true;
					return;
				}

				bool dispatched_one = false;
				// if some cluster is independent, dispatch jobs on that clusters
				if (independent_cluster != -1) {
					for (const auto& j : eligible_jobs_per_cluster[independent_cluster]) {
						dispatched_one |= dispatch(n, j, independent_cluster);
					}
				}
				else
				{
					for (int cluster_id = 0; cluster_id < num_clusters; cluster_id++) {
						// if the earliest time a job may start on the cluster  
						// is later than when a job certainly starts on any cluster, 
						// we do not dispatch anything on that cluster
						if (t_min[cluster_id] > upbnd_t_wc_any)
							continue;

						for (const auto& j : eligible_jobs_per_cluster[cluster_id]) {
							// if a job may start before any other job, we dispatch it
							if (j.first->earliest_arrival() <= upbnd_t_wc_any)
								dispatched_one |= dispatch(n, j, cluster_id);
						}
					}
				}

				// check for a dead end
				if (!dispatched_one && !all_jobs_scheduled(n)) {
					// out of options and we didn't schedule all jobs
					observed_deadline_miss = true;
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
				int last_time;
				unsigned int target_depth;
				
				if (verbose) {
					std::cout << "0%";
					last_time = get_cpu_time();
					target_depth = std::max((unsigned int)state_space_data.num_jobs(), max_depth);
				}

				int last_num_states = 0;
				make_initial_node();

				while (current_job_count < state_space_data.num_jobs()) {
					unsigned long n;
#ifdef CONFIG_PARALLEL
					const auto& new_nodes_part = nodes_storage.back();
					n = 0;
					for (const Nodes& new_nodes : new_nodes_part) {
						n += new_nodes.size();
					}
#else
					Nodes& exploration_front = nodes();
					n = exploration_front.size();
#endif
					// id there is no node to explore  at the current depth, 
					// check next depth until depth is equal to the number of jobs to schedule
					if (n == 0)
					{
						current_job_count++; // current_job_count is the depth of the graph we currently explore
						continue;
					}
					
					// keep track of exploration front width
					max_width = std::max(max_width, n);
					width[current_job_count] = { n, num_states - last_num_states };
					last_num_states = num_states;

					if (verbose) {
						int time = get_cpu_time(); 
						if (time > last_time+4) { // update progress information approxmately every 4 seconds of runtime
							std::cout << "\r" << (int)(((double)current_job_count / target_depth) * 100) << "%";
							last_time = time;
						}
					}

					check_depth_abort();
					check_cpu_timeout();
					if (aborted)
						break;

#ifdef CONFIG_PARALLEL

					parallel_for(new_nodes_part.range(),
						[&](typename Split_nodes::const_range_type& r) {
							for (auto it = r.begin(); it != r.end(); it++) {
								const Nodes& new_nodes = *it;
								auto s = new_nodes.size();
								tbb::parallel_for(tbb::blocked_range<size_t>(0, s),
									[&](const tbb::blocked_range<size_t>& r) {
										for (size_t i = r.begin(); i != r.end(); i++)
											explore(new_nodes[i]);
									});
							}
						});

#else
					for (const Node& n : exploration_front) {
						explore(n);
						check_cpu_timeout();
						if (aborted)
							break;
					}
#endif

					// clean up the state cache if necessary
					if (!be_naive) {
						// remove nodes in the current front in nodes_by_key
						for (const Node& n : exploration_front) {
							nodes_by_key.erase(n.get_key());
						}
					}

					current_job_count++;

#ifndef CONFIG_COLLECT_SCHEDULE_GRAPH
					// If we don't need to collect all nodes, we can remove
					// all those that we are done with, which saves a lot of
					// memory.
#ifdef CONFIG_PARALLEL
					parallel_for(nodes_storage.front().range(),
						[](typename Split_nodes::range_type& r) {
							for (auto it = r.begin(); it != r.end(); it++)
								it->clear();
						});
#endif
					exploration_front.clear();
#endif
				}
				if (verbose)
					std::cout << "\r100%" << std::endl << "Terminating" << std::endl;

#ifdef CONFIG_PARALLEL
				// propagate any updates to the response-time estimates
				for (auto& r : partial_rta) {
					for (int i = 0; i < r.size(); ++i) {
						if (r[i].valid)
							update_finish_times(rta, i, r[i].rt);
					}
				}
#endif


#ifndef CONFIG_COLLECT_SCHEDULE_GRAPH
				// clean out any remaining nodes
				for (int i = 0; i < nodes_storage.size(); i++) {
#ifdef CONFIG_PARALLEL
					parallel_for(nodes_storage.front().range(),
						[](typename Split_nodes::range_type& r) {
							for (auto it = r.begin(); it != r.end(); it++)
								it->clear();
						});
#endif
					nodes_storage[i].clear();
				}
#endif

#ifdef CONFIG_PARALLEL
				for (auto& c : edge_counter)
					num_edges += c;
#endif
			}


#ifdef CONFIG_COLLECT_SCHEDULE_GRAPH
			friend std::ostream& operator<< (std::ostream& out,
				const State_space<Time>& space)
			{
				std::map<const Schedule_node<Time>*, unsigned int> node_id;
				unsigned int i = 0;
				out << "digraph {" << std::endl;
#ifdef CONFIG_PARALLEL
				for (const Split_nodes& nodes : space.get_nodes()) {
					for (const Schedule_node<Time>& n : tbb::flattened2d<Split_nodes>(nodes)) {
#else
				for (const auto& front : space.get_nodes()) {
					for (const Schedule_node<Time>& n : front) {
#endif
						/*node_id[&n] = i++;
						out << "\tS" << node_id[&n]
							<< "[label=\"S" << node_id[&n] << ": ";
						n.print_vertex_label(out, space.jobs);
						out << "\"];" << std::endl;*/
						node_id[&n] = i++;
						out << "\tN" << node_id[&n]
							<< "[label=\"N" << node_id[&n] << ": {";
						const auto* n_states = n.get_states();

						for (State* s : *n_states)
						{
							out << "[";
							s->print_vertex_label(out, space.state_space_data.jobs);
							out << "]\\n";
						}
						out << "}"
							<< "\"];"
							<< std::endl;
					}
				}
				for (const auto& e : space.get_edges()) {
					out << "\tN" << node_id[e.source]
						<< " -> "
						<< "N" << node_id[e.target]
						<< "[label=\"";
					for (int i = 0; i < e.scheduled.size(); ++i) {
						if (e.scheduled[i] != NULL) {
							out << "[T" << e.scheduled[i]->get_task_id()
								<< " J" << e.scheduled[i]->get_job_id()
								<< "\\nDL=" << e.scheduled[i]->get_deadline()
								<< "\\nES=" << e.earliest_start_time(i)
								<< "\\nLS=" << e.latest_start_time(i)
								<< "\\nEF=" << e.earliest_finish_time(i)
								<< "\\nLF=" << e.latest_finish_time(i)
								<< "]\n";
						}
					}
					out << "\"";
					if (e.deadline_miss_possible()) {
						out << ",color=Red,fontcolor=Red";
					}
					out << ",fontsize=8" << "]"
						<< ";"
						<< std::endl;
					if (e.deadline_miss_possible()) {
						out << "N" << node_id[e.target]
							<< "[color=Red];"
							<< std::endl;
					}
				}
				out << "}" << std::endl;
				return out;
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
