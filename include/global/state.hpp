#ifndef GLOBAL_STATE_HPP
#define GLOBAL_STATE_HPP
#include <algorithm>
#include <cassert>
#include <iostream>
#include <ostream>
#include <set>

#include "config.h"
#include "cache.hpp"
#include "index_set.hpp"
#include "jobs.hpp"
#include "statistics.hpp"
#include "util.hpp"
#include "problem.hpp"	
#include "global/trackers/core_availability_tracker.hpp"
#include "global/trackers/start_times_tracker.hpp"
#include "global/trackers/finish_times_tracker.hpp"
#include "global/trackers/running_jobs_tracker.hpp"
#include "global/trackers/priority_tracker.hpp"
#include "global/trackers/certain_dispatch_times_tracker.hpp"
#include "inter_job_constraints.hpp"

#ifdef CONFIG_ANALYSIS_EXTENSIONS
#include "global/extension/state_extension.hpp"
#endif // CONFIG_ANALYSIS_EXTENSIONS

namespace NP {

	namespace Global {

		typedef Index_set Job_set;
		typedef std::vector<Job_index> Job_precedence_set;

		template<class Time> class State_space_data;

		/**
		 * @brief Represents the state of the schedule at a given point in time. NOTE: Schedule_state is not thread-safe. Thread-safety must be enforced by callers.
		 * 
		 */
		template<class Time> class Schedule_state
		{
		private:
			typedef const Job<Time>* Job_ref;
			using Workload = typename Scheduling_problem<Time>::Workload;
			using Core_availability = typename Core_availability_tracker<Time>::Core_availability;
			using Running_jobs = typename Running_jobs_tracker<Time>::Running_jobs;
			
			// core availability intervals
			Core_availability_tracker<Time> core_avail;

			// keeps track of the earliest time a job with at least one predecessor or mutual exclusion constraint is certainly ready 
			// and certainly has enough free cores to start executing,
			// and the earliest time a gang independent job (a job with no predecessor that requires more than one core to execute) 
			// is certainly arrived and certainly has enough free cores to start executing
			Certain_dispatch_times_tracker<Time> certain_dispatch_times;
			
			// imprecise set of certainly running jobs, on how many cores they run, and when they should finish
			Running_jobs_tracker<Time> certain_running_jobs;

			// start times of all the jobs that still have an unscheduled successor with a start_to_start constraint
			Start_times_tracker<Time> job_start_times;
			// finish times of all the jobs that still have an unscheduled successor with a finish_to_start constraint
			Finish_times_tracker<Time> job_finish_times;

			// the job with the minimum priority among the jobs that can be dispatched next
			Priority_tracker<Time> min_next_prio_job;
#ifdef CONFIG_ANALYSIS_EXTENSIONS
			// possible extensions of the state (e.g., for task chain analysis)
			State_extensions<Time> extensions;
#endif // CONFIG_ANALYSIS_EXTENSIONS

		public:
			/**
			 * @brief Construct an initial empty schedule state.
			 *
			 * Initializes the state for a system with `num_processors`. No jobs
			 * have been dispatched or finished. Additional state-space data is
			 * provided via `state_space_data` to initialize auxiliary trackers
			 * (e.g., earliest certain gang source release).
			 *
			 * @param num_processors Number of processors/cores in the system.
			 * @param state_space_data Reference to state-space metadata used to
			 *        initialise various trackers.
			 */
			Schedule_state(const unsigned int num_processors, const State_space_data<Time>& state_space_data)
				: core_avail{ num_processors }
				, certain_dispatch_times{ state_space_data.get_earliest_certain_gang_independent_job_release(), state_space_data.get_earliest_certain_mutex_source_job_release(), Time_model::constants<Time>::infinity()}
#ifdef CONFIG_ANALYSIS_EXTENSIONS
				, extensions(state_space_data.get_state_extension_registry())
#endif
			{
#ifdef CONFIG_ANALYSIS_EXTENSIONS
				extensions.construct(*this, num_processors, state_space_data);
#endif // CONFIG_ANALYSIS_EXTENSIONS
			}

			/**
			 * @brief Construct an initial state from a given processor availability
			 *        configuration.
			 *
			 * This overload initializes the schedule state using a vector of
			 * `Interval<Time>` representing the initial availability of the
			 * processors. Other trackers are initialized using
			 * `state_space_data` as in the default constructor.
			 *
			 * @param proc_initial_state Initial availability intervals for each
			 *        processor/core.
			 * @param state_space_data Reference to state-space metadata used to
			 *        initialise various trackers.
			 */
			Schedule_state(const std::vector<Interval<Time>>& proc_initial_state, const State_space_data<Time>& state_space_data)
				: core_avail{ proc_initial_state }
				, certain_dispatch_times{ state_space_data.get_earliest_certain_gang_independent_job_release(), state_space_data.get_earliest_certain_mutex_source_job_release(), Time_model::constants<Time>::infinity()}
#ifdef CONFIG_ANALYSIS_EXTENSIONS
				, extensions(state_space_data.get_state_extension_registry())
#endif
			{
#ifdef CONFIG_ANALYSIS_EXTENSIONS
				extensions.construct(*this, proc_initial_state, state_space_data);
#endif // CONFIG_ANALYSIS_EXTENSIONS
			}

			/**
			 * @brief Construct a new state by applying a dispatch transition.
			 *
			 * Creates a new schedule state that results from scheduling job `j`
			 * on `ncores` starting/finishing within the intervals `start_times`
			 * and `finish_times`, based on a source state `from`.
			 *
			 * @param from The source schedule state before dispatching `j`.
			 * @param j Index of the job being scheduled.
			 * @param start_times Interval of possible start times for `j`.
			 * @param finish_times Interval of possible finish times for `j`.
			 * @param scheduled_jobs Set of jobs already scheduled in the state.
			 * @param jobs_with_pending_start_succ Indices of jobs that still have
			 *        pending start-to-start successors.
			 * @param jobs_with_pending_finish_succ Indices of jobs that still
			 *        have pending finish-to-start successors.
			 * @param ready_succ_jobs Vector of pointers to successor jobs that are
			 *        currently ready.
			 * @param state_space_data Reference to state-space metadata (constraints,
			 *        finished-predecessors mapping, etc.).
			 * @param next_source_job_rel The next source job release time used to
			 *        update certain-dispatch trackers.
			 * @param ncores Number of cores allocated to job `j` (default 1).
			 */
			Schedule_state(
				const Schedule_state& from,
				Job_index j,
				const Interval<Time>& start_times,
				const Interval<Time>& finish_times,
				const Job_set& scheduled_jobs,
				const std::vector<Job_index>& jobs_with_pending_start_succ,
				const std::vector<Job_index>& jobs_with_pending_finish_succ,
				const std::vector<const Job<Time>*>& ready_succ_jobs,
				const State_space_data<Time>& state_space_data,
				Time next_source_job_rel,
				unsigned int ncores = 1)
				: core_avail(from.core_avail.num_cores())
#ifdef CONFIG_ANALYSIS_EXTENSIONS
				, extensions(state_space_data.get_state_extension_registry())
#endif
			{
				const auto& constraints = state_space_data.inter_job_constraints;
				const Job_precedence_set & finished_predecessors = state_space_data.get_finished_jobs_if_starts(j);
				// update the set of certainly running jobs and
				// get the number of cores certainly used by active predecessors
				int n_prec = certain_running_jobs.update_and_count_predecessors(from.certain_running_jobs, j, start_times, finish_times, ncores, finished_predecessors);

				// calculate the cores availability this_intervals resulting from dispatching job j on ncores in state 'from'
				core_avail.update(from.core_avail, start_times, finish_times, ncores, n_prec);
				// save the job start of every job with a successor with a start to start constraint that is not executed yet in the current state
				job_start_times.update(from.job_start_times, j, start_times, jobs_with_pending_start_succ, constraints[j]);
				// save the job finish of every job with a successor with a finish to start constraint that is not executed yet in the current state
				job_finish_times.update(from.job_finish_times, j, start_times, finish_times, jobs_with_pending_finish_succ, constraints[j], core_avail.num_cores() == 1);

				// NOTE: must be done after the start times, finish times and core availabilities have been updated
				certain_dispatch_times.update(*this, state_space_data, ready_succ_jobs, core_avail, scheduled_jobs, next_source_job_rel);
				// NOTE: must be done after the start times and finish times have been updated			
				min_next_prio_job.update(*this, constraints, state_space_data.conditional_dispatch_constraints, ready_succ_jobs, scheduled_jobs, core_avail.num_cores());

#ifdef CONFIG_ANALYSIS_EXTENSIONS
				extensions.construct(*this, from, j, start_times, finish_times,
					scheduled_jobs, jobs_with_pending_start_succ, jobs_with_pending_finish_succ, ready_succ_jobs,
					state_space_data, next_source_job_rel, ncores);
#endif // CONFIG_ANALYSIS_EXTENSIONS

				DM("*** new state: constructed " << *this << std::endl);
			}

			/**
			 * @brief Reset the state to an initial empty state with given
			 *        number of processors.
			 *
			 * Resets all internal trackers (core availability, running jobs,
			 * start/finish time trackers, dispatch/priority trackers) to an
			 * initial configuration for `num_processors` cores.
			 *
			 * @param num_processors Number of processors/cores in the system.
			 * @param state_space_data Reference to state-space metadata used to
			 *        reinitialise certain trackers.
			 */
			void reset(const unsigned int num_processors, const State_space_data<Time>& state_space_data)
			{
				core_avail.reset(num_processors);
				certain_dispatch_times.reset(state_space_data.get_earliest_certain_gang_independent_job_release(), state_space_data.get_earliest_certain_mutex_source_job_release(), Time_model::constants<Time>::infinity());
				job_start_times.clear();
				job_finish_times.clear();
				certain_running_jobs.clear();
				min_next_prio_job.reset();
				assert(core_avail.size() > 0);

#ifdef CONFIG_ANALYSIS_EXTENSIONS
				extensions.reset(*this, num_processors, state_space_data);
#endif // CONFIG_ANALYSIS_EXTENSIONS
			}

			/**
			 * @brief Reset the state to an initial configuration using explicit
			 *        processor availability intervals.
			 *
			 * Equivalent to `reset(num_processors, state_space_data)` but allows
			 * specifying the exact initial availability intervals for the
			 * processors.
			 *
			 * @param proc_initial_state Initial availability intervals for each
			 *        processor/core.
			 * @param state_space_data Reference to state-space metadata used to
			 *        reinitialise certain trackers.
			 */
			void reset(const std::vector<Interval<Time>>& proc_initial_state, const State_space_data<Time>& state_space_data)
			{
				core_avail.reset(proc_initial_state);
				certain_dispatch_times.reset(state_space_data.get_earliest_certain_gang_independent_job_release(), state_space_data.get_earliest_certain_mutex_source_job_release(), Time_model::constants<Time>::infinity());
				job_start_times.clear();
				job_finish_times.clear();
				certain_running_jobs.clear();
				min_next_prio_job.reset();
#ifdef CONFIG_ANALYSIS_EXTENSIONS
				extensions.reset(*this, proc_initial_state, state_space_data);
#endif // CONFIG_ANALYSIS_EXTENSIONS
			}

			/**
			 * @brief Reset this state by applying the same dispatch transition as
			 *        the transition-constructor.
			 *
			 * Updates this state in-place so it becomes the result of dispatching
			 * job `j` in `from` with the provided start/finish intervals and
			 * auxiliary information. This mirrors the non-mutating transition
			 * constructor but operates by resetting this instance instead of 
			 * creating a new one.
			 *
			 * @param from The source schedule state before dispatching `j`.
			 * @param j Index of the job being scheduled.
			 * @param start_times Interval of possible start times for `j`.
			 * @param finish_times Interval of possible finish times for `j`.
			 * @param scheduled_jobs Set of jobs already scheduled in the state.
			 * @param jobs_with_pending_start_succ Indices of jobs that still have
			 *        pending start-to-start successors.
			 * @param jobs_with_pending_finish_succ Indices of jobs that still
			 *        have pending finish-to-start successors.
			 * @param ready_succ_jobs Vector of pointers to successor jobs that are
			 *        currently ready.
			 * @param state_space_data Reference to state-space metadata (constraints,
			 *        finished-predecessors mapping, etc.).
			 * @param next_source_job_rel The next source job release time used to
			 *        update certain-dispatch trackers.
			 * @param ncores Number of cores allocated to job `j` (default 1).
			 */
			void reset(
				const Schedule_state& from,
				Job_index j,
				const Interval<Time>& start_times,
				const Interval<Time>& finish_times,
				const Job_set& scheduled_jobs,
				const std::vector<Job_index>& jobs_with_pending_start_succ,
				const std::vector<Job_index>& jobs_with_pending_finish_succ,
				const std::vector<const Job<Time>*>& ready_succ_jobs,
				const State_space_data<Time>& state_space_data,
				Time next_source_job_rel,
				unsigned int ncores = 1)
			{
				const auto& constraints = state_space_data.inter_job_constraints;
				const Job_precedence_set & finished_predecessors = state_space_data.get_finished_jobs_if_starts(j);
				// update the set of certainly running jobs and
				// get the number of cores certainly used by active predecessors
				int n_prec = certain_running_jobs.update_and_count_predecessors(from.certain_running_jobs, j, start_times, finish_times, ncores, finished_predecessors);

				// calculate the cores availability this_intervals resulting from dispatching job j on ncores in state 'from'
				core_avail.update(from.core_avail, start_times, finish_times, ncores, n_prec);

				assert(core_avail.num_cores() > 0);

				// save the job start of every job with a successor with a start to start constraint that is not executed yet in the current state
				job_start_times.update(from.job_start_times, j, start_times, jobs_with_pending_start_succ, constraints[j]);
				// save the job finish of every job with a successor with a finish to start constraint that is not executed yet in the current state
				job_finish_times.update(from.job_finish_times, j, start_times, finish_times, jobs_with_pending_finish_succ, constraints[j], core_avail.num_cores() == 1);

				// NOTE: must be done after the start times, finish times and core availabilities have been updated
				certain_dispatch_times.update(*this, state_space_data, ready_succ_jobs, core_avail, scheduled_jobs, next_source_job_rel);
				// NOTE: must be done after the start times and finish times have been updated
				min_next_prio_job.update(*this, constraints, state_space_data.conditional_dispatch_constraints, ready_succ_jobs, scheduled_jobs, core_avail.num_cores());

#ifdef CONFIG_ANALYSIS_EXTENSIONS
				extensions.reset(*this, from, j, start_times, finish_times,
					scheduled_jobs, jobs_with_pending_start_succ, jobs_with_pending_finish_succ, ready_succ_jobs,
					state_space_data, next_source_job_rel, ncores);
#endif // CONFIG_ANALYSIS_EXTENSIONS

				DM("*** new state: constructed " << *this << std::endl);
			}

#ifdef CONFIG_ANALYSIS_EXTENSIONS
			/**
			 * @brief Accessor for optional state extensions.
			 *
			 * Returns the collection of additional state extensions when the
			 * `CONFIG_ANALYSIS_EXTENSIONS` build option is enabled. These
			 * extensions can carry analysis-specific auxiliary data.
			 */
			const State_extensions<Time>& get_extensions() const
			{
				return extensions;
			}
#endif // CONFIG_ANALYSIS_EXTENSIONS

			/**
			 * @brief Get the full core availability structure.
			 *
			 * Returns a reference to the internal representation of the
			 * availability intervals for all cores.
			 *
			 * @return Reference to availability intervals.
			 */
			const Core_availability& get_cores_availability() const
			{
				return core_avail.get_all_intervals();
			}

			/**
			 * @brief Get the availability interval for the p-th core to become
			 *        available.
			 *
			 * @param p The number of the core availability to query (1 = first
			 *        available core).
			 * @return Time interval when the p-th core is available.
			 */
			Interval<Time> core_availability(unsigned long p = 1) const
			{
				return core_avail.get_availability(p);
			}

			/**
			 * @brief Return the earliest time any core becomes available.
			 *
			 * @return The earliest availability time across all cores.
			 */
			Time earliest_core_availability() const
			{
				return core_avail.earliest_availability();
			}

			/**
			 * @brief Query the finish-time interval for a dispatched job.
			 *
			 * If the finish time interval for job `j` is known (i.e., recorded
			 * in the finish-time tracker), this writes it into `ftimes` and
			 * returns true. Otherwise returns false.
			 *
			 * @param j Job index to query.
			 * @param ftimes Output parameter to receive the finish interval.
			 * @return true if an interval was found and written to `ftimes`.
			 */
			bool get_finish_times(Job_index j, Interval<Time>& ftimes) const
			{
				return job_finish_times.get(j, ftimes);
			}

			/**
			 * @brief Return a pointer to the job with minimum priority among the
			 *        jobs that can be dispatched next.
			 *
			 * @return Job_ref Pointer to the minimum-priority ready job or nullptr
			 *         if none is available.
			 */
			Job_ref get_next_dispatched_job_min_priority() const
			{
				return min_next_prio_job.get_min_priority_job();
			}

			/**
			 * @brief Query the start-time interval for a dispatched job.
			 *
			 * If the start time interval for job `j` is known (i.e., recorded
			 * in the start-time tracker), this writes it into `stimes` and
			 * returns true. Otherwise returns false.
			 *
			 * @param j Job index to query.
			 * @param stimes Output parameter to receive the start interval.
			 * @return true if an interval was found and written to `stimes`.
			 */
			bool get_start_times(Job_index j, Interval<Time>& stimes) const
			{
				return job_start_times.get(j, stimes);
			}

			/**
			 * @brief Get earliest time at which a gang independent job is certainly dispatchable.
			 */
			Time next_certain_gang_independent_job_dispatch() const
			{
				return certain_dispatch_times.get_gang_independent_dispatch_time();
			}

			/**
			 * @brief Get earliest time at which a source job with mutual
			 *        exclusion constraints is certainly dispatchable.
			 */
			Time next_certain_mutex_source_job_dispatch() const
			{
				return certain_dispatch_times.get_mutex_source_dispatch_time();
			}

			/**
			 * @brief Get earliest time at which successor jobs are certainly dispatchable.
			 */
			Time next_certain_successor_jobs_dispatch() const
			{
				return certain_dispatch_times.get_successor_dispatch_time();
			}

			/**
			 * @brief Access the set of certainly running jobs.
			 *
			 * Returns the internal representation of imprecisely-known running
			 * jobs including their parallelism and finish intervals.
			 *
			 * @return Reference to the certainly-running jobs.
			 */
			const Running_jobs& get_cert_running_jobs() const
			{
				return certain_running_jobs.get_running_jobs();
			}

			/**
			 * @brief Determine whether job `j` is certainly ready before the
			 *        first core becomes available.
			 * 
			 * This method checks if the job j is certainly arrived and all its predecedence and mutual exclusion constraints 
			 * are certainly fulfilled before the first core becomes available.
			 * 
			 * @param j The job to check.
			 * @param delay_constraints The inter-job constraints.
			 * @param scheduled_jobs The set of jobs that have already been dispatched in the state.
			 * @return true if j is certainly ready before the first core becomes available, false otherwise.
			*/
			bool certainly_ready(Job_ref j, const Inter_job_constraints<Time>& delay_constraints, const Job_set& scheduled_jobs) const
			{
				// the job cannot be ready if it is released after the first core becomes available
				if (j->latest_arrival() > core_availability(1).min())
					return false;

				// check if all the predecessors of j with a start_to_start constraint are certainly started at least delay time units before the first core becomes available
				const auto& s_predecessors = delay_constraints[j->get_job_index()].predecessors_start_to_start;
				for (const auto& p : s_predecessors) {
					if (!certainly_started(p.reference_job, p.delay.max()))
						return false;
				}
				// check if all the predecessors of j are certainly finished when the first core becomes available (accounting for the suspension time)
				const auto& f_predecessors = delay_constraints[j->get_job_index()].predecessors_finish_to_start;
				for (const auto& p : f_predecessors) {
					if (!certainly_finished(p.reference_job, p.delay.max(), delay_constraints, scheduled_jobs)) 
						return false;
				}
				// check if all the exclusion constraints of j are certainly fulfilled when the first core becomes available
				const auto& between_starts = delay_constraints[j->get_job_index()].between_starts;
				for (const auto& p : between_starts) {
					if (scheduled_jobs.contains(p.reference_job->get_job_index()) && !certainly_started(p.reference_job, p.delay.max()))
						return false;
				}
				const auto& between_executions = delay_constraints[j->get_job_index()].between_executions;
				for (const auto& p : between_executions) {
					if (scheduled_jobs.contains(p.reference_job->get_job_index()) && !certainly_finished(p.reference_job, p.delay.max(), delay_constraints, scheduled_jobs))
						return false;
				}
				return true;
			}

			/**
			 * @brief Test whether `other` schedule state can be merged with this
			 *        state.
			 *
			 * The merging criteria are based primarily on core availability
			 * overlap. If `use_job_times == true` the method additionally
			 * requires compatible job start/finish times to overlap. The
			 * `conservative` flag controls whether strict containment semantics are
			 * applied.
			 *
			 * @param other The other state to test for mergeability.
			 * @param conservative If true, perform a conservative overlap test.
			 * @param use_job_times If true, require tracked job start/finish times
			 *        to also overlap.
			 * @return true if the two states can be merged, false otherwise.
			 */
			bool can_merge_with(const Schedule_state<Time>& other, bool conservative, bool use_job_times = false) const
			{
				bool other_in_this;
				if (core_avail.overlap_with(other.core_avail, conservative, other_in_this))
				{
					if (use_job_times) {
						return job_start_times.overlap_with(other.job_start_times, conservative, other_in_this) &&
							job_finish_times.overlap_with(other.job_finish_times, conservative, other_in_this);
					} else return true;
				}
				else
					return false;
			}

			/**
			 * @brief Attempt to merge `other` into this state in-place.
			 *
			 * First checks mergeability via `can_merge_with`. If the states can
			 * be merged, performs the in-place merge of all internal trackers
			 * and returns true.
			 *
			 * @param other The other state to merge into this one.
			 * @param conservative If true, use conservative overlap semantics.
			 * @param use_job_times If true, require tracked job start/finish times
			 *        to also overlap.
			 * @return true if the merge was performed, false otherwise.
			 */
			bool try_to_merge(const Schedule_state<Time>& other, bool conservative, bool use_job_times = false)
			{
				if (!can_merge_with(other, conservative, use_job_times))
					return false;

				merge(other.core_avail, other.job_start_times, other.job_finish_times, other.certain_running_jobs, other.certain_dispatch_times, other.min_next_prio_job);

#ifdef CONFIG_ANALYSIS_EXTENSIONS
				extensions.merge(*this, other);
#endif // CONFIG_ANALYSIS_EXTENSIONS

				DM("+++ merged " << other << " into " << *this << std::endl);
				return true;
			}

			/**
			 * @brief Merge external tracker views into this state.
			 *
			 * @param cav Core availability tracker to merge.
			 * @param jst Start-times tracker to merge.
			 * @param jft Finish-times tracker to merge.
			 * @param cert_j Running-jobs tracker to merge.
			 * @param cert_disp Certain-dispatch times tracker to merge.
			 * @param min_p_next_job Priority tracker for next-job to merge.
			 */
			void merge(
				const Core_availability_tracker<Time>& cav,
				const Start_times_tracker<Time>& jst,
				const Finish_times_tracker<Time>& jft,
				const Running_jobs_tracker<Time>& cert_j,
				const Certain_dispatch_times_tracker<Time>& cert_disp,
				const Priority_tracker<Time>& min_p_next_job)
			{
				core_avail.merge(cav);
				certain_running_jobs.merge(cert_j);

				// merge job start and finish times
				job_start_times.merge(jst);
				job_finish_times.merge(jft);

				// update times where gang jobs and jobs with predecessors are certainly dispatchable
				certain_dispatch_times.merge(cert_disp);

				// update the minimum priority job that can be dispatched next
				min_next_prio_job.merge(min_p_next_job);
				DM("+++ merged (cav,jft,cert_t) into " << *this << std::endl);
			}

			/**
			 * @brief Export the state in a human-readable CSV-like form.
			 *
			 * Writes internal information (core availability intervals, certainly
			 * running jobs, stored start/finish times for predecessors, etc.)
			 * to the provided output stream. Intended for debugging and
			 * SAG exports.
			 *
			 * @param stream Output stream to write the state to.
			 * @param jobs The workload (job vector) used to resolve task/job ids
			 *        for human-friendly output.
			 */
			void export_state (std::ostream& stream, const Workload& jobs)
			{
				stream << "=====State=====\n"
					<< "Cores availability\n";
				// Core availability this_intervals
				for (const auto& a : core_avail.get_all_intervals())
					stream << "[" << a.min() << "," << a.max() << "]\n";

				stream << "Certainly running jobs: [<task_id>,<job_id>]:[<par_min>,<par_max>],[<ft_min>,<ft_max>]\n";
				// Running jobs: <task_id>,<job_id>,<par_min>,<par_max>,<ft_min>,<ft_max>\n
				for (const auto& rj : certain_running_jobs.get_running_jobs()) {
					const auto j = jobs[rj.idx];
					stream << "[" << j.get_task_id() << "," << j.get_job_id() << "]:[" << rj.parallelism.min() << "," << rj.parallelism.max() << "],[" << rj.finish_time.min() << "," << rj.finish_time.max() << "]\n";
				}
				stream << "Finish times predecessors: [<task_id>,<job_id>]:[<ft_min>,<ft_max>]\n";
				// Jobs with pending successors: <job_id>,<ft_min>,<ft_max>\n
				for (const auto& jft : job_finish_times.get()) {
					const auto j = jobs[jft.job_idx];
					stream << "[" << j.get_task_id() << "," << j.get_job_id() << "]:[" << jft.time_interval.min() << "," << jft.time_interval.max() << "]\n";
				}
				stream << "Start times predecessors: [<task_id>,<job_id>]:[<st_min>,<st_max>]\n";
				// Jobs with pending successors: <job_id>,<ft_min>,<ft_max>\n
				for (const auto& jst : job_start_times.get()) {
					const auto j = jobs[jst.job_idx];
					stream << "[" << j.get_task_id() << "," << j.get_job_id() << "]:[" << jst.time_interval.min() << "," << jst.time_interval.max() << "]\n";
				}
				stream << "Start times predecessors: [<task_id>,<job_id>]:[<st_min>,<st_max>]\n";
				// Jobs with pending successors: <job_id>,<ft_min>,<ft_max>\n
				for (const auto& jst : job_start_times.get()) {
					const auto j = jobs[jst.job_idx];
					stream << "[" << j.get_task_id() << "," << j.get_job_id() << "]:[" << jst.time_interval.min() << "," << jst.time_interval.max() << "]\n";
				}
			}

		private:

			/**
			 * @brief Check whether a dispatched job was certainly finished at
			 *        least `delay` time units before the first core becomes
			 *        available.
			 *
			 * @param j Pointer to the dispatched job to check (assumed to be dispatched. No additional check performed).
			 * @param delay Required time separation between finish of `j` and
			 *        the first core availability.
			 * @param delay_constraints Inter-job constraint structure.
			 * @param scheduled_jobs Set of jobs already scheduled in the state.
			 * @return true if `j` certainly finished at least `delay` before the
			 *         earliest available core, false otherwise.
			 */
			bool certainly_finished(Job_ref j, Time delay, const Inter_job_constraints<Time>& delay_constraints, const Job_set& scheduled_jobs) const
			{
				// if there is only one core, then the job is certainly finished if it was dispatched already
				if (core_avail.num_cores() == 1 && delay == 0)
					return true;
				// check if the job either finished in the past or is certainly running on the first core that will become available
				Interval<Time> ft{ 0, 0 };
				bool has_ft = get_finish_times(j->get_job_index(), ft);
				assert(has_ft);

				if (delay > 0) {
					// check if j certainly finished dealy time units before the first core may become available
					if (ft.max() + delay <= core_availability(1).min())
						return true;
				}
				else {
					if (ft.max() < core_availability(2).min())
						return true;

					// check if one of the successors of j is in the list of certainly running jobs
					for (const auto& s : delay_constraints[j->get_job_index()].finish_to_successors_start) {
						if (scheduled_jobs.contains(s.reference_job->get_job_index())) {
							return true;
						}
					}
				}
				return false;
			}

			/**
			 * @brief Check if a job was certainly started at least delay time units before the first core becomes available.
			 * @param j The job to check (assumed to be dispatched)
			 * @param delay The minimum delay between j started and the first core becomes available
			 */
			/**
			 * @brief Check whether a dispatched job was certainly started at
			 *        least `delay` time units before the first core becomes
			 *        available.

			 * @param j Pointer to the dispatched job to check (assumed to be dispatched. No additional check performed).
			 * @param delay Required time separation between start of `j` and the
			 *        first core availability.
			 * @return true if `j` certainly started at least `delay` before the
			 *         earliest available core, false otherwise.
			 */
			bool certainly_started(Job_ref j, Time delay) const
			{
				// if the job is certainly dispatched, then it is certainly started too
				if (delay == 0)
					return true;
				// check if the job certainly started delay time units before the first core becomes available
				Interval<Time> st{ 0, 0 };
				bool has_st = job_start_times.get(j->get_job_index(), st);
				assert(has_st);

				// check if j certainly started delay time units before the first core may become available
				if (st.max() + delay <= core_availability(1).min())
					return true;

				return false;
			}

			/**
			 * @brief Deleted copy constructor.
			 *
			 * Copying schedule states is disallowed.
			 */
			Schedule_state(const Schedule_state& origin) = delete;
		};
	}
}

#endif