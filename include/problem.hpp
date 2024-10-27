#ifndef NP_PROBLEM_HPP
#define NP_PROBLEM_HPP

#include "jobs.hpp"
#include "precedence.hpp"
#include "aborts.hpp"
#include "affinity.hpp"

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
		// Number of (identical) processors in each cluster 
		// on which the jobs are being dispatched. 
		// Jobs are dispatched globally (in priority order) 
		// on each cluster but cannot migrate between clusters
		std::vector<unsigned int> num_processors;


		// Classic default setup: no abort actions
		Scheduling_problem(const Workload& jobs, const Precedence_constraints& prec,
			const std::vector<unsigned int>& num_processors)
			: num_processors(num_processors)
			, jobs(jobs)
			, prec(prec)
		{
			assert(num_processors.size() > 0);
			validate_prec_cstrnts<Time>(this->prec, jobs);
			validate_affinities<Time>(jobs, num_processors.size());
		}

		// Classic default setup: no abort actions
		Scheduling_problem(const Workload& jobs, const Precedence_constraints& prec,
			unsigned int num_processors = 1)
			: num_processors({num_processors})
			, jobs(jobs)
			, prec(prec)
		{
			assert(num_processors > 0);
			validate_prec_cstrnts<Time>(this->prec, jobs);
			validate_affinities<Time>(jobs, 1);
		}

		// Constructor with abort actions and precedence constraints
		Scheduling_problem(const Workload& jobs, const Precedence_constraints& prec,
		                   const Abort_actions& aborts,
						   const std::vector<unsigned int>& num_processors)
		: num_processors(num_processors)
		, jobs(jobs)
		, prec(prec)
		, aborts(aborts)
		{
			assert(num_processors.size() > 0);
			validate_prec_cstrnts<Time>(this->prec, jobs);
			validate_abort_refs<Time>(aborts, jobs);
			validate_affinities<Time>(jobs, num_processors.size());
		}

		// Constructor with abort actions and precedence constraints
		Scheduling_problem(const Workload& jobs, const Precedence_constraints& prec,
			const Abort_actions& aborts,
			unsigned int num_processors = 1)
			: num_processors({ num_processors })
			, jobs(jobs)
			, prec(prec)
			, aborts(aborts)
		{
			assert(num_processors > 0);
			validate_prec_cstrnts<Time>(this->prec, jobs);
			validate_abort_refs<Time>(aborts, jobs);
			validate_affinities<Time>(jobs, 1);
		}

		// Convenience constructor: no DAG, no abort actions
		Scheduling_problem(const Workload& jobs,
						   const std::vector<unsigned int>& num_processors)
		: jobs(jobs)
		, num_processors(num_processors)
		{
			assert(num_processors.size() > 0);
			validate_affinities<Time>(jobs, num_processors.size());
		}

		// Convenience constructor: no DAG, no abort actions
		Scheduling_problem(const Workload& jobs,
			unsigned int num_processors = 1)
			: jobs(jobs)
			, num_processors({ num_processors })
		{
			assert(num_processors > 0);
			validate_affinities<Time>(jobs, 1);
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
