#ifndef GLOBAL_STATE_HPP
#define GLOBAL_STATE_HPP
#include <algorithm>
#include <cassert>
#include <iostream>
#include <ostream>

#include <vector>
#include <deque>

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

		typedef Index_set Dispatched_job_set;
		typedef std::vector<Job_index> Job_precedence_set;

		template<class Time> class State_space_data;

		template<class Time> class Schedule_node;

		template<class Time> class Schedule_state
		{
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

		public:
			// initial state -- nothing yet has finished, nothing is running
			Schedule_state(const std::vector<unsigned int>& num_cpus, const State_space_data<Time>& spdata)
			{
				assert(num_cpus.size() > 0);
				clusters.reserve(num_cpus.size());
				for (int i = 0; i < num_cpus.size(); i++)
				{
					clusters.push_back(std::make_shared<Cluster_state<Time>>(i, num_cpus[i], spdata.get_earliest_certain_gang_source_job_release(i)));
				}
			}

			// transition: new state by scheduling a job 'j' on 'ncores' cores in an existing state 'from'
			Schedule_state(
				const Schedule_state& from,
				const Job<Time>& j,
				Interval<Time> start_times,
				Interval<Time> finish_times,
				const Dispatched_job_set& scheduled_jobs,
				const std::vector<const Job<Time>*>& ready_succ_jobs,
				const State_space_data<Time>& state_space_data,
				Time next_source_job_rel,
				unsigned int ncores = 1)
			{
				const Successors& successors_of = state_space_data.successors_suspensions;
				const Predecessors& predecessors_of = state_space_data.predecessors_suspensions;
				const Job_precedence_set & predecessors = state_space_data.predecessors_of(j);
				clusters.reserve(from.clusters.size());
				for (int i = 0; i < from.clusters.size(); i++) {
					if (i == j.get_affinity())
						clusters.push_back(std::make_shared<Cluster_state<Time>>(from.cluster(i), j.get_job_index(), start_times, finish_times, scheduled_jobs, state_space_data, next_source_job_rel, ncores));
					else
						clusters.push_back(from.clusters[i]);
				}
				// save the job finish time of every job with a successor that is not executed yet in the current state
				update_job_finish_times(from, &j, start_times, finish_times, successors_of, predecessors_of, scheduled_jobs);
				updated_earliest_certain_successor_job_disptach(ready_succ_jobs, predecessors_of);
			}

			// transition: new state by scheduling a set of jobs in an existing state
			Schedule_state(
				const Schedule_state& from,
				const std::vector<const Job<Time>*>& j_set,
				const std::vector<Interval<Time>>& start_times,
				const std::vector<Interval<Time>>& finish_times,
				const std::vector<unsigned int>& ncores,
				const Dispatched_job_set& scheduled_jobs,
				const std::vector<const Job<Time>*>& ready_succ_jobs,
				const State_space_data<Time>& state_space_data,
				const std::vector<Time>& next_source_job_rel)
			{
				assert(j_set.size() == from.clusters.size());
				assert(start_times.size() == from.clusters.size());
				assert(finish_times.size() == from.clusters.size());
				assert(next_source_job_rel.size() == from.clusters.size());
				assert(ncores.size() == from.clusters.size());

				clusters.reserve(from.clusters.size());
				for (int i = 0; i < from.clusters.size(); i++)
				{
					const Job<Time>* j = j_set[i];
					if (j == NULL)
						clusters.push_back(from.cluster(i));
					else
					{
						Job_index j_idx = j->get_job_index();
						clusters.push_back(std::make_shared<Cluster_state<Time>>(from.cluster(i), j_idx, start_times[i], finish_times[i], scheduled_jobs, state_space_data, next_source_job_rel[i], ncores[i]));
					}
				}
				assert(clusters.size() == from.clusters.size());

				// save the job finish time of every job with a successor that is not executed yet in the current state
				const Successors& successors_of = state_space_data.successors_suspensions;
				const Predecessors& predecessors_of = state_space_data.predecessors_suspensions;
				update_job_finish_times(from, j_set, start_times, finish_times, successors_of, predecessors_of, scheduled_jobs);
				updated_earliest_certain_successor_job_disptach(ready_succ_jobs, predecessors_of);

				DM("*** new state: constructed " << *this << std::endl);
			}

			const Cluster_state<Time>& cluster(unsigned int cluster_id) const
			{
				assert(cluster_id < clusters.size());
				return *clusters[cluster_id];
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
			bool can_merge_with(const Schedule_state<Time>& other, bool conservative = false, bool use_job_finish_times = false) const
			{
				bool other_in_this;
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
					}
				}

				// merge job_finish_times
				widen_finish_times(other.job_finish_times);

				DM("+++ merged " << other << " into " << *this << std::endl);
				return true;
			}

			void print_vertex_label(std::ostream& out,
				const typename Job<Time>::Job_set& jobs) const
			{
				for (int i = 0; i < clusters.size(); ++i)
				{
					out << "[";
					clusters[i]->print_vertex_label(out, jobs);
					out << "]";
				}
			}

		private:

			// Calculates the latest time a job 'j' is certainly ready if all its predecessors are completed.
			// Returns infinity otherwise.
			Time calculate_latest_ready_time(Job_ref j, const Predecessors& predecessors_of, const Schedule_state& prev_state, const std::vector<Job_ref>& j_set_newly_dispatched, const std::vector<Interval<Time>> finish_times, const Dispatched_job_set& scheduled_jobs)
			{
				Time avail = clusters[j->get_affinity()]->core_availability(j->get_min_parallelism()).max();
				Time ready_time = std::max(avail, j->latest_arrival());
				for (const auto& pred : predecessors_of[j->get_job_index()])
				{
					Interval<Time> ftimes(0, 0);
					auto from_job = pred.first->get_job_index();
					auto affinity = pred.first->get_affinity();
					if (j_set_newly_dispatched[affinity]!=NULL && from_job == j_set_newly_dispatched[affinity]->get_job_index())
					{
						Time susp_max = pred.second.max();
						ready_time = std::max(ready_time, finish_times[affinity].max() + susp_max);
					}
					else if (scheduled_jobs.contains(from_job) && prev_state.get_finish_times(from_job, ftimes))
					{
						Time susp_max = pred.second.max();
						ready_time = std::max(ready_time, ftimes.max() + susp_max);
					}
					else
					{
						return Time_model::constants<Time>::infinity();
					}
				}
				return ready_time;
			}

			// update the list of finish times of jobs with successors w.r.t. the previous system state
			void update_job_finish_times(const Schedule_state& from,
				const std::vector<Job_ref>& j_set,
				const std::vector<Interval<Time>>& start_times,
				const std::vector<Interval<Time>>& finish_times,
				const Successors& successors_of,
				const Predecessors& predecessors_of,
				const Dispatched_job_set& scheduled_jobs)
			{
				job_finish_times.reserve(job_finish_times.size() + clusters.size());

				// sort the jobs that are newly dispatched in increasing job index order
				std::vector<std::pair<Job_ref, unsigned int>> newly_dispatched_j_sorted;
				newly_dispatched_j_sorted.reserve(j_set.size());
				for (unsigned int i = 0; i < clusters.size(); i++) {
					if (j_set[i] != NULL)
						newly_dispatched_j_sorted.emplace_back(j_set[i], i);
				}
				std::sort(newly_dispatched_j_sorted.begin(), newly_dispatched_j_sorted.end(),
					[](const std::pair<Job_ref, unsigned int>& a, const std::pair<Job_ref, unsigned int>& b) {
						return a.first->get_job_index() < b.first->get_job_index();
					});

				// go through all the job finish times we had saved in the previous state, remove those 
				// for which all pending successors completed and add the finish times of the jobs that 
				// were just dispatched
				auto ft = from.job_finish_times.begin();

				for (const auto& added_job : newly_dispatched_j_sorted)
				{
					Job_ref j = added_job.first;
					Job_index j_idx = j->get_job_index();
					unsigned int affinity = added_job.second;
					bool added_j = false;

					for (; ft != from.job_finish_times.end(); ++ft)
					{
						auto job_ref = ft->first;
						auto job = job_ref->get_job_index();
						auto job_eft = ft->second.min();
						auto job_lft = ft->second.max();
						auto job_aff = job_ref->get_affinity();

						// if there is a single core, then we know that 
						// jobs that were disptached in the past cannot have 
						// finished later than when our new job starts executing
						if (j_set[job_aff] != NULL && clusters[job_aff]->num_cpus() == 1)
						{
							if (job_lft > start_times[job_aff].max())
								job_lft = start_times[job_aff].max();
						}

						if (!added_j && job > j_idx)
						{
							if (!successors_of[j_idx].empty())
								job_finish_times.emplace_back(j, finish_times[affinity]);
							break;
						}

						bool successor_pending = false;
						for (const auto& succ : successors_of[job]) {
							auto to_job = succ.first->get_job_index();
							if (!scheduled_jobs.contains(to_job))
							{
								successor_pending = true;
								break;
							}
						}
						if (successor_pending)
							job_finish_times.emplace_back(job_ref, Interval<Time>(job_eft, job_lft));
					}

					if (!added_j)
					{
						if (!successors_of[j_idx].empty())
							job_finish_times.emplace_back(j, finish_times[affinity]);
					}
				}

				for (; ft != from.job_finish_times.end(); ++ft)
				{
					auto job_ref = ft->first;
					auto job = job_ref->get_job_index();
					auto job_eft = ft->second.min();
					auto job_lft = ft->second.max();
					auto job_aff = job_ref->get_affinity();

					// if there is a single core, then we know that 
					// jobs that were disptached in the past cannot have 
					// finished later than when our new job starts executing
					if (j_set[job_aff] != NULL && clusters[job_aff]->num_cpus() == 1)
					{
						if (job_lft > start_times[job_aff].max())
							job_lft = start_times[job_aff].max();
					}

					bool successor_pending = false;
					for (const auto& succ : successors_of[job]) {
						auto to_job = succ.first->get_job_index();
						if (!scheduled_jobs.contains(to_job))
						{
							successor_pending = true;
							break;
						}
					}
					if (successor_pending)
						job_finish_times.emplace_back(job_ref, Interval<Time>(job_eft, job_lft));
				}
			}

			// update the list of finish times of jobs with successors w.r.t. the previous system state
			void update_job_finish_times(const Schedule_state& from,
				const Job_ref j,
				const Interval<Time>& start_time,
				const Interval<Time>& finish_time,
				const Successors& successors_of,
				const Predecessors& predecessors_of,
				const Dispatched_job_set& scheduled_jobs)
			{
				job_finish_times.reserve(job_finish_times.size() + 1);

				// go through all the job finish times we had saved in the previous state, remove those 
				// for which all pending successors completed and add the finish times of the jobs that 
				// were just dispatched
				auto ft = from.job_finish_times.begin();

				Job_index j_idx = j->get_job_index();
				unsigned int affinity = j->get_affinity();
				bool added_j = false;

				for (; ft != from.job_finish_times.end(); ++ft)
				{
					auto job_ref = ft->first;
					auto job = job_ref->get_job_index();
					auto job_eft = ft->second.min();
					auto job_lft = ft->second.max();
					auto job_aff = job_ref->get_affinity();

					// if there is a single core, then we know that 
					// jobs that were disptached in the past cannot have 
					// finished later than when our new job starts executing
					if (affinity == job_aff && clusters[job_aff]->num_cpus() == 1)
					{
						if (job_lft > start_time.max())
							job_lft = start_time.max();
					}

					if (!added_j && job > j_idx)
					{
						if (!successors_of[j_idx].empty())
							job_finish_times.emplace_back(j, finish_time);
						break;
					}

					bool successor_pending = false;
					for (const auto& succ : successors_of[job]) {
						auto to_job = succ.first->get_job_index();
						if (!scheduled_jobs.contains(to_job))
						{
							successor_pending = true;
							break;
						}
					}
					if (successor_pending)
						job_finish_times.emplace_back(job_ref, Interval<Time>(job_eft, job_lft));
				}

				if (!added_j)
				{
					if (!successors_of[j_idx].empty())
						job_finish_times.emplace_back(j, finish_time);
				}

				for (; ft != from.job_finish_times.end(); ++ft)
				{
					auto job_ref = ft->first;
					auto job = job_ref->get_job_index();
					auto job_eft = ft->second.min();
					auto job_lft = ft->second.max();
					auto job_aff = job_ref->get_affinity();

					// if there is a single core, then we know that 
					// jobs that were disptached in the past cannot have 
					// finished later than when our new job starts executing
					if (affinity != NULL && clusters[job_aff]->num_cpus() == 1)
					{
						if (job_lft > start_time.max())
							job_lft = start_time.max();
					}

					bool successor_pending = false;
					for (const auto& succ : successors_of[job]) {
						auto to_job = succ.first->get_job_index();
						if (!scheduled_jobs.contains(to_job))
						{
							successor_pending = true;
							break;
						}
					}
					if (successor_pending)
						job_finish_times.emplace_back(job_ref, Interval<Time>(job_eft, job_lft));
				}
			}

			//calculate the earliest time a job with precedence constraints will become ready to dispatch
			void updated_earliest_certain_successor_job_disptach(
				const std::vector<const Job<Time>*>& ready_succ_jobs,
				const Predecessors& predecessors_of)
			{
				std::vector<Time> earliest_certain_successor_job_disptach(clusters.size(), Time_model::constants<Time>::infinity());
				// we go through all successor jobs that are ready and update the earliest ready time
				for (const Job<Time>* rj : ready_succ_jobs) {
					auto affinity = rj->get_affinity();
					Time avail = clusters[affinity]->core_availability(rj->get_min_parallelism()).max();
					Time ready_time = std::max(avail, rj->latest_arrival());
					for (const auto& pred : predecessors_of[rj->get_job_index()])
					{
						Interval<Time> ftimes(0, 0);
						auto from_job = pred.first->get_job_index();
						get_finish_times(from_job, ftimes);
						Time susp_max = pred.second.max();
						ready_time = std::max(ready_time, ftimes.max() + susp_max);
					}
					
					earliest_certain_successor_job_disptach[affinity] =
						std::min(earliest_certain_successor_job_disptach[affinity], ready_time);
				}
				
				// update earliest successor job ready times on each cluster
				for (int i = 0; i < clusters.size(); i++) {
					if (clusters[i].unique())
						clusters[i]->set_in_place_earliest_certain_successor_job_disptach(earliest_certain_successor_job_disptach[i]);
					else if (clusters[i]->next_certain_successor_jobs_disptach() != earliest_certain_successor_job_disptach[i])
						clusters[i] = clusters[i]->set_earliest_certain_successor_job_disptach(earliest_certain_successor_job_disptach[i]);
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
					else if (from_it->first < state_it->first)
						from_it++;
					else
						state_it++;
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

#endif