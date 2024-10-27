#ifndef GLOBAL_NODE_H
#define GLOBAL_NODE_H

#include <algorithm>
#include <cassert>
#include <iostream>
#include <ostream>
#include <vector>
#include <deque>

#include "config.h"
#include "cache.hpp"
#include "jobs.hpp"
#include "util.hpp"
#include "interval.hpp"
#include "global/state.hpp"

namespace NP {

	namespace Global {

		template<class Time> class Schedule_node
		{
		private:

			typedef typename std::vector<Interval<Time>> CoreAvailability;
			std::vector<Time> earliest_pending_release;
			std::vector<Time> next_certain_successor_jobs_disptach;
			std::vector<Time> next_certain_source_job_release;
			std::vector<Time> next_certain_sequential_source_job_release;
			std::vector<Time> next_certain_gang_source_job_disptach;

			Dispatched_job_set scheduled_jobs;
			hash_value_t lookup_key;
			std::vector<Interval<Time>> finish_time;
			std::vector<Time> latest_core_availability;
			std::vector<unsigned int> num_cpus; //number of cpus in each cluster
			unsigned int num_jobs_scheduled;

			// no accidental copies
			Schedule_node(const Schedule_node& origin) = delete;

			typedef Schedule_state<Time> State;
			typedef std::deque<State*> State_ref_queue;
			State_ref_queue states;

		public:

			// initial node
			Schedule_node(const std::vector<unsigned int>& num_cores)
				: lookup_key{ 0 }
				, num_cpus(num_cores)
				, finish_time(num_cores.size(), { 0,0 })
				, latest_core_availability(num_cores.size(), 0)
				, num_jobs_scheduled(0)
				, earliest_pending_release(num_cores.size(), 0)
				, next_certain_successor_jobs_disptach(num_cores.size(), Time_model::constants<Time>::infinity())
				, next_certain_source_job_release(num_cores.size(), Time_model::constants<Time>::infinity())
				, next_certain_sequential_source_job_release(num_cores.size(), Time_model::constants<Time>::infinity())
				, next_certain_gang_source_job_disptach(num_cores.size(), Time_model::constants<Time>::infinity())
			{
			}

			Schedule_node(
				const std::vector<unsigned int>& num_cores, const State_space_data<Time>& state_space_data)
				: lookup_key{ 0 }
				, num_cpus(num_cores)
				, finish_time(num_cores.size(), { 0,0 })
				, latest_core_availability(num_cores.size(), 0)
				, num_jobs_scheduled(0)
				, next_certain_successor_jobs_disptach(num_cores.size(), Time_model::constants<Time>::infinity())
			{
				int num_clusters = num_cores.size();
				next_certain_source_job_release.reserve(num_clusters);
				next_certain_sequential_source_job_release.reserve(num_clusters);
				next_certain_gang_source_job_disptach.reserve(num_clusters);
				earliest_pending_release.reserve(num_clusters);
				for (int i = 0; i < num_clusters; i++)
				{
					earliest_pending_release.push_back(state_space_data.get_earliest_possible_source_job_release(i));

					Time seq_rel = state_space_data.get_earliest_certain_seq_source_job_release(i);
					next_certain_sequential_source_job_release.push_back(seq_rel);

					Time gang_rel = state_space_data.get_earliest_certain_gang_source_job_release(i);
					next_certain_gang_source_job_disptach.push_back(gang_rel);

					next_certain_source_job_release.push_back(std::min(seq_rel, gang_rel));
				}
			}

			// transition: new node by scheduling a job 'j' in an existing node 'from'
			Schedule_node(
				const Schedule_node& from,
				const Job<Time>& j,
				std::size_t idx,
				const Time next_earliest_release,
				const Time next_certain_source_job_release, // the next time a job without predecessor is certainly released
				const Time next_certain_sequential_source_job_release // the next time a job without predecessor that can execute on a single core is certainly released
			)
				: scheduled_jobs{ from.scheduled_jobs, idx }
				, lookup_key{ from.next_key(j).second }
				, num_cpus(from.num_cpus)
				, num_jobs_scheduled(from.num_jobs_scheduled + 1)
				, finish_time(from.num_cpus.size(), { 0, Time_model::constants<Time>::infinity() })
				, latest_core_availability(from.num_cpus.size(), Time_model::constants<Time>::infinity()) // set to infinity because depends on states and there is no state in the node yet
				, earliest_pending_release{ from.earliest_pending_release }
				, next_certain_source_job_release{ from.next_certain_source_job_release }
				, next_certain_successor_jobs_disptach(from.num_cpus.size(), Time_model::constants<Time>::infinity()) // set to infinity because depends on states and there is no state in the node yet
				, next_certain_sequential_source_job_release{ from.next_certain_sequential_source_job_release }
				, next_certain_gang_source_job_disptach(from.num_cpus.size(), Time_model::constants<Time>::infinity()) // set to infinity because depends on states and there is no state in the node yet
			{
				earliest_pending_release[j.get_affinity()] = next_earliest_release;
				this->next_certain_source_job_release[j.get_affinity()] = next_certain_source_job_release;
				this->next_certain_sequential_source_job_release[j.get_affinity()] = next_certain_sequential_source_job_release;
			}

			// transition: new node by scheduling a set of jobs in an existing node 'from'
			Schedule_node(
				const Schedule_node& from,
				const std::vector<const Job<Time>*> j_set,
				const std::vector<Job_index>& idx_set,
				const std::vector<Time>& next_earliest_release,
				const std::vector<Time>& next_certain_source_job_release, // the next time a job without predecessor is certainly released
				const std::vector<Time>& next_certain_sequential_source_job_release // the next time a job without predecessor that can execute on a single core is certainly released
			)
				: scheduled_jobs{ from.scheduled_jobs, idx_set }
				, lookup_key{ from.next_key(j_set).second }
				, num_cpus(from.num_cpus)
				, num_jobs_scheduled(from.num_jobs_scheduled + j_set.size())
				, finish_time(from.num_cpus.size(), { 0, Time_model::constants<Time>::infinity() })
				, latest_core_availability(from.num_cpus.size(), Time_model::constants<Time>::infinity()) // set to infinity because depends on states and there is no state in the node yet
				, earliest_pending_release{ next_earliest_release }
				, next_certain_source_job_release{ next_certain_source_job_release }
				, next_certain_successor_jobs_disptach(from.num_cpus.size(), Time_model::constants<Time>::infinity()) // set to infinity because depends on states and there is no state in the node yet
				, next_certain_sequential_source_job_release{ next_certain_sequential_source_job_release }
				, next_certain_gang_source_job_disptach(from.num_cpus.size(), Time_model::constants<Time>::infinity()) // set to infinity because depends on states and there is no state in the node yet
			{
			}

			~Schedule_node()
			{
				for (State* s : states)
					delete s;
			}

			const unsigned int number_of_scheduled_jobs() const
			{
				return num_jobs_scheduled;
			}

			Time earliest_job_release(const unsigned int cluster) const
			{
				return earliest_pending_release[cluster];
			}

			Time get_next_certain_source_job_release(const unsigned int cluster) const
			{
				return next_certain_source_job_release[cluster];
			}

			const std::vector<Time>& get_next_certain_source_job_releases() const
			{
				return next_certain_source_job_release;
			}

			Time get_next_certain_sequential_source_job_release(const unsigned int cluster) const
			{
				return next_certain_sequential_source_job_release[cluster];
			}

			Time next_certain_job_ready_time(const unsigned int cluster) const
			{
				return std::min(next_certain_successor_jobs_disptach[cluster],
					std::min(next_certain_sequential_source_job_release[cluster],
						next_certain_gang_source_job_disptach[cluster]));
			}

			std::pair<unsigned int, hash_value_t> get_key() const
			{
				return std::make_pair(num_jobs_scheduled, lookup_key);
			}

			const Dispatched_job_set& get_scheduled_jobs() const
			{
				return scheduled_jobs;
			}

			const bool job_incomplete(Job_index j) const
			{
				return !scheduled_jobs.contains(j);
			}

			const bool job_ready(const Job_precedence_set& predecessors) const
			{
				for (auto j : predecessors)
					if (!scheduled_jobs.contains(j))
						return false;
				return true;
			}

			const bool job_dependent_on_other_cluster(const Job<Time>& j, const Job_precedence_set& predecessors, const typename Job<Time>::Job_set& jobs, const Time latest_start) const
			{
				unsigned int aff = j.get_affinity();
				for (auto p : predecessors)
				{
					unsigned int aff_pred = jobs[p].get_affinity();
					// check if the predecessor is on a different cluster, 
					// was not dispatched yet, 
					// and may possibly finish before something must start on the same cluster as j
					if (aff_pred != aff
						&& latest_start >= std::max(jobs[p].earliest_arrival(), finish_time[aff_pred].min()) + jobs[p].least_exec_time(jobs[p].get_max_parallelism())
						&& !scheduled_jobs.contains(p))
					{
						return true;
					}
				}
				return false;
			}

			bool matches(const Schedule_node& other) const
			{
				return lookup_key == other.lookup_key &&
					scheduled_jobs == other.scheduled_jobs;
			}

			std::pair<unsigned int, hash_value_t> next_key(const Job<Time>& j) const
			{
				return std::make_pair(num_jobs_scheduled + 1, lookup_key ^ j.get_key());
			}

			std::pair<unsigned int, hash_value_t> next_key(const std::vector<const Job<Time>*>& j_set) const
			{
				int n_jobs = 0;
				auto key = lookup_key;
				for (auto j : j_set) {
					if (j != NULL) {
						n_jobs++;
						key = key ^ j->get_key();
					}
				}
				return std::make_pair(num_jobs_scheduled + n_jobs, key);
			}

			//  finish_range / finish_time contains information about the
			//     earliest and latest core availability for core 0.
			//     whenever a state is changed (through merge) or added,
			//     that interval should be adjusted.
			const Interval<Time>& finish_range(unsigned int cluster_id) const
			{
				return finish_time[cluster_id];
			}

			Time get_latest_core_availability(unsigned int cluster_id) const
			{
				assert(cluster_id < latest_core_availability.size());
				return latest_core_availability[cluster_id];
			}

			void add_state(State* s)
			{
				// Update finish_time
				if (states.empty()) {
					for (int i = 0; i < latest_core_availability.size(); i++) {
						finish_time[i] = s->cluster(i).core_availability();
						latest_core_availability[i] = s->cluster(i).core_availability(num_cpus[i]).max();
						next_certain_successor_jobs_disptach[i] = s->cluster(i).next_certain_successor_jobs_disptach();
						next_certain_gang_source_job_disptach[i] = s->cluster(i).next_certain_gang_source_job_disptach();
					}
				}
				else {
					for (int i = 0; i < latest_core_availability.size(); i++) {
						finish_time[i].widen(s->cluster(i).core_availability());
						latest_core_availability[i] = std::max(latest_core_availability[i], s->cluster(i).core_availability(num_cpus[i]).max());
						next_certain_successor_jobs_disptach[i] = std::max(next_certain_successor_jobs_disptach[i], s->cluster(i).next_certain_successor_jobs_disptach());
						next_certain_gang_source_job_disptach[i] = std::max(next_certain_gang_source_job_disptach[i], s->cluster(i).next_certain_gang_source_job_disptach());
					}
				}
				states.push_back(s);
			}

			friend std::ostream& operator<< (std::ostream& stream,
				const Schedule_node<Time>& n)
			{
				stream << "Node(" << n.states.size() << ")";
				return stream;
			}

			//return the number of states in the node
			int states_size() const
			{
				return states.size();
			}

			const State_ref_queue* get_states() const
			{
				return &states;
			}

			// try to merge state 's' with up to 'budget' states already recorded in this node. 
			// The option 'conservative' allow a merge of twos states to happen only if the availability 
			// intervals of one state are constained in the availability intervals of the other state. If
			// the conservative option is used, the budget parameter is ignored.
			// The option 'use_job_finish_times' controls whether or not the job finish time intervals of jobs 
			// with pending successors must overlap to allow two states to merge. Setting it to true should 
			// increase accurracy of the analysis but increases runtime significantly.
			// The 'budget' defines how many states can be merged at once. If 'budget = -1', then there is no limit. 
			// Returns the number of existing states the new state was merged with.
			int merge_states(const Schedule_state<Time>& s, bool conservative, bool use_job_finish_times = false, int budget = 1)
			{
				// if we do not use a conservative merge, try to merge with up to 'budget' states if possible.
				int merge_budget = conservative ? 1 : budget;

				State* last_state_merged;
				bool result = false;
				for (auto it = states.begin(); it != states.end();)
				{
					State* state = *it;
					if (result == false)
					{
						if (state->try_to_merge(s, conservative, use_job_finish_times))
						{
							for (int i = 0; i < num_cpus.size(); i++) {
								// Update the node finish_time
								finish_time[i].widen(s.cluster(i).core_availability());
								latest_core_availability[i] = std::max(latest_core_availability[i], s.cluster(i).core_availability(num_cpus[i]).max());
								//update the certain next job ready time
								next_certain_successor_jobs_disptach[i] = std::max(next_certain_successor_jobs_disptach[i], s.cluster(i).next_certain_successor_jobs_disptach());
								next_certain_gang_source_job_disptach[i] = std::max(next_certain_gang_source_job_disptach[i], s.cluster(i).next_certain_gang_source_job_disptach());
							}
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
							delete state;

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
					return result;
				else
					return (budget - merge_budget);
			}
		};
	}
}

#endif