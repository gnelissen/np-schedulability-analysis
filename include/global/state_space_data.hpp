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

namespace NP {
	namespace Global {

		template<class Time> class Schedule_state;
		template<class Time> class Schedule_node;

		template<class Time> class State_space_data
		{
		public:

			typedef Scheduling_problem<Time> Problem;
			typedef typename Scheduling_problem<Time>::Task_set Task_set;
			typedef const Subtask<Time>* Subtask_ref;
			typedef typename Scheduling_problem<Time>::Abort_actions Abort_actions;
			typedef Schedule_state<Time> State;
			typedef Schedule_node<Time> Node;
			typedef typename std::vector<Interval<Time>> CoreAvailability;

		private:
			// list of actions when a job is aborted
			std::vector<const Abort_action<Time>*> abort_actions;

			// number of cores
			const unsigned int num_cpus;

		public:
			const Task_set tasks;

			State_space_data(const Task_set& tasks,
				const Abort_actions& aborts,
				unsigned int num_cpus)
				: tasks(tasks)
				, num_cpus(num_cpus)
				, abort_actions(tasks.size(), NULL)
			{
				/*for (const Abort_action<Time>& a : aborts) {
					const Job<Time>& j = lookup<Time>(jobs, a.get_id());
					abort_actions[j.get_job_index()] = &a;
				}*/
			}

			size_t num_tasks() const
			{
				return tasks.size();
			}

			const Abort_action<Time>* abort_action_of(Task_index t, Subtask_index j) const
			{
				return abort_actions[j];//[t][j]
			}

			// returns the ready time interval of `j` in `s`
			// assumes all predecessors of j are dispatched
			Interval<Time> ready_times(const State& s, const Subtask<Time>& j) const
			{
				Task_index t_id = j.task_id();
				const Task<Time>& task = tasks[t_id];

				Interval<Time> r = s.get_release_times(t_id, j.id());
				const auto& predecessors = task.get_predecessors_of(j.id());
				for (const auto& pred : predecessors.start_before_start)
				{
					Interval<Time> st = s.get_start_times(t_id, pred.subtask);
					r.lower_bound(st.min() + pred.delay.min());
					r.extend_to(st.max() + pred.delay.max());
				}
				for (const auto& pred : predecessors.finish_before_start)
				{
					Interval<Time> ft = s.get_finish_times(t_id, pred.subtask);
					r.lower_bound(ft.min() + pred.delay.min());
					r.extend_to(ft.max() + pred.delay.max());
				}
				for (const auto& pred : predecessors.exclusions)
				{
					Interval<Time> ft = s.get_finish_times(t_id, pred.subtask);
					if (ft.min() > 0) {
						r.lower_bound(ft.min() + pred.delay.min());
						r.extend_to(ft.max() + pred.delay.max());
					}
				}

				return r;
			}

			// returns the earliest time at which `j` may become ready in `s`
			// assumes all predecessors of `j` are completed
			Time earliest_ready_time(const State& s, const Subtask<Time>& j) const
			{
				return ready_times(s, j).min();
			}

			// This function assumes `j` is already dispatched in `s`.
			// It then checks if the subtask `j` certainly completed its execution in `s`
			bool is_cert_finished(const Subtask<Time>& j, const Node& n, const State& s) const
			{
				// If there is a single core, all dispatched jobs must have finished when the core becomes available.
				if (num_cpus == 1)
					return true;

				// The optimization above can be generalized to multiple cores, using the following knowledge:
				// We will prove the following claim: (ft(j) denotes finish time of j and ca(n) denotes core_availability(n))
				// If ft(j).min() <= ca(1).min && ft(j).max() <= ca(2).min() then no core can be available
				//     before j is finished.
				// Proof:
				// (A) Assume for a contradiction that a core becomes available at time T before j is finished at time F > T.
				//
				// (B) Since a core became available at time T, it must hold that ca(1).min <= T <= ca(1).max().
				//
				// (C) Since j finishes at time F > T, we know that at least 2 cores must be available at time F:
				//     - the one that became available at time T, and
				//     - the one used by j
				//
				// (D) So ca(2).min() <= F <= ft(j).max() hence ca(2).min() <= ft(j).max().
				//
				// (E) From the condition ft(j).max() <= ca(2).min(), it follows that ft(j).max() == ft(j).min() == F.
				//
				// (F) Since ca(1).min() <= T < F == ft(j).min(), it follows that ca(1).min() < ft(j).min(),
				//     which contradicts the condition that ft(j).min() <= ca(1).min().
				Interval<Time> ft = s.get_finish_times(j.task_id(), j.id());
				if (ft.min() <= s.core_availability(1).min() && ft.max() <= s.core_availability(2).min())
					return true;

				// Alternatively, if we check that `ft(j).max() < ca(2).min()` (strictly smaller),
				// we would already derive a contradiction at (E) since ca(2).min() <= ft(j).max() contradicts ft(j).max() < ca(2).min()
				if (ft.max() < s.core_availability(2).min())
					return true;

				// If at least one successor of j has already been dispatched, then j must have finished already.
				const auto& successors = tasks[j.task_id()].get_successors_of(j.id());
				for (const auto& succ : successors.start_after_finish) {
					if (is_dispatched(n, j.task_id(), succ.subtask)) {
						return true;
					}
				}

				return false;
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
				const Subtask<Time>& j_high, const Subtask<Time>& j_low,
				const unsigned int num_cores_j_low = 1) const
			{
				Time latest_ready_high = s.get_release_times(j_high.task_id(), j_high.id()).max();

				// if the minimum parallelism of j_high is more than num_cores_j_low, then
				// for j_high to be released and have its predecessors completed
				// is not enough to interfere with j_low.
				// It must also have enough cores free.
				if (j_high.get_min_parallelism() > num_cores_j_low)
				{
					// max {rj_max,Amax(sjmin)}
					latest_ready_high = std::max(latest_ready_high, s.core_availability(j_high.get_min_parallelism()).max());
				}

				const auto& predecessors_j_low = tasks[j_low.task_id()].get_predecessors_of(j_low.id());

				// j_high is not ready until all its predecessors have completed, and their corresponding suspension delays are over.
				// But, since we are assuming that `j_low` is dispatched next and all predecessors of `j` have been dispatched,
				// we can disregard some of them.
				const auto& predecessors_j_high = tasks[j_high.task_id()].get_predecessors_of(j_high.id());
				for (const auto& pred_j_high : predecessors_j_high.finish_before_start)
				{
					const Subtask<Time>& j_pred = tasks[j_high.task_id()].get_subtask(pred_j_high.subtask);

					// If the suspension is 0 and j_pred is certainly finished when j_low is dispatched, then j_pred cannot postpone
					// the (latest) ready time of j_high.
					if (pred_j_high.delay.max() == 0 && is_cert_finished(j_pred, n, s))
						continue;

					// If j_pred is a predecessor of both j_high and j_low, we can disregard it if the maximum suspension from j_pred to j_high
					// is at most the minimum suspension from j_pred to j_low: susp_max(j_pred -> j_high) <= susp_min(j_pred -> j_low).
					//
					// To illustrate this, assume that j_low becomes ready at some time `t`. Then, due to the suspension, we know that
					// j_pred must have finished no later than `t - susp_min(j_pred -> j_low)`, and that `j_pred` can only block `j_high`
					// up to time `t + susp_max(j_pred -> j_high) - susp_min(j_pred -> j_low) <= t`. So either:
					// - j_high is ready when j_low becomes ready, so the assumption that j_low is dispatched next must be false, or
					// - something else causes j_high to become ready later than j_low, so this constraint is not important
					// Either way, this constraint can be disregarded.
					//
					// Note that j_pred can be a predecessor of both j_high and j_low only if j_high and j_low are subtasks of the same task.
					if (j_high.task_id() == j_low.task_id()) {
						bool can_disregard = false;
						for (const auto& pred_j_low : predecessors_j_low.finish_before_start) {
							// Note that the condition `susp_max(j_pred -> j_high) <= susp_min(j_pred -> j_low)` will be true if and only if there
							// exists a constraint from j_pred to j_low whose *minimum* suspension is at least `susp_max(j_pred -> j_high)`. So we can
							// stop searching as soon as we find one such constraint.
							if (pred_j_low.subtask == j_pred.id() && pred_j_low.delay.min() >= pred_j_high.delay.max()) {
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
					}

					Interval<Time> ft = s.get_finish_times(j_pred.task_id(), j_pred.id());
					latest_ready_high = std::max(latest_ready_high, ft.max() + pred_j_high.delay.max());
				}
				return latest_ready_high;
			}

			// Assuming that `reference_subtask` is dispatched next on `ncores`, find the earliest time by which a job 
			// of higher priority than the reference_subtask is certainly ready in system state 's'.
			//
			// Let `ready_min` denote the earliest time at which `reference_subtask` becomes ready
			// and let `latest_ready_high` denote the return value of this function.
			//
			// If `latest_ready_high <= `ready_min`, the assumption that `reference_subtask` is dispatched next leads to a contradiction,
			// hence `reference_subtask` cannot be dispatched next. In this case, the exact value of `latest_ready_high` is meaningless,
			// except that it must be at most `ready_min`. After all, it was computed under an assumption that cannot happen.
			Time next_certain_higher_priority_job_ready_time(
				const Node& n,
				const State& s,
				const Subtask<Time>& reference_subtask,
				const unsigned int ncores
			) const {
				auto ready_min = earliest_ready_time(s, reference_subtask);
				Time latest_ready_high = Time_model::constants<Time>::infinity();

				// a higer priority successor job cannot be ready before 
				// a job of any priority is released
				const auto& ready_subtasks = n.get_ready_subtasks();
				for (int i = 0; i < num_tasks(); i++) {
					for (int j=0; j < ready_subtasks[i].size(); j++)
					{
						const Subtask<Time>& j_high = *ready_subtasks[i][j];

						// j_high is not relevant if it is already scheduled or not of higher priority
						if (j_high.higher_priority_than(reference_subtask)) {
							// does it beat what we've already seen?
							latest_ready_high = std::min(latest_ready_high, conditional_latest_ready_time(n, s, j_high, reference_subtask, ncores));
							if (latest_ready_high <= ready_min) 
								return latest_ready_high;
						}
					}
				}
				return latest_ready_high;
			}

		private:
			
			bool is_dispatched(const Node& n, const Subtask<Time>& j) const
			{
				return n.is_dispatched(j);
			}

			bool is_dispatched(const Node& n, Task_index i, Subtask_index j) const
			{
				return n.is_dispatched(i, j);
			}

			State_space_data(const State_space_data& origin) = delete;
		};
	}
}
#endif