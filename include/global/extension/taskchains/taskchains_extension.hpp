#ifndef EXT_TASKCHAINS_HPP
#define EXT_TASKCHAINS_HPP

#include "global/extension/extension.hpp"
#include "global/extension/taskchains/taskchains_sp_data_extension.hpp"
#include "global/extension/taskchains/taskchains_state_extension.hpp"
#include "global/extension/taskchains/taskchains_problem_extension.hpp"

namespace NP {
	namespace Global {
		namespace Taskchains_analysis {

			// Extension for task chains analysis. Adds extensions to State_space_data and Schedule_state.
			template<class Time>
			class Taskchains_analysis_extension : public Extension<Time, Taskchains_state_extension<Time>, Taskchains_sp_data_extension<Time>>
			{
			};
		}
	}

}


#endif // !EXT_TASKCHAINS_HPP
