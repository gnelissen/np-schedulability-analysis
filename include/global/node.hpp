#ifndef NODE_HPP
#define NODE_HPP
#include <algorithm>
#include <cassert>
#include <iostream>
#include <ostream>
#include <vector>

#ifdef CONFIG_PARALLEL
#include <tbb/spin_rw_mutex.h>
#include <tbb/spin_mutex.h>
#endif

#include "global/state.hpp"
#include "global/state_space_data.hpp"
#include "global/trackers/ready_jobs_tracker.hpp"
#include "global/trackers/pending_successors_tracker.hpp"
#include "inter_job_constraints.hpp"
#include "jobs.hpp"
#include "util.hpp"

namespace NP {

	namespace Global {

		typedef Index_set Job_set;
		typedef std::vector<Job_index> Job_precedence_set;

		template<class Time> class State_space_data;
		template<class Time> class Schedule_state;

		/**
		 * @brief A node in the schedule-state search space.
		 *
		 * Each Schedule_node groups several Schedule_state<Time> instances that
		 * share a set of scheduled jobs. The node maintains aggregated information 
		 * about core availability and ready jobs that is used to guide job 
		 * dispatching and state merging operations.
		 */
		template<class Time> class Schedule_node
		{
		private:
			using Workload = typename State_space_data<Time>::Workload;
			typedef Schedule_state<Time> State;
			typedef std::shared_ptr<State> State_ref;
			typedef std::vector<Interval<Time>> Core_availability;
			
			// variables caching frequently accessed data about when jobs become ready 
			// and can be dispatched in any state contained in this node.
			// Help quickly decide which jobs can be dispatched next without inspecting 
			// each state individually or recomputing the same data multiple times.
			/** Earliest time at which any pending (not-yet-dispatched) job may arrive. */
			Time earliest_potential_release;
			/** Next time when a successor job is certainly dispatchable across all states. */
			Time next_certain_successor_jobs_dispatch;
			/** Next time for any source job (either sequential or gang) is certainly released. */
			Time next_certain_source_job_release;
			/** Next time a sequential independent jobs is certainly released. */
			Time next_certain_sequential_independent_job_release;
			/** Next time a gang independent jobs can certainly be dispatched (i.e., enough cores are certainly available). */
			Time next_certain_gang_independent_job_dispatch;
			/** Next time a source job with mutual exclusion can certainly be dispatched (i.e., enough cores are certainly 
			 * available and mutual exclusion constraints are satisfied). */
			Time next_certain_mutex_source_job_dispatch;
			/** Earliest availability of the first core across all states in this node. */
			Time a_min;
			/** Latest time when all cores are available across all states in this node. */
			Time a_max;
			/** Number of CPUs (cores) in the system. */
			unsigned int num_cpus;
			/** Set of jobs that have already been dispatched (true for all states contained in this node). */
			Job_set scheduled_jobs;
			/** Number of jobs already dispatched (cached for quick access). */
			unsigned long long num_jobs_scheduled;
			/** Tracker of the set of jobs that have all their predecessors completed and are not dispatched yet. */
			Ready_jobs_tracker<Time> ready_successor_jobs;
			/** Tracker of jobs that have at least one unscheduled successor with start constraints. */
			Pending_successors_tracker<Time> jobs_with_pending_successors;

			/** Hash key used to look up this node in the explored state space. */
			hash_value_t lookup_key;

#ifdef CONFIG_PARALLEL
			// Mutex to ensure thread-safe state container access
			mutable tbb::spin_rw_mutex states_mutex;
#endif
			struct eft_compare
			{
				/** 
				 * @brief Compare two Schedule_state shared pointers by their earliest core availability.
				 * @param x first state pointer
				 * @param y second state pointer
				 * @return true if x has an earlier earliest_core_availability() than y
				 */
				bool operator() (State_ref x, State_ref y) const
				{
					return x->earliest_core_availability() < y->earliest_core_availability();
				}
			};

			/** list of states contained in this node */
			//typedef typename std::multiset<State_ref, eft_compare> State_ref_queue;
			typedef std::vector<State_ref> State_ref_queue;
			State_ref_queue states;

		public:

			/** 
			 * @brief Construct an empty initial node with given number of cores (for tests purposes).
			 * @param num_cores number of cores in the system
			 */
			Schedule_node(unsigned int num_cores)
				: lookup_key{ 0 }
				, num_cpus(num_cores)
				, a_min{ 0 }
				, a_max{ 0 }
				, num_jobs_scheduled(0)
				, earliest_potential_release{ 0 }
				, next_certain_source_job_release{ Time_model::constants<Time>::infinity() }
				, next_certain_successor_jobs_dispatch{ Time_model::constants<Time>::infinity() }
				, next_certain_sequential_independent_job_release{ Time_model::constants<Time>::infinity() }
				, next_certain_gang_independent_job_dispatch{ Time_model::constants<Time>::infinity() }
				, next_certain_mutex_source_job_dispatch{ Time_model::constants<Time>::infinity() }
			{
			}

			/** 
			 * @brief Construct initial node with given number of cores.
			 * @param num_cores number of cores
			 * @param state_space_data metadata used to set initial event times
			 */
			Schedule_node(unsigned int num_cores, const State_space_data<Time>& state_space_data)
				: lookup_key{ 0 }
				, num_cpus(num_cores)
				, a_min{ 0 }
				, a_max{ 0 }
				, num_jobs_scheduled(0)
				, earliest_potential_release{ state_space_data.get_earliest_job_arrival() }
				, next_certain_successor_jobs_dispatch{ Time_model::constants<Time>::infinity() }
				, next_certain_sequential_independent_job_release{ state_space_data.get_earliest_certain_seq_source_job_release() }
				, next_certain_gang_independent_job_dispatch{ Time_model::constants<Time>::infinity() }
				, next_certain_mutex_source_job_dispatch{ Time_model::constants<Time>::infinity() }
			{
				next_certain_source_job_release = std::min(next_certain_sequential_independent_job_release, 
															std::min(state_space_data.get_earliest_certain_gang_independent_job_release(), 
																		state_space_data.get_earliest_certain_mutex_source_job_release()));
			}

			/**
			 * @brief Construct initial node with explicit processor availability intervals.
			 * @param proc_initial_state per-core availability intervals
			 * @param state_space_data metadata used to set initial event times
			 */
			Schedule_node(const std::vector<Interval<Time>>& proc_initial_state, const State_space_data<Time>& state_space_data)
				: lookup_key{ 0 }
				, num_cpus((unsigned int) proc_initial_state.size())
				, a_min{ Time_model::constants<Time>::infinity() }
				, a_max{ 0 }
				, num_jobs_scheduled(0)
				, earliest_potential_release{ state_space_data.get_earliest_job_arrival() }
				, next_certain_successor_jobs_dispatch{ Time_model::constants<Time>::infinity() }
				, next_certain_sequential_independent_job_release{ state_space_data.get_earliest_certain_seq_source_job_release() }
				, next_certain_gang_independent_job_dispatch{ Time_model::constants<Time>::infinity() }
				, next_certain_mutex_source_job_dispatch{ Time_model::constants<Time>::infinity() }
			{
				for (const auto& a : proc_initial_state) {
					a_max = std::max(a_max, a.max());
					a_min = std::min(a_min, a.min());
				}

				next_certain_source_job_release = std::min(next_certain_sequential_independent_job_release, 
															std::min(state_space_data.get_earliest_certain_gang_independent_job_release(), 
																		state_space_data.get_earliest_certain_mutex_source_job_release()));
			}


			/**
			 * @brief Transition: create new node by scheduling a job 'j' in an existing node 'from'.
			 * @param from source node to copy scheduled set from
			 * @param j job scheduled
			 * @param idx job index of j
			 * @param state_space_data metadata used to set next event times
			 */
			Schedule_node(
				const Schedule_node& from,
				const Job<Time>& j,
				std::size_t idx,
				const State_space_data<Time>& state_space_data
			)
				: scheduled_jobs{ from.scheduled_jobs, state_space_data.conditional_dispatch_constraints.get_incompatible_jobs(idx) }
				, lookup_key{ from.next_key(idx, state_space_data) }
				, num_cpus(from.num_cpus)
				, num_jobs_scheduled(from.num_jobs_scheduled + state_space_data.conditional_dispatch_constraints.get_incompatible_jobs(idx).size())
				, a_min{ 0 }
				, a_max{ Time_model::constants<Time>::infinity() }
				, next_certain_successor_jobs_dispatch{ Time_model::constants<Time>::infinity() }
				, next_certain_gang_independent_job_dispatch{ Time_model::constants<Time>::infinity() }
				, next_certain_mutex_source_job_dispatch{ Time_model::constants<Time>::infinity() }
			{
				earliest_potential_release = state_space_data.earliest_possible_job_release(from.earliest_potential_release, *this);
				next_certain_sequential_independent_job_release = state_space_data.earliest_certain_sequential_source_job_release(from.next_certain_sequential_independent_job_release, *this);
				Time next_certain_gang_indep = state_space_data.earliest_certain_gang_independent_job_release(from.next_certain_source_job_release, *this);
				Time next_certain_mutex_source = state_space_data.earliest_certain_mutex_source_job_release(from.next_certain_source_job_release, *this);
				next_certain_source_job_release = std::min(next_certain_sequential_independent_job_release, 
															std::min(next_certain_gang_indep, next_certain_mutex_source));
				ready_successor_jobs.update(from.ready_successor_jobs, idx, state_space_data.inter_job_constraints, state_space_data.conditional_dispatch_constraints, scheduled_jobs);
				jobs_with_pending_successors.update(from.jobs_with_pending_successors, state_space_data.conditional_dispatch_constraints.get_incompatible_jobs_with_pending_successors(idx), state_space_data.inter_job_constraints, this->scheduled_jobs);
			}

			/**
			 * @brief Reset node (clear states, scheduled jobs and other cached data) to represent the given initial processor state.
			 * 
			 * @param proc_initial_state per-core availability intervals
			 * @param state_space_data metadata used to set next event times
			 */
			void reset(const std::vector<Interval<Time>>& proc_initial_state, const State_space_data<Time>& state_space_data)
			{
#ifdef CONFIG_PARALLEL
				tbb::spin_rw_mutex::scoped_lock lock(states_mutex, true); // write lock
#endif
				states.clear();

				lookup_key = 0;
				num_cpus = (unsigned int) proc_initial_state.size();
				a_min = Time_model::constants<Time>::infinity();
				a_max = 0;
				Time a_min_first_core = Time_model::constants<Time>::infinity();
				Time a_max_first_core = Time_model::constants<Time>::infinity();
				for (const auto& a : proc_initial_state) {
					a_max = std::max(a_max, a.max());
					a_min = std::min(a_min, a.min());
				}
				scheduled_jobs.clear();
				num_jobs_scheduled = 0;
				ready_successor_jobs.clear();
				jobs_with_pending_successors.clear();
				earliest_potential_release = state_space_data.get_earliest_job_arrival();
				next_certain_successor_jobs_dispatch = Time_model::constants<Time>::infinity();
				next_certain_sequential_independent_job_release = state_space_data.get_earliest_certain_seq_source_job_release();
				next_certain_gang_independent_job_dispatch = Time_model::constants<Time>::infinity();
				next_certain_mutex_source_job_dispatch = Time_model::constants<Time>::infinity();
				next_certain_source_job_release = std::min(next_certain_sequential_independent_job_release, 
															std::min(state_space_data.get_earliest_certain_gang_independent_job_release(), 
																		state_space_data.get_earliest_certain_mutex_source_job_release()));
			}

			/**
			 * @brief Reset node (clear states, scheduled jobs and other cached data) to a clean initial state with num_cores cores.
			 * @param num_cores number of processor cores
			 * @param state_space_data metadata for initial event times
			 */
			void reset(unsigned int num_cores, const State_space_data<Time>& state_space_data)
			{
#ifdef CONFIG_PARALLEL
				tbb::spin_rw_mutex::scoped_lock lock(states_mutex, true); // write lock
#endif
				states.clear();

				lookup_key = 0;
				num_cpus = num_cores;
				a_min = 0;
				a_max = 0;
				scheduled_jobs.clear();
				num_jobs_scheduled = 0;
				ready_successor_jobs.clear();
				jobs_with_pending_successors.clear();
				earliest_potential_release = state_space_data.get_earliest_job_arrival();
				next_certain_successor_jobs_dispatch = Time_model::constants<Time>::infinity();
				next_certain_sequential_independent_job_release = state_space_data.get_earliest_certain_seq_source_job_release();
				next_certain_gang_independent_job_dispatch = Time_model::constants<Time>::infinity();
				next_certain_mutex_source_job_dispatch = Time_model::constants<Time>::infinity();
				next_certain_source_job_release = std::min(next_certain_sequential_independent_job_release, 
															std::min(state_space_data.get_earliest_certain_gang_independent_job_release(), 
																		state_space_data.get_earliest_certain_mutex_source_job_release()));
			}

			/**
			 * @brief Reset this node and build the successor node that results from scheduling job j in from.
			 * @param from source node
			 * @param j job scheduled
			 * @param idx index of the scheduled job
			 * @param state_space_data metadata used to compute next events
			 */
			void reset(
				const Schedule_node& from,
				const Job<Time>& j,
				std::size_t idx,
				const State_space_data<Time>& state_space_data
			)
			{
#ifdef CONFIG_PARALLEL
				tbb::spin_rw_mutex::scoped_lock lock(states_mutex, true); // write lock
#endif
				states.clear();
				const auto& incomp_jobs = state_space_data.conditional_dispatch_constraints.get_incompatible_jobs(idx);
				scheduled_jobs.set(from.scheduled_jobs, incomp_jobs);
				lookup_key = from.next_key(idx, state_space_data);
				num_cpus = from.num_cpus;
				num_jobs_scheduled = from.num_jobs_scheduled + incomp_jobs.size();
				a_min = 0;
				a_max = Time_model::constants<Time>::infinity();
				earliest_potential_release = state_space_data.earliest_possible_job_release(from.earliest_potential_release, *this);
				next_certain_successor_jobs_dispatch = Time_model::constants<Time>::infinity();
				next_certain_sequential_independent_job_release = state_space_data.earliest_certain_sequential_source_job_release(from.next_certain_sequential_independent_job_release, *this);
				Time certain_gang_indep_release = state_space_data.earliest_certain_gang_independent_job_release(from.next_certain_source_job_release, *this);
				Time cetain_mutex_source_release = state_space_data.earliest_certain_mutex_source_job_release(from.next_certain_source_job_release, *this);
				next_certain_source_job_release = std::min(next_certain_sequential_independent_job_release, 
															std::min(certain_gang_indep_release, cetain_mutex_source_release));
				next_certain_gang_independent_job_dispatch = Time_model::constants<Time>::infinity();
				next_certain_mutex_source_job_dispatch = Time_model::constants<Time>::infinity();
				ready_successor_jobs.update(from.ready_successor_jobs, idx, state_space_data.inter_job_constraints, state_space_data.conditional_dispatch_constraints, scheduled_jobs);
				jobs_with_pending_successors.update(from.jobs_with_pending_successors, state_space_data.conditional_dispatch_constraints.get_incompatible_jobs_with_pending_successors(idx), state_space_data.inter_job_constraints, this->scheduled_jobs);
			}

			/**
			 * @brief Return number of jobs that have been dispatched until reach this node.
			 * @return number of dispatched jobs
			 */
			const long long number_of_scheduled_jobs() const
			{
				return num_jobs_scheduled;
			}

			/** @brief Return earliest pending job release time (across all contained states). */
			Time earliest_job_release() const
			{
				return earliest_potential_release;
			}

			/** @brief Next certain source job release time (either gang or sequential). */
			Time get_next_certain_source_job_release() const
			{
				return next_certain_source_job_release;
			}

			/** @brief Next certain sequential source job release time. */
			Time get_next_certain_sequential_independent_job_release() const
			{
				return next_certain_sequential_independent_job_release;
			}

			/** 
			 * @brief Return the next time when some job is certainly ready (either successor or source) and has enough cores available to be dispatched.
			 */
			Time next_certain_job_ready_time() const
			{
				return std::min(next_certain_successor_jobs_dispatch,
							std::min(next_certain_sequential_independent_job_release,
									std::min(next_certain_gang_independent_job_dispatch, 
										next_certain_mutex_source_job_dispatch)));
			}

			/** 
			 * @brief Return successor jobs that are ready (jobs that are not dispatched yet with all predecessors dispatched).
			 */
			const std::vector<const Job<Time>*>& get_ready_successor_jobs() const
			{
				return ready_successor_jobs.get_ready_successors();
			}

			/** @brief Return indices of dispatched jobs that have pending start-to-start-type successors. */
			const std::vector<Job_index>& get_jobs_with_pending_start_successors() const
			{
				return jobs_with_pending_successors.get_jobs_with_pending_start_successors();
			}

			/** @brief Return indices of dispatched jobs that have pending finish-to-start-type successors. */
			const std::vector<Job_index>& get_jobs_with_pending_finish_successors() const
			{
				return jobs_with_pending_successors.get_jobs_with_pending_finish_successors();
			}

			/** @brief Return the node's lookup key used for hashing/lookup. */
			hash_value_t get_key() const
			{
				return lookup_key;
			}

			/** @brief Return the set of dispatched jobs (bitset-like). */
			const Job_set& get_scheduled_jobs() const
			{
				return scheduled_jobs;
			}

			/**
			 * @brief Check whether job j has been dispatched until reaching this node.
			 * @param j job index to query
			 * @return true if job j is already dispatched
			 */
			const bool job_dispatched(Job_index j) const
			{
				return scheduled_jobs.contains(j);
			}

			/**
			 * @brief Check if a job with the given constraints is ready in this node (all predecessors dispatched).
			 * @param j job index (actually not used)
			 * @param constraints precedence constraints of the job j to check
			 * @return true if all predecessor in constraints are contained in scheduled_jobs
			 */
			bool is_ready(const Job_index j, const Job_constraints<Time>& constraints) const
			{
				for (const auto& pred : constraints.predecessors_start_to_start)
				{
					if (!scheduled_jobs.contains(pred.reference_job->get_job_index()))
						return false;
				}
				for (const auto& pred : constraints.predecessors_finish_to_start)
				{
					if (!scheduled_jobs.contains(pred.reference_job->get_job_index()))
						return false;
				}
				return true;
			}

			/**
			 * @brief Compare two nodes for equivalence (same lookup key and scheduled jobs).
			 * @param other node to compare against
			 * @return true if both nodes are equivalent
			 */
			bool matches(const Schedule_node& other) const
			{
				return lookup_key == other.lookup_key &&
					scheduled_jobs == other.scheduled_jobs;
			}

			/**
			 * @brief Compute next node lookup key after dispatching job j.
			 * @param j job to dispatch
			 * @return next key after dispatching job j
			 */
			hash_value_t next_key(Job_index j, const State_space_data<Time>& sp_data) const
			{
				hash_value_t new_key = lookup_key;
				const Workload& jobs = sp_data.jobs;
				for (Job_index idx : sp_data.conditional_dispatch_constraints.get_incompatible_jobs(j)) {
					new_key ^= jobs[idx].get_key();
				}
				return new_key;
			}

			/** @brief Earliest time when any core becomes available accros all states in this node. */
			Time earliest_core_availability() const
			{
				return a_min;
			}

			/** @brief Latest time when all cores become available accros all states in this node. */
			Time latest_core_availability() const
			{
				return a_max;
			}


			/**
			 * @brief Add a state to the node and update aggregated variables.
			 * @param s State to insert
			 */
			void add_state(State_ref s)
			{
#ifdef CONFIG_PARALLEL
				tbb::spin_rw_mutex::scoped_lock lock(states_mutex, true); // write lock
#endif
				update_internal_variables(s);
				states.push_back(s);
			}

			/**
			 * @brief Export a human-readable representation of the node to a stream.
			 * @param stream output stream to write to
			 * @param jobs workload vector to translate job indices into task/job ids
			 */
			void export_node(std::ostream& stream, const Workload& jobs)
			{
				stream << "=====Node=====\n"
					<< "Ready successors: [[<task_id>,<job_id>], ...]\n"
					<< "[";
                // Ready successor jobs: [<task_id>,<job_id>]
				unsigned int i = 0;
				const auto& ready_successors = ready_successor_jobs.get_ready_successors();
				const auto num_ready_succ = ready_successors.size();
                for (const auto* job : ready_successors) {
                    stream << "[" << job->get_task_id() << "," << job->get_job_id() << "]";
					++i;
					if (i < num_ready_succ) 
						stream << ",";
                }
				stream << "]\n"
					<< "Scheduled jobs: [[<task_id>,<job_id>], ...]\n"
					<< "[";
                // Scheduled jobs: <task_id>,<job_id>\n
                // We need to iterate over n.scheduled_jobs.
				i = 0;
				for (int idx = 0; idx < scheduled_jobs.size(); ++idx) {
					if (scheduled_jobs.contains(idx)) {
						const auto& j = jobs[idx];
						stream << "[" << j.get_task_id() << "," << j.get_job_id() << "]";
						i++;
						if (i < num_jobs_scheduled) 
							stream << ",";
					}
                }
				stream << "]\n";
            }

			/**
			 * @brief Return the number of states currently stored in this node.
			 */
			int states_size() const
			{
#ifdef CONFIG_PARALLEL
				tbb::spin_rw_mutex::scoped_lock lock(states_mutex, false); // read lock
#endif
				return states.size();
			}

			/**
			 * @brief Return the first state in the list of states.
			 * @note Caller must ensure the node is not empty prior to calling.
			 */
			const State_ref get_first_state() const
			{
#ifdef CONFIG_PARALLEL
				tbb::spin_rw_mutex::scoped_lock lock(states_mutex, false); // read lock
#endif
				auto first = states.begin();
				return *first;
			}

			/**
			 * @brief Return the last state in the list of states.
			 * @note Caller must ensure the node is not empty prior to calling.
			 */
			const State_ref get_last_state() const
			{
#ifdef CONFIG_PARALLEL
				tbb::spin_rw_mutex::scoped_lock lock(states_mutex, false); // read lock
#endif
				auto last = --(states.end());
				return *last;
			}

			/**
			 * @brief Get a pointer to the internal container of states.
			 * @warning The returned pointer references internal state; caller must synchronize externally in parallel builds.
			 */
			const State_ref_queue* get_states() const
			{
#ifdef CONFIG_PARALLEL
				// Note: This method returns a pointer to the internal container,
				// which is inherently not thread-safe. The caller must ensure
				// proper synchronization when accessing the returned pointer.
#endif
				return &states;
			}

			/**
			 * @brief Attempt to merge a new state with existing states in the node.
			 *
			 * If 'conservative' is true, merges only when one state's availability
			 * intervals are contained in the other's. If 'conservative' is true, the 'budget' parameter 
			 * is ignored. Otherwise, try up to 'budget' merges. The optional 'use_job_finish_times' 
			 * increases merging strictness by requiring job finish-times to overlap in merged states.
			 *
			 * @param s state to merge
			 * @param conservative whether to use conservative merging (containment instead of overlap)
			 * @param use_job_finish_times whether to require job finish-time overlap for merging
			 * @param budget maximum number of states that can be merged with s (use -1 for unlimited)
			 * @return number of existing states merged with the state s provided as input
			 */
			int merge_states(const Schedule_state<Time>& s, bool conservative, bool use_job_finish_times = false, int budget = 1)
			{
#ifdef CONFIG_PARALLEL
				tbb::spin_rw_mutex::scoped_lock lock(states_mutex, true); // write lock
#endif
				// if we do not use a conservative merge, try to merge with up to 'budget' states if possible.
				int merge_budget = conservative ? 1 : budget;

				State_ref last_state_merged;
				bool result = false;
				for (auto it = states.begin(); it != states.end();)
				{
					State_ref state = *it;
					if (result == false)
					{
						if (state->try_to_merge(s, conservative, use_job_finish_times))
						{
							// Update the node finish_time
							a_min = std::min(a_min, s.core_availability().min());
							a_max = std::max(a_max, s.core_availability(num_cpus).max());
							//update the certain next job ready time
							next_certain_successor_jobs_dispatch = std::max(next_certain_successor_jobs_dispatch, s.next_certain_successor_jobs_dispatch());
							next_certain_gang_independent_job_dispatch = std::max(next_certain_gang_independent_job_dispatch, s.next_certain_gang_independent_job_dispatch());
							next_certain_mutex_source_job_dispatch = std::max(next_certain_mutex_source_job_dispatch, s.next_certain_mutex_source_job_dispatch());

							result = true;

							// Try to merge with a few more states.
							merge_budget--;
							if (merge_budget == 0)
								break;

							last_state_merged = state;
						}
						++it;
					}
					else // if we already merged with one state at least
					{
						if (last_state_merged->try_to_merge(*state, conservative, use_job_finish_times))
						{
							// the state was merged => we can thus remove the old one from the list of states
							it = states.erase(it);

							// Try to merge with a few more states.
							// std::cerr << "Merged with " << merge_budget << " of " << states.size() << " states left.\n";
							merge_budget--;
							if (merge_budget == 0)
								break;
						}
						else
							++it;
					}
				}

				if (conservative)
					return result ? 1 : 0;
				else
					return (budget - merge_budget);
			}

		private:
			/**
			 * @brief Update aggregated node variables when inserting a state.
			 * @param s state to incorporate
			 */
			void update_internal_variables(const State_ref& s)
			{
				Interval<Time> ft = s->core_availability();
				if (states.empty()) {
					a_min = ft.min();
					a_max = s->core_availability(num_cpus).max();
					next_certain_successor_jobs_dispatch = s->next_certain_successor_jobs_dispatch();
					next_certain_gang_independent_job_dispatch = s->next_certain_gang_independent_job_dispatch();
					next_certain_mutex_source_job_dispatch = s->next_certain_mutex_source_job_dispatch();
				}
				else {
					a_min = std::min(a_min, ft.min());
					a_max = std::max(a_max, s->core_availability(num_cpus).max());
					next_certain_successor_jobs_dispatch = std::max(next_certain_successor_jobs_dispatch, s->next_certain_successor_jobs_dispatch());
					next_certain_gang_independent_job_dispatch = std::max(next_certain_gang_independent_job_dispatch, s->next_certain_gang_independent_job_dispatch());
					next_certain_mutex_source_job_dispatch = std::max(next_certain_mutex_source_job_dispatch, s->next_certain_mutex_source_job_dispatch());
				}
			}

			// no accidental copies
			Schedule_node(const Schedule_node& origin) = delete;
		};

	}
}

#endif