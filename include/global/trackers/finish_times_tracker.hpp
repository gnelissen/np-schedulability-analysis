#ifndef GLOBAL_FINISH_TIMES_TRACKER_HPP
#define GLOBAL_FINISH_TIMES_TRACKER_HPP
#include "global/trackers/job_timing_tracker.hpp"
#include "inter_job_constraints.hpp"
namespace NP {
namespace Global {
/**
 * @brief Tracks job finish times for jobs with pending successors.
 * 
 * This class extends Job_timing_tracker to specifically manage
 * finish times of jobs. It provides methods to merge timing
 * information and check for overlaps during state merging.
 */
template<class Time>
class Finish_times_tracker : public Job_timing_tracker<Time>
{
public:
    Finish_times_tracker() = default;

    /**
	 * @brief Update finish times after dispatching a job.
	 * 
	 * @param from Previous tracker state
	 * @param dispatched_job The job being dispatched
	 * @param start_time_interval When the job starts
	 * @param finish_time_interval When the job finishes
	 * @param jobs_with_pending_succ Jobs that still have pending successors
	 * @param constraints Precedence and mutual exclusion constraints of the dispatched job
	 * @param single_core True if system has only one core
	 */
	void update(
		const Finish_times_tracker& from,
		Job_index dispatched_job,
		const Interval<Time>& start_time_interval,
		const Interval<Time>& finish_time_interval,
		const std::vector<Job_index>& jobs_with_pending_succ,
		const Job_constraints<Time>& constraints,
		bool single_core)
	{
		Job_timing_tracker<Time>::job_times.clear();
		Job_timing_tracker<Time>::job_times.reserve(jobs_with_pending_succ.size());

		const Time lst = start_time_interval.max();

		auto it = from.job_times.begin();
		for (Job_index job : jobs_with_pending_succ) {
			if (job == dispatched_job) {
				Job_timing_tracker<Time>::job_times.emplace_back(job, finish_time_interval);
			} else {
				// Find this job in the previous tracker
                // Note that with conditional DAGs, it is *NOT TRUE* that if `job` has non-completed successors in the new state,
				// it must have had non-completed successors in the previous state too.
				while (it != from.job_times.end() && it->job_idx < job) {
					++it;
				}
				if (it == from.job_times.end() || it->job_idx > job)
					continue;

				// FT^min(job,v') = FT^min(job,v)
				// FT^max(job,v') = 
				// \begin{cases}
				//	 LST(j,v) & m=1 \text{i.e., single core} \\
				//   min( FT^max(job,v), LST(j,v) - delay_min(job,j) ) & m>1 \wedge job \in Pred^f(j) U Mutx^f(j) \\
				//	 FT^max(job,v) & \text{otherwise}
				// \end{cases}
				Time job_eft = it->time_interval.min();
				Time job_lft = it->time_interval.max();
				if (single_core && job_lft > lst) {
					// If there is a single-core, we know that jobs dispatched in the past
					// must finish before the new job can start
					// FT^max(job,v') = LST(j,v)
					job_lft = lst;
				}
				else {
					// if job was already dispatched and job must finish at least delay_min time units before the newly dispatched job starts,
					// then job should certainly have finished delay_min time units before the newly dispatched job certainly starts
					Time min_delay = constraints.get_min_delay_after_finish_of(job);
					if (min_delay >= 0)
						job_lft = std::min(job_lft, lst - min_delay);
				}
				Job_timing_tracker<Time>::job_times.emplace_back(job, Interval<Time>{job_eft, job_lft});
			}
		}
	}
};
} // namespace Global
} // namespace NP
#endif // GLOBAL_FINISH_TIMES_TRACKER_HPP