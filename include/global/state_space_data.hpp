#ifndef PROBLEM_DATA_H
#define PROBLEM_DATA_H

#include <algorithm>
#include <deque>
#include <forward_list>
#include <map>
#include <unordered_map>
#include <vector>

#include <cassert>
#include <iostream>
#include <ostream>

#include "problem.hpp"
#include "global/node.hpp"
#include "global/state.hpp"
#include "inter_job_constraints.hpp"
#include "conditional_dispatch_constraints.hpp"

#ifdef CONFIG_ANALYSIS_EXTENSIONS
#include "global/extension/state_space_data_extension.hpp"
#include "global/extension/state_extension.hpp"
#endif // CONFIG_ANALYSIS_EXTENSIONS

namespace NP {
	namespace Global {

		template<class Time> class Schedule_state;
		template<class Time> class Schedule_node;

		template<class Time> class State_space_data
		{
		public:
			using Workload = typename Scheduling_problem<Time>::Workload;
			using Precedence_constraints = typename Scheduling_problem<Time>::Precedence_constraints;
			using Mutex_constraints = typename Scheduling_problem<Time>::Mutex_constraints;
			using Abort_actions = typename Scheduling_problem<Time>::Abort_actions;
			typedef Schedule_state<Time> State;
			typedef Schedule_node<Time> Node;
			typedef const NP::Job<Time>* Job_ref;
			typedef std::vector<Job_index> Job_precedence_set;

		private:
			typedef std::multimap<Time, Job_ref> By_time_map;

			// various job maps sorted by time for quick access
			// not touched after initialization
			By_time_map _successor_jobs_by_latest_arrival;
			By_time_map _sequential_independent_jobs_by_latest_arrival;
			By_time_map _gang_independent_jobs_by_latest_arrival;
			By_time_map _mutex_source_jobs_by_latest_arrival;
			By_time_map _jobs_by_earliest_arrival;
			By_time_map _jobs_by_deadline;
			// for each job, the set of jobs that must be finished before starting it
			std::vector<Job_precedence_set> _must_be_finished_jobs;
			// ======================
			/** **CRITICAL: ORDERING CONSTRAINT:** DO NOT CHANGE DECLARATION ORDER 
			 * _inter_job_constraints must be initialized before _conditional_dispatch_constraints */
			// inter-job constraints for all jobs in the workload
			Inter_job_constraints<Time> _inter_job_constraints;
			// conditional dispatch constraints for all jobs in the workload (conditional siblings, incompatible jobs)
			Conditional_dispatch_constraints<Time> _conditional_dispatch_constraints;
			// ======================
			// list of actions when a job is aborted
			std::vector<const Abort_action<Time>*> abort_actions;

			// number of cores
			const unsigned int num_cpus;

#ifdef CONFIG_ANALYSIS_EXTENSIONS
			// possible extensions of the state space data (e.g., for task chains analysis)
			State_space_data_extensions<Time> extensions;
			// Registry for state extension types
			State_extension_registry<Time> state_extension_registry;
#endif // CONFIG_ANALYSIS_EXTENSIONS
					
		public:
			// use these const references to ensure read-only access
			const Workload& jobs;
			const By_time_map& jobs_by_earliest_arrival;
			const By_time_map& jobs_by_deadline;
			const By_time_map& successor_jobs_by_latest_arrival;
			const By_time_map& sequential_independent_jobs_by_latest_arrival;
			const By_time_map& gang_independent_jobs_by_latest_arrival;
			const By_time_map& mutex_source_jobs_by_latest_arrival;
			const std::vector<Job_precedence_set>& must_be_finished_jobs;
			const Inter_job_constraints<Time>& inter_job_constraints;
			const Conditional_dispatch_constraints<Time>& conditional_dispatch_constraints;

			/**
			 * @brief Construct state-space metadata from the scheduling problem definition.
			 * 
			 * Initializes various job maps and constraints used during state-space exploration.
			 * 
			 * @param jobs The set of jobs in the workload.
			 * @param edges The precedence constraints between jobs.
			 * @param aborts The abort actions associated with jobs.
			 * @param mutexes The mutual exclusion constraints between jobs.
			 * @param num_cpus Number of processors/cores in the system.
			 */
			State_space_data(const Workload& jobs,
				const Precedence_constraints& edges,
				const Abort_actions& aborts,
				const Mutex_constraints& mutexes,
				unsigned int num_cpus)
				: jobs(jobs)
				, num_cpus(num_cpus)
				, successor_jobs_by_latest_arrival(_successor_jobs_by_latest_arrival)
				, sequential_independent_jobs_by_latest_arrival(_sequential_independent_jobs_by_latest_arrival)
				, mutex_source_jobs_by_latest_arrival(_mutex_source_jobs_by_latest_arrival)
				, gang_independent_jobs_by_latest_arrival(_gang_independent_jobs_by_latest_arrival)
				, jobs_by_earliest_arrival(_jobs_by_earliest_arrival)
				, jobs_by_deadline(_jobs_by_deadline)
				, _must_be_finished_jobs(jobs.size())
				, must_be_finished_jobs(_must_be_finished_jobs)
				, _inter_job_constraints(jobs, edges, mutexes)
				, inter_job_constraints(_inter_job_constraints)
				, _conditional_dispatch_constraints(jobs, edges, _inter_job_constraints)
				, conditional_dispatch_constraints(_conditional_dispatch_constraints)
				, abort_actions(jobs.size(), NULL)
			{
				// Add the jobs involved in finish-to-start precedence constraints to the set of jobs 
				// that must be finished before starting a job
				for (const auto& e : edges) {
					if (e.get_type() == finish_to_start) {
						_must_be_finished_jobs[e.get_toIndex()].push_back(e.get_fromIndex());
					}
				}
				// Add the jobs involved in execution exclusion constraints to the set of jobs 
				// that must be finished before starting a job
				for (const auto& m : mutexes) {
					if (m.get_type() == exec_exclusion) {
						_must_be_finished_jobs[m.get_jobA_index()].push_back(m.get_jobB_index());
						_must_be_finished_jobs[m.get_jobB_index()].push_back(m.get_jobA_index());
					}
				}
				// Build various job maps for quick access
				for (const Job<Time>& j : jobs) {
					const Job_constraints<Time>& job_constraints = _inter_job_constraints[j.get_job_index()];
					if (job_constraints.predecessors_finish_to_start.size() > 0 || job_constraints.predecessors_start_to_start.size() > 0) {
						_successor_jobs_by_latest_arrival.insert({ j.latest_arrival(), &j });
					}
					else if (job_constraints.between_starts.size() > 0 || job_constraints.between_executions.size() > 0) {
						_mutex_source_jobs_by_latest_arrival.insert({ j.latest_arrival(), &j });
						_jobs_by_earliest_arrival.insert({ j.earliest_arrival(), &j });
					}
					else if (j.get_min_parallelism() == 1) {
						_sequential_independent_jobs_by_latest_arrival.insert({ j.latest_arrival(), &j });
						_jobs_by_earliest_arrival.insert({ j.earliest_arrival(), &j });
					}
					else {
						_gang_independent_jobs_by_latest_arrival.insert({ j.latest_arrival(), &j });
						_jobs_by_earliest_arrival.insert({ j.earliest_arrival(), &j });
					}					
					_jobs_by_deadline.insert({ j.get_deadline(), &j });
				}
				// Map abort actions to their corresponding jobs
				for (const Abort_action<Time>& a : aborts) {
					const Job<Time>& j = lookup<Time>(jobs, a.get_id());
					abort_actions[j.get_job_index()] = &a;
				}
			}

#ifdef CONFIG_ANALYSIS_EXTENSIONS
			/** @brief Get a const reference to the state space data extensions */
			const State_space_data_extensions<Time>& get_extensions() const
			{
				return extensions;
			}

			/** @brief Get a reference to the state space data extensions */
			State_space_data_extensions<Time>& get_extensions()
			{
				return extensions;
			}

			/** @brief Get a const reference to the state extension registry */
			const State_extension_registry<Time>& get_state_extension_registry() const
			{
				return state_extension_registry;
			}

			State_extension_registry<Time>& get_state_extension_registry()
			{
				return state_extension_registry;
			}
#endif // CONFIG_ANALYSIS_EXTENSIONS
			
			/** @brief Get the number of processors/cores in the system */
			size_t get_num_cpus() const
			{
				return num_cpus;
			}

			/** @brief Get the number of jobs in the workload */
			size_t num_jobs() const
			{
				return jobs.size();
			}

			/** 
			 * @brief Get the set of jobs that must be finished before starting job j
			 * @param j index of the job to query
			 * @return set of job indices that must be finished before starting job j
			 */
			const Job_precedence_set& get_finished_jobs_if_starts(Job_index j) const
			{
				return must_be_finished_jobs[j];
			}
			
			/** @brief Get the abort action associated with job j */
			const Abort_action<Time>* abort_action_of(Job_index j) const
			{
				return abort_actions[j];
			}

			/**
			 * @brief Compute the ready time interval of job j in state s within node n. 
			 * NOTE: Assumes all predecessors of j are already dispatched.
			 */
			Interval<Time> ready_times(const Node& n, const State& s, const Job<Time>& j) const
			{
				Interval<Time> r = j.arrival_window();
				Time lower_bound = r.min();
				if (j.get_type() == Job<Time>::Job_type::C_JOIN) {
					lower_bound = Time_model::constants<Time>::infinity();
				}
				const auto& cstr = inter_job_constraints[j.get_job_index()];
				for (const auto& pred : cstr.predecessors_start_to_start)
				{
					assert(dispatched(n, *pred.reference_job)); // function should not be called if a predecessor is not dispatched yet
					Interval<Time> st{ 0, 0 };
					bool has_st = s.get_start_times(pred.reference_job->get_job_index(), st);
					if (has_st) {
						if (j.get_type() == Job<Time>::Job_type::C_JOIN) {
							// for conditional join nodes, take the minimum constraint imposed by predecessors
							lower_bound = std::min(lower_bound, st.min() + pred.delay.min());
						} else {
							// for other jobs, take the maximum constraint imposed by predecessors
							lower_bound = std::max(lower_bound, st.min() + pred.delay.min());
						}
						r.extend_to(st.max() + pred.delay.max());
					}
					// only reason a predecessor of j might not have start times is if it is a conditional join node
					assert(has_st || j.get_type() == Job<Time>::Job_type::C_JOIN);
				}
				for (const auto& excl : cstr.between_starts)
				{
					if (!dispatched(n, *excl.reference_job))
						continue; // doesn't constrain anything if the other job is not dispatched yet
					Interval<Time> st{ 0, 0 };
					bool has_st = s.get_start_times(excl.reference_job->get_job_index(), st);
					assert(has_st); // start times must be available for dispatched jobs
					r.lower_bound(st.min() + excl.delay.min());
					r.extend_to(st.max() + excl.delay.max());
				}
				for (const auto& pred : cstr.predecessors_finish_to_start)
				{
					assert(dispatched(n, *pred.reference_job)); // function should not be called if a predecessor is not dispatched yet
					Interval<Time> ft{ 0, 0 };
					bool has_ft =s.get_finish_times(pred.reference_job->get_job_index(), ft);
					if (has_ft) {
						if (j.get_type() == Job<Time>::Job_type::C_JOIN) {
							// for conditional join nodes, take the minimum constraint imposed by predecessors
							lower_bound = std::min(lower_bound, ft.min() + pred.delay.min());
						} else {
							// for other jobs, take the maximum constraint imposed by predecessors
							lower_bound = std::max(lower_bound, ft.min() + pred.delay.min());
						}
						r.extend_to(ft.max() + pred.delay.max());
					}
				}
				for (const auto& excl : cstr.between_executions)
				{
					if (!dispatched(n, *excl.reference_job))
						continue; // doesn't constrain anything if the other job is not dispatched yet
					Interval<Time> ft{ 0, 0 };
					bool has_ft = s.get_finish_times(excl.reference_job->get_job_index(), ft);
					assert(has_ft); // finish times must be available for dispatched jobs
					r.lower_bound(ft.min() + excl.delay.min());
					r.extend_to(ft.max() + excl.delay.max());
				}
				r.lower_bound(lower_bound);
				return r;
			}

			/**
			 * @brief Check whether job j is certainly finished when the first core becomes available in state s within node n. 
			 * NOTE: Assumes that job j is already dispatched with its finish time within the interval finish_time.
			 * @param n The schedule node containing the state.
			 * @param s The schedule state to query.
			 * @param j The index of the job to check.
			 * @param finish_time The finish time interval of job j.
			 * @return true if num_cpus == 1 or FT^max(j) < A^min_2(s) or \exists k \in Succ^f(j) \cap Omega(s); false otherwise.
			*/
			bool is_certainly_finished(const Node& n, const State& s, const Job_index j, const Interval<Time>& finish_time) const
			{
				// job j must be already dispatched
				assert(n.job_dispatched(j));
				// If there is a single core, `j` there is a single core on which can run. 
				// Thus, `j` must have finished its execution when the first and only core becomes available.
				if (num_cpus == 1) 
					return true;

				// The optimization above can be generalized to multiple cores, as follows (ft(j) denotes
				// the finish time of j and ca(n) denotes core_availability(n)):
				// If ft(j).max() < ca(2).min() then no core can be available before j is finished.
				// Proof:
				// (A) Assume by contradiction that a core becomes available at time T before j is finished at time F > T.
				// (B) Since a core became available at time T, it must hold that ca(1).min <= T <= ca(1).max().
				// (C) Since j finishes at time F > T, we know that at least 2 cores must be available at time F:
				//     - the one that became available at time T, and
				//     - the one used by j
				// (D) So ca(2).min() <= F <= ft(j).max() hence ca(2).min() <= ft(j).max().
				// (E) this yields a contradiction with the assumption that ft(j).max() < ca(2).min().
				if (finish_time.max() < s.core_availability(2).min())
					return true; // by the proof above j is certainly finished when the first core becomes available

				// If at least one successor of j has already been dispatched, then j must have finished already.
				for (const auto& s : inter_job_constraints[j].finish_to_successors_start) {
					if (dispatched(n, *s.reference_job))
						return true;
				}
				// Else there is no guarantee that j is finished before the first core becomes available.
				return false;
			}

			/**
			 * @brief Assuming that `j_low` is dispatched next on ncores, find the latest time by which the higher priority job `j_high` 
			 * 		  is certainly ready in system state 's' of node 'n'.
			 *
			 * Assuming that:
			 * - `j_low` is dispatched next on ncores, and
			 * - `j_high` is of higher priority than `j_low`, and
			 * - all predecessors of `j_high` have been dispatched
			 * this function computes the latest ready time of `j_high` in system state 's'. When calculating the ready time of `j_high`, 
			 * this function discards all precedence constraints of j_high that are certainly fulfilled when `j_low` is dispatched.
			 * 
			 * Calculate: max{ r^max(j_high), 
			 * 				   max_{k \in Pred^s(j_high)'} { ST^max(k,s) + delay^max(k,j_high) } },
			 * 				   max_{k \in Pred^f(j_high)'} { FT^max(k,s) + delay^max(k,j_high) } },
			 * 				   max_{k \in Excl^s(j_high)' \cap Omega(s)} { ST^max(k,s) + delay^max(k,j_high) } },
			 * 				   max_{k \in Excl^f(j_high)' \cap Omega(s)} { FT^max(k,s) + delay^max(k,j_high) } } }
			 * 			  where:
			 * 				- Pred^s(j_high)' = Pred^s(j_high) \ {j | delay^max(j,j_high) = 0 }
			 * 												   \ {j | j \in Pred^s(j_low) and delay^min(j,j_low) >= delay^max(j,j_high) } 
			 * 												   \ {j | j \in Pred^f(j_low) and C^min(j) + delay^min(j,j_low) >= delay^max(j,j_high) }
			 * 												   \ {j | j \in Mutx^s(j_low) and delay^min(j,j_low) >= delay^max(j,j_high) } 
			 * 												   \ {j | j \in Mutx^f(j_low) and C^min(j) + delay^min(j,j_low) >= delay^max(j,j_high) }
			 * 			    - Pred^f(j_high)' = \begin{cases}
			 * 									emptyset & if m = 1 (single core) \\
			 * 									Pred^f(j_high) \ {j | delay^max(j,j_high) = 0 and (FT^max(j,s) < A^min_2(s) or \exists k \in Succ^f(j) \cap Omega(s)) }
			 * 												   \ {j | j \in Pred^s(j_low) and delay^min(j,j_low) >= C^max(j) + delay^max(j,j_high) }
			 * 												   \ {j | j \in Pred^f(j_low) and delay^min(j,j_low) >= delay^max(j,j_high) }
			 * 												   \ {j | j \in Mutx^s(j_low) and delay^min(j,j_low) >= C^max(j) + delay^max(j,j_high) }
			 * 												   \ {j | j \in Mutx^f(j_low) and delay^min(j,j_low) >= delay^max(j,j_high) } & if m > 1
			 * 								    \end{cases}
			 * 				- Mutx^s(j_high)' = Mutx^s(j_high) \ {j | j \in Pred^s(j_low) and delay^min(j,j_low) >= delay^max(j,j_high) }
			 * 												   \ {j | j \in Pred^f(j_low) and C^min(j) + delay^min(j,j_low) >= delay^max(j,j_high) }
			 * 												   \ {j | j \in Mutx^s(j_low) and delay^min(j,j_low) >= delay^max(j,j_high) } 
			 * 												   \ {j | j \in Mutx^f(j_low) and C^min(j) + delay^min(j,j_low) >= delay^max(j,j_high) }
			 * 			    - Mutx^f(j_high)' = \begin{cases}
			 * 									emptyset & if m = 1 (single core) \\
			 * 									Mutx^f(j_high) \ {j | delay^max(j,j_high) = 0 and (FT^max(j,s) < A^min_2(s) or \exists k \in Succ^f(j) \cap Omega(s)) }
			 * 												   \ {j | j \in Pred^s(j_low) and delay^min(j,j_low) >= C^max(j) + delay^max(j,j_high) }
			 * 												   \ {j | j \in Pred^f(j_low) and delay^min(j,j_low) >= delay^max(j,j_high) }
			 * 												   \ {j | j \in Mutx^s(j_low) and delay^min(j,j_low) >= C^max(j) + delay^max(j,j_high) }
			 * 												   \ {j | j \in Mutx^f(j_low) and delay^min(j,j_low) >= delay^max(j,j_high) } & if m > 1
			 * 								    \end{cases}
			 * 
			 * @param n The node state s belongs to.
			 * @param s The state in which to search for the latest ready time.
			 * @param j_high The higher priority job whose latest ready time is to be computed.
			 * @param j_low The lower priority job that is assumed to be dispatched next.
			 * @param j_low_required_cores The number of cores required by j_low when dispatched.
			 * @return The latest time by which j_high is certainly ready in state s of node n.
			 */
			Time conditional_latest_ready_time(
				const Node& n, const State& s,
				const Job<Time>& j_high, const Job_index j_low,
				const unsigned int j_low_required_cores = 1) const
			{
				Time latest_ready_high = j_high.arrival_window().max();

				// if the minimum parallelism of j_high is more than j_low_required_cores, then
				// for j to be released and have its successors completed
				// is not enough to interfere with a lower priority job.
				// It must also have enough cores free.
				if (j_high.get_min_parallelism() > j_low_required_cores)
				{
					// max {rj_max,Amax(sjmin)}
					latest_ready_high = std::max(latest_ready_high, s.core_availability(j_high.get_min_parallelism()).max());
				}

				// j_high is not ready until all its predecessors have completed, and their corresponding delays are over.
				// But, since we are assuming that `j_low` is dispatched next and all predecessors of `j_high` have been dispatched,
				// we can disregard some of the predecessors.
				const auto& cstr = inter_job_constraints[j_high.get_job_index()];
				for (const auto& prec : cstr.predecessors_start_to_start)
				{
					const auto delay_max = prec.delay.max();
					// Since all predecessors must have been dispatched already, we can disregard
					// this constraint if there is no delay between the predecessor's start and j_high's start
					if (delay_max == 0) 
						continue;

					const auto& pred = *prec.reference_job;
					const auto pred_idx = pred.get_job_index();
					// skip if its also a predecessor of j_low and the delay to j_low's start can not be smaller than the delay to j_high's start
					Time min_s2s_delay = inter_job_constraints[j_low].get_min_delay_after_start_of(pred_idx);
					if (min_s2s_delay != -1 && min_s2s_delay >= delay_max) 
						continue;

					Time min_f2s_delay = inter_job_constraints[j_low].get_min_delay_after_finish_of(pred_idx);
					if (min_f2s_delay != -1 && min_f2s_delay + pred.get_bcet() >= delay_max) 
						continue;

					Interval<Time> st{ 0, 0 };
					bool has_st = s.get_start_times(pred_idx, st);
					if (!has_st) {
						// only reason a predecessor of j_high might not have start times is if j_high is a conditional join node
						assert(j_high.get_type() == Job<Time>::Job_type::C_JOIN);
						continue;
					}
					latest_ready_high = std::max(latest_ready_high, st.max() + delay_max);
				}

				for (const auto& excl : cstr.between_starts)
				{
					const auto& other = *excl.reference_job;
					// disregard this exclusion constraint if the job is not dispatched yet
					if (!dispatched(n, other))
						continue; 

					const auto delay_max = excl.delay.max();
					const auto other_idx = other.get_job_index();
					// skip if its also a predecessor of j_low and the delay to j_low's start can not be smaller than the delay to j_high's start
					Time min_s2s_delay = inter_job_constraints[j_low].get_min_delay_after_start_of(other_idx);
					if (min_s2s_delay != -1 && min_s2s_delay >= delay_max) 
						continue;

					Time min_f2s_delay = inter_job_constraints[j_low].get_min_delay_after_finish_of(other_idx);
					if (min_f2s_delay != -1 && min_f2s_delay + other.get_bcet() >= delay_max) 
						continue;

					Interval<Time> st{ 0, 0 };
					s.get_start_times(other_idx, st);
					latest_ready_high = std::max(latest_ready_high, st.max() + delay_max);
				}

				for (const auto& prec : cstr.predecessors_finish_to_start)
				{
					const auto& pred = *prec.reference_job;
					auto pred_idx = pred.get_job_index();
					Interval<Time> ft{ 0, 0 };
					bool has_ft = s.get_finish_times(pred_idx, ft);
					if (!has_ft) {
						// only reason a predecessor of j_high might not have finish times is if j_high is a conditional join node
						assert(j_high.get_type() == Job<Time>::Job_type::C_JOIN);
						continue;
					}

					// If the delay is 0 and j_pred is certainly finished when j_low is dispatched, then j_pred cannot postpone
					// the (latest) ready time of j_high.
					const auto delay_max = prec.delay.max();
					if (delay_max == 0 && is_certainly_finished(n, s, pred_idx, ft)) 
						continue;

					// If j_pred is a predecessor of both j_high and j_low, we can disregard it if the maximum delay from j_pred to j_high
					// is at most the minimum delay from j_pred to j_low: delay_max(j_pred -> j_high) <= delay_min(j_pred -> j_low).
					Time min_f2s_delay = inter_job_constraints[j_low].get_min_delay_after_finish_of(pred_idx);
					if (min_f2s_delay != -1 && min_f2s_delay >= delay_max) 
						continue;

					Time min_s2s_delay = inter_job_constraints[j_low].get_min_delay_after_start_of(pred_idx);
					if (min_s2s_delay != -1 && min_s2s_delay >= delay_max + pred.get_wcet()) 
						continue;

					latest_ready_high = std::max(latest_ready_high, ft.max() + delay_max);
				}

				for (const auto& excl : cstr.between_executions)
				{
					const auto& other = *excl.reference_job;
					if (!dispatched(n, other))
						continue; // disregard this exclusion constraint if the job is not dispatched yet

					auto other_idx = other.get_job_index();
					Interval<Time> ft{ 0, 0 };
					s.get_finish_times(other_idx, ft);
					const auto delay_max = excl.delay.max();
					// If the delay is 0 and j_pred is certainly finished when j_low is dispatched, then j_pred cannot postpone
					// the (latest) ready time of j_high.
					if (delay_max == 0 && is_certainly_finished(n, s, other_idx, ft))
						continue;

					// If j_pred is a predecessor of both j_high and j_low, we can disregard it if the maximum delay from j_pred to j_high
					// is at most the minimum delay from j_pred to j_low: delay_max(j_pred -> j_high) <= delay_min(j_pred -> j_low).
					Time min_f2s_delay = inter_job_constraints[j_low].get_min_delay_after_finish_of(other_idx);
					if (min_f2s_delay != -1 && min_f2s_delay >= delay_max) 
						continue;

					Time min_s2s_delay = inter_job_constraints[j_low].get_min_delay_after_start_of(other_idx);
					if (min_s2s_delay != -1 && min_s2s_delay >= delay_max + other.get_wcet())
						continue;

					latest_ready_high = std::max(latest_ready_high, ft.max() + delay_max);
				}
				return latest_ready_high;
			}

			/**
			 * @brief Compute the earliest ready time of job j in state s within node n. 
			 * NOTE: Assumes all predecessors of j are already dispatched.
			 */
			Time earliest_ready_time(const Node& n, const State& s, const Job<Time>& j) const
			{
				return ready_times(n, s, j).min();
			}

			/** 
			 * @brief Find next time by which a sequential independent job (i.e., a job without predecessors or mutual exclusion that can 
			 * 		 execute on a single core) of higher priority than the reference_job is certainly released in any state in the 
			 * 		 node 'n'. The release time is searched within the interval [0, until).
			 * 
			 * Calculate: min{ until, min_{j \in SeqIndependentJobs, prio(j) > prio(reference_job)} { r^max(j) } }
			 *
			 * @param n The node in which to search for the next release time.
			 * @param reference_job The job whose priority is used as reference.
			 * @param until Upper bound on the time interval in which to search.
			 * @return The next time by which a sequential independent job of higher priority than the reference_job is certainly released.
			 */
			Time next_certain_higher_priority_seq_independent_job_release(
				const Node& n,
				const Job<Time>& reference_job,
				Time until = Time_model::constants<Time>::infinity()) const
			{
				Time when = until;

				// a higher priority source job (i.e., without predecessors) cannot be released before 
				// a source job of any priority is released
				Time t_earliest = n.get_next_certain_source_job_release();

				for (auto it = sequential_independent_jobs_by_latest_arrival.lower_bound(t_earliest);
					it != sequential_independent_jobs_by_latest_arrival.end(); it++)
				{
					const Job<Time>& j = *(it->second);

					// check if we can stop looking
					if (when < j.latest_arrival())
						break; // yep, nothing can lower 'when' at this point

					// j is not relevant if it is already scheduled or not of higher priority
					// we also ignore jobs with exclusion constraints
					if (not_dispatched(n, j) && j.higher_priority_than(reference_job))
					{
						when = j.latest_arrival();
						// Jobs are ordered by latest_arrival, so next jobs are later. 
						// We can thus stop searching.
						break;
					}
				}
				return when;
			}

			/**
			 * @brief Assuming that `reference_job` is dispatched next on ncores, find the next time by which a gang independent job (i.e., 
			 * 		  a job without predecessors or mutual exclusion that cannot execute on a single core)  of higher priority than the reference_job is 
			 * 		  certainly released in state 's' of node 'n' and may interfere with the reference_job. 
			 * 		  The release time is searched within the interval [lower_bound, until).
			 * 
			 * Calculate: min{ until, min_{j \in GangIndependentJobs, prio(j) > prio(reference_job)} { \rho^max(j,s) }
			 * 			  where \rho^max(j,s) = \begin{cases}
			 * 										max( r^max(j), A^max_p^min(j)(s) ) } & if p^min(j) > ncores \\
			 * 										r^max(j) & otherwise
			 * 								    \end{cases}
			 * 
			 * @param n The node state s belongs to.
			 * @param s The state in which to search for the next release time.
			 * @param reference_job The job whose priority is used as reference.
			 * @param ncores The number of cores on which the reference_job is dispatched.
			 * @param lower_bound A lower bound on the time interval in which to search.
			 * @param until An upper bound on the time interval in which to search.
			 */
			Time next_certain_higher_priority_gang_independent_job_ready_time(
				const Node& n,
				const State& s,
				const Job<Time>& reference_job,
				const unsigned int ncores,
				Time lower_bound,
				Time until = Time_model::constants<Time>::infinity()) const
			{
				assert(lower_bound < until);
				Time when = until;

				// a higher priority source job (i.e., without predecessors) cannot be released before 
				// a source job of any priority is released
				Time t_earliest = n.get_next_certain_source_job_release();

				for (auto it = gang_independent_jobs_by_latest_arrival.lower_bound(t_earliest);
					it != gang_independent_jobs_by_latest_arrival.end(); it++)
				{
					const Job<Time>& j = *(it->second);

					// check if we can stop looking
					if (when < j.latest_arrival())
						break; // yep, nothing can lower 'when' at this point

					// j is not relevant if it is already scheduled or not of higher priority
					if (not_dispatched(n, j) && j.higher_priority_than(reference_job))
					{
						// if the minimum parallelism of j is more than ncores, then 
						// for j to be released and have its successors completed 
						// is not enough to interfere with a lower priority job.
						// It must also have enough cores free.
						if (j.get_min_parallelism() > ncores)
						{
							// max {rj_max,Amax(sjmin)}
							when = std::min(when, std::max(j.latest_arrival(), s.core_availability(j.get_min_parallelism()).max()));
							// no break as other jobs may require less cores to be available and thus be ready earlier
						}
						else
						{
							when = std::min(when, j.latest_arrival());
							// jobs are ordered in non-decreasing latest arrival order, 
							// => nothing tested after can be ready earlier than j
							// => we break
							break;
						}
					}
					if (when <= lower_bound) {
						break;
					}
				}
				return when;
			}

			/**
			 * @brief Assuming that `reference_job` is dispatched next on ncores, find the earliest time by which a successor job (i.e., 
			 * 		  a job with predecessors) of higher priority than the reference_job is certainly ready in system state 's' of node 'n'.
			 * 		  The ready time is searched within the interval [lower_bound, infinity).
			 *
			 * Calculate: min_{j \in SuccessorJobs, prio(j) > prio(reference_job)} { \rho^max(j,s) }
			 * 			  where \rho^max(j,s) = \begin{cases}
			 * 									  +infinity & \exists k \in CS(j) : prio(k) < prio(reference_job) \\
			 * 									  max_{k \in CS(j)} { R^max(k|reference_job,s) } & otherwise
			 * 								    \end{cases}
			 * 			  and CS(j) is the set of conditional siblings of job j (including j itself),
			 * 			  and R^max(k|reference_job,s) is the latest ready time of job k in state s knowing that `reference_job` is dispatched next on ncores.
			 * 
			 * @param n The node state s belongs to.
			 * @param s The state in which to search for the next ready time.
			 * @param reference_job The job whose priority is used as reference.
			 * @param ncores The number of cores on which the reference_job is dispatched.
			 * @param lower_bound A lower bound on the time interval in which to search.
			 * @return The next time by which a successor job of higher priority than the reference_job is certainly ready.
			 */
			Time next_certain_higher_priority_successor_job_ready_time(
				const Node& n,
				const State& s,
				const Job<Time>& reference_job,
				const unsigned int ncores,
				Time lower_bound
			) const {
				Time latest_ready_high = Time_model::constants<Time>::infinity();

				// a higer priority successor job cannot be ready before 
				// a job of any priority is released
				for (auto it = n.get_ready_successor_jobs().begin();
					it != n.get_ready_successor_jobs().end(); it++)
				{
					const Job<Time>& j_high = **it;
					Job_index j_h_index = j_high.get_job_index();

					// j_high is not relevant if it is not of higher priority or is incompatible with reference_job
					if (j_high.higher_priority_than(reference_job) && not_incompatible(j_h_index, reference_job.get_job_index())) {
						if (conditional_dispatch_constraints.has_conditional_siblings(j_h_index)) {
							// if j_high has conditional siblings, we must take the sibling with the largest ready time
							Time ready_max = (Time) 0;
							for (auto sibling : *conditional_dispatch_constraints.get_conditional_siblings(j_h_index)) {
								if (sibling->higher_priority_than(reference_job))
									ready_max = std::max(ready_max, conditional_latest_ready_time(n, s, *sibling, reference_job.get_job_index(), ncores));
								else {
									// if at least one of the siblings has a lower priority than the reference_job than there is a scenario 
									// where none of the siblings may prevent reference_job to execute due to the FP scheduling rule
									ready_max = Time_model::constants<Time>::infinity();
									break;
								}
							}
							latest_ready_high = std::min(latest_ready_high, ready_max);
						} else {
							latest_ready_high = std::min(latest_ready_high, conditional_latest_ready_time(n, s, j_high, reference_job.get_job_index(), ncores));
						}
						if (latest_ready_high <= lower_bound) break;
					}
				}
				return latest_ready_high;
			}

			/**
			 * @brief Assuming that `reference_job` is dispatched next on ncores, find the next time by which a source mutex job (i.e., 
			 * 		  a job without predecessors but with mutual exclusion constraints) of higher priority than the reference_job is 
			 * 		  certainly ready in system state 's' of node 'n'. 
			 * 		  The release time is searched within the interval [lower_bound, until).
			 * 
			 * Calculate: min{ until, min_{j \in MutexJobs, prio(j) > prio(reference_job)} { R^max(k|reference_job,s) }
			 * 			  where R^max(k|reference_job,s) is the latest ready time of job k in state s knowing that `reference_job` is dispatched next on ncores.
			 * 
			 * @param n The node state s belongs to.
			 * @param s The state in which to search for the next release time.
			 * @param reference_job The job whose priority is used as reference.
			 * @param ncores The number of cores on which the reference_job is dispatched.
			 * @param lower_bound A lower bound on the time interval in which to search.
			 * @param until An upper bound on the time interval in which to search.
			 */
			Time next_certain_higher_priority_mutex_job_ready_time(
				const Node& n,
				const State& s,
				const Job<Time>& reference_job,
				const unsigned int ncores,
				Time lower_bound,
				Time until = Time_model::constants<Time>::infinity()) const
			{
				assert(lower_bound < until);
				Time when = until;
				auto ref_job_index = reference_job.get_job_index();

				// a higher priority source job (i.e., without predecessors) cannot be released before 
				// a source job of any priority is released
				Time t_earliest = n.get_next_certain_source_job_release();

				for (auto it = mutex_source_jobs_by_latest_arrival.lower_bound(t_earliest);
					it != mutex_source_jobs_by_latest_arrival.end(); it++)
				{
					const Job<Time>& j = *(it->second);

					// check if we can stop looking
					if (when < j.latest_arrival())
						break; // nothing can lower 'when' at this point

					// j is not relevant if it is already scheduled or not of higher priority
					if (not_dispatched(n, j) && j.higher_priority_than(reference_job)) {
						when = std::min(when, conditional_latest_ready_time(n, s, j, ref_job_index, ncores));
						if (when <= lower_bound) 
							break;
					}
				}
				return when;
			}

			/**
			 * @brief Find the earliest possible job release of all jobs at or after time 'after'
			 * @param after time after which to search for the earliest possible job release
			 * @param n the node in which to search
			 */
			Time earliest_possible_job_release(Time after, const Node& n) const
			{
				for (auto it = jobs_by_earliest_arrival.lower_bound(after);
					it != jobs_by_earliest_arrival.end(); 	it++)
				{
					const Job<Time>& j = *(it->second);
					// skip if it is dispatched already
					if (dispatched(n, j))
						continue;
					// the job was not dispatched yet => found the earliest since jobs are ordered by earliest arrival
					return j.earliest_arrival();
				}
				// no more jobs => return infinity
				return Time_model::constants<Time>::infinity();
			}

			/**
			 * @brief Find the earliest certain job release of all sequential source jobs 
			 * (i.e., without predecessors and with minimum parallelism = 1) 
			 * at or after time 'after'
			 * @param after time after which to search for the earliest certain sequential source job release
			 * @param n the node in which to search
			 */
			Time earliest_certain_sequential_source_job_release(Time after, const Node& n) const
			{
				for (auto it = sequential_independent_jobs_by_latest_arrival.lower_bound(after);
					it != sequential_independent_jobs_by_latest_arrival.end(); it++)
				{
					const Job<Time>* jp = it->second;
					// skip if the job was dispatched already
					if (dispatched(n, *jp))
						continue;
					// the job was not dispatched yet => found the earliest since jobs are ordered by latest arrival 
					return jp->latest_arrival();
				}
				// no more sequential source jobs => return infinity
				return Time_model::constants<Time>::infinity();
			}

			/** 
			 * @brief Find the earliest certain gang source job release of all source jobs 
			 * at or after time 'after'
			 * @param after time after which to search for the earliest certain gang source job release
			 * @param n the node in which to search
			 */
			Time earliest_certain_gang_independent_job_release(Time after, const Node& n) const
			{
				for (auto it = gang_independent_jobs_by_latest_arrival.lower_bound(after);
					it != gang_independent_jobs_by_latest_arrival.end(); it++)
				{
					const Job<Time>* jp = it->second;
					// skip if the job was dispatched already
					if (dispatched(n, *jp))
						continue;
					// the job was not dispatched yet => found the earliest since jobs are ordered by latest arrival
					return jp->latest_arrival();
				}
				return Time_model::constants<Time>::infinity();
			}

			/** 
			 * @brief Find the earliest certain job release of all mutex source jobs at or after time 'after'
			 * @param after time after which to search for the earliest certain mutex source job release
			 * @param n the node in which to search
			 */
			Time earliest_certain_mutex_source_job_release(Time after, const Node& n) const
			{
				for (auto it = mutex_source_jobs_by_latest_arrival.lower_bound(after);
					it != mutex_source_jobs_by_latest_arrival.end(); it++)
				{
					const Job<Time>* jp = it->second;
					// skip if the job was dispatched already
					if (dispatched(n, *jp))
						continue;
					// the job was not dispatched yet => found the earliest since jobs are ordered by latest arrival
					return jp->latest_arrival();
				}
				return Time_model::constants<Time>::infinity();
			}

			/** 
			 * @brief Return the earliest possible job arrival of all jobs when the system starts 
			 * NOTE: ignores successor jobs because they cannot be ready before their predecessors 
			 * are released, hence successor jobs' releases are lower bounded by those of the source jobs
			 */
			Time get_earliest_job_arrival() const
			{
				if (jobs_by_earliest_arrival.empty())
					return Time_model::constants<Time>::infinity();
				else
					return jobs_by_earliest_arrival.begin()->first;
			}

			/**
			 * @brief Get the earliest certain release time among all **sequential** *source* jobs when the system starts.
			 */
			Time get_earliest_certain_seq_source_job_release() const
			{
				if (sequential_independent_jobs_by_latest_arrival.empty())
					return Time_model::constants<Time>::infinity();
				else
					return sequential_independent_jobs_by_latest_arrival.begin()->first;
			}

			/**
			 * @brief Get the earliest certain release time among all **gang** *source* jobs when the system starts.
			 */
			Time get_earliest_certain_gang_independent_job_release() const
			{
				if (gang_independent_jobs_by_latest_arrival.empty())
					return Time_model::constants<Time>::infinity();
				else
					return gang_independent_jobs_by_latest_arrival.begin()->first;
			}

			/**
			 * @brief Get the earliest certain release time among all *mutex* *source* jobs when the system starts.
			 */
			Time get_earliest_certain_mutex_source_job_release() const
			{
				if (mutex_source_jobs_by_latest_arrival.empty())
					return Time_model::constants<Time>::infinity();
				else
					return mutex_source_jobs_by_latest_arrival.begin()->first;
			}

		private:

			inline bool not_dispatched(const Node& n, const Job<Time>& j) const
			{
				return not n.job_dispatched(j.get_job_index());
			}

			inline bool dispatched(const Node& n, const Job<Time>& j) const
			{
				return n.job_dispatched(j.get_job_index());
			}

			inline bool not_incompatible(const Job_index j1, const Job_index j2) const
			{
				return !conditional_dispatch_constraints.are_incompatible(j1, j2);
			}

			State_space_data(const State_space_data& origin) = delete;
		};
	}
}
#endif
