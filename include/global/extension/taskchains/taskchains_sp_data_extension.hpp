#ifndef TASKCHAINS_SP_DATA_EXTENSION_HPP
#define TASKCHAINS_SP_DATA_EXTENSION_HPP

#include "global/extension/state_space_data_extension.hpp"
#include "global/extension/taskchains/taskchains.hpp"
#include "jobs.hpp"
#include "problem.hpp"

namespace NP {
	namespace Global {
		namespace Taskchains_analysis {
			// State space data extension for task chains analysis
			template<class Time>
			class Taskchains_sp_data_extension : public State_space_data_extension<Time>
			{
				typedef typename Scheduling_problem<Time>::Workload Workload;
				typedef typename std::vector<Task_chain<Time>> Task_chains;

				// info about a task in a task chain (which chain it belongs to, position in the chain, is it a sink task)
				struct Task_in_chain_info {
					unsigned long chain_id;
					unsigned long position_in_chain;
					bool is_sink;
				};
				// lookup table that maps task IDs to the list of task chains the task belongs to
				std::vector<std::vector<Task_in_chain_info>> _task_to_chain;
				// max data age and max reaction time for each task chain
				Task_chains_result<Time> results;
				// task chains to be analyzed
				const Task_chains& task_chains;

			public:
				/**
				 * @brief Constructor
				 * @param jobs Workload containing all jobs
				 * @param task_chains Vector of task chains that must be analyzed
				 */
				Taskchains_sp_data_extension(const Workload& jobs, const Task_chains& task_chains)
					: task_chains(task_chains)
				{
					// init the task chain result
					results.data_ages.resize(task_chains.size(), -1);
					results.reaction_times.resize(task_chains.size(), -1);

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
				/**
				 * @brief Get the task chains to be analyzed
				 */
				const Task_chains& get_task_chains() const
				{
					return task_chains;
				}
				/**
				 * @brief Get the list of task chains a given task belongs to
				 * @param task Task ID
				 * @return Vector of Task_in_chain_info objects containing information about the chains the task belongs to
				 */
				const std::vector<Task_in_chain_info>& get_task_chains_of(unsigned int task) const
				{
					return _task_to_chain[task];
				}
				/**
				 * @brief Get the analysis results for the task chains
				 */
				const Task_chains_result<Time>& get_results() const
				{
					return results;
				}
				/**
				 * @brief Get the maximum data age for a given task chain
				 * @param taskchain Task chain ID
				 */
				Time get_max_data_age(unsigned long taskchain) const {
					if (taskchain < results.data_ages.size()) {
						return results.data_ages[taskchain];
					}
					return -1;
				}
				/**
				 * @brief Get the maximum reaction time for a given task chain
				 * @param taskchain Task chain ID
				 */
				Time get_max_reaction_time(unsigned long taskchain) const {
					if (taskchain < results.reaction_times.size()) {
						return results.reaction_times[taskchain];
					}
					return -1;
				}
				/**
				 * @brief Submit a new data age measurement for a given task chain
				 * @param taskchain Task chain ID
				 * @param DA Data age to submit
				 */
				void submit_data_age(unsigned long taskchain, Time DA) {
					results.data_ages[taskchain] = std::max(DA, results.data_ages[taskchain]);
				}
				/**
				 * @brief Submit a new reaction time measurement for a given task chain
				 * @param taskchain Task chain ID
				 * @param RT Reaction time to submit
				 */
				void submit_reaction_time(unsigned long taskchain, Time RT) {
					results.reaction_times[taskchain] = std::max(RT, results.reaction_times[taskchain]);
				}
				/**
				 * @brief Export the task chains analysis results in CSV format
				 */
				std::ostringstream export_results(const State_space_data<Time>& sp_data) const override
				{
					auto ss = std::ostringstream();
					ss << "Task Chain ID, Max Data Age, Max Reaction Time" << std::endl;
					for (unsigned long i = 0; i < task_chains.size(); ++i) {
						ss << task_chains[i].get_id() << ", "
						   << results.data_ages[i] << ", "
						   << results.reaction_times[i] << std::endl;
					}
					return ss;
				}
			};
		}
	}
}

#endif // !TASKCHAINS_SP_DATA_EXTENSION_HPP
