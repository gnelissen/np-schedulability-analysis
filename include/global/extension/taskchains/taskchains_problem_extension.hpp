#ifndef TASKCHAINS_PROBLEM_EXTENSION_HPP
#define TASKCHAINS_PROBLEM_EXTENSION_HPP

#include "global/extension/problem_extension.hpp"
#include "global/extension/taskchains/taskchains.hpp"

namespace NP {
	namespace Global {
		namespace Taskchains_analysis {

			// Problem extension for task chains analysis
			template<class Time>
			class Taskchains_problem_extension : public Problem_extension
			{
				typedef std::vector<Task_chain<Time>> Taskchains;
				// task chains to be analyzed
				Taskchains task_chains;
			public:
				/**
				 * @brief Constructor
				 * @param task_chains Vector of Task_chain objects representing the task chains to be analyzed
				 */
				Taskchains_problem_extension(const Taskchains& task_chains)
					: task_chains(task_chains)
				{
				}
				/**
				 * @brief Get the task chains to be analyzed
				 * @return Vector of Task_chain objects
				 */
				const Taskchains& get_task_chains() const {
					return task_chains;
				}
			};

		} // namespace Taskchains_analysis
	} // namespace Global
} // namespace NP

#endif // !TASKCHAINS_PROBLEM_EXTENSION_HPP
