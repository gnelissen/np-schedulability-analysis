#ifndef NP_PROBLEM_HPP
#define NP_PROBLEM_HPP

#include "jobs.hpp"
#include "precedence.hpp"
#include "aborts.hpp"
#ifdef CONFIG_PRUNING
#include "pruning_cond.hpp"
#endif
#ifdef CONFIG_ANALYSIS_EXTENSIONS
#include "global/extension/problem_extension.hpp"
#endif

namespace NP {

	// Description of a non-preemptive scheduling problem
	template<class Time>
	struct Scheduling_problem {

		typedef typename Job<Time>::Job_set Workload;
		typedef typename std::vector<Abort_action<Time>> Abort_actions;
		typedef typename std::vector<Precedence_constraint<Time>> Precedence_constraints;

		// ** Description of the workload:
		// (1) a set of jobs
		Workload jobs;
		// (2) a set of precedence constraints among the jobs
		Precedence_constraints prec;
		// (3) abort actions for (some of) the jobs
		Abort_actions aborts;

		// ** Platform model:
		// initial state (availability intervals) of the identical processors 
		// on which the jobs are being dispatched (globally, in priority order)
		std::vector<Interval<Time>> processors_initial_state;

#ifdef CONFIG_ANALYSIS_EXTENSIONS
		// ** Potential extensions to the scheduling problem (e.g., task chains)
		Problem_extensions problem_extensions;
#endif // CONFIG_ANALYSIS_EXTENSIONS

		// Classic default setup: no abort actions
		Scheduling_problem(const Workload& jobs, const Precedence_constraints& prec,
		                   unsigned int num_processors = 1)
		: jobs(jobs)
		, prec(prec)
		, processors_initial_state(num_processors, Interval<Time>(0, 0))
		{
			assert(num_processors > 0);
			validate_prec_cstrnts<Time>(this->prec, jobs);
		}

		Scheduling_problem(const Workload& jobs, const Precedence_constraints& prec,
			const std::vector<Interval<Time>>& proc_init_state)
		: jobs(jobs)
		, prec(prec)
		, processors_initial_state(proc_init_state)
		{
			assert(processors_initial_state.size() > 0);
			validate_prec_cstrnts<Time>(this->prec, jobs);
		}

		// Constructor with abort actions and precedence constraints
		Scheduling_problem(const Workload& jobs, const Precedence_constraints& prec,
		                   const Abort_actions& aborts,
		                   unsigned int num_processors)
		: jobs(jobs)
		, prec(prec)
		, aborts(aborts)
		, processors_initial_state(num_processors, Interval<Time>(0, 0))
		{
			assert(num_processors > 0);
			validate_prec_cstrnts<Time>(this->prec, jobs);
			validate_abort_refs<Time>(aborts, jobs);
		}

		Scheduling_problem(const Workload& jobs, const Precedence_constraints& prec,
			const Abort_actions& aborts,
			const std::vector<Interval<Time>>& proc_init_state)
			: jobs(jobs)
			, prec(prec)
			, aborts(aborts)
			, processors_initial_state(proc_init_state)
		{
			assert(processors_initial_state.size() > 0);
			validate_prec_cstrnts<Time>(this->prec, jobs);
			validate_abort_refs<Time>(aborts, jobs);
		}

		// Convenience constructor: no DAG, no abort actions
		Scheduling_problem(const Workload& jobs,
		                   unsigned int num_processors = 1)
		: jobs(jobs)
		, processors_initial_state(num_processors, Interval<Time>(0, 0))
		{
			assert(num_processors > 0);
		}

		Scheduling_problem(const Workload& jobs,
			const std::vector<Interval<Time>>& proc_init_state)
			: jobs(jobs)
			, processors_initial_state(proc_init_state)
		{
			assert(processors_initial_state.size() > 0);
		}
	};

	// Common options to pass to the analysis engines
	struct Analysis_options {
		// After how many seconds of CPU time should we give up?
		// Zero means unlimited.
		double timeout;

		long max_memory_usage = 0; // in KiB

		// After how many scheduling decisions (i.e., depth of the
		// schedule graph) should we terminate the analysis?
		// Zero means unlimited.
		unsigned int max_depth;

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

#ifdef CONFIG_PARALLEL
		// Parallel execution options
		bool parallel_enabled = true;
		unsigned int num_threads = 0;  // 0 = auto-detect
		unsigned int min_nodes_per_thread = 4;  // Minimum nodes per thread for load balancing
#endif

#ifdef CONFIG_PRUNING
		// Pruning options
		bool pruning_active = false;
		Pruning_condition pruning_cond;
#endif

		Analysis_options()
		: timeout(0)
		, max_depth(0)
		, early_exit(true)
		, be_naive(false)
		, merge_conservative(false)
		, merge_use_job_finish_times(false)
		, merge_depth(1)
		, verbose(false)
#ifdef CONFIG_PARALLEL
		, parallel_enabled(true)
		, num_threads(0)
		, min_nodes_per_thread(4)
#endif
#ifdef CONFIG_PRUNING
		, pruning_active(false)
		, pruning_cond()
#endif
		{
		}
	};
}

#endif
