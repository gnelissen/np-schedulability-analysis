#ifndef NP_PROBLEM_HPP
#define NP_PROBLEM_HPP

#include "tasks.hpp"
#include "aborts.hpp"

namespace NP {

	// Description of a non-preemptive scheduling problem
	template<class Time>
	struct Scheduling_problem {

		typedef typename Task<Time>::Task_set Task_set;
		typedef typename std::vector<Abort_action<Time>> Abort_actions;

		// ** Description of the workload:
		// (1) a set of recurrent tasks
	    Task_set tasks;
		// (3) abort actions for (some of) the jobs
		Abort_actions aborts;

		// ** Platform model:
		// on how many (identical) processors are the jobs being
		// dispatched (globally, in priority order)
		unsigned int num_processors;

		// Classic default setup: no abort actions
		Scheduling_problem(const Task_set& tasks, unsigned int num_processors = 1)
		: num_processors(num_processors)
		, tasks(tasks)
		{
			assert(num_processors > 0);
		}

		// Constructor with abort actions and precedence constraints
	    Scheduling_problem(const Task_set& tasks, const Abort_actions& aborts,
		                   unsigned int num_processors)
		: num_processors(num_processors)
		, tasks(tasks)
		, aborts(aborts)
		{
			assert(num_processors > 0);
			//validate_abort_refs<Time>(aborts, tasks);
		}
	};

	// Common options to pass to the analysis engines
	struct Analysis_options {
		// After how many seconds of CPU time should we give up?
		// Zero means unlimited.
		double timeout;

		// After how many scheduling decisions (i.e., depth of the
		// schedule graph) should we terminate the analysis?
		// Zero means unlimited.
		unsigned int max_depth;

		// length of the observation window analyzed by the tool
		unsigned int l_obs_window; 

		// Should we terminate the analysis upon encountering the first
		// deadline miss?
		bool early_exit;

		// Should we use state-merging techniques or naively explore the
		// whole state space in a brute-force manner (only useful as a
		// baseline).
		bool be_naive;

		// If we use state merging, defines options to use
		bool merge_conservative;
		bool merge_use_job_finish_times;
		int merge_depth;

		// Should we write where we are in the analysis?
		bool verbose;

		Analysis_options()
		: timeout(0)
		, max_depth(0)
		, l_obs_window(0)
		, early_exit(true)
		, be_naive(false)
		, merge_conservative(false)
		, merge_use_job_finish_times(false)
		, merge_depth(1)
		, verbose(false)
		{
		}
	};
}

#endif
