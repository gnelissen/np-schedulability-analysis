#ifndef GLOBAL_CERTAIN_DISPATCH_TIMES_TRACKER_HPP
#define GLOBAL_CERTAIN_DISPATCH_TIMES_TRACKER_HPP

#include <algorithm>
#include "time.hpp"
#include "jobs.hpp"
#include "global/state.hpp"
#include "inter_job_constraints.hpp"
#include "conditional_dispatch_constraints.hpp"
#include "index_set.hpp"

namespace NP {
namespace Global {

/**
 * @brief Tracks certain dispatch/ready times for different job categories.
 * 
 * This class tracks:
 * - When successor jobs (with predecessors) are certainly ready to dispatch
 * - When independent gang jobs (multi-core jobs without predecessors or mutual exclusion constraints) can be dispatched
 * - When source jobs (without predecessors) with mutual exclusion constraints can be dispatched
 */
template<class Time>
class Certain_dispatch_times_tracker
{
	typedef Index_set Job_set;
public:
	/**
	 * @brief Default constructor initializes all times to infinity.
	 */
	Certain_dispatch_times_tracker()
		: earliest_certain_successor_job_dispatch(Time_model::constants<Time>::infinity())
		, earliest_certain_gang_independent_job_dispatch(Time_model::constants<Time>::infinity())
		, earliest_certain_mutex_source_job_dispatch(Time_model::constants<Time>::infinity())
	{
	}
	
	/**
	 * @brief Construct with initial values.
	 * 
	 * @param gang_independent_time Earliest time a gang independent job can dispatch
	 * @param mutex_source_time Earliest time a source job with mutual exclusion constraints can dispatch
	 * @param succ_ready_time Earliest time a successor job is certainly ready
	 */
	explicit Certain_dispatch_times_tracker(Time gang_independent_time, Time mutex_source_time, Time succ_ready_time = Time_model::constants<Time>::infinity())
		: earliest_certain_successor_job_dispatch(succ_ready_time)
		, earliest_certain_gang_independent_job_dispatch(gang_independent_time)
		, earliest_certain_mutex_source_job_dispatch(mutex_source_time)
	{
	}
	
	/**
	 * @brief Get the earliest time a successor job is certainly ready.
	 */
	Time get_successor_dispatch_time() const
	{
		return earliest_certain_successor_job_dispatch;
	}
	
	/**
	 * @brief Get the earliest time a gang independent job can dispatch.
	 */
	Time get_gang_independent_dispatch_time() const
	{
		return earliest_certain_gang_independent_job_dispatch;
	}

	/**
	 * @brief Get the earliest time a source job (i.e., without predecessors) with mutual exclusion constraints can dispatch.
	 */
	Time get_mutex_source_dispatch_time() const
	{
		return earliest_certain_mutex_source_job_dispatch;
	}
	
	/**
	 * @brief Merge dispatch times from another tracker.
	 * 
	 * Takes the maximum of each time category, since we need the
	 * latest "certain" time when merging states.
	 * 
	 * @param other The other tracker to merge with
	 */
	void merge(const Certain_dispatch_times_tracker& other)
	{
		earliest_certain_successor_job_dispatch = 
			std::max(earliest_certain_successor_job_dispatch,
			         other.earliest_certain_successor_job_dispatch);
		earliest_certain_gang_independent_job_dispatch = 
			std::max(earliest_certain_gang_independent_job_dispatch,
			         other.earliest_certain_gang_independent_job_dispatch);
		earliest_certain_mutex_source_job_dispatch = 
			std::max(earliest_certain_mutex_source_job_dispatch,
			         other.earliest_certain_mutex_source_job_dispatch);
	}
	
	/**
	 * @brief Reset to initial state.
	 * 
	 * @param gang_independent_time Initial gang independent dispatch time
	 * @param mutex_source_time Initial mutex source dispatch time
	 * @param succ_ready_time Earliest time a successor job is certainly ready
	 */
	void reset(Time gang_independent_time, Time mutex_source_time, Time succ_ready_time = Time_model::constants<Time>::infinity())
	{
		earliest_certain_successor_job_dispatch = succ_ready_time;
		earliest_certain_gang_independent_job_dispatch = gang_independent_time;
		earliest_certain_mutex_source_job_dispatch = mutex_source_time;
	}
	
	/**
	 * @brief Reset all times to infinity.
	 */
	void reset()
	{
		earliest_certain_successor_job_dispatch = Time_model::constants<Time>::infinity();
		earliest_certain_gang_independent_job_dispatch = Time_model::constants<Time>::infinity();
		earliest_certain_mutex_source_job_dispatch = Time_model::constants<Time>::infinity();
	}

	void update(
		const Schedule_state<Time>& state,
		const State_space_data<Time>& state_space_data,
		const std::vector<const Job<Time>*>& ready_succ_jobs,
		const Core_availability_tracker<Time>& core_avail,
		const Job_set& scheduled_jobs,
		Time next_source_job_release) 
	{
		update_earliest_certain_gang_independent_job_dispatch(next_source_job_release, scheduled_jobs, state_space_data, core_avail);
		update_earliest_certain_mutex_source_job_dispatch(next_source_job_release, state, scheduled_jobs, state_space_data, core_avail);
		update_earliest_certain_successor_job_dispatch(state, ready_succ_jobs, state_space_data.inter_job_constraints, state_space_data.conditional_dispatch_constraints, core_avail);
	}

private:
	// Earliest time when a job with predecessors is certainly ready to dispatch
	Time earliest_certain_successor_job_dispatch;

	// Earliest time when a source job (no predecessors) with mutual exclusion constraints is certainly ready 
	// to dispatch and has enough cores available
	Time earliest_certain_mutex_source_job_dispatch;
	
	// Earliest time when a gang independent job (no predecessors, no mutual exclusion constraint, multi-core)
	// is certainly ready to dispatch
	Time earliest_certain_gang_independent_job_dispatch;


	/**
	 * @brief Finds the earliest time a gang independent job (i.e., a job without predecessors or mutual exclusion constraints 
	 * that requires more than one core to start executing) is certainly released and has enough cores available to start executing 
	 * at or after time `after`
	 * 
	 * calculate: min_{j \in GangSourceJobs \ Omega(v)} { max( r^max(j), A^max_p^min(j)(v)) ) }
	 * 				where GangSourceJobs = { j | Pred(j) = emptyset and p^min(j) > 1 }
	 * 
	 * @param after Time after which we look for the earliest time a gang source job is certainly ready to dispatch
	 * @param scheduled_jobs Set of jobs that were already dispatched
	 * @param state_space_data State space data
	 * @param core_avail List of cores availability intervals
	 */
	void update_earliest_certain_gang_independent_job_dispatch(
		Time after,
		const Job_set& scheduled_jobs,
		const State_space_data<Time>& state_space_data,
		const Core_availability_tracker<Time>& core_avail)
	{
		earliest_certain_gang_independent_job_dispatch = Time_model::constants<Time>::infinity();

		for (auto it = state_space_data.gang_independent_jobs_by_latest_arrival.lower_bound(after);
			it != state_space_data.gang_independent_jobs_by_latest_arrival.end(); it++)
		{
			const Job<Time>* jp = it->second;
			if (jp->latest_arrival() >= earliest_certain_gang_independent_job_dispatch)
				break;

			// skip if the job was dispatched already
			if (scheduled_jobs.contains(jp->get_job_index()))
				continue;

			// max( r^max(j), A^max_p^min(j)(v) )
			Time disp = std::max(jp->latest_arrival(),
					core_avail.get_availability(jp->get_min_parallelism()).max());
			earliest_certain_gang_independent_job_dispatch = std::min(earliest_certain_gang_independent_job_dispatch, disp);
		}
	}

	/**
	 * @brief Calculate the earliest time a source job (i.e, without predecessors) with mutual exclusion constraints will become ready to dispatch.
	 * 
	 * calculate: min_{j \in MutxSourceJobs \ Omega(v)} { max{ r^max(j), A^max_p^min(j)(v)),
	 * 													   max_{i \in Mutx^f(j) \cup Omega(v)} { FT^max(i) + delay^max(i,j) },
	 * 													   max_{i \in Mutx^s(j) \cup Omega(v)} { ST^max(i) + delay^max(i,j) } } }
	 * 
	 * @param after Time after which we look for the earliest time a mutual exclusion source job is certainly ready to dispatch
	 * @param state The current schedule state
	 * @param scheduled_jobs Set of jobs that were already dispatched
	 * @param state_space_data State space data
	 * @param core_avail The core availability intervals
	 */
	void update_earliest_certain_mutex_source_job_dispatch(
		Time after,
		const Schedule_state<Time>& state,
		const Job_set& scheduled_jobs,
		const State_space_data<Time>& state_space_data,
		const Core_availability_tracker<Time>& core_avail)
	{
		earliest_certain_mutex_source_job_dispatch = Time_model::constants<Time>::infinity();

		for (auto it = state_space_data.mutex_source_jobs_by_latest_arrival.lower_bound(after);
			it != state_space_data.mutex_source_jobs_by_latest_arrival.end(); it++)
		{
			const Job<Time>* jp = it->second;
			if (jp->latest_arrival() >= earliest_certain_mutex_source_job_dispatch)
				break;

			// skip if the job was dispatched already
			if (scheduled_jobs.contains(jp->get_job_index()))
				continue;

			// max{ r^max(jp), A^max_p^min(jp)(v))}
			Time avail = core_avail.get_availability(jp->get_min_parallelism()).max();
			Time ready_time = std::max(avail, jp->latest_arrival());

			const auto& constraints = state_space_data.inter_job_constraints[jp->get_job_index()];
			// we now account for mutual exclusion constraints with other jobs
			// calculate max_{i \in Mutx^f(jp) \cup Omega(v)} { FT^max(i) + delay^max(i,jp) }
			for (const auto& excl : constraints.between_executions)
			{
				Interval<Time> ftimes(0, 0);
				// if it has executed then we account for the exclusion constraint
				if( state.get_finish_times(excl.reference_job->get_job_index(), ftimes) )
					ready_time = std::max(ready_time, ftimes.max() + excl.delay.max());
			}
			// if ready_time is already infinity no to check further, we move to the next job
			if (ready_time == Time_model::constants<Time>::infinity())
				continue;

			// calculate max_{i \in Mutx^s(jp) \cup Omega(v)} { ST^max(i) + delay^max(i,jp) }
			for (const auto& excl : constraints.between_starts)
			{
				Interval<Time> stimes(0, 0);
				// if it has started then we account for the constraint
				if( state.get_start_times(excl.reference_job->get_job_index(), stimes) )
					ready_time = std::max(ready_time, stimes.max() + excl.delay.max());
			}

			earliest_certain_mutex_source_job_dispatch =
				std::min(earliest_certain_mutex_source_job_dispatch, ready_time);
		}
	}
	
	/**
	 * @brief Calculate the earliest time a job with precedence constraints will become ready to dispatch.
	 * 
	 * calculate: min_{j \in R^pot(v)} { max_{sib \in CondSibs(j)} {
	 * 									 max{ r^max(sib), A^max_p^min(sib)(v)),
	 * 										  max_{i \in Pred^s(sib)} { ST^max(i) + delay^max(i,sib) }, 
	 * 										  max_{i \in Pred^f(sib)} { FT^max(i) + delay^max(i,sib) }, 
	 * 										  max_{i \in Mutx^s(sib) \cup Omega(v)} { ST^max(i) + delay^max(i,sib) }, 
	 * 										  max_{i \in Mutx^f(sib) \cup Omega(v)} { FT^max(i) + delay^max(i,sib) } } } }
	 * 
	 * @param state The current schedule state
	 * @param ready_succ_jobs The list of ready-successor jobs
	 * @param constraints The list of precedence and mutual exclusion constraints between jobs
	 * @param cond_constr The conditional dispatch constraints (conditional siblings and incompatible jobs)
	 * @param core_avail The core availability intervals
	 */
	void update_earliest_certain_successor_job_dispatch(
		const Schedule_state<Time>& state,
		const std::vector<const Job<Time>*>& ready_succ_jobs,
		const Inter_job_constraints<Time>& constraints,
		const Conditional_dispatch_constraints<Time>& cond_constr,
		const Core_availability_tracker<Time>& core_avail)
	{
		earliest_certain_successor_job_dispatch = Time_model::constants<Time>::infinity();
		// we go through all successor jobs that are ready and update the earliest ready time
		for (const Job<Time>* rj : ready_succ_jobs) {
			// we go through all conditional siblings of rj to find the latest ready time among all siblings
			// note that rj is included in its set of conditional siblings, and if rj does not have conditional siblings
			// then the set only includes rj itself
			Time ready_time = 0;
			for (auto sibling : *cond_constr.get_conditional_siblings(rj->get_job_index())) {			
				Job_index sib_index = sibling->get_job_index();
				// max{ r^max(sib), A^max_p^min(sib)(v))}
				Time avail = core_avail.get_availability(sibling->get_min_parallelism()).max();
				ready_time = std::max(ready_time, std::max(avail, sibling->latest_arrival()));
				// calculate max_{i \in Pred^s(sib)} { ST^max(i) + delay^max(i,sib) }
				for (const auto& pred : constraints[sib_index].predecessors_start_to_start)
				{
					Interval<Time> stimes(0, 0);
					// pred may not have been dispatched if sibling is a conditional join job, in which case we ignore the constraint
					bool has_st = state.get_start_times(pred.reference_job->get_job_index(), stimes);
					if (has_st)
						ready_time = std::max(ready_time, stimes.max() + pred.delay.max());
					else if (sibling->get_type() != Job<Time>::C_JOIN) {
						// if sibling is not a conditional join job and pred has not started yet, then we cannot guarantee that sibling can start
						// so we set ready_time to infinity and break out of the loop
						ready_time = Time_model::constants<Time>::infinity();
						break;
					}
				}
				// if ready_time is already infinity we break out of the loop
				if (ready_time == Time_model::constants<Time>::infinity())
					break;

				// calculate max_{i \in Pred^f(sib)} { FT^max(i) + delay^max(i,sib) }
				for (const auto& pred : constraints[sib_index].predecessors_finish_to_start)
				{
					Interval<Time> ftimes(0, 0);
					// pred may not have been dispatched if sibling is a conditional join job, in which case we ignore the constraint
					bool has_ft = state.get_finish_times(pred.reference_job->get_job_index(), ftimes);
					if (has_ft)
						ready_time = std::max(ready_time, ftimes.max() + pred.delay.max());
					else if (sibling->get_type() != Job<Time>::C_JOIN) {
						// if sibling is not a conditional join job and pred has not finished yet, then we cannot guarantee that sibling can start
						// so we set ready_time to infinity and break out of the loop
						ready_time = Time_model::constants<Time>::infinity();
						break;
					}
				}
				// if ready_time is already infinity we break out of the loop
				if (ready_time == Time_model::constants<Time>::infinity())
					break;

				// we now account for mutual exclusion constraints between sibling and other jobs
				// calculate max_{i \in Mutx^s(sib) \cup Omega(v)} { ST^max(i) + delay^max(i,sib) }
				for (const auto& excl : constraints[sib_index].between_starts)
				{
					Interval<Time> stimes(0, 0);
					// if it has started then we account for the constraint
					if( state.get_start_times(excl.reference_job->get_job_index(), stimes) )
						ready_time = std::max(ready_time, stimes.max() + excl.delay.max());
				}
				// if ready_time is already infinity we break out of the loop
				if (ready_time == Time_model::constants<Time>::infinity())
					break;

				// calculate max_{i \in Mutx^f(sib) \cup Omega(v)} { FT^max(i) + delay^max(i,sib) }
				for (const auto& excl : constraints[sib_index].between_executions)
				{
					Interval<Time> ftimes(0, 0);
					// if it has executed then we account for the exclusion constraint
					if( state.get_finish_times(excl.reference_job->get_job_index(), ftimes) )
						ready_time = std::max(ready_time, ftimes.max() + excl.delay.max());
				}
				// if ready_time is already infinity we break out of the loop
				if (ready_time == Time_model::constants<Time>::infinity())
					break;
			}
			earliest_certain_successor_job_dispatch =
				std::min(earliest_certain_successor_job_dispatch, ready_time);
		}
	}
};

} // namespace Global
} // namespace NP

#endif // GLOBAL_CERTAIN_DISPATCH_TIMES_TRACKER_HPP
