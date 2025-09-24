#ifndef TASKCHAINS_SP_DATA_EXTENSION_HPP
#define TASKCHAINS_SP_DATA_EXTENSION_HPP

#include "global/extension/state_space_data_extension.hpp"
#include "global/extension/taskchains/taskchains.hpp"
#include "jobs.hpp"
#include "problem.hpp"

namespace NP {
	namespace Global {
		namespace Taskchains_analysis {

			template<class Time>
			class Taskchains_sp_data_extension : public State_space_data_extension
			{
				typedef typename Scheduling_problem<Time>::Workload Workload;
				typedef typename std::vector<Task_chain<Time>> Task_chains;

				// lookup table that maps task IDs to task chains and the position of the task in the task chain
				struct Task_in_chain_info {
					unsigned long chain_id;
					unsigned long position_in_chain;
					bool is_sink;
				};
				std::vector<std::vector<Task_in_chain_info>> _task_to_chain;

				// max data age and max reaction time for each task chain
				Task_chains_result<Time> results;

				const Task_chains& task_chains;

			public:

				Taskchains_sp_data_extension(const Workload& jobs, const Task_chains& task_chains)
					: task_chains(task_chains)
				{
					// init the task chain result
					results.data_ages.resize(task_chains.size(), 0);
					results.reaction_times.resize(task_chains.size(), 0);

					// init the task to chain lookup table
					unsigned long max_task_id = 0;
					for (const Job<Time>& j : jobs) {
						if (j.get_task_id() > max_task_id)
							max_task_id = j.get_task_id();
					}
					_task_to_chain.resize(max_task_id + 1);
					for (unsigned long i = 0; i < task_chains.size(); i++) {
						const Task_chain<Time>& tc = task_chains[i];
						for (unsigned long j = 0; j < tc.get_tasks().size(); j++) {
							unsigned long task_id = tc.get_tasks()[j];
							bool is_sink = (j == tc.get_tasks().size() - 1);
							// if the task is not already in the lookup table, add it
							if (task_id >= _task_to_chain.size()) {
								_task_to_chain.resize(task_id + 1);
							}
							_task_to_chain[task_id].push_back({ i, j, is_sink });
						}
					}
				}

				const Task_chains& get_task_chains() const
				{
					return task_chains;
				}

				const std::vector<Task_in_chain_info>& get_task_chains_of(unsigned int task) const
				{
					return _task_to_chain[task];
				}

				const Task_chains_result<Time>& get_results() const
				{
					return results;
				}

				Time get_max_data_age(unsigned long taskchain) const {
					if (taskchain < results.data_ages.size()) {
						return results.data_ages[taskchain];
					}
					return 0;
				}

				Time get_max_reaction_time(unsigned long taskchain) const {
					if (taskchain < results.reaction_times.size()) {
						return results.reaction_times[taskchain];
					}
					return 0;
				}

				void submit_data_age(unsigned long taskchain, Time DA) {
					results.data_ages[taskchain] = std::max(DA, results.data_ages[taskchain]);
				}

				void submit_reaction_time(unsigned long taskchain, Time RT) {
					results.reaction_times[taskchain] = std::max(RT, results.reaction_times[taskchain]);
				}
			};
		}
	}
}

#endif // !TASKCHAINS_SP_DATA_EXTENSION_HPP
