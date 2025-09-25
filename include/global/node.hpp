#ifndef GLOBAL_NODE_HPP
#define GLOBAL_NODE_HPP

#include <algorithm>
#include <cassert>
#include <iostream>
#include <ostream>
#include <deque>
#include <memory>
#include <vector>

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
#include "global/state.hpp"
#include "global/space.hpp"

namespace NP {
	namespace Global {
        
        template<class Time> class Schedule_node
		{
		private:
			typedef typename State_space_data<Time>::Workload Workload;
			typedef Schedule_state<Time> State;
			typedef std::shared_ptr<State> State_ref;
			typedef const Job<Time>* Job_ref;
			typedef typename std::vector<Interval<Time>> Core_availability;
			typedef std::vector<std::pair<Job_ref, Interval<Time>>> Susp_list;
			typedef std::vector<Susp_list> Successors;
			typedef std::vector<Susp_list> Predecessors;

			std::vector<Time> earliest_pending_release;
			std::vector<Time> next_certain_successor_jobs_disptach;
			std::vector<Time> next_certain_source_job_release;
			std::vector<Time> next_certain_sequential_source_job_release;
			std::vector<Time> next_certain_gang_source_job_disptach;

			Job_set scheduled_jobs;
			// set of jobs that have all their predecessors completed and were not dispatched yet
			std::vector<std::vector<Job_ref>> ready_successor_jobs;
			// set of jobs that have all their predecessors assigned to the same cluster as them completed but still pending predecessors on other clusters
			std::vector<std::vector<Job_ref>> locally_ready_successor_jobs;
			// set of dispatched jobs that have at least one successor pending
			std::vector<Job_ref> jobs_with_pending_succ;

			hash_value_t lookup_key;
			std::vector<Interval<Time>> finish_time;
			std::vector<Time> a_max; // latest time a core will become available on each cluster
			int num_clusters; // number of clusters
			std::vector<unsigned int> num_cpus; //number of cpus in each cluster
			unsigned int num_jobs_scheduled;

#ifdef CONFIG_PARALLEL
			// Thread-safe state container and synchronization
			mutable tbb::spin_rw_mutex states_mutex;
#endif

			// no accidental copies
			Schedule_node(const Schedule_node& origin) = delete;

			typedef typename std::vector<State_ref> State_ref_queue;
			State_ref_queue states;

		public:

			// initial node (for convenience for unit tests)
			Schedule_node(const unsigned int num_cores)
				: lookup_key{ 0 }
				, scheduled_jobs()
				, num_clusters(1)
				, num_cpus(1, num_cores)
				, finish_time(1, { 0,0 })
				, a_max(1, 0)
				, num_jobs_scheduled(0)
				, earliest_pending_release(1, 0)
				, next_certain_successor_jobs_disptach(1, Time_model::constants<Time>::infinity())
				, next_certain_source_job_release(1, Time_model::constants<Time>::infinity())
				, next_certain_sequential_source_job_release(1, Time_model::constants<Time>::infinity())
				, next_certain_gang_source_job_disptach(1, Time_model::constants<Time>::infinity())
				, ready_successor_jobs(1)
				, locally_ready_successor_jobs(1)
            {
			}

			// initial node (for convenience for unit tests)
			Schedule_node(const std::vector<unsigned int>& num_cores)
				: lookup_key{ 0 }
				, scheduled_jobs()
				, num_clusters(num_cores.size())
				, num_cpus(num_cores)
				, finish_time(num_cores.size(), { 0,0 })
				, a_max(num_cores.size(), 0)
				, num_jobs_scheduled(0)
				, earliest_pending_release(num_cores.size(), 0)
				, next_certain_successor_jobs_disptach(num_cores.size(), Time_model::constants<Time>::infinity())
				, next_certain_source_job_release(num_cores.size(), Time_model::constants<Time>::infinity())
				, next_certain_sequential_source_job_release(num_cores.size(), Time_model::constants<Time>::infinity())
				, next_certain_gang_source_job_disptach(num_cores.size(), Time_model::constants<Time>::infinity())
				, ready_successor_jobs(num_cores.size())
				, locally_ready_successor_jobs(num_cores.size())
            {
			}

			// initial node
			Schedule_node(const std::vector<unsigned int>& num_cores, const State_space_data<Time>& state_space_data)
				: lookup_key{ 0 }
				, scheduled_jobs()
				, num_clusters(num_cores.size())
				, num_cpus(num_cores)
				, finish_time(num_cores.size(), { 0,0 })
				, a_max(num_cores.size(), 0)
				, num_jobs_scheduled(0)
				, next_certain_successor_jobs_disptach(num_cores.size(), Time_model::constants<Time>::infinity())
				, ready_successor_jobs(num_cores.size())
				, locally_ready_successor_jobs(state_space_data.get_locally_ready_jobs())
			{
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

			Schedule_node(const std::vector<std::vector<Interval<Time>>>& proc_initial_state, const State_space_data<Time>& state_space_data)
				: lookup_key{ 0 }
				, scheduled_jobs()
				, num_clusters(proc_initial_state.size())
				, finish_time(proc_initial_state.size(), { 0,0 })
				, a_max(proc_initial_state.size(), Time_model::constants<Time>::infinity())
				, num_jobs_scheduled(0)
				, next_certain_successor_jobs_disptach(proc_initial_state.size(),Time_model::constants<Time>::infinity())
				, num_cpus(proc_initial_state.size())
				, ready_successor_jobs(proc_initial_state.size())
				, locally_ready_successor_jobs(state_space_data.get_locally_ready_jobs())
			{
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

					num_cpus[i] = proc_initial_state[i].size();
                }

				for (int i = 0; i < num_clusters; i++) {
					Time a_min = Time_model::constants<Time>::infinity();
                    for (const auto& a : proc_initial_state[i]) {
                        a_max[i] = std::min(a_max[i], a.max());
                        a_min = std::min(a_min, a.min());
                    }
                    finish_time[i] = {a_min, a_max[i]};
                }
			}


			// transition: new node by scheduling a job 'j' in an existing node 'from'
			Schedule_node(
				const Schedule_node& from,
				const Job<Time>& j,
				std::size_t idx,
				const State_space_data<Time>& state_space_data,
				const Time next_earliest_release,
				const Time next_certain_source_job_release, // the next time a job without predecessor is certainly released
				const Time next_certain_sequential_source_job_release // the next time a job without predecessor that can execute on a single core is certainly released
			)
				: scheduled_jobs{ from.scheduled_jobs, idx }
				, lookup_key{ from.next_key(j) }
				, num_clusters(from.num_clusters)
				, num_cpus(from.num_cpus)
				, num_jobs_scheduled(from.num_jobs_scheduled + 1)
				, finish_time(from.num_clusters, { 0, Time_model::constants<Time>::infinity() })
				, a_max(from.num_clusters, Time_model::constants<Time>::infinity())
				, earliest_pending_release{ from.earliest_pending_release }
				, ready_successor_jobs(from.num_clusters)
				, locally_ready_successor_jobs(from.num_clusters)
				, next_certain_source_job_release{ from.next_certain_source_job_release }
				, next_certain_successor_jobs_disptach(from.num_clusters, Time_model::constants<Time>::infinity())
				, next_certain_sequential_source_job_release{ from.next_certain_sequential_source_job_release }
				, next_certain_gang_source_job_disptach(from.num_clusters, Time_model::constants<Time>::infinity())
			{
				earliest_pending_release[j.get_affinity()] = next_earliest_release;
				this->next_certain_source_job_release[j.get_affinity()] = next_certain_source_job_release;
				this->next_certain_sequential_source_job_release[j.get_affinity()] = next_certain_sequential_source_job_release;
				
                const auto& succ = state_space_data.successors_suspensions;
				const auto& pred = state_space_data.predecessors_suspensions;
                update_ready_successors(from, j, succ, pred, this->scheduled_jobs);
				update_jobs_with_pending_succ(from, j, succ, pred, this->scheduled_jobs);
			}

			// initial node
			void reset(const std::vector<unsigned int>& num_cores, const State_space_data<Time>& state_space_data)
			{
#ifdef CONFIG_PARALLEL
				tbb::spin_rw_mutex::scoped_lock lock(states_mutex, true); // write lock
#endif
				clear();
				scheduled_jobs.clear();				
				num_jobs_scheduled = 0;
				lookup_key = 0;
				num_clusters = num_cores.size();
				num_cpus = num_cores;
				a_max = std::vector<Time>(num_clusters, 0);
				finish_time = std::vector<Interval<Time>>(num_clusters, { 0, 0 });
				next_certain_successor_jobs_disptach = std::vector<Time>(num_clusters, Time_model::constants<Time>::infinity());
				next_certain_source_job_release.resize(num_clusters);
				next_certain_sequential_source_job_release.resize(num_clusters);
				next_certain_gang_source_job_disptach.resize(num_clusters);
				earliest_pending_release.resize(num_clusters);
				ready_successor_jobs.resize(num_clusters);
				locally_ready_successor_jobs = state_space_data.get_locally_ready_jobs();
				for (int i = 0; i < num_clusters; i++)
				{
					earliest_pending_release[i] = state_space_data.get_earliest_possible_source_job_release(i);

					Time seq_rel = state_space_data.get_earliest_certain_seq_source_job_release(i);
					next_certain_sequential_source_job_release[i] = seq_rel;

					Time gang_rel = state_space_data.get_earliest_certain_gang_source_job_release(i);
					next_certain_gang_source_job_disptach[i] = gang_rel;

					next_certain_source_job_release[i] = std::min(seq_rel, gang_rel);

					ready_successor_jobs[i].clear();
					//locally_ready_successor_jobs[i].clear();
                }
			}

			void reset(const std::vector<std::vector<Interval<Time>>>& proc_initial_state, const State_space_data<Time>& state_space_data)
			{
#ifdef CONFIG_PARALLEL
				tbb::spin_rw_mutex::scoped_lock lock(states_mutex, true); // write lock
#endif
				clear();
				scheduled_jobs.clear();
				num_jobs_scheduled = 0;
				lookup_key = 0;
				num_clusters = proc_initial_state.size();
				next_certain_successor_jobs_disptach = std::vector<Time>(num_clusters, Time_model::constants<Time>::infinity());
				
				a_max = std::vector<Time>(num_clusters, Time_model::constants<Time>::infinity());
				finish_time.resize(num_clusters);				
				for (int i = 0; i < num_clusters; i++) {
					Time a_min = Time_model::constants<Time>::infinity();
					for (const auto& a : proc_initial_state[i]) {
						a_max[i] = std::min(a_max[i], a.max());
						a_min = std::min(a_min, a.min());
					}
					finish_time[i]={a_min, a_max[i]};
				}
				
				next_certain_source_job_release.resize(num_clusters);
				next_certain_sequential_source_job_release.resize(num_clusters);
				next_certain_gang_source_job_disptach.resize(num_clusters);
				earliest_pending_release.resize(num_clusters);
				ready_successor_jobs.resize(num_clusters);
				locally_ready_successor_jobs = state_space_data.get_locally_ready_jobs();
				num_cpus.resize(num_clusters);
				for (int i = 0; i < num_clusters; i++)
				{
					earliest_pending_release[i] = state_space_data.get_earliest_possible_source_job_release(i);

					Time seq_rel = state_space_data.get_earliest_certain_seq_source_job_release(i);
					next_certain_sequential_source_job_release[i] = seq_rel;

					Time gang_rel = state_space_data.get_earliest_certain_gang_source_job_release(i);
					next_certain_gang_source_job_disptach[i] = gang_rel;

					next_certain_source_job_release[i] = std::min(seq_rel, gang_rel);

					ready_successor_jobs[i].clear();
					//locally_ready_successor_jobs[i].clear();

					num_cpus[i] = proc_initial_state[i].size();
                }
			}

			// transition: new node by scheduling a job 'j' in an existing node 'from'
			void reset(
				const Schedule_node& from,
				const Job<Time>& j,
				std::size_t idx,
				const State_space_data<Time>& state_space_data,
				const Time next_earliest_release,
				const Time next_certain_source_job_release, // the next time a job without predecessor is certainly released
				const Time next_certain_sequential_source_job_release // the next time a job without predecessor that can execute on a single core is certainly released
			)
			{
#ifdef CONFIG_PARALLEL
				tbb::spin_rw_mutex::scoped_lock lock(states_mutex, true); // write lock
#endif
				assert(num_clusters == from.num_clusters);
				assert(num_cpus == from.num_cpus);
				assert(finish_time.size() == from.finish_time.size());
				assert(a_max.size() == from.a_max.size());
				assert(next_certain_successor_jobs_disptach.size() == from.next_certain_successor_jobs_disptach.size());
				assert(next_certain_gang_source_job_disptach.size() == from.next_certain_gang_source_job_disptach.size());
				clear();
				scheduled_jobs.set(from.scheduled_jobs, idx);
				lookup_key = from.next_key(j);
				//num_clusters = from.num_clusters;
				//num_cpus = from.num_cpus;
				num_jobs_scheduled = from.num_jobs_scheduled + 1;
				//finish_time = std::vector<Interval<Time>>(from.num_clusters, { 0, Time_model::constants<Time>::infinity() });
				std::fill(finish_time.begin(), finish_time.end(), Interval<Time>{0, Time_model::constants<Time>::infinity()});
				//a_max = std::vector<Time>(num_clusters, Time_model::constants<Time>::infinity());
				std::fill(a_max.begin(), a_max.end(), Time_model::constants<Time>::infinity());
				earliest_pending_release = from.next_certain_source_job_release;
				earliest_pending_release[j.get_affinity()] = next_earliest_release;
				this->next_certain_source_job_release = from.next_certain_source_job_release;
				this->next_certain_source_job_release[j.get_affinity()] = next_certain_source_job_release;
				//next_certain_successor_jobs_disptach = std::vector<Time>(num_clusters, Time_model::constants<Time>::infinity());
				std::fill(next_certain_successor_jobs_disptach.begin(), next_certain_successor_jobs_disptach.end(), Time_model::constants<Time>::infinity());
				this->next_certain_sequential_source_job_release = from.next_certain_sequential_source_job_release;
				this->next_certain_sequential_source_job_release[j.get_affinity()] = next_certain_sequential_source_job_release;
				//next_certain_gang_source_job_disptach = std::vector<Time>(num_clusters, Time_model::constants<Time>::infinity());
				std::fill(next_certain_gang_source_job_disptach.begin(), next_certain_gang_source_job_disptach.end(), Time_model::constants<Time>::infinity());

				update_ready_successors(from, j, state_space_data.successors_suspensions, state_space_data.predecessors_suspensions, this->scheduled_jobs);
				update_jobs_with_pending_succ(from, j, state_space_data.successors_suspensions, state_space_data.predecessors_suspensions, this->scheduled_jobs);
			}

			void clear() {
				for (auto& s : states) {
					release_state(s);
				}
				states.clear();
				jobs_with_pending_succ.clear();
			}

			const unsigned int get_num_clusters() const
			{
				return num_clusters;
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

			const std::vector<std::vector<Job_ref>>& get_ready_successor_jobs() const
			{
				return ready_successor_jobs;
			}

			const std::vector<Job_ref>& get_ready_successor_jobs(const unsigned int cluster) const
			{
				assert(cluster < ready_successor_jobs.size());
				return ready_successor_jobs[cluster];
			}

			const std::vector<std::vector<Job_ref>>& get_locally_ready_successor_jobs() const
			{
				return locally_ready_successor_jobs;
			}

			const std::vector<Job_ref>& get_locally_ready_successor_jobs(const unsigned int cluster) const
			{
				assert(cluster < locally_ready_successor_jobs.size());
				return locally_ready_successor_jobs[cluster];
			}

			const std::vector<Job_ref>& get_jobs_with_pending_successors() const
			{
				return jobs_with_pending_succ;
			}

			const Job_set& get_scheduled_jobs() const
			{
				return scheduled_jobs;
			}

			const bool job_not_dispatched(Job_index j) const
			{
				return !scheduled_jobs.contains(j);
			}

			const bool job_dispatched(Job_index j) const
			{
				return scheduled_jobs.contains(j);
			}

			const bool job_ready(const Job_precedence_set& predecessors) const
			{
				for (auto j : predecessors)
					if (!scheduled_jobs.contains(j))
						return false;
				return true;
			}

            const bool job_dependent_on_other_cluster(const Job<Time>& j, const Susp_list& predecessors, const typename Job<Time>::Job_set& jobs, const Time latest_start) const
			{
				unsigned int aff = j.get_affinity();
				for (auto p : predecessors)
				{
					Time min_susp = p.second.min();
					const auto pred = p.first;
					auto pred_id = pred->get_job_index();
					unsigned int aff_pred = pred->get_affinity();
					// check if the predecessor is on a different cluster, 
					// was not dispatched yet, 
					// and may possibly fulfill its precedence constraint before something must start on the same cluster as j
					if (aff_pred != aff
						&& latest_start >= std::max(pred->earliest_arrival(), finish_time[aff_pred].min()) + pred->get_bcet() + min_susp
						&& !scheduled_jobs.contains(pred_id))
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

            hash_value_t get_key() const
			{
				return lookup_key;
			}

			hash_value_t next_key(const Job<Time>& j) const
			{
				return get_key() ^ j.get_key();
			}

			//  finish_range / finish_time contains information about the
			//     earliest and latest core availability for core 0.
			//     whenever a state is changed (through merge) or added,
			//     that interval should be adjusted.
			const Interval<Time>& finish_range(const unsigned int cluster) const
			{
				return finish_time[cluster];
			}

			Time earliest_core_availability(const unsigned int cluster) const
			{
				assert(cluster < finish_time.size());
				return finish_time[cluster].min();
			}

			Time latest_core_availability(const unsigned int cluster) const
			{
				assert(cluster < a_max.size());
                return a_max[cluster];
			}

			void add_state(const State_ref& s)
			{
#ifdef CONFIG_PARALLEL
				tbb::spin_rw_mutex::scoped_lock lock(states_mutex, true); // write lock
#endif
				update_internal_variables(s);
				states.push_back(s);
			}

			// export the node information to the stream
			void export_node (std::ostream& stream, const Workload& jobs)
			{
				stream << "=====Node=====\n"
					<< "Ready successors: [[<task_id>,<job_id>], ...]\n"
					<< "[";
                // Ready successor jobs: [<task_id>,<job_id>]
				int i = 0;
				for (int idx = 0; idx < num_clusters; ++idx) {
					stream << "Cluster " << idx << ": ";
					for (const auto* job : ready_successor_jobs[idx]) {
						stream << "[" << job->get_task_id() << "," << job->get_job_id() << "]";
						++i;
						if (i < ready_successor_jobs.size()) 
							stream << ",";
					}
				}
				stream << "]\n"
					<< "Locally ready successors: [[<task_id>,<job_id>], ...]\n"
					<< "[";
				// Ready successor jobs: [<task_id>,<job_id>]
				i = 0;
				for (int idx = 0; idx < num_clusters; ++idx) {
					stream << "Cluster " << idx << ": ";
					for (const auto* job : locally_ready_successor_jobs[idx]) {
						stream << "[" << job->get_task_id() << "," << job->get_job_id() << "]";
						++i;
						if (i < locally_ready_successor_jobs.size())
							stream << ",";
					}
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

			//return the number of states in the node
			int states_size() const
			{
#ifdef CONFIG_PARALLEL
				tbb::spin_rw_mutex::scoped_lock lock(states_mutex, false); // read lock
#endif
				return states.size();
			}

			const State_ref get_first_state() const
			{
#ifdef CONFIG_PARALLEL
				tbb::spin_rw_mutex::scoped_lock lock(states_mutex, false); // read lock
#endif
				auto first = states.begin();
				return *first;
			}

			const State_ref get_last_state() const
			{
#ifdef CONFIG_PARALLEL
				tbb::spin_rw_mutex::scoped_lock lock(states_mutex, false); // read lock
#endif
				auto last = --(states.end());
				return *last;
			}

			const State_ref_queue& get_states() const
			{
#ifdef CONFIG_PARALLEL
				// Note: This method returns a pointer to the internal container,
				// which is inherently not thread-safe. The caller must ensure
				// proper synchronization when accessing the returned pointer.
				// Consider using states_size() and iterator methods instead.
#endif
				return states;
			}

			// try to merge state 's' with up to 'budget' states already recorded in this node. 
			// The option 'conservative' allow a merge of two states to happen only if the availability 
			// intervals of one state are constained in the availability intervals of the other state. If
			// the conservative option is used, the budget parameter is ignored.
			// The option 'use_job_finish_times' controls whether or not the job finish time intervals of jobs 
			// with pending successors must overlap to allow two states to merge. Setting it to true should 
			// increase accurracy of the analysis but increases runtime significantly.
			// The 'budget' defines how many states can be merged at once. If 'budget = -1', then there is no limit. 
			// Returns the number of existing states the new state was merged with.
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
                            for (int i = 0; i < num_clusters; i++) {
                                // Update the node finish_time
                                finish_time[i].widen(s.cluster(i).core_availability());
                                a_max[i] = std::max(a_max[i], s.cluster(i).core_availability(num_cpus[i]).max());
                                //update the certain next job ready time
                                next_certain_successor_jobs_disptach[i] = std::max(next_certain_successor_jobs_disptach[i], s.next_certain_successor_jobs_disptach(i));
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
			void update_internal_variables(const State_ref& s)
			{
				if (states.empty()) {
                    for(int i=0; i<num_clusters; i++) {
                        finish_time[i] = s->cluster(i).core_availability();
                        a_max[i] = s->cluster(i).core_availability(num_cpus[i]).max();
                        next_certain_successor_jobs_disptach[i] = s->next_certain_successor_jobs_disptach(i);
                        next_certain_gang_source_job_disptach[i] = s->cluster(i).next_certain_gang_source_job_disptach();
                    }
				}
				else {
                    for(int i=0; i<num_clusters; i++) {
                        finish_time[i].widen(s->cluster(i).core_availability());
                        a_max[i] = std::max(a_max[i], s->cluster(i).core_availability(num_cpus[i]).max());
                        next_certain_successor_jobs_disptach[i] = std::max(next_certain_successor_jobs_disptach[i], s->next_certain_successor_jobs_disptach(i));
                        next_certain_gang_source_job_disptach[i] = std::max(next_certain_gang_source_job_disptach[i], s->cluster(i).next_certain_gang_source_job_disptach());
                    }
				}
			}

			// update the list of jobs that have all their predecessors completed and were not dispatched yet
			void update_ready_successors(const Schedule_node& from,
				const Job<Time>& j, const Successors& successors_of,
				const Predecessors& predecessors_of,
				const Job_set& scheduled_jobs)
			{
				auto j_index = j.get_job_index();
				auto affinity = j.get_affinity();
				//ready_successor_jobs.resize(num_clusters);
				//locally_ready_successor_jobs.resize(num_clusters);
				locally_ready_successor_jobs = from.locally_ready_successor_jobs;
				for (int i=0; i < num_clusters; i++)
				{
					if (i == affinity) {
						ready_successor_jobs[i].clear();
						ready_successor_jobs[i].reserve(from.ready_successor_jobs[i].size() + successors_of[j_index].size());
					}
					else {
						ready_successor_jobs[i] = from.ready_successor_jobs[i];
					}
				}				
				// add all jobs that were ready and were not the last job dispatched
				for (Job_ref rj : from.ready_successor_jobs[affinity])
				{
					if (rj->get_job_index() != j_index)
						ready_successor_jobs[affinity].push_back(rj);
				}
				// add all successors of j that are ready
				for (const auto& succ : successors_of[j_index])
				{
					bool ready = true;
					// succ can only **become** locally ready if the last job disptached is on the same cluster as succ
					auto succ_aff = succ.first->get_affinity();
					bool locally_ready = (succ_aff == affinity);
					for (const auto& pred : predecessors_of[succ.first->get_job_index()])
					{
						auto from_job = pred.first->get_job_index();
						if (from_job != j_index && !scheduled_jobs.contains(from_job))
						{
							ready = false;
							if (!locally_ready)
								break;
							else if (pred.first->get_affinity() == affinity) {
								locally_ready = false;
								break;
							}
						}
					}
					if (ready) {
						ready_successor_jobs[succ_aff].push_back(succ.first);
						// if the job was locally ready before becoming globally ready, we remove it from the local ready queue
						// Note that a job may hav been locally ready only if it is mapped on different cluster than the last job dispatched.
						if (succ_aff != affinity) {
							auto it = std::find(locally_ready_successor_jobs[succ_aff].begin(), locally_ready_successor_jobs[succ_aff].end(), succ.first);
							if (it != locally_ready_successor_jobs[succ_aff].end())
								locally_ready_successor_jobs[succ_aff].erase(it);
						}
					}
					else if (locally_ready)
						locally_ready_successor_jobs[affinity].push_back(succ.first);
				}
			}

			// update the list of jobs with non-dispatched successors 
			void update_jobs_with_pending_succ(const Schedule_node& from,
				const Job<Time>& j, const Successors& successors_of,
				const Predecessors& predecessors_of,
				const Job_set& scheduled_jobs)
			{
				jobs_with_pending_succ.reserve(from.jobs_with_pending_succ.size() + 1);
				auto j_index = j.get_job_index();
				bool added_j = successors_of[j_index].empty(); // we only need to add j if it has successors
				for (Job_ref job : from.jobs_with_pending_succ)
				{
					auto job_index = job->get_job_index();
					if (!added_j && job_index > j_index)
					{
						jobs_with_pending_succ.push_back(&j);
						added_j = true;
					}

					bool successor_pending = false;
					for (const auto& succ : successors_of[job_index]) {
						auto to_job = succ.first->get_job_index();
						if (!scheduled_jobs.contains(to_job))
						{
							successor_pending = true;
							break;
						}
					}
					if (successor_pending)
						jobs_with_pending_succ.push_back(job);
				}

				if (!added_j)
					jobs_with_pending_succ.push_back(&j);
			}
		};

	}
}

#endif // GLOBAL_NODE_HPP