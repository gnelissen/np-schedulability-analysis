#ifndef TASKCHAINS_PROBLEM_EXTENSION_HPP
#define TASKCHAINS_PROBLEM_EXTENSION_HPP

#include "global/extension/problem_extension.hpp"
#include "global/extension/taskchains/taskchains.hpp"

namespace NP {
	namespace Global {
		namespace Taskchains_analysis {

			// Problem extension for task chains analysis
			template<class Time>
			struct Taskchains_problem_extension : public Problem_extension<Taskchains_problem_extension<Time>>
			{
				typedef std::vector<Task_chain<Time>> Taskchains;
				Taskchains taskchains;

				Taskchains_problem_extension(const Taskchains& taskchains)
					: taskchains(taskchains)
				{
				}
			};

		} // namespace Taskchains_analysis
	} // namespace Global
} // namespace NP

#endif // !TASKCHAINS_PROBLEM_EXTENSION_HPP
