#ifndef GLOBAL_NODE_HPP
#define GLOBAL_NODE_HPP
#include <algorithm>
#include <cassert>
#include <iostream>
#include <ostream>

#include <set>

#include "config.h"
#include "cache.hpp"
#include "index_set.hpp"
#include "tasks.hpp"
#include "statistics.hpp"
#include "util.hpp"
#include "global/state_space_data.hpp"
#include "global/state.hpp"
#include "sched_policy.hpp"

#ifdef CONFIG_PARALLEL
#include <tbb/mutex.h>
#endif

namespace NP {

	namespace Global {

		template<class Time> class State_space_data;
		template<class Time> class Schedule_state;

		template<class Time> class Schedule_node
		{
		public:
			typedef std::vector<Index_set> Subtask_set;
		private:

			typedef typename std::vector<Interval<Time>> Core_availability;
			typedef const Subtask<Time>* Subtask_ref;
			typedef typename State_space_data<Time>::Task_set Task_set;

			// set of subtasks whose last released job has already been dispatched on a core
			Subtask_set scheduled_subtasks;

			// set of subtasks that have all their predecessors completed and were not dispatched yet
			std::vector< std::vector<Subtask_ref>> ready_subtasks;

			hash_value_t lookup_key;
			Interval<Time> first_core_availability;
			Time a_max;
			unsigned int num_cpus;
			unsigned int num_jobs_scheduled;

			// no accidental copies
			Schedule_node(const Schedule_node& origin) = delete;

			typedef Schedule_state<Time> State;

			struct eft_compare
			{
				bool operator() (State* x, State* y) const
				{
					return x->earliest_finish_time() < y->earliest_finish_time();
				}
			};

#ifdef CONFIG_PARALLEL
			tbb::mutex mtx;
#endif
			typedef typename std::multiset<State*, eft_compare> State_ref_queue;
			State_ref_queue states;

		public:

			// initial node
			Schedule_node(unsigned int num_cores, const State_space_data<Time>& state_space_data)
				: lookup_key{ 0 }
				, num_cpus(num_cores)
				, first_core_availability{ 0,0 }
				, a_max{ 0 }
				, num_jobs_scheduled(0)
			{
				// initialize the scheduled_subtasks and ready_subtasks vectors
				scheduled_subtasks.reserve(state_space_data.tasks.size());
				ready_subtasks.resize(state_space_data.tasks.size(), {});
				for (std::size_t i = 0; i < state_space_data.tasks.size(); ++i) {
					// initialize the scheduled_subtasks vector such that it records no subtask dispatched yet
					scheduled_subtasks.push_back(Index_set{ state_space_data.tasks[i].get_subtasks().size() });
					// if a subtask has no predecessors, it is ready
					for (const auto& st : state_space_data.tasks[i].get_subtasks()) {
						const auto& pred = state_space_data.tasks[i].get_predecessors_of(st.id());
						if (pred.start_before_start.empty() && pred.finish_before_start.empty())
							ready_subtasks[i].push_back(&st);
					}
				}
			}

			// transition: new node by scheduling a job 'j' in an existing node 'from'
			Schedule_node(
				const Schedule_node& from, const Subtask<Time>& subtsk,
				const State_space_data<Time>& state_space_data )
				: lookup_key{ from.next_key(subtsk) },
				num_cpus(from.num_cpus),
				num_jobs_scheduled(from.num_jobs_scheduled + 1),
				first_core_availability{ 0, Time_model::constants<Time>::infinity() },
				a_max{ Time_model::constants<Time>::infinity() },
				ready_subtasks(from.ready_subtasks.size())
			{
				scheduled_subtasks = from.scheduled_subtasks; 
				scheduled_subtasks[subtsk.task_id()].add(subtsk.id());

				update_ready_subtasks(from, subtsk, state_space_data.tasks);
			}

			void reset(unsigned int num_cores, const State_space_data<Time>& state_space_data)
			{
				lookup_key = 0;
				num_cpus = num_cores;
				first_core_availability = { 0,0 };
				a_max = 0;
				// clear the scheduled_subtasks vector and update ready_subtasks
				for (std::size_t i = 0; i < state_space_data.num_tasks(); ++i) {
					scheduled_subtasks[i].reset();
					ready_subtasks[i].clear();
					// if a subtask has no predecessors, it is ready
					for (const auto& st : state_space_data.tasks[i].get_subtasks()) {
						const auto& pred = state_space_data.tasks[i].get_predecessors_of(st.id());
						if (pred.start_before_start.empty() && pred.finish_before_start.empty())
							ready_subtasks[i].push_back(&st);
					}
				}
				num_jobs_scheduled = 0;
				states.clear();
			}

			// transition: new node by scheduling a job 'j' in an existing node 'from'
			void reset(
				const Schedule_node& from,
				const Subtask<Time>& subtsk,
				const State_space_data<Time>& state_space_data
			)
			{
				states.clear();
				scheduled_subtasks = from.scheduled_subtasks;
				scheduled_subtasks[subtsk.task_id()].add(subtsk.id());
				lookup_key = from.next_key(subtsk);
				num_cpus = from.num_cpus;
				num_jobs_scheduled = from.num_jobs_scheduled + 1;
				first_core_availability = { 0, Time_model::constants<Time>::infinity() };
				a_max = Time_model::constants<Time>::infinity();

				update_ready_subtasks(from, subtsk, state_space_data.tasks);
			}

			const unsigned int number_of_scheduled_jobs() const
			{
				return num_jobs_scheduled;
			}

			const std::vector<std::vector<Subtask_ref>>& get_ready_subtasks() const
			{
				return ready_subtasks;
			}

			bool is_dispatched(const Subtask<Time>& subtsk) const
			{
				return scheduled_subtasks[subtsk.task_id()].contains(subtsk.id());
			}

			bool is_dispatched(Task_index i, Subtask_index j) const
			{
				return scheduled_subtasks[i].contains(j);
			}

			const Subtask_set& get_scheduled_subtasks() const
			{
				return scheduled_subtasks;
			}

			hash_value_t get_key() const
			{
				return lookup_key;
			}

			bool matches(const Schedule_node& other) const
			{
				if (lookup_key != other.lookup_key)
					return false; 

				for (std::size_t i = 0; i < scheduled_subtasks.size(); ++i) {
					if (scheduled_subtasks[i] != other.scheduled_subtasks[i])
						return false;
				}
				return true;
			}

			hash_value_t next_key(const Subtask<Time>& st) const
			{
				return get_key() ^ st.get_key();
			}

			//  finish_range / first_core_availability contains information about the
			//     earliest and latest core availability for core 0.
			//     whenever a state is changed (through merge) or added,
			//     that interval should be adjusted.
			Interval<Time> finish_range() const
			{
				return first_core_availability;
			}

			Time latest_core_availability() const
			{
				return a_max;
			}


			void add_state(State* s)
			{
#ifdef CONFIG_PARALLEL
				tbb::mutex::scoped_lock lock(mtx);
#endif
				update_internal_variables(s);
				states.insert(s);
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

			const State* get_first_state() const
			{
				auto first = states.begin();
				return *first;
			}

			const State* get_last_state() const
			{
				auto last = --(states.end());
				return *last;
			}

			const State_ref_queue* get_states() const
			{
				return &states;
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
			int merge_states(const Schedule_state<Time>& s, const Sched_policy sched_policy, bool conservative, bool use_job_finish_times = false, int budget = 1)
			{
#ifdef CONFIG_PARALLEL
				tbb::mutex::scoped_lock lock(mtx);
#endif
				// if we do not use a conservative merge, try to merge with up to 'budget' states if possible.
				int merge_budget = conservative ? 1 : budget;

				State* last_state_merged;
				bool result = false;
				for (auto it = states.begin(); it != states.end();)
				{
					State* state = *it;
					if (result == false)
					{
						if (state->try_to_merge(s, sched_policy, conservative, use_job_finish_times))
						{
							// Update the node first_core_availability
							first_core_availability.widen(s.core_availability());
							a_max = std::max(a_max, s.core_availability(num_cpus).max());
							
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
						if (last_state_merged->try_to_merge(*state, sched_policy, conservative, use_job_finish_times))
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
					return result ? 1 : 0;
				else
					return (budget - merge_budget);
			}

		private:
			void update_internal_variables(const State* s)
			{
				Interval<Time> ft = s->core_availability();
				if (states.empty()) {
					first_core_availability = ft;
					a_max = s->core_availability(num_cpus).max();
				}
				else {
					first_core_availability.widen(ft);
					a_max = std::max(a_max, s->core_availability(num_cpus).max());
				}
			}

			void update_ready_subtasks(const Schedule_node& from, const Subtask<Time>& subtsk,
				const Task_set& tasks) 
			{
				// add all subtasks that were ready in the previous state
				std::copy(from.ready_subtasks.begin(), from.ready_subtasks.end(), ready_subtasks.begin());

				Task_index task_id = subtsk.task_id();

				// remove the subtask that was just dispatched
				for (auto it = ready_subtasks[task_id].begin(); it != ready_subtasks[task_id].end(); it++) {
					if ((*it)->id() == subtsk.id()) {
						ready_subtasks[task_id].erase(it);
						break;
					}
				}

				// add all successors of subtsk that are ready now
				const Task<Time>& task = tasks[task_id];
				const auto& successors = task.get_successors_of(subtsk.id());
				for (const auto& succ : successors.start_after_start) {
					bool ready = true;
					const auto& predecessors = task.get_predecessors_of(succ.subtask);
					for (const auto& pred : predecessors.start_before_start) {
						if (!scheduled_subtasks[task_id].contains(pred.subtask)) {
							ready = false;
							break;
						}
					}
					if (!ready)
						continue;

					for (const auto& pred : predecessors.finish_before_start) {
						if (!scheduled_subtasks[task_id].contains(pred.subtask)) {
							ready = false;
							break;
						}
					}
					if (!ready)
						continue;
					
					ready_subtasks[task_id].push_back(&(task.get_subtask(succ.subtask)));
				}

				for (const auto& succ : successors.start_after_finish) {
					bool ready = true;
					const auto& predecessors = task.get_predecessors_of(succ.subtask);
					for (const auto& pred : predecessors.start_before_start) {
						if (!scheduled_subtasks[task_id].contains(pred.subtask)) {
							ready = false;
							break;
						}
					}
					if (!ready)
						continue;

					for (const auto& pred : predecessors.finish_before_start) {
						if (!scheduled_subtasks[task_id].contains(pred.subtask)) {
							ready = false;
							break;
						}
					}
					if (!ready)
						continue;

					ready_subtasks[task_id].push_back(&(task.get_subtask(succ.subtask)));
				}

				// if all subtasks have been dispatched, we release a new instance of the task
				if (ready_subtasks[task_id].empty()) {
					scheduled_subtasks[task_id].reset();
					// if a subtask has no predecessors, it is ready
					for (const auto& st : task.get_subtasks()) {
						const auto& pred = task.get_predecessors_of(st.id());
						if (pred.start_before_start.empty() && pred.finish_before_start.empty())
							ready_subtasks[task_id].push_back(&st);
					}
				}
			}
		};
	}
}

#endif