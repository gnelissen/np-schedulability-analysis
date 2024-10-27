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
			std::vector<By_time_map> _successor_jobs_by_latest_arrival_by_cluster;
			std::vector<By_time_map> _sequential_source_jobs_by_latest_arrival_by_cluster;
			std::vector<By_time_map> _gang_source_jobs_by_latest_arrival_by_cluster;
			std::vector<By_time_map> _jobs_by_earliest_arrival_by_cluster;
			std::vector<By_time_map> _jobs_by_deadline;
			std::vector<Job_precedence_set> _predecessors;

			// not touched after initialization
			std::vector<Suspensions_list> _predecessors_suspensions;
			std::vector<Suspensions_list> _successors_suspensions;

			// list of actions when a job is aborted
			std::vector<const Abort_action<Time>*> abort_actions;

			// number of clusters and cores per cluster
			const unsigned int num_clusters;
			const std::vector<unsigned int> num_cpus;
		
		public:
			// use these const references to ensure read-only access
			const Workload& jobs;
			const std::vector<By_time_map>& successor_jobs_by_latest_arrival_by_cluster;
			const std::vector<By_time_map>& sequential_source_jobs_by_latest_arrival_by_cluster;
			const std::vector<By_time_map>& gang_source_jobs_by_latest_arrival_by_cluster;
			const std::vector<By_time_map>& jobs_by_earliest_arrival_by_cluster;
			const std::vector<By_time_map>& jobs_by_deadline;
			const std::vector<Job_precedence_set>& predecessors;
			const std::vector<Suspensions_list>& predecessors_suspensions;
			const std::vector<Suspensions_list>& successors_suspensions;

			State_space_data(const Workload& jobs,
				const Precedence_constraints& edges,
				const Abort_actions& aborts,
				const std::vector<unsigned int>& num_cpus)
				: jobs(jobs)
				, num_clusters(num_cpus.size())
				, num_cpus(num_cpus)
				, _successor_jobs_by_latest_arrival_by_cluster(num_cpus.size())
				, _sequential_source_jobs_by_latest_arrival_by_cluster(num_cpus.size())
				, _gang_source_jobs_by_latest_arrival_by_cluster(num_cpus.size())
				, _jobs_by_earliest_arrival_by_cluster(num_cpus.size())
				, _jobs_by_deadline(num_cpus.size())
				, successor_jobs_by_latest_arrival_by_cluster(_successor_jobs_by_latest_arrival_by_cluster)
				, sequential_source_jobs_by_latest_arrival_by_cluster(_sequential_source_jobs_by_latest_arrival_by_cluster)
				, gang_source_jobs_by_latest_arrival_by_cluster(_gang_source_jobs_by_latest_arrival_by_cluster)
				, jobs_by_earliest_arrival_by_cluster(_jobs_by_earliest_arrival_by_cluster)
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

				for (const Job<Time>& j : jobs) {
					if (_predecessors_suspensions[j.get_job_index()].size() > 0) {
						_successor_jobs_by_latest_arrival_by_cluster[j.get_affinity()].insert({ j.latest_arrival(), &j });
					}
					else if (j.get_min_parallelism() == 1) {
						_sequential_source_jobs_by_latest_arrival_by_cluster[j.get_affinity()].insert({ j.latest_arrival(), &j });
					}
					else {
						_gang_source_jobs_by_latest_arrival_by_cluster[j.get_affinity()].insert({ j.latest_arrival(), &j });
					}
					_jobs_by_earliest_arrival_by_cluster[j.get_affinity()].insert({ j.earliest_arrival(), &j });
					_jobs_by_deadline[j.get_affinity()].insert({ j.get_deadline(), &j });
				}

				for (const Abort_action<Time>& a : aborts) {
					const Job<Time>& j = lookup<Time>(jobs, a.get_id());
					abort_actions[j.get_job_index()] = &a;
				}
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
			// assumes all predecessors of j are completed
			Interval<Time> ready_times(const State& s, const Job<Time>& j) const
			{
				Interval<Time> r = j.arrival_window();
				for (const auto& pred : predecessors_suspensions[j.get_job_index()])
				{
					auto pred_idx = pred.first->get_job_index();
					auto pred_susp = pred.second;
					Interval<Time> ft{ 0, 0 };
					//if (!s.get_finish_times(pred_idx, ft))
					//	ft = get_finish_times(jobs[pred_idx]);
					s.get_finish_times(pred_idx, ft);
					r.lower_bound(ft.min() + pred_susp.min());
					r.extend_to(ft.max() + pred_susp.max());
				}
				return r;
			}

			// returns the ready time interval of `j` in `s` when dispatched on `ncores`
			// assumes all predecessors of j are completed
			// ignores the finish time of the predecessors in the `disregard` set.
			Interval<Time> ready_times(
				const State& s, const Job<Time>& j,
				const Job_precedence_set& disregard,
				const unsigned int ncores = 1) const
			{
				unsigned int affinity = j.get_affinity();
				const auto& cs = s.cluster(affinity);
				Time avail_min = cs.earliest_finish_time();
				Interval<Time> r = j.arrival_window();

				// if the minimum parallelism of j is more than ncores, then 
				// for j to be released and have its successors completed 
				// is not enough to interfere with a lower priority job.
				// It must also have enough cores free.
				if (j.get_min_parallelism() > ncores)
				{
					// max {rj_max,Amax(sjmin)}
					r.extend_to(cs.core_availability(j.get_min_parallelism()).max());
				}

				for (const auto& pred : predecessors_suspensions[j.get_job_index()])
				{
					auto pred_idx = pred.first->get_job_index();
					// skip if part of disregard
					if (contains(disregard, pred_idx))
						continue;

					// if the predecessor and the job j arre assigned on the same cluster and
					// there is no suspension time and there is a single core in the cluster, then
					// predecessors are finished as soon as the processor becomes available
					auto pred_susp = pred.second;
					unsigned int aff_pred = pred.first->get_affinity();
					if (aff_pred == affinity && num_cpus[affinity] == 1 && pred_susp.max() == 0)
					{
						r.lower_bound(avail_min);
						r.extend_to(avail_min);
					}
					else
					{
						Interval<Time> ft{ 0, 0 };
						//if (!s.get_finish_times(pred_idx, ft))
						//	ft = get_finish_times(jobs[pred_idx]);
						s.get_finish_times(pred_idx, ft);
						r.lower_bound(ft.min() + pred_susp.min());
						r.extend_to(ft.max() + pred_susp.max());
					}
				}
				return r;
			}

			// returns the latest time at which `j` may become ready in `s`
			// assumes all predecessors of `j` are completed
			Time latest_ready_time(const State& s, const Job<Time>& j) const
			{
				return ready_times(s, j).max();
			}

			// returns the latest time at which `j_hp` may become ready in `s` when executing on `ncores`
			// ignoring the finish time of all predecessors `j_hp` has in common with `j_ref`.
			// assumes all predecessors of `j_hp` are completed
			Time latest_ready_time(
				const State& s, Time earliest_ref_ready,
				const Job<Time>& j_hp, const Job<Time>& j_ref,
				const unsigned int ncores = 1) const
			{
				auto rt = ready_times(s, j_hp, predecessors_of(j_ref), ncores);
				return std::max(rt.max(), earliest_ref_ready);
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
				auto cluster_id = reference_job.get_affinity();

				Time when = until;

				// a higher priority source job cannot be released before 
				// a source job of any priority is released
				Time t_earliest = n.get_next_certain_source_job_release(cluster_id);

				for (auto it = sequential_source_jobs_by_latest_arrival_by_cluster[cluster_id].lower_bound(t_earliest);
					it != sequential_source_jobs_by_latest_arrival_by_cluster[cluster_id].end(); it++)
				{
					const Job<Time>& j = *(it->second);

					// check if we can stop looking
					if (when < j.latest_arrival())
						break; // yep, nothing can lower 'when' at this point

					// j is not relevant if it is already scheduled or not of higher priority
					if (j.higher_priority_than(reference_job) && ready(n, j))
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
				auto cluster_id = reference_job.get_affinity();
				const auto& cs = s.cluster(cluster_id);

				Time when = until;

				// a higher priority source job cannot be released before 
				// a source job of any priority is released
				Time t_earliest = n.get_next_certain_source_job_release(cluster_id);

				for (auto it = gang_source_jobs_by_latest_arrival_by_cluster[cluster_id].lower_bound(t_earliest);
					it != gang_source_jobs_by_latest_arrival_by_cluster[cluster_id].end(); it++)
				{
					const Job<Time>& j = *(it->second);

					// check if we can stop looking
					if (when < j.latest_arrival())
						break; // yep, nothing can lower 'when' at this point

					// j is not relevant if it is already scheduled or not of higher priority
					if (ready(n, j) && j.higher_priority_than(reference_job))
					{
						// if the minimum parallelism of j is more than ncores, then 
						// for j to be released and have its successors completed 
						// is not enough to interfere with a lower priority job.
						// It must also have enough cores free.
						if (j.get_min_parallelism() > ncores)
						{
							// max {rj_max,Amax(sjmin)}
							when = std::min(when, std::max(j.latest_arrival(), cs.core_availability(j.get_min_parallelism()).max()));
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

			// Find next time by which a successor job (i.e., a job with predecessors) 
			// of higher priority than the reference_job
			// is certainly released in system state 's' at or before a time 'until'.
			Time next_certain_higher_priority_successor_job_ready_time(
				const Node& n,
				const State& s,
				const Job<Time>& reference_job,
				const unsigned int ncores,
				Time until = Time_model::constants<Time>::infinity()) const
			{
				auto cluster_id = reference_job.get_affinity();
				auto ready_min = earliest_ready_time(s, reference_job);
				Time when = until;

				// a higer priority successor job cannot be ready before 
				// a job of any priority is released
				Time t_earliest = n.earliest_job_release(cluster_id);
				for (auto it = successor_jobs_by_latest_arrival_by_cluster[cluster_id].lower_bound(t_earliest);
					it != successor_jobs_by_latest_arrival_by_cluster[cluster_id].end(); it++)
				{
					const Job<Time>& j = *(it->second);

					// check if we can stop looking
					if (when < j.latest_arrival())
						break; // yep, nothing can lower 'when' at this point

					// j is not relevant if it is already scheduled or not of higher priority
					if (ready(n, j) && j.higher_priority_than(reference_job)) {
						// does it beat what we've already seen?
						when = std::min(when,
							latest_ready_time(s, ready_min, j, reference_job, ncores));
						// No break, as later jobs might have less suspension or require less cores to start executing.
					}
				}
				return when;
			}

			// Find the earliest possible job release of all jobs in a node except for the ignored job
			Time earliest_possible_job_release(
				const Node& n,
				const Job<Time>& ignored_job) const
			{
				unsigned int cluster_id = ignored_job.get_affinity();

				DM("      - looking for earliest possible job release starting from: "
					<< n.earliest_job_release(cluster_id) << std::endl);

				for (auto it = jobs_by_earliest_arrival_by_cluster[cluster_id].lower_bound(n.earliest_job_release(cluster_id));
					it != jobs_by_earliest_arrival_by_cluster[cluster_id].end(); it++)
				{
					const Job<Time>& j = *(it->second);

					DM("         * looking at " << j << std::endl);

					// skip if it is the one we're ignoring or if it was dispatched already
					if (&j == &ignored_job || !unfinished(n, j))
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
				unsigned int cluster_id = ignored_job.get_affinity();

				DM("      - looking for earliest certain source job release starting from: "
					<< n.get_next_certain_source_job_release() << std::endl);

				for (auto it = sequential_source_jobs_by_latest_arrival_by_cluster[cluster_id].lower_bound(n.get_next_certain_source_job_release(cluster_id));
					it != sequential_source_jobs_by_latest_arrival_by_cluster[cluster_id].end(); it++)
				{
					const Job<Time>* jp = it->second;
					DM("         * looking at " << *jp << std::endl);

					// skip if it is the one we're ignoring or the job was dispatched already
					if (jp == &ignored_job || !unfinished(n, *jp))
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
				unsigned int cluster_id = ignored_job.get_affinity();

				DM("      - looking for earliest certain source job release starting from: "
					<< n.get_next_certain_source_job_release() << std::endl);

				Time rmax = earliest_certain_sequential_source_job_release(n, ignored_job);

				for (auto it = gang_source_jobs_by_latest_arrival_by_cluster[cluster_id].lower_bound(n.get_next_certain_source_job_release(cluster_id));
					it != gang_source_jobs_by_latest_arrival_by_cluster[cluster_id].end(); it++)
				{
					const Job<Time>* jp = it->second;
					DM("         * looking at " << *jp << std::endl);

					// skip if it is the one we're ignoring or the job was dispatched already
					if (jp == &ignored_job || !unfinished(n, *jp))
						continue;

					DM("         * found it: " << jp->latest_arrival() << std::endl);
					// it's incomplete and not ignored => found the earliest
					return std::min(rmax, jp->latest_arrival());
				}

				DM("         * No more future releases" << std::endl);
				return rmax;
			}

			// Find the earliest certain job release of all sequential source jobs
			// (i.e., without predecessors and with minimum parallelism = 1) when
			// the system starts
			Time get_earliest_certain_seq_source_job_release(unsigned int cluster_id) const
			{
				if (sequential_source_jobs_by_latest_arrival_by_cluster[cluster_id].empty())
					return Time_model::constants<Time>::infinity();
				else
					return sequential_source_jobs_by_latest_arrival_by_cluster[cluster_id].begin()->first;
			}

			// Find the earliest certain job release of all gang source jobs
			// (i.e., without predecessors and with possible parallelism > 1) when
			// the system starts
			Time get_earliest_certain_gang_source_job_release(unsigned int cluster_id) const
			{
				if (gang_source_jobs_by_latest_arrival_by_cluster[cluster_id].empty())
					return Time_model::constants<Time>::infinity();
				else
					return gang_source_jobs_by_latest_arrival_by_cluster[cluster_id].begin()->first;
			}

			// Find the earliest possible job release of all source jobs
			// (i.e., without predecessors) when the system starts
			Time get_earliest_possible_source_job_release(unsigned int cluster_id) const
			{
				return jobs_by_earliest_arrival_by_cluster[cluster_id].begin()->first;
			}

		private:
			// Check wether a job is ready (not dspatched yet and all its predecessors are completed).
			bool ready(const Node& n, const Job<Time>& j) const
			{
				return n.job_incomplete(j.get_job_index()) && n.job_ready(predecessors_of(j));
			}

			bool unfinished(const Node& n, const Job<Time>& j) const
			{
				return n.job_incomplete(j.get_job_index());
			}

			State_space_data(const State_space_data& origin) = delete;
		};
	}
}
#endif
