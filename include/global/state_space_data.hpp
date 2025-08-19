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
#include "global/state.hpp"

#ifdef CONFIG_ANALYSIS_EXTENSIONS
#include "global/extension/state_space_data_extension.hpp"
#endif // CONFIG_ANALYSIS_EXTENSIONS

namespace NP {
	namespace Global {

		template<class Time> class Schedule_state;
		template<class Time> class Schedule_node;

		template<class Time> class State_space_data
		{
		public:

			typedef Scheduling_problem<Time> Problem;
			typedef typename Scheduling_problem<Time>::Workload Workload;
			typedef typename Scheduling_problem<Time>::Precedence_constraints Precedence_constraints;
			typedef typename Scheduling_problem<Time>::Abort_actions Abort_actions;
			typedef Schedule_state<Time> State;
			typedef Schedule_node<Time> Node;
			typedef typename std::vector<Interval<Time>> CoreAvailability;
			typedef const Job<Time>* Job_ref;
			typedef std::vector<Job_index> Job_precedence_set;
			typedef std::vector<std::pair<Job_ref, Interval<Time>>> Suspensions_list;

		private:
			typedef std::multimap<Time, Job_ref> By_time_map;

			// not touched after initialization
			By_time_map _successor_jobs_by_latest_arrival;
			By_time_map _sequential_source_jobs_by_latest_arrival;
			By_time_map _gang_source_jobs_by_latest_arrival;
			By_time_map _jobs_by_earliest_arrival;
			By_time_map _jobs_by_deadline;
			std::vector<Job_precedence_set> _predecessors;

			// not touched after initialization
			std::vector<Suspensions_list> _predecessors_suspensions;
			std::vector<Suspensions_list> _successors_suspensions;

			// list of actions when a job is aborted
			std::vector<const Abort_action<Time>*> abort_actions;

			// number of cores
			const unsigned int num_cpus;

#ifdef CONFIG_ANALYSIS_EXTENSIONS
			// possible extensions of the state space data (e.g., for task chains analysis)
			State_space_data_extensions extensions;
#endif // CONFIG_ANALYSIS_EXTENSIONS
					
		public:
			// use these const references to ensure read-only access
			const Workload& jobs;
			const By_time_map& jobs_by_earliest_arrival;
			const By_time_map& jobs_by_deadline;
			const By_time_map& successor_jobs_by_latest_arrival;
			const By_time_map& sequential_source_jobs_by_latest_arrival;
			const By_time_map& gang_source_jobs_by_latest_arrival;
			const std::vector<Job_precedence_set>& predecessors;
			const std::vector<Suspensions_list>& predecessors_suspensions;
			const std::vector<Suspensions_list>& successors_suspensions;

			State_space_data(const Workload& jobs,
				const Precedence_constraints& edges,
				const Abort_actions& aborts,
				unsigned int num_cpus)
				: jobs(jobs)
				, num_cpus(num_cpus)
				, successor_jobs_by_latest_arrival(_successor_jobs_by_latest_arrival)
				, sequential_source_jobs_by_latest_arrival(_sequential_source_jobs_by_latest_arrival)
				, gang_source_jobs_by_latest_arrival(_gang_source_jobs_by_latest_arrival)
				, jobs_by_earliest_arrival(_jobs_by_earliest_arrival)
				, jobs_by_deadline(_jobs_by_deadline)
				, _predecessors(jobs.size())
				, predecessors(_predecessors)
				, _predecessors_suspensions(jobs.size())
				, _successors_suspensions(jobs.size())
				, predecessors_suspensions(_predecessors_suspensions)
				, successors_suspensions(_successors_suspensions)
				, abort_actions(jobs.size(), NULL)
			{
				for (const auto& e : edges) {
					_predecessors_suspensions[e.get_toIndex()].push_back({ &jobs[e.get_fromIndex()], e.get_suspension() });
					_predecessors[e.get_toIndex()].push_back(e.get_fromIndex());
					_successors_suspensions[e.get_fromIndex()].push_back({ &jobs[e.get_toIndex()], e.get_suspension() });
				}

				// sort the predecessors and successors suspensions lists by non-increasing priority order
				for (auto& pred : _predecessors_suspensions) {
					std::sort(pred.begin(), pred.end(),
						[](const std::pair<Job_ref, Interval<Time>>& a, const std::pair<Job_ref, Interval<Time>>& b) {
							return a.first->higher_priority_than(*(b.first));
						});
				}
				for (auto& succ : _successors_suspensions) {
					std::sort(succ.begin(), succ.end(),
						[](const std::pair<Job_ref, Interval<Time>>& a, const std::pair<Job_ref, Interval<Time>>& b) {
							return a.first->higher_priority_than(*(b.first));
						});
				}

				for (const Job<Time>& j : jobs) {
					if (_predecessors_suspensions[j.get_job_index()].size() > 0) {
						_successor_jobs_by_latest_arrival.insert({ j.latest_arrival(), &j });
					}
					else if (j.get_min_parallelism() == 1) {
						_sequential_source_jobs_by_latest_arrival.insert({ j.latest_arrival(), &j });
						_jobs_by_earliest_arrival.insert({ j.earliest_arrival(), &j });
					}
					else {
						_gang_source_jobs_by_latest_arrival.insert({ j.latest_arrival(), &j });
						_jobs_by_earliest_arrival.insert({ j.earliest_arrival(), &j });
					}					
					_jobs_by_deadline.insert({ j.get_deadline(), &j });
				}

				for (const Abort_action<Time>& a : aborts) {
					const Job<Time>& j = lookup<Time>(jobs, a.get_id());
					abort_actions[j.get_job_index()] = &a;
				}
			}

#ifdef CONFIG_ANALYSIS_EXTENSIONS
			const State_space_data_extensions& get_extensions() const
			{
				return extensions;
			}
#endif // CONFIG_ANALYSIS_EXTENSIONS

			size_t get_num_cpus() const
			{
				return num_cpus;
			}

			size_t num_jobs() const
			{
				return jobs.size();
			}

			const Job_precedence_set& predecessors_of(const Job<Time>& j) const
			{
				return predecessors[j.get_job_index()];
			}

			const Job_precedence_set& predecessors_of(Job_index j) const
			{
				return predecessors[j];
			}

			const Abort_action<Time>* abort_action_of(Job_index j) const
			{
				return abort_actions[j];
			}

			// returns the ready time interval of `j` in `s`
			// assumes all predecessors of j are dispatched
			Interval<Time> ready_times(const State& s, const Job<Time>& j) const
			{
				Interval<Time> r = j.arrival_window();
				for (const auto& pred : predecessors_suspensions[j.get_job_index()])
				{
					auto pred_idx = pred.first->get_job_index();
					auto pred_susp = pred.second;
					Interval<Time> ft{ 0, 0 };
					s.get_finish_times(pred_idx, ft);
					r.lower_bound(ft.min() + pred_susp.min());
					r.extend_to(ft.max() + pred_susp.max());
				}
				return r;
			}

			// Assuming that:
			// - `j_low` is dispatched next, and
			// - `j_high` is of higher priority than `j_low`, and
			// - all predecessors of `j_high` have been dispatched
			//
			// this function computes the latest ready time of `j_high` in system state 's'.
			//
			// Let `ready_low` denote the earliest time at which `j_low` becomes ready
			// and let `latest_ready_high` denote the return value of this function.
			//
			// If `latest_ready_high <= `ready_low`, the assumption that `j_low` is dispatched next lead to a contradiction,
			// hence `j_low` cannot be dispatched next. In this case, the exact value of `latest_ready_high` is meaningless,
			// except that it must be at most `ready_low`. After all, it was computed under an assumption that cannot happen.
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

				// j is not ready until all its predecessors have completed, and their corresponding suspension delays are over.
				// But, since we are assuming that `j_low` is dispatched next and all predecessors of `j` have been dispatched,
				// we can disregard some of them.
				for (const auto& pred : predecessors_suspensions[j_high.get_job_index()])
				{
					const auto high_suspension = pred.second;
					auto pred_idx = pred.first->get_job_index();

					Interval<Time> ft{ 0, 0 };
					s.get_finish_times(pred_idx, ft);

					// If the suspension is 0 and j_pred is certainly finished when j_low is dispatched, then j_pred cannot postpone
					// the (latest) ready time of j_high.
					if (high_suspension.max() == 0) {

						// If there is a single core, all predecessors of `j_high` must have finished when the core becomes available,
						// since we assumed that all predecessors of `j_high` were already dispatched.
						if (num_cpus == 1) continue;

						// The optimization above can be generalized to multiple cores, using the following knowledge:
						// (1) When j_pred cannot postpone the ready time of j_high to a time instant *later than* the moment j_low can start,
						//     we can safely disregard j_pred.
						// (2) j_low cannot start until at least 1 core is available.
						// (3) So if all cores are certainly occupied until j_pred is finished, we can disregard j_pred.
						//
						// We will prove the following claim: (ft(j) denotes the finish time of j and ca(n) denotes core_availability(n))
						// (4) If ft(j_pred).max() < ca(2).min() then no core can be available before j_pred is finished.
						// Proof:
						// (A) Assume for a contradiction that a core becomes available at time T before j_pred is finished at time F > T.
						//
						// (B) Since a core became available at time T, it must hold that ca(1).min <= T <= ca(1).max().
						//
						// (C) Since j_pred finishes at time F > T, we know that at least 2 cores must be available at time F:
						//     - the one that became available at time T, and
						//     - the one used by j_pred
						//
						// (D) So ca(2).min() <= F <= ft(j_pred).max() hence ca(2).min() <= ft(j_pred).max().
						//
						// (E) this yields a contradiction with the condition ft(j_pred).max() < ca(2).min().
						if (ft.max() < s.core_availability(2).min()) continue;

						// If at least one successor of j_pred has already been dispatched, then j_pred must have finished already.
						bool can_disregard = false;
						for (const auto &successor_suspension : successors_suspensions[pred_idx]) {
							if (dispatched(n, *successor_suspension.first)) {
								can_disregard = true;
								break;
							}
						}
						if (can_disregard) continue;
					}

					// If j_pred is a predecessor of both j_high and j_low, we can disregard it if the maximum suspension from j_pred to j_high
					// is at most the minimum suspension from j_pred to j_low: susp_max(j_pred -> j_high) <= susp_min(j_pred -> j_low).
					//
					// To illustrate this, assume that j_low becomes ready at some time `t`. Then, due to the suspension, we know that
					// j_pred must have finished no later than `t - susp_min(j_pred -> j_low)`, and that `j_pred` can only block `j_high`
					// up to time `t + susp_max(j_pred -> j_high) - susp_min(j_pred -> j_low) <= t`. So either:
					// - j_high is ready when j_low becomes ready, so the assumption that j_low is dispatched next must be false, or
					// - something else causes j_high to become ready later than j_low, so this constraint is not important
					// Either way, this constraint can be disregarded.
					bool can_disregard = false;
					for (const auto &low_suspension : predecessors_suspensions[j_low]) {
						// Note that the condition `susp_max(j_pred -> j_high) <= susp_min(j_pred -> j_low)` will be true if and only if there
						// exists a constraint from j_pred to j_low whose *minimum* suspension is at least `susp_max(j_pred -> j_high)`. So we can
						// stop searching as soon as we find one such constraint.
						if (low_suspension.first->get_job_index() == pred_idx && low_suspension.second.min() >= high_suspension.max()) {
							can_disregard = true;
							break;
						}
					}
					if (can_disregard) {
						// Disregards *this* constraint, but other constraints from j_pred to j_high in predecessors_suspensions[j_high.get_job_index()]
						// will be evaluated in their own iteration of this loop.
						//
						// Note that only the constraint with the largest *maximum* suspension from j_pred to j_high is important for
						// the computation of susp_max(j_pred -> j_high), and that this is also the only constraint from j_pred to j_high that could
						// affect the final value of latest_ready_high. Therefor, it is irrelevant whether other constraints from j_pred to j_high
						// are disregarded.
						continue;
					}

					latest_ready_high = std::max(latest_ready_high, ft.max() + high_suspension.max());
				}
				return latest_ready_high;
			}

			// returns the earliest time at which `j` may become ready in `s`
			// assumes all predecessors of `j` are completed
			Time earliest_ready_time(const State& s, const Job<Time>& j) const
			{
				return ready_times(s, j).min(); // std::max(s.core_availability().min(), j.arrival_window().min());
			}

			// Find next time by which a sequential source job (i.e., 
			// a job without predecessors that can execute on a single core) 
			// of higher priority than the reference_job
			// is certainly released in any state in the node 'n'. 
			Time next_certain_higher_priority_seq_source_job_release(
				const Node& n,
				const Job<Time>& reference_job,
				Time until = Time_model::constants<Time>::infinity()) const
			{
				Time when = until;

				// a higher priority source job cannot be released before 
				// a source job of any priority is released
				Time t_earliest = n.get_next_certain_source_job_release();

				for (auto it = sequential_source_jobs_by_latest_arrival.lower_bound(t_earliest);
					it != sequential_source_jobs_by_latest_arrival.end(); it++)
				{
					const Job<Time>& j = *(it->second);

					// check if we can stop looking
					if (when < j.latest_arrival())
						break; // yep, nothing can lower 'when' at this point

					// j is not relevant if it is already scheduled or not of higher priority
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

			// Find next time by which a gang source job (i.e., 
			// a job without predecessors that cannot execute on a single core) 
			// of higher priority than the reference_job
			// is certainly released in state 's' of node 'n'. 
			Time next_certain_higher_priority_gang_source_job_ready_time(
				const Node& n,
				const State& s,
				const Job<Time>& reference_job,
				const unsigned int ncores,
				Time until = Time_model::constants<Time>::infinity()) const
			{
				Time when = until;

				// a higher priority source job cannot be released before 
				// a source job of any priority is released
				Time t_earliest = n.get_next_certain_source_job_release();

				for (auto it = gang_source_jobs_by_latest_arrival.lower_bound(t_earliest);
					it != gang_source_jobs_by_latest_arrival.end(); it++)
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
				}
				return when;
			}

			// Assuming that `reference_job` is dispatched next, find the earliest time by which a successor job (i.e., a job with predecessors) 
			// of higher priority than the reference_job is certainly ready in system state 's'.
			//
			// Let `ready_min` denote the earliest time at which `reference_job` becomes ready
			// and let `latest_ready_high` denote the return value of this function.
			//
			// If `latest_ready_high <= `ready_min`, the assumption that `reference_job` is dispatched next lead to a contradiction,
			// hence `reference_job` cannot be dispatched next. In this case, the exact value of `latest_ready_high` is meaningless,
			// except that it must be at most `ready_min`. After all, it was computed under an assumption that cannot happen.
			Time next_certain_higher_priority_successor_job_ready_time(
				const Node& n,
				const State& s,
				const Job<Time>& reference_job,
				const unsigned int ncores
			) const {
				auto ready_min = earliest_ready_time(s, reference_job);
				Time latest_ready_high = Time_model::constants<Time>::infinity();

				// a higer priority successor job cannot be ready before 
				// a job of any priority is released
				for (auto it = n.get_ready_successor_jobs().begin();
					it != n.get_ready_successor_jobs().end(); it++)
				{
					const Job<Time>& j_high = **it;

					// j_high is not relevant if it is already scheduled or not of higher priority
					if (j_high.higher_priority_than(reference_job)) {
						// does it beat what we've already seen?
						latest_ready_high = std::min(latest_ready_high, conditional_latest_ready_time(n, s, j_high, reference_job.get_job_index(), ncores));
						if (latest_ready_high <= ready_min) break;
					}
				}
				return latest_ready_high;
			}

			// Find the earliest possible job release of all jobs in a node except for the ignored job
			Time earliest_possible_job_release(
				const Node& n,
				const Job<Time>& ignored_job) const
			{
				DM("      - looking for earliest possible job release starting from: "
					<< n.earliest_job_release() << std::endl);

				for (auto it = jobs_by_earliest_arrival.lower_bound(n.earliest_job_release());
					it != jobs_by_earliest_arrival.end(); 	it++)
				{
					const Job<Time>& j = *(it->second);

					DM("         * looking at " << j << std::endl);

					// skip if it is the one we're ignoring or if it was dispatched already
					if (&j == &ignored_job || dispatched(n, j))
						continue;

					DM("         * found it: " << j.earliest_arrival() << std::endl);
					// it's incomplete and not ignored => found the earliest
					return j.earliest_arrival();
				}

				DM("         * No more future releases" << std::endl);
				return Time_model::constants<Time>::infinity();
			}

			// Find the earliest certain job release of all sequential source jobs 
			// (i.e., without predecessors and with minimum parallelism = 1) 
			// in a node except for the ignored job
			Time earliest_certain_sequential_source_job_release(
				const Node& n,
				const Job<Time>& ignored_job) const
			{
				DM("      - looking for earliest certain source job release starting from: "
					<< n.get_next_certain_source_job_release() << std::endl);

				for (auto it = sequential_source_jobs_by_latest_arrival.lower_bound(n.get_next_certain_source_job_release());
					it != sequential_source_jobs_by_latest_arrival.end(); it++)
				{
					const Job<Time>* jp = it->second;
					DM("         * looking at " << *jp << std::endl);

					// skip if it is the one we're ignoring or the job was dispatched already
					if (jp == &ignored_job || dispatched(n, *jp))
						continue;

					DM("         * found it: " << jp->latest_arrival() << std::endl);
					// it's incomplete and not ignored => found the earliest
					return jp->latest_arrival();
				}
				DM("         * No more future source job releases" << std::endl);
				return Time_model::constants<Time>::infinity();
			}

			// Find the earliest certain job release of all source jobs (i.e., without predecessors) 
			// in a node except for the ignored job
			Time earliest_certain_source_job_release(
				const Node& n,
				const Job<Time>& ignored_job) const
			{
				DM("      - looking for earliest certain source job release starting from: "
					<< n.get_next_certain_source_job_release() << std::endl);

				Time rmax = earliest_certain_sequential_source_job_release(n, ignored_job);

				for (auto it = gang_source_jobs_by_latest_arrival.lower_bound(n.get_next_certain_source_job_release());
					it != gang_source_jobs_by_latest_arrival.end(); it++)
				{
					const Job<Time>* jp = it->second;
					DM("         * looking at " << *jp << std::endl);

					// skip if it is the one we're ignoring or the job was dispatched already
					if (jp == &ignored_job || dispatched(n, *jp))
						continue;

					DM("         * found it: " << jp->latest_arrival() << std::endl);
					// it's incomplete and not ignored => found the earliest
					return std::min(rmax, jp->latest_arrival());
				}

				DM("         * No more future releases" << std::endl);
				return rmax;
			}

			Time get_earliest_job_arrival() const
			{
				if (jobs_by_earliest_arrival.empty())
					return Time_model::constants<Time>::infinity();
				else
					return jobs_by_earliest_arrival.begin()->first;
			}

			// Find the earliest certain job release of all sequential source jobs
			// (i.e., without predecessors and with minimum parallelism = 1) when
			// the system starts
			Time get_earliest_certain_seq_source_job_release() const
			{
				if (sequential_source_jobs_by_latest_arrival.empty())
					return Time_model::constants<Time>::infinity();
				else
					return sequential_source_jobs_by_latest_arrival.begin()->first;
			}

			// Find the earliest certain job release of all gang source jobs
			// (i.e., without predecessors and with possible parallelism > 1) when
			// the system starts
			Time get_earliest_certain_gang_source_job_release() const
			{
				if (gang_source_jobs_by_latest_arrival.empty())
					return Time_model::constants<Time>::infinity();
				else
					return gang_source_jobs_by_latest_arrival.begin()->first;
			}

		private:

			bool not_dispatched(const Node& n, const Job<Time>& j) const
			{
				return n.job_not_dispatched(j.get_job_index());
			}

			bool dispatched(const Node& n, const Job<Time>& j) const
			{
				return n.job_dispatched(j.get_job_index());
			}

			State_space_data(const State_space_data& origin) = delete;
		};
	}
}
#endif
