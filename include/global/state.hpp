#ifndef GLOBAL_STATE_HPP
#define GLOBAL_STATE_HPP
#include <algorithm>
#include <cassert>
#include <iostream>
#include <ostream>
#include <set>

#ifdef CONFIG_PARALLEL
#include <tbb/spin_rw_mutex.h>
#include <tbb/spin_mutex.h>
#endif

#include "config.h"
#include "cache.hpp"
#include "index_set.hpp"
#include "jobs.hpp"
#include "statistics.hpp"
#include "util.hpp"
#include "global/state_space_data.hpp"
#include "global/cluster.hpp"

namespace NP {

	namespace Global {

		typedef Index_set Job_set;
		typedef std::vector<Job_index> Job_precedence_set;

		template<class Time> class State_space_data;

		template<class Time> class Schedule_node;

		// NOTE: Schedule_state is not thread-safe. Thread-safety must be enforced by callers.
		template<class Time> class Schedule_state
		{
		private:
			typedef typename State_space_data<Time>::Workload Workload;
			typedef const Job<Time>* Job_ref;
			typedef std::vector<std::pair<Job_ref, Interval<Time>>> Job_finish_times;
			typedef std::vector<std::pair<Job_ref, Interval<Time>>> Susp_list;
			typedef std::vector<Susp_list> Successors;
			typedef std::vector<Susp_list> Predecessors;
			typedef typename Cluster_state<Time>::Cluster_state_ref Cluster_state_ref;

		private:	
			std::vector<Cluster_state_ref> clusters;
			// job_finish_times holds the finish times of all the jobs that still have an unscheduled successor
			Job_finish_times job_finish_times;
			// earliest_certain_successor_job_dispatch holds the earliest time a job with at least one predecessor 
			// is certainly ready and certainly has enough free cores to start executing
			std::vector<Time> earliest_certain_successor_job_dispatch;

		public:
			~Schedule_state()
			{
				for (auto& cluster : clusters) {
					release_cluster(cluster);
				}
				//clusters.clear();
			}
			
			// initial state -- nothing yet has finished, nothing is running
			Schedule_state(const std::vector<unsigned int>& num_cpus, const State_space_data<Time>& state_space_data)
			: earliest_certain_successor_job_dispatch(num_cpus.size(), Time_model::constants<Time>::infinity())
			{
				assert(num_cpus.size() > 0);
				clusters.reserve(num_cpus.size());
				for (int i = 0; i < num_cpus.size(); i++)
				{
					clusters.push_back(acquire_cluster<Time>(i, num_cpus[i], state_space_data.get_earliest_certain_gang_source_job_release(i)));
				}
			}

			Schedule_state(const std::vector<std::vector<Interval<Time>>>& proc_initial_state, const State_space_data<Time>& state_space_data)
			: earliest_certain_successor_job_dispatch(proc_initial_state.size(), Time_model::constants<Time>::infinity())
			{
				clusters.reserve(proc_initial_state.size());
				for (int i = 0; i < proc_initial_state.size(); i++)
				{
					clusters.push_back(acquire_cluster<Time>(i, proc_initial_state[i], state_space_data.get_earliest_certain_gang_source_job_release(i)));
				}
			}

			// transition: new state by scheduling a job 'j' in an existing state 'from'
			Schedule_state(
				const Schedule_state& from,
				const Job<Time>& j,
				Interval<Time> start_times,
				Interval<Time> finish_times,
				const Job_set& scheduled_jobs,
				const std::vector<Job_ref>& jobs_with_pending_succ,
				const std::vector<std::vector<Job_ref>>& ready_succ_jobs,
				const State_space_data<Time>& state_space_data,
				Time next_source_job_rel,
				unsigned int ncores = 1)
				: earliest_certain_successor_job_dispatch(from.clusters.size())
			{
				clusters.reserve(from.clusters.size());
				for (int i = 0; i < from.clusters.size(); i++) {
					if (i == j.get_affinity())
						clusters.push_back(acquire_cluster<Time>(from.cluster(i), j.get_job_index(), start_times, finish_times, scheduled_jobs, state_space_data, next_source_job_rel, ncores));
					else
						clusters.push_back(from.clusters[i]);
				}
				// save the job finish time of every job with a successor that is not executed yet in the current state
				const Predecessors& predecessors_of = state_space_data.predecessors_suspensions;
				update_job_finish_times(from, j, start_times, finish_times, predecessors_of, jobs_with_pending_succ);
				// NOTE: must be done after the finish times and core availabilities have been updated
				const Successors& successors_of = state_space_data.successors_suspensions;
				update_earliest_certain_successor_job_dispatch(ready_succ_jobs, predecessors_of, successors_of);
			}

			// initial state -- nothing yet has finished, nothing is running
			void reset(const std::vector<unsigned int>& num_cpus, const State_space_data<Time>& state_space_data)
			{
				assert(num_cpus.size() > 0);
				clear();
				clusters.reserve(num_cpus.size());
				for (int i = 0; i < num_cpus.size(); i++)
				{
					clusters.push_back(acquire_cluster(i, num_cpus[i], state_space_data.get_earliest_certain_gang_source_job_release(i)));
				}
				earliest_certain_successor_job_dispatch = std::vector<Time>(num_cpus.size(), Time_model::constants<Time>::infinity());
			}

			void reset(const std::vector<std::vector<Interval<Time>>>& proc_initial_state, const State_space_data<Time>& state_space_data)
			{
				assert(proc_initial_state.size() > 0 && proc_initial_state[0].size() > 0);
				clear();
				clusters.reserve(proc_initial_state.size());
				for (int i = 0; i < proc_initial_state.size(); i++)
				{
					clusters.push_back(acquire_cluster<Time>(i, proc_initial_state[i], state_space_data.get_earliest_certain_gang_source_job_release(i)));
				}
				earliest_certain_successor_job_dispatch = std::vector<Time>(proc_initial_state.size(), Time_model::constants<Time>::infinity());
			}

			// transition: new state by scheduling a job 'j' in an existing state 'from'
			void reset(
				const Schedule_state& from,
				const Job<Time>& j,
				Interval<Time> start_times,
				Interval<Time> finish_times,
				const Job_set& scheduled_jobs,
				const std::vector<Job_ref>& jobs_with_pending_succ,
				const std::vector<std::vector<Job_ref>>& ready_succ_jobs,
				const State_space_data<Time>& state_space_data,
				Time next_source_job_rel,
				unsigned int ncores = 1)
			{	
				clear();
				clusters.reserve(from.clusters.size());
				for (int i = 0; i < from.clusters.size(); i++) {
					if (i == j.get_affinity())
						clusters.push_back(acquire_cluster<Time>(from.cluster(i), j.get_job_index(), start_times, finish_times, scheduled_jobs, state_space_data, next_source_job_rel, ncores));
					else
						clusters.push_back(from.clusters[i]);
				}
				// save the job finish time of every job with a successor that is not executed yet in the current state
				const Predecessors& predecessors_of = state_space_data.predecessors_suspensions;
				update_job_finish_times(from, j, start_times, finish_times, predecessors_of, jobs_with_pending_succ);
				// NOTE: must be done after the finish times and core availabilities have been updated
				earliest_certain_successor_job_dispatch.resize(from.clusters.size());
				const Successors& successors_of = state_space_data.successors_suspensions;
				update_earliest_certain_successor_job_dispatch(ready_succ_jobs, predecessors_of, successors_of);
			}

			void clear() {
				for (auto& cluster : clusters) {
					release_cluster(cluster);
				}
				clusters.clear();
				job_finish_times.clear();
			}

			// get the cluster state by its index
			const Cluster_state<Time>& cluster(unsigned int idx) const
			{
				assert(idx < clusters.size());
				return *(clusters[idx]);
			}

			Time next_certain_successor_jobs_dispatch(const unsigned int cluster_id) const
			{
				assert(cluster_id < earliest_certain_successor_job_dispatch.size());
				return earliest_certain_successor_job_dispatch[cluster_id];
			}

			// writes the finish time interval of job 'j' in 'ftimes' if the finish time of 'j' is known. Returns false if the finish time of 'j' is not known.
			bool get_finish_times(Job_index j, Interval<Time>& ftimes) const
			{
				int offset = jft_find(j);
				if (offset < job_finish_times.size() && job_finish_times[offset].first->get_job_index() == j)
				{
					ftimes = job_finish_times[offset].second;
					return true;
				}
				else {
					ftimes = Interval<Time>{ 0, Time_model::constants<Time>::infinity() };
					return false;
				}
			}

			// check if 'other' state can merge with this state
			bool can_merge_with(const Schedule_state<Time>& other, bool conservative, bool use_job_finish_times = false) const
			{
				bool other_in_this=false;
				for (int i = 0; i < clusters.size(); i++) {
					if (clusters[i] != other.clusters[i] && !clusters[i]->can_merge_with(other.clusters[i], conservative, other_in_this))
						return false;
				}
				if (use_job_finish_times)
					return check_finish_times_overlap(other.job_finish_times, conservative, other_in_this);
				else
					return true;
			}

			// first check if 'other' state can merge with this state, then, if yes, merge 'other' with this state.
			bool try_to_merge(const Schedule_state<Time>& other, bool conservative, bool use_job_finish_times = false)
			{
				if (!can_merge_with(other, conservative, use_job_finish_times))
					return false;

				for (int i = 0; i < clusters.size(); i++) {
					if (clusters[i] != other.clusters[i]) {
						if (clusters[i].unique())
							clusters[i]->merge_in_place(other.clusters[i]);
						else
							clusters[i] = clusters[i]->merge(other.clusters[i]);
						
						earliest_certain_successor_job_dispatch[i] = std::max(earliest_certain_successor_job_dispatch[i], other.earliest_certain_successor_job_dispatch[i]);
					}
				}

				// merge job_finish_times
				widen_finish_times(other.job_finish_times);
				DM("+++ merged " << other << " into " << *this << std::endl);
				return true;
			}

			// output the state in CSV format
			void export_state (std::ostream& stream, const Workload& jobs)
			{
				stream << "=====State=====\n";
				for (int i = 0; i < clusters.size(); ++i)
				{
					stream << "## Cluster " << i << ": ##\n";
					clusters[i]->export_state(stream, jobs);
				}
				stream << "Finish times predecessors: [<task_id>,<job_id>]:[<ft_min>,<ft_max>]\n";
				// Jobs with pending successors: <job_id>,<ft_min>,<ft_max>\n
				for (const auto& jft : job_finish_times) {
					const auto& j = *(jft.first);
					stream << "[" << j.get_task_id() << "," << j.get_job_id() << "]:[" << jft.second.min() << "," << jft.second.max() << "]\n";
				}
			}

		private:

			// update the list of finish times of jobs with successors w.r.t. the previous system state
			void update_job_finish_times(const Schedule_state& from,
				const Job<Time>& j, 
				const Interval<Time>& start_times,
				const Interval<Time>& finish_times,
				const Predecessors& predecessors_of,
				const std::vector<Job_ref>& jobs_with_pending_succ)
			{
				auto j_index = j.get_job_index();
				auto affinity = j.get_affinity();
				Time lst = start_times.max();
				Time lft = finish_times.max();

				job_finish_times.reserve(jobs_with_pending_succ.size());

				auto it = from.job_finish_times.begin();
				for (Job_ref job : jobs_with_pending_succ)
				{
					auto job_index = job->get_job_index();
					if (job_index == j_index)
						job_finish_times.emplace_back(job, finish_times);
					else {
						// we find the finish time interval of `job` from the previous state. 
						// Note that that with conditional DAGs, it is *NOT TRUE* anymore that if 
						// `job` has non-completed successors in the new state,
						// it must have had non-completed successors in the previous state too
						auto it_idx = it->first->get_job_index();
						// advance the iterator until we find the job or pass it
						while (it != from.job_finish_times.end() && it_idx < job_index) {
							it++;
						}
						if (it == from.job_finish_times.end() || it_idx > job_index) {
							continue;
						}
						Time job_eft = it->second.min();
						Time job_lft = it->second.max();
						// if both jobs execute on the same cluster and
						// there is a single core, then we know that jobs that were
						// dispatched in the past on that cluster cannot have
						// finished later than when our new job starts executing
						if (job->get_affinity() == affinity && clusters[affinity]->num_cpus() == 1)
						{
							if (job_lft > lst)
								job_lft = lst;
						}
						// if it is a predecessor of the new dispatched job,
						// then it should certainly finish before the new dispatched job starts
						const auto &predecessors = predecessors_of[j_index];
						for (const auto &pred : predecessors) {
							if (pred.first->get_job_index() == job_index) {
								job_lft = std::min(job_lft, lst);
								break;
							}
						}
						job_finish_times.emplace_back(job, Interval<Time>{ job_eft, job_lft });
					}
				}
			}

			//calculate the earliest time a job with precedence constraints will become ready to dispatch
			void update_earliest_certain_successor_job_dispatch(
				const std::vector<std::vector<Job_ref>>& ready_succ_jobs,
				const Predecessors& predecessors_of,
				const Successors& successors_of)
			{
				assert(earliest_certain_successor_job_dispatch.size() == ready_succ_jobs.size());
				std::fill(earliest_certain_successor_job_dispatch.begin(), earliest_certain_successor_job_dispatch.end(), Time_model::constants<Time>::infinity());
				// we go through all successor jobs that are ready and update the earliest ready time
				for (unsigned int cluster = 0; cluster < ready_succ_jobs.size(); cluster++)
				{
					for (const Job<Time>* rj : ready_succ_jobs[cluster]) {
						Time avail = clusters[cluster]->core_availability(rj->get_min_parallelism()).max();
						Time ready_time = std::max(avail, rj->latest_arrival());

						// if the job predecessor is a conditional fork, we need to take the latest ready time between 
						// itself and all its conditional siblings
						const auto& predecessors = predecessors_of[rj->get_job_index()];
						if (rj->is_conditional_sibling()) {
							const auto& pred = predecessors[0];
							Job_index from_job = pred.first->get_job_index();
							Interval<Time> ftimes(0, 0);
							get_finish_times(from_job, ftimes);
							// go through all the conditional siblings
							for (const auto& sibling : successors_of[from_job])
							{
								Time susp_max = sibling.second.max();
								ready_time = std::max(ready_time, ftimes.max() + susp_max);
								ready_time = std::max(ready_time, sibling.first->latest_arrival());
							}
						}
						else {
							// go through all the predecessors of `rj` and take the maximum of the finish time of its predecessors
							for (const auto& pred : predecessors)
							{
								auto from_job = pred.first->get_job_index();
								Interval<Time> ftimes(0, 0);
								bool has_ft = get_finish_times(from_job, ftimes);
								if (has_ft) {
									Time susp_max = pred.second.max();
									ready_time = std::max(ready_time, ftimes.max() + susp_max);
								}
								// the only reason one of the predecessors may not have a finish time is if 
								// `rj` is a conditional join node
								assert(has_ft || rj->get_type() == Job<Time>::Job_type::C_JOIN); 
							}
						}
						earliest_certain_successor_job_dispatch[cluster] =
							std::min(earliest_certain_successor_job_dispatch[cluster], ready_time);
					}
				}
			}

			// Check whether the job_finish_times overlap.
			bool check_finish_times_overlap(const Job_finish_times& other_ft, bool conservative = false, const bool other_in_this = false) const
			{
				bool all_jobs_intersect = true;
				// The Job_finish_times vectors are sorted.
				// Check intersect for matching jobs.
				auto other_it = other_ft.begin();
				auto state_it = job_finish_times.begin();
				while (other_it != other_ft.end() &&
					state_it != job_finish_times.end())
				{
					if (other_it->first == state_it->first)
					{
						if (conservative) {
							if (other_in_this == false && !other_it->second.contains(state_it->second))
							{
								all_jobs_intersect = false; // not all the finish time intervals of this are within those of other
								break;
							}
							else if (other_in_this == true && !state_it->second.contains(other_it->second))
							{
								all_jobs_intersect = false; // not all the finish time intervals of other are within those of this
								break;
							}
						}
						else {
							if (!other_it->second.intersects(state_it->second))
							{
								all_jobs_intersect = false;
								break;
							}
						}
						other_it++;
						state_it++;
					}
					else if (conservative)
						return false; // the list of finish time intervals do not match
					else if (other_it->first < state_it->first)
						other_it++;
					else
						state_it++;
				}
				return all_jobs_intersect;
			}

			void widen_finish_times(const Job_finish_times& from_pwj)
			{
				// The Job_finish_times vectors are sorted.
				// Assume check_overlap() is true.
				auto from_it = from_pwj.begin();
				auto state_it = job_finish_times.begin();
				while (from_it != from_pwj.end() &&
					state_it != job_finish_times.end())
				{
					if (from_it->first == state_it->first)
					{
						state_it->second.widen(from_it->second);
						from_it++;
						state_it++;
					}
					else if (from_it->first < state_it->first) {
						// insert the missing job finish time interval in the right place
						state_it = job_finish_times.insert(state_it, *from_it);
						from_it++;
					}
					else
						state_it++;
				}
				while (from_it != from_pwj.end())
				{
					job_finish_times.push_back(*from_it);
					from_it++;
				}
			}

			// Find the offset in the Job_finish_times vector where the index j should be located.
			int jft_find(const Job_index j) const
			{
				int start = 0;
				int end = job_finish_times.size();
				while (start < end) {
					int mid = (start + end) / 2;
					if (job_finish_times[mid].first->get_job_index() == j)
						return mid;
					else if (job_finish_times[mid].first->get_job_index() < j)
						start = mid + 1;  // mid is too small, mid+1 might fit.
					else
						end = mid;
				}
				return start;
			}

			// no accidental copies
			Schedule_state(const Schedule_state& origin) = delete;
		};
	}
}
#endif // GLOBAL_STATE_HPP