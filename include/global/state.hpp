#ifndef GLOBAL_STATE_HPP
#define GLOBAL_STATE_HPP
#include <algorithm>
#include <cassert>
#include <iostream>
#include <ostream>
#include <set>

#ifdef CONFIG_PARALLEL
#include <tbb/spin_rw_mutex.h>
#include <tbb/spin_mutex.h>
#endif

#include "config.h"
#include "cache.hpp"
#include "index_set.hpp"
#include "jobs.hpp"
#include "statistics.hpp"
#include "util.hpp"
#include "global/state_space_data.hpp"

#ifdef CONFIG_ANALYSIS_EXTENSIONS
#include "global/extension/state_extension.hpp"
#endif // CONFIG_ANALYSIS_EXTENSIONS

namespace NP {

	namespace Global {

		typedef Index_set Job_set;
		typedef std::vector<Job_index> Job_precedence_set;

		template<class Time> class State_space_data;

		template<class Time> class Schedule_node;

		// NOTE: Schedule_state is not thread-safe. Thread-safety must be enforced by callers.
		template<class Time> class Schedule_state
		{
		private:
			typedef typename State_space_data<Time>::Workload Workload;
			typedef const Job<Time>* Job_ref;

			struct Job_interval {
				Job_index job_idx;
				Interval<Time> time_interval;

				Job_interval(Job_index idx, Interval<Time> time_interval)
					: job_idx(idx), time_interval(time_interval)
				{}
			};
			typedef std::vector<Job_interval> Job_intervals;
			typedef std::vector<Interval<Time>> Core_availability;
			typedef Interval<unsigned int> Parallelism;
			typedef typename NP::Global::State_space_data<Time>::Delay_list Delay_list;
			typedef typename NP::Global::State_space_data<Time>::Inter_job_constraints Inter_job_constraints;
			typedef std::vector<Inter_job_constraints> Constraints;

			// system availability this_intervals
			Core_availability core_avail;

			// keeps track of the earliest time a job with at least one predecessor is certainly ready and certainly has enough free cores to start executing
			Time earliest_certain_successor_job_dispatch;

			// keeps track of the earliest time a gang source job (a job with no predecessor that requires more than one core to execute) 
			// is certainly arrived and certainly has enough free cores to start executing
			Time earliest_certain_gang_source_job_dispatch;

			struct Running_job {
				Job_index idx;
				Parallelism parallelism;
				Interval<Time> finish_time;

				Running_job(
					Job_index idx,
					Parallelism parallelism,
					Interval<Time> finish_time
				)
					: idx(idx),
					parallelism(parallelism),
					finish_time(finish_time)
				{
				}
			};

			// imprecise set of certainly running jobs, on how many cores they run, and when they should finish
			std::vector<Running_job> certain_jobs;

			// start times of all the jobs that still have an unscheduled successor with a start_to_start constraint
			Job_intervals job_start_times;
			// finish times of all the jobs that still have an unscheduled successor with a finish_to_start constraint
			Job_intervals job_finish_times;

#ifdef CONFIG_ANALYSIS_EXTENSIONS
			// possible extensions of the state (e.g., for task chain analysis)
			State_extensions<Time> extensions;
#endif // CONFIG_ANALYSIS_EXTENSIONS

			// the job with a priority at least equal to that of the first job disptached after the current state
			Job_ref min_next_prio_job; 

		public:
			// initial state -- nothing yet has finished, nothing is running
			Schedule_state(const unsigned int num_processors, const State_space_data<Time>& state_space_data)
				: core_avail{ num_processors, Interval<Time>(Time(0), Time(0)) }
				, certain_jobs{}
				, earliest_certain_successor_job_dispatch{ Time_model::constants<Time>::infinity() }
				, earliest_certain_gang_source_job_dispatch{ state_space_data.get_earliest_certain_gang_source_job_release() }
				, min_next_prio_job{ NULL }
			{
				assert(core_avail.size() > 0);
#ifdef CONFIG_ANALYSIS_EXTENSIONS
				extensions.construct(*this, num_processors, state_space_data);
#endif // CONFIG_ANALYSIS_EXTENSIONS
			}

			Schedule_state(const std::vector<Interval<Time>>& proc_initial_state, const State_space_data<Time>& state_space_data)
				: core_avail{ proc_initial_state.size() }
				, certain_jobs{}
				, earliest_certain_successor_job_dispatch{ Time_model::constants<Time>::infinity() }
				, earliest_certain_gang_source_job_dispatch{ state_space_data.get_earliest_certain_gang_source_job_release() }
				, min_next_prio_job{ NULL }
			{
				assert(core_avail.size() > 0);
				std::vector<Time> amin, amax;
				amin.reserve(proc_initial_state.size());
				amax.reserve(proc_initial_state.size());
				for (const auto& a : proc_initial_state) {
					amin.push_back(a.min());
					amax.push_back(a.max());
				}
				std::sort(amin.begin(), amin.end());
				std::sort(amax.begin(), amax.end());
				for (unsigned int i = 0; i < proc_initial_state.size(); i++) {
					core_avail[i].extend_to(amax[i]);
					core_avail[i].lower_bound(amin[i]);
				}
#ifdef CONFIG_ANALYSIS_EXTENSIONS
				extensions.construct(*this, proc_initial_state, state_space_data);
#endif // CONFIG_ANALYSIS_EXTENSIONS
			}

			// transition: new state by scheduling a job 'j' in an existing state 'from'
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
			{
				const auto& constraints = state_space_data.inter_job_constraints;
				const Job_precedence_set & finished_predecessors = state_space_data.get_finished_jobs_if_starts(j);
				// update the set of certainly running jobs and
				// get the number of cores certainly used by active predecessors
				int n_prec = update_certainly_running_jobs_and_get_num_prec(from, j, start_times, finish_times, ncores, finished_predecessors);

				// calculate the cores availability this_intervals resulting from dispatching job j on ncores in state 'from'
				update_core_avail(from, j, n_prec, start_times, finish_times, ncores);

				assert(core_avail.size() > 0);

				// save the job start of every job with a successor with a start to start constraint that is not executed yet in the current state
				update_job_start_times(from, j, start_times, finish_times, jobs_with_pending_start_succ);
				// save the job finish of every job with a successor with a finish to start constraint that is not executed yet in the current state
				update_job_finish_times(from, j, start_times, finish_times, jobs_with_pending_finish_succ);

				// NOTE: must be done after the finish times and core availabilities have been updated
				update_earliest_certain_successor_job_dispatch(ready_succ_jobs, constraints);

				// NOTE: must be done after the core availabilities have been updated
				update_earliest_certain_gang_source_job_dispatch(next_source_job_rel, scheduled_jobs, state_space_data);
				
				update_ready_successor_jobs_prio(constraints, ready_succ_jobs, scheduled_jobs);
				
#ifdef CONFIG_ANALYSIS_EXTENSIONS
				extensions.construct(*this, from, j, start_times, finish_times,
					scheduled_jobs, jobs_with_pending_start_succ, jobs_with_pending_finish_succ, ready_succ_jobs,
					state_space_data, next_source_job_rel, ncores);
#endif // CONFIG_ANALYSIS_EXTENSIONS

				DM("*** new state: constructed " << *this << std::endl);
			}

			// initial state -- nothing yet has finished, nothing is running
			void reset(const unsigned int num_processors, const State_space_data<Time>& state_space_data)
			{
				core_avail = Core_availability(num_processors, Interval<Time>(Time(0), Time(0)));
				earliest_certain_successor_job_dispatch = Time_model::constants<Time>::infinity();
				earliest_certain_gang_source_job_dispatch = state_space_data.get_earliest_certain_gang_source_job_release();
				job_start_times.clear();
				job_finish_times.clear();
				min_next_prio_job = NULL;
				assert(core_avail.size() > 0);

#ifdef CONFIG_ANALYSIS_EXTENSIONS
				extensions.reset(*this, num_processors, state_space_data);
#endif // CONFIG_ANALYSIS_EXTENSIONS
			}

			void reset(const std::vector<Interval<Time>>& proc_initial_state, const State_space_data<Time>& state_space_data)
			{
				core_avail = Core_availability(proc_initial_state.size(), Interval<Time>(Time(0), Time(0)));
				earliest_certain_successor_job_dispatch = Time_model::constants<Time>::infinity();
				earliest_certain_gang_source_job_dispatch = state_space_data.get_earliest_certain_gang_source_job_release();
				job_start_times.clear();
				job_finish_times.clear();
				min_next_prio_job = NULL;
				assert(core_avail.size() > 0);
				std::vector<Time> amin, amax;
				amin.reserve(proc_initial_state.size());
				amax.reserve(proc_initial_state.size());
				for (const auto& a : proc_initial_state) {
					amin.push_back(a.min());
					amax.push_back(a.max());
				}
				std::sort(amin.begin(), amin.end());
				std::sort(amax.begin(), amax.end());
				for (unsigned int i = 0; i < proc_initial_state.size(); i++) {
					core_avail[i].extend_to(amax[i]);
					core_avail[i].lower_bound(amin[i]);
				}

#ifdef CONFIG_ANALYSIS_EXTENSIONS
				extensions.reset(*this, proc_initial_state, state_space_data);
#endif // CONFIG_ANALYSIS_EXTENSIONS
			}

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
				certain_jobs.clear();
				int n_prec = update_certainly_running_jobs_and_get_num_prec(from, j, start_times, finish_times, ncores, finished_predecessors);

				// calculate the cores availability this_intervals resulting from dispatching job j on ncores in state 'from'
				core_avail.clear();
				update_core_avail(from, j, n_prec, start_times, finish_times, ncores);

				assert(core_avail.size() > 0);

				// save the job start of every job with a successor with a start to start constraint that is not executed yet in the current state
				job_start_times.clear();
				update_job_start_times(from, j, start_times, finish_times, jobs_with_pending_start_succ);
				// save the job finish of every job with a successor with a finish to start constraint that is not executed yet in the current state
				job_finish_times.clear();
				update_job_finish_times(from, j, start_times, finish_times, jobs_with_pending_finish_succ);

				// NOTE: must be done after the finish times and core availabilities have been updated
				update_earliest_certain_successor_job_dispatch(ready_succ_jobs, constraints);

				// NOTE: must be done after the core availabilities have been updated
				update_earliest_certain_gang_source_job_dispatch(next_source_job_rel, scheduled_jobs, state_space_data);

				update_ready_successor_jobs_prio(state_space_data.inter_job_constraints, ready_succ_jobs, scheduled_jobs);

#ifdef CONFIG_ANALYSIS_EXTENSIONS
				extensions.reset(*this, from, j, start_times, finish_times,
					scheduled_jobs, jobs_with_pending_start_succ, jobs_with_pending_finish_succ, ready_succ_jobs,
					state_space_data, next_source_job_rel, ncores);
#endif // CONFIG_ANALYSIS_EXTENSIONS

				DM("*** new state: constructed " << *this << std::endl);
			}

#ifdef CONFIG_ANALYSIS_EXTENSIONS
			const State_extensions<Time>& get_extensions() const
			{
				return extensions;
			}
#endif // CONFIG_ANALYSIS_EXTENSIONS

			const Core_availability& get_cores_availability() const
			{
				return core_avail;
			}

			Interval<Time> core_availability(unsigned long p = 1) const
			{
				assert(core_avail.size() > 0);
				assert(core_avail.size() >= p);
				assert(p > 0);
				return core_avail[p - 1];
			}

			Time earliest_finish_time() const
			{
				return core_avail[0].min();
			}

			// return true if the finish time interval of the job `j` is known. If so, it writes the finish time interval in `ftimes` 
			bool get_finish_times(Job_index j, Interval<Time>& ftimes) const
			{
				int offset = jft_find(j);
				if (offset < job_finish_times.size() && job_finish_times[offset].job_idx == j)
				{
					ftimes = job_finish_times[offset].time_interval;
					return true;
				}
				else {
					ftimes = Interval<Time>{ 0, Time_model::constants<Time>::infinity() };
					return false;
				}
			}

			Job_ref get_next_dispatched_job_min_priority() const
			{
				return min_next_prio_job;
			}

			bool get_start_times(Job_index j, Interval<Time>& stimes) const
			{
				int offset = jst_find(j);
				if (offset < job_start_times.size() && job_start_times[offset].job_idx == j)
				{
					stimes = job_start_times[offset].time_interval;
					return true;
				}
				else {
					stimes = Interval<Time>{ 0, Time_model::constants<Time>::infinity() };
					return false;
				}
			}

			Time next_certain_gang_source_job_dispatch() const
			{
				return earliest_certain_gang_source_job_dispatch;
			}

			Time next_certain_successor_jobs_dispatch() const
			{
				return earliest_certain_successor_job_dispatch;
			}

			const std::vector<Running_job>& get_cert_running_jobs() const
			{
				return certain_jobs;
			}

			// returns true if the availability inervals of one state overlaps with the other state.
			// Conservative means that all the availability this_intervals of one state must be within 
			// the interval of the other state.
			// If conservative is false, the a simple overlap or contiguity between inverals is enough.
			// If conservative is true, then sets `other_in_this` to true if all availability this_intervals
			// of other are subintervals of this. Otherwise, `other_in_this` is set to false.
			bool core_avail_overlap(const Core_availability& other, bool conservative, bool& other_in_this) const
			{
				assert(core_avail.size() == other.size());
				other_in_this = false;
				// Conservative means that all the availability this_intervals of one state must be within 
				// the interval of the other state.
				// If conservative is false, the a simple overlap or contiguity between inverals is enough
				if (conservative) {
					bool overlap = true;
					// check if all availability this_intervals of other are within the this_intervals of this
					for (int i = 0; i < core_avail.size(); i++) {
						if (!core_avail[i].contains(other[i])) {
							overlap = false;
							break;
						}
					}
					if (overlap == true) {
						other_in_this = true;
						return true;
					}
					// check if all availability this_intervals of this are within the this_intervals of other
					for (int i = 0; i < core_avail.size(); i++) {
						if (!other[i].contains(core_avail[i])) {
							return false;;
						}
					}
				}
				else {
					for (int i = 0; i < core_avail.size(); i++)
						if (!core_avail[i].intersects(other[i]))
							return false;
				}
				return true;
			}

			// check if 'other' state can merge with this state
			bool can_merge_with(const Schedule_state<Time>& other, bool conservative, bool use_job_times = false) const
			{
				bool other_in_this;
				if (core_avail_overlap(other.core_avail, conservative, other_in_this))
				{
					if (use_job_times) {
						return check_intervals_overlap(this->job_start_times, other.job_start_times, conservative, other_in_this) &&
							check_intervals_overlap(this->job_finish_times, other.job_finish_times, conservative, other_in_this);
					} else return true;
				}
				else
					return false;
			}

			// first check if 'other' state can merge with this state, then, if yes, merge 'other' with this state.
			bool try_to_merge(const Schedule_state<Time>& other, bool conservative, bool use_job_times = false)
			{
				if (!can_merge_with(other, conservative, use_job_times))
					return false;

				merge(other.core_avail, other.job_start_times, other.job_finish_times, other.certain_jobs, other.earliest_certain_successor_job_dispatch, other.min_next_prio_job);

#ifdef CONFIG_ANALYSIS_EXTENSIONS
				extensions.merge(*this, other);
#endif // CONFIG_ANALYSIS_EXTENSIONS

				DM("+++ merged " << other << " into " << *this << std::endl);
				return true;
			}

			void merge(
				const Core_availability& cav,
				const Job_intervals& jst,
				const Job_intervals& jft,
				const std::vector<Running_job>& cert_j,
				Time ecsj_ready_time,
				Job_ref min_p_next_job)
			{
				for (int i = 0; i < core_avail.size(); i++)
					core_avail[i] |= cav[i];

				// vector to collect joint certain jobs
				std::vector<Running_job> new_cj;

				// walk both sorted job lists to see if we find matches
				auto it = certain_jobs.begin();
				auto it_end = certain_jobs.end();
				auto jt = cert_j.begin();
				auto jt_end = cert_j.end();
				while (it != it_end &&
					jt != jt_end) {
					if (it->idx == jt->idx) {
						// same job
						new_cj.emplace_back(it->idx, it->parallelism | jt->parallelism, it->finish_time | jt->finish_time);
						it++;
						jt++;
					}
					else if (it->idx < jt->idx)
						it++;
					else
						jt++;
				}
				// move new certain jobs into the state
				certain_jobs.swap(new_cj);

				// merge job_times
				widen_intervals(job_start_times, jst);
				widen_intervals(job_finish_times, jft);

				// update certain ready time of jobs with predecessors
				earliest_certain_successor_job_dispatch = std::max(earliest_certain_successor_job_dispatch, ecsj_ready_time);

				// update the minimum priority job that can be dispatched next
				if (min_next_prio_job == NULL || min_p_next_job == NULL)
					min_next_prio_job = NULL;
				else if (min_next_prio_job->higher_priority_than(*min_p_next_job))
					min_next_prio_job = min_p_next_job;

				DM("+++ merged (cav,jft,cert_t) into " << *this << std::endl);
			}

			// output the state in CSV format
			void export_state (std::ostream& stream, const Workload& jobs)
			{
				stream << "=====State=====\n"
					<< "Cores availability\n";
				// Core availability this_intervals
				for (const auto& a : core_avail)
					stream << "[" << a.min() << "," << a.max() << "]\n";

				stream << "Certainly running jobs: [<task_id>,<job_id>]:[<par_min>,<par_max>],[<ft_min>,<ft_max>]\n";
				// Running jobs: <task_id>,<job_id>,<par_min>,<par_max>,<ft_min>,<ft_max>\n
				for (const auto& rj : certain_jobs) {
					const auto j = jobs[rj.idx];
					stream << "[" << j.get_task_id() << "," << j.get_job_id() << "]:[" << rj.parallelism.min() << "," << rj.parallelism.max() << "],[" << rj.finish_time.min() << "," << rj.finish_time.max() << "]\n";
				}
				stream << "Finish times predecessors: [<task_id>,<job_id>]:[<ft_min>,<ft_max>]\n";
				// Jobs with pending successors: <job_id>,<ft_min>,<ft_max>\n
				for (const auto& jft : job_finish_times) {
					const auto j = jobs[jft.job_idx];
					stream << "[" << j.get_task_id() << "," << j.get_job_id() << "]:[" << jft.time_interval.min() << "," << jft.time_interval.max() << "]\n";
				}
				stream << "Start times predecessors: [<task_id>,<job_id>]:[<st_min>,<st_max>]\n";
				// Jobs with pending successors: <job_id>,<ft_min>,<ft_max>\n
				for (const auto& jst : job_start_times) {
					const auto j = jobs[jst.job_idx];
					stream << "[" << j.get_task_id() << "," << j.get_job_id() << "]:[" << jst.time_interval.min() << "," << jst.time_interval.max() << "]\n";
				}
				stream << "Start times predecessors: [<task_id>,<job_id>]:[<st_min>,<st_max>]\n";
				// Jobs with pending successors: <job_id>,<ft_min>,<ft_max>\n
				for (const auto& jst : job_start_times) {
					const auto j = jobs[jst.job_idx];
					stream << "[" << j.get_task_id() << "," << j.get_job_id() << "]:[" << jst.time_interval.min() << "," << jst.time_interval.max() << "]\n";
				}
			}

		private:

			// update the list of jobs that are certainly running in the current system state 
			// and returns the number of predecessors of job `j` that were certainly running on cores in the previous system state
			int update_certainly_running_jobs_and_get_num_prec(const Schedule_state& from,
				Job_index j, Interval<Time> start_times,
				Interval<Time> finish_times, unsigned int ncores,
				const Job_precedence_set& finished_predecessors)
			{
				certain_jobs.reserve(from.certain_jobs.size() + 1);

				Time lst = start_times.max();
				int n_prec = 0;

				// update the set of certainly running jobs
				// keep them sorted to simplify merging
				bool added_j = false;
				for (const auto& rj : from.certain_jobs)
				{
					auto running_job = rj.idx;
					if (contains(finished_predecessors, running_job))
					{
						n_prec += rj.parallelism.min(); // keep track of the number of finished predecessors of j that are certainly running
					}
					else if (lst < rj.finish_time.min())
					{
						if (!added_j && running_job > j)
						{
							// right place to add j
							Parallelism p(ncores, ncores);
							certain_jobs.emplace_back(j, p, finish_times);
							added_j = true;
						}
						certain_jobs.emplace_back(rj);
					}
				}
				// if we didn't add it yet, add it at the back
				if (!added_j)
				{
					Parallelism p(ncores, ncores);
					certain_jobs.emplace_back(j, p, finish_times);
				}

				return n_prec;
			}

			// update the core availability resulting from scheduling job j on m cores in state 'from'
			void update_core_avail(const Schedule_state& from, const Job_index j, 
				int n_prec, const Interval<Time> start_times, 
				const Interval<Time> finish_times, const unsigned int m)
			{
				int n_cores = from.core_avail.size();
				core_avail.reserve(n_cores);

				auto est = start_times.min();
				auto lst = start_times.max();
				auto eft = finish_times.min();
				auto lft = finish_times.max();

				// Stack allocation threshold - use stack for small core counts to avoid heap allocations
				constexpr int STACK_ALLOCATION_THRESHOLD = 64;
				
				Time ca_stack[STACK_ALLOCATION_THRESHOLD];
				Time pa_stack[STACK_ALLOCATION_THRESHOLD];
				
				Time* ca;
				Time* pa;
				std::unique_ptr<Time[]> ca_heap;
				std::unique_ptr<Time[]> pa_heap;
				
				// Use stack allocation for small arrays, heap allocation for larger ones
				if (n_cores <= STACK_ALLOCATION_THRESHOLD) {
					ca = ca_stack;
					pa = pa_stack;
				} else {
					ca_heap = std::make_unique<Time[]>(n_cores);
					pa_heap = std::make_unique<Time[]>(n_cores);
					ca = ca_heap.get();
					pa = pa_heap.get();
				}

				unsigned int ca_idx = 0, pa_idx = 0;

				// Keep pa and ca sorted, by adding the value at the correct place.
				bool eft_added_to_pa = false;
				bool lft_added_to_ca = false;

				// note, we must skip the first ncores elements in from.core_avail
				if (n_prec > m) {
					// if there are n_prec finished predecessors running, n_prec cores must be available when j starts
					for (int i = m; i < n_prec; i++) {
						pa[pa_idx] = est; pa_idx++; //pa.push_back(est); // TODO: GN: check whether we can replace by est all the time since predecessors must possibly be finished by est to let j start
						ca[ca_idx] = std::min(lst, std::max(est, from.core_avail[i].max())); ca_idx++; //ca.push_back(std::min(lst, std::max(est, from.core_avail[i].max())));
					}
				}
				else {
					n_prec = m;
				}
				for (int i = n_prec; i < from.core_avail.size(); i++) {
					if (!eft_added_to_pa && eft < from.core_avail[i].min())
					{
						// add the finish time of j ncores times since it runs on ncores
						for (int p = 0; p < m; p++) {
							pa[pa_idx] = eft; pa_idx++; //pa.push_back(eft);
						}
						eft_added_to_pa = true;
					}
					pa[pa_idx] = std::max(est, from.core_avail[i].min()); pa_idx++; //pa.push_back(std::max(est, from.core_avail[i].min()));
					if (!lft_added_to_ca && lft < from.core_avail[i].max()) {
						// add the finish time of j ncores times since it runs on ncores
						for (int p = 0; p < m; p++) {
							ca[ca_idx] = lft; ca_idx++; //ca.push_back(lft);
						}
						lft_added_to_ca = true;
					}
					ca[ca_idx] = std::max(est, from.core_avail[i].max()); ca_idx++; //ca.push_back(std::max(est, from.core_avail[i].max()));
				}
				if (!eft_added_to_pa) {
					// add the finish time of j ncores times since it runs on ncores
					for (int p = 0; p < m; p++) {
						pa[pa_idx] = eft; pa_idx++; //pa.push_back(eft);
					}
				}
				if (!lft_added_to_ca) {
					// add the finish time of j ncores times since it runs on ncores
					for (int p = 0; p < m; p++) {
						ca[ca_idx] = lft; ca_idx++; //ca.push_back(lft);
					}
				}

				for (int i = 0; i < from.core_avail.size(); i++)
				{
					DM(i << " -> " << pa[i] << ":" << ca[i] << std::endl);
					core_avail.emplace_back(pa[i], ca[i]);
				}
			}

			// finds the earliest time a gang source job (i.e., a job without predecessors that requires more than one core to start executing)
			// is certainly released and has enough cores available to start executing at or after time `after`
			void update_earliest_certain_gang_source_job_dispatch(
				Time after,
				const Job_set& scheduled_jobs,
				const State_space_data<Time>& state_space_data)
			{
				earliest_certain_gang_source_job_dispatch = Time_model::constants<Time>::infinity();

				for (auto it = state_space_data.gang_source_jobs_by_latest_arrival.lower_bound(after);
					it != state_space_data.gang_source_jobs_by_latest_arrival.end(); it++)
				{
					const Job<Time>* jp = it->second;
					if (jp->latest_arrival() >= earliest_certain_gang_source_job_dispatch)
						break;

					// skip if the job was dispatched already
					if (scheduled_jobs.contains(jp->get_job_index()))
						continue;

					// it's incomplete and not ignored 
					earliest_certain_gang_source_job_dispatch = std::min(earliest_certain_gang_source_job_dispatch,
						std::max(jp->latest_arrival(),
							core_availability(jp->get_min_parallelism()).max()));
				}
			}

			// update the list of finish times of jobs with pending successors with a finish_to_start constraint
			void update_job_finish_times(const Schedule_state& from,
				Job_index j, Interval<Time> start_times,
				Interval<Time> finish_times,
				const std::vector<Job_index>& jobs_with_pending_succ)
			{
				Time lst = start_times.max();
				Time lft = finish_times.max();

				bool single_core = (core_avail.size() == 1);

				job_finish_times.reserve(jobs_with_pending_succ.size());

				auto it = from.job_finish_times.begin();
				for (Job_index job : jobs_with_pending_succ)
				{
					if (job == j)
						job_finish_times.emplace_back(job, finish_times);
					else {
						// we find the finish time interval of `job` from the previous state. 
						// Note that if `job` has non-completed successors in the new state,
						// it must have had non-completed successors in the previous state too, 
						// thus there is no risk to reach the end iterator
						while (it->job_idx != job) {
							assert(it != from.job_finish_times.end());
							it++;
						}
						Time job_eft = it->time_interval.min();
						Time job_lft = it->time_interval.max();
						// if there is a single core, then we know that 
						// jobs that were dispatched in the past cannot have 
						// finished later than when our new job starts executing
						if (single_core)
						{
							if (job_lft > lst)
								job_lft = lst;
						}
						
						job_finish_times.emplace_back(job, Interval<Time>{ job_eft, job_lft });
					}
				}
			}

			// update the list of start times of jobs with pending successors with a start_to_start constraint
			void update_job_start_times(const Schedule_state& from,
				Job_index j, Interval<Time> start_times,
				Interval<Time> finish_times,
				const std::vector<Job_index>& jobs_with_pending_succ)
			{
				Time lst = start_times.max();
				Time lft = finish_times.max();

				job_start_times.reserve(jobs_with_pending_succ.size());

				auto it = from.job_start_times.begin();
				for (Job_index job : jobs_with_pending_succ)
				{
					if (job == j) {
						job_start_times.emplace_back(job, start_times);
					} else {
						// we find the time interval of `job` from the previous state.
						// Note that if `job` has non-completed successors in the new state,
						// it must have had non-completed successors in the previous state too, 
						// thus there is no risk to reach the end iterator
						while (it->job_idx != job)
							it++;
						Time job_et = it->time_interval.min();
						Time job_lt = it->time_interval.max();
						job_start_times.emplace_back(job, Interval<Time>{ job_et, job_lt });
					}
				}
			}

			//calculate the earliest time a job with precedence constraints will become ready to dispatch
			void update_earliest_certain_successor_job_dispatch(
				const std::vector<const Job<Time>*>& ready_succ_jobs,
				const Constraints& constraints)
			{
				earliest_certain_successor_job_dispatch = Time_model::constants<Time>::infinity();
				// we go through all successor jobs that are ready and update the earliest ready time
				for (const Job<Time>* rj : ready_succ_jobs) {
					Time avail = core_avail[rj->get_min_parallelism() - 1].max();
					Time ready_time = std::max(avail, rj->latest_arrival());
					for (const auto& pred : constraints[rj->get_job_index()].predecessors_start_to_start)
					{
						Interval<Time> stimes(0, 0);
						get_start_times(pred.reference_job->get_job_index(), stimes);
						ready_time = std::max(ready_time, stimes.max() + pred.delay.max());
					}
					for (const auto& pred : constraints[rj->get_job_index()].predecessors_finish_to_start)
					{
						Interval<Time> ftimes(0, 0);
						get_finish_times(pred.reference_job->get_job_index(), ftimes);
						ready_time = std::max(ready_time, ftimes.max() + pred.delay.max());
					}
					for (const auto& excl : constraints[rj->get_job_index()].between_starts)
					{
						Interval<Time> stimes(0, 0);
						// if it has started then we account for the constraint
						if( get_start_times(excl.reference_job->get_job_index(), stimes) )
							ready_time = std::max(ready_time, stimes.max() + excl.delay.max());
					}
					for (const auto& excl : constraints[rj->get_job_index()].between_executions)
					{
						Interval<Time> ftimes(0, 0);
						// if it has executed then we account for the exclusion constraint
						if( get_finish_times(excl.reference_job->get_job_index(), ftimes) )
							ready_time = std::max(ready_time, ftimes.max() + excl.delay.max());
					}
					earliest_certain_successor_job_dispatch =
						std::min(earliest_certain_successor_job_dispatch, ready_time);
				}
			}

			// Assume: `j` was dispatched already
			// returns true if `j` is certainly finished at least `delay` time units before when the first core becomes available
			bool certainly_finished(Job_ref j, Time delay, const Constraints& delay_constraints, const Job_set& scheduled_jobs) const
			{
				// if there is only one core, then the job is certainly finished if it was dispatched already
				if (core_avail.size() == 1 && delay == 0)
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

			// Assume: j was dispatched already
			// returns true if j is certainly started at least delay time units before the first core becomes available
			bool certainly_started(Job_ref j, Time delay) const
			{
				// if the job is certainly dispatched, then it is certainly started too
				if (delay == 0)
					return true;
				// check if the job certainly started delay time units before the first core becomes available
				Interval<Time> st{ 0, 0 };
				bool has_st = get_start_times(j->get_job_index(), st);
				assert(has_st);

				// check if j certainly started delay time units before the first core may become available
				if (st.max() + delay <= core_availability(1).min())
					return true;

				return false;
			}

			// Assume: j is not dispatched yet and all its predecessors were dispatched already
			// returns true if the job j is certainly arrived when the first core becomes available at the earliest (r_j^min <= A_1^min)
			// and all its predecedence constraints are certainly fulfilled when the first core becomes available (i.e., they finished/started before the first core
			// becomes available at the earliest or they are certainly running on the first core that becomes available)
			bool certainly_ready(Job_ref j, const Constraints& delay_constraints, const Job_set& scheduled_jobs) const
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

			void update_ready_successor_jobs_prio(
				const Constraints& constraints,
				const std::vector<Job_ref>& ready_succ_jobs,
				const Job_set& scheduled_jobs)
			{
				min_next_prio_job = NULL;
				int num_cpus = core_avail.size();
				//ready_successor_jobs_prio.reserve(num_cpus);
				std::vector<Job_ref> predecessors;
				predecessors.reserve(4 * num_cpus);
				// we must find num_cpus jobs that will be ready as soon as their predecessors complete.
				int c = num_cpus;
				int rem_rjobs = ready_succ_jobs.size();

				for (auto j : ready_succ_jobs) {
					// skip gang jobs that require more than one core
					if (j->get_min_parallelism() > 1) {
						rem_rjobs--;
						continue;
					}
					// if the job is certainly ready, we know it will be ready when the first core becomes free
					if (certainly_ready(j, constraints, scheduled_jobs)) {
						min_next_prio_job = j;
						// since ready_succ_jobs is sorted in decreasing priority order, 
						// we found the highest priority ready job, no need to continue
						return;
					}
					else if (rem_rjobs >= c) {
						// check if it has predecessors that are also predecessors of another ready job
						// and if j certainly arrives before all its predecessors finish 
						const auto& pred_j = constraints[j->get_job_index()].predecessors_finish_to_start;
						bool overlap = false;
						bool arrives_before_finish = false;
						bool suspending = false;
						for (const auto& p : pred_j) {
							if (p.delay.max() > 0) {
								suspending = true;
								break;
							}
							auto jp = p.reference_job;
							if (std::find(predecessors.begin(), predecessors.end(), jp) != predecessors.end()) {
								overlap = true;
								break;
							}
							if (!arrives_before_finish) {
								Interval<Time> ftimes(0, 0);
								if (get_finish_times(jp->get_job_index(), ftimes) && ftimes.min() >= j->latest_arrival()) {
									// if the predecessor is not finished before j arrives, there is no delay between jp finish time and j's ready time
									arrives_before_finish = true;
								}
							}
						}
						if (!overlap && !suspending) {
							const auto& s_pred_j = constraints[j->get_job_index()].predecessors_start_to_start;
							for (const auto& p : s_pred_j) {
								if (p.delay.max() > 0) {
									suspending = true;
									break;
								}
								auto jp = p.reference_job;
								if (std::find(predecessors.begin(), predecessors.end(), jp) != predecessors.end()) {
									overlap = true;
									break;
								}
								if (!arrives_before_finish) {
									Interval<Time> stimes(0, 0);
									if (get_start_times(jp->get_job_index(), stimes) && stimes.min() >= j->latest_arrival()) {
										// if the predecessor is not started before j arrives, there is no delay between jp start time and j's ready time
										arrives_before_finish = true;
									}
								}
							}
						}
						if (!overlap && !suspending) {
							const auto& between_starts = constraints[j->get_job_index()].between_starts;
							for (const auto& p : between_starts) {
								if (!scheduled_jobs.contains(p.reference_job->get_job_index()))
									continue;
								if (p.delay.max() > 0) {
									suspending = true;
									break;
								}
								auto jp = p.reference_job;
								if (std::find(predecessors.begin(), predecessors.end(), jp) != predecessors.end()) {
									overlap = true;
									break;
								}
								if (!arrives_before_finish) {
									Interval<Time> stimes(0, 0);
									if (get_start_times(jp->get_job_index(), stimes) && stimes.min() >= j->latest_arrival()) {
										// if the predecessor is not started before j arrives, there is no delay between jp start time and j's ready time
										arrives_before_finish = true;
									}
								}
							}
						}
						if (!overlap && !suspending) {
							const auto& between_executions = constraints[j->get_job_index()].between_executions;
							for (const auto& p : between_executions) {
								if (!scheduled_jobs.contains(p.reference_job->get_job_index()))
									continue;
								if (p.delay.max() > 0) {
									suspending = true;
									break;
								}
								auto jp = p.reference_job;
								if (std::find(predecessors.begin(), predecessors.end(), jp) != predecessors.end()) {
									overlap = true;
									break;
								}
								if (!arrives_before_finish) {
									Interval<Time> ftimes(0, 0);
									if (get_finish_times(jp->get_job_index(), ftimes) && ftimes.min() >= j->latest_arrival()) {
										// if the predecessor is not finished before j arrives, there is no delay between jp finish time and j's ready time
										arrives_before_finish = true;
									}
								}
							}
						}

						if (!overlap && !suspending && arrives_before_finish) {
							c--;
							// record the predecessors of j if they have more than one successor
							for (const auto& p : pred_j) {
								auto jp = p.reference_job;
								if (constraints[jp->get_job_index()].start_to_successors_start.size() + constraints[jp->get_job_index()].finish_to_successors_start.size() > 1)
									predecessors.push_back(jp);
							}
							// if we already have num_cpus job that will certainly be ready when a core becomes free, 
							// then we know one of those job will be ready when the first job becoes free. 
							// In the worst-case, it is the lowest priority job.
							if (c == 0) {
								min_next_prio_job = j;
								// since ready_succ_jobs is sorted in decreasing priority order, 
								// we found our job, no need to continue
								return;
							}
						}
					}
					rem_rjobs--;
				}
			}

			// Check whether the job_start_times or job_finish_times overlap.
			bool check_intervals_overlap (const Job_intervals& this_times, const Job_intervals& other_times,
					bool conservative = false, const bool other_in_this = false) const 
			{
				bool all_jobs_intersect = true;
				// The Job_times vectors are sorted.
				// Check intersect for matching jobs.
				auto other_it = other_times.begin();
				auto state_it = this_times.begin();
				while (other_it != other_times.end() &&
					state_it != this_times.end())
				{
					if (other_it->job_idx == state_it->job_idx)
					{
						if (conservative) {
							if (other_in_this == false && !other_it->time_interval.contains(state_it->time_interval))
							{
								all_jobs_intersect = false; // not all the time this_intervals of this are within those of other
								break;
							}
							else if (other_in_this == true && !state_it->time_interval.contains(other_it->time_interval))
							{
								all_jobs_intersect = false; // not all the time this_intervals of other are within those of this
								break;
							}
						}
						else {
							if (!other_it->time_interval.intersects(state_it->time_interval))
							{
								all_jobs_intersect = false;
								break;
							}
						}
						other_it++;
						state_it++;
					}
					else if (conservative)
						return false; // the list of finish time intervals do not match
					else if (other_it->job_idx < state_it->job_idx)
						other_it++;
					else
						state_it++;
				}
				return all_jobs_intersect;
			}

			void widen_intervals(Job_intervals& this_intervals, const Job_intervals& other_intervals)
			{
				// The Job_times vectors are sorted.
				// Assume check_overlap() is true.
				auto from_it = other_intervals.begin();
				auto state_it = this_intervals.begin();
				while (from_it != other_intervals.end() &&
					   state_it != this_intervals.end())
				{
					if (from_it->job_idx == state_it->job_idx)
					{
						state_it->time_interval.widen(from_it->time_interval);
						from_it++;
						state_it++;
					}
					else if (from_it->job_idx < state_it->job_idx)
						from_it++;
					else
						state_it++;
				}
			}

			// Find the offset in the Job_finish_times vector where the index j should be located.
			int jft_find(const Job_index j) const
			{
				int start = 0;
				int end = job_finish_times.size();
				while (start < end) {
					int mid = (start + end) / 2;
					if (job_finish_times[mid].job_idx == j)
						return mid;
					else if (job_finish_times[mid].job_idx < j)
						start = mid + 1;  // mid is too small, mid+1 might fit.
					else
						end = mid;
				}
				return start;
			}

			// Find the offset in the Job_start_times vector where the index j should be located.
			int jst_find(const Job_index j) const
			{
				int start = 0;
				int end = job_start_times.size();
				while (start < end) {
					int mid = (start + end) / 2;
					if (job_start_times[mid].job_idx == j)
						return mid;
					else if (job_start_times[mid].job_idx < j)
						start = mid + 1;  // mid is too small, mid+1 might fit.
					else
						end = mid;
				}
				return start;
			}

			// no accidental copies
			Schedule_state(const Schedule_state& origin) = delete;
		};

		template<class Time> class Schedule_node
		{
		private:
			typedef typename State_space_data<Time>::Workload Workload;
			typedef Schedule_state<Time> State;
			typedef std::shared_ptr<State> State_ref;
			typedef typename std::vector<Interval<Time>> Core_availability;
			typedef typename State_space_data<Time>::Delay_list Delay_list;
			typedef std::vector<Delay_list> Successors;
			typedef std::vector<Delay_list> Predecessors;
			typedef typename State_space_data<Time>::Inter_job_constraints Inter_job_constraints;
			typedef std::vector<Inter_job_constraints> Constraints;

			Time earliest_pending_release;
			Time next_certain_successor_jobs_dispatch;
			Time next_certain_source_job_release;
			Time next_certain_sequential_source_job_release;
			Time next_certain_gang_source_job_dispatch;

			Job_set scheduled_jobs;
			// set of jobs that have all their predecessors completed and were not dispatched yet
			std::vector<const Job<Time>*> ready_successor_jobs;
			std::vector<Job_index> jobs_with_pending_start_succ;
			std::vector<Job_index> jobs_with_pending_finish_succ;

			hash_value_t lookup_key;
			Interval<Time> finish_time;
			Time a_max;
			unsigned int num_cpus;
			unsigned int num_jobs_scheduled;

#ifdef CONFIG_PARALLEL
			// Thread-safe state container and synchronization
			mutable tbb::spin_rw_mutex states_mutex;
#endif

			// no accidental copies
			Schedule_node(const Schedule_node& origin) = delete;

			struct eft_compare
			{
				bool operator() (State_ref x, State_ref y) const
				{
					return x->earliest_finish_time() < y->earliest_finish_time();
				}
			};

			typedef typename std::multiset<State_ref, eft_compare> State_ref_queue;
			State_ref_queue states;

		public:

			// initial node (for convenience for unit tests)
			Schedule_node(unsigned int num_cores)
				: lookup_key{ 0 }
				, num_cpus(num_cores)
				, finish_time{ 0,0 }
				, a_max{ 0 }
				, num_jobs_scheduled(0)
				, earliest_pending_release{ 0 }
				, next_certain_source_job_release{ Time_model::constants<Time>::infinity() }
				, next_certain_successor_jobs_dispatch{ Time_model::constants<Time>::infinity() }
				, next_certain_sequential_source_job_release{ Time_model::constants<Time>::infinity() }
				, next_certain_gang_source_job_dispatch{ Time_model::constants<Time>::infinity() }
			{
			}

			// initial node
			Schedule_node(unsigned int num_cores, const State_space_data<Time>& state_space_data)
				: lookup_key{ 0 }
				, num_cpus(num_cores)
				, finish_time{ 0,0 }
				, a_max{ 0 }
				, num_jobs_scheduled(0)
				, earliest_pending_release{ state_space_data.get_earliest_job_arrival() }
				, next_certain_successor_jobs_dispatch{ Time_model::constants<Time>::infinity() }
				, next_certain_sequential_source_job_release{ state_space_data.get_earliest_certain_seq_source_job_release() }
				, next_certain_gang_source_job_dispatch{ Time_model::constants<Time>::infinity() }
			{
				next_certain_source_job_release = std::min(next_certain_sequential_source_job_release, state_space_data.get_earliest_certain_gang_source_job_release());
			}

			Schedule_node(const std::vector<Interval<Time>>& proc_initial_state, const State_space_data<Time>& state_space_data)
				: lookup_key{ 0 }
				, num_cpus(proc_initial_state.size())
				, finish_time{ 0, 0 }
				, a_max{ Time_model::constants<Time>::infinity() }
				, num_jobs_scheduled(0)
				, earliest_pending_release{ state_space_data.get_earliest_job_arrival() }
				, next_certain_successor_jobs_dispatch{ Time_model::constants<Time>::infinity() }
				, next_certain_sequential_source_job_release{ state_space_data.get_earliest_certain_seq_source_job_release() }
				, next_certain_gang_source_job_dispatch{ Time_model::constants<Time>::infinity() }
			{
				Time a_min = Time_model::constants<Time>::infinity();
				for (const auto& a : proc_initial_state) {
					a_max = std::min(a_max, a.max());
					a_min = std::min(a_min, a.min());
				}
				finish_time.extend_to(a_max);
				finish_time.lower_bound(a_min);

				next_certain_source_job_release = std::min(next_certain_sequential_source_job_release, state_space_data.get_earliest_certain_gang_source_job_release());
			}


			// transition: new node by scheduling a job 'j' in an existing node 'from'
			Schedule_node(
				const Schedule_node& from,
				const Job<Time>& j,
				std::size_t idx,
				const State_space_data<Time>& state_space_data,
				const Time next_earliest_release,
				const Time next_certain_source_job_release, // the next time a job without predecessor is certainly released
				const Time next_certain_sequential_source_job_release // the next time a job without predecessor that can execute on a single core is certainly released
			)
				: scheduled_jobs{ from.scheduled_jobs, idx }
				, lookup_key{ from.next_key(j) }
				, num_cpus(from.num_cpus)
				, num_jobs_scheduled(from.num_jobs_scheduled + 1)
				, finish_time{ 0, Time_model::constants<Time>::infinity() }
				, a_max{ Time_model::constants<Time>::infinity() }
				, earliest_pending_release{ next_earliest_release }
				, next_certain_source_job_release{ next_certain_source_job_release }
				, next_certain_successor_jobs_dispatch{ Time_model::constants<Time>::infinity() }
				, next_certain_sequential_source_job_release{ next_certain_sequential_source_job_release }
				, next_certain_gang_source_job_dispatch{ Time_model::constants<Time>::infinity() }
			{
				update_ready_successors(from, idx, state_space_data.inter_job_constraints, this->scheduled_jobs);
				update_jobs_with_pending_succ(from, idx, state_space_data.inter_job_constraints, this->scheduled_jobs);
			}

			void reset(const std::vector<Interval<Time>>& proc_initial_state, const State_space_data<Time>& state_space_data)
			{
#ifdef CONFIG_PARALLEL
				tbb::spin_rw_mutex::scoped_lock lock(states_mutex, true); // write lock
#endif
				states.clear();

				lookup_key = 0;
				num_cpus = proc_initial_state.size();
				finish_time = { 0,0 };
				a_max = Time_model::constants<Time>::infinity();
				Time a_min = Time_model::constants<Time>::infinity();
				for (const auto& a : proc_initial_state) {
					a_max = std::min(a_max, a.max());
					a_min = std::min(a_min, a.min());
				}
				finish_time.extend_to(a_max);
				finish_time.lower_bound(a_min);

				scheduled_jobs.clear();
				num_jobs_scheduled = 0;
				jobs_with_pending_start_succ.clear();
				jobs_with_pending_finish_succ.clear();
				earliest_pending_release = state_space_data.get_earliest_job_arrival();
				next_certain_successor_jobs_dispatch = Time_model::constants<Time>::infinity();
				next_certain_sequential_source_job_release = state_space_data.get_earliest_certain_seq_source_job_release();
				next_certain_gang_source_job_dispatch = Time_model::constants<Time>::infinity();
				next_certain_source_job_release = std::min(next_certain_sequential_source_job_release, state_space_data.get_earliest_certain_gang_source_job_release());
			}

			void reset(unsigned int num_cores, const State_space_data<Time>& state_space_data)
			{
#ifdef CONFIG_PARALLEL
				tbb::spin_rw_mutex::scoped_lock lock(states_mutex, true); // write lock
#endif
				states.clear();

				lookup_key = 0;
				num_cpus = num_cores;
				finish_time = { 0,0 };
				a_max = 0;
				scheduled_jobs.clear();
				num_jobs_scheduled = 0;
				jobs_with_pending_start_succ.clear();
				jobs_with_pending_finish_succ.clear();
				earliest_pending_release = state_space_data.get_earliest_job_arrival();
				next_certain_successor_jobs_dispatch = Time_model::constants<Time>::infinity();
				next_certain_sequential_source_job_release = state_space_data.get_earliest_certain_seq_source_job_release();
				next_certain_gang_source_job_dispatch = Time_model::constants<Time>::infinity();
				next_certain_source_job_release = std::min(next_certain_sequential_source_job_release, state_space_data.get_earliest_certain_gang_source_job_release());
			}

			// transition: new node by scheduling a job 'j' in an existing node 'from'
			void reset(
				const Schedule_node& from,
				const Job<Time>& j,
				std::size_t idx,
				const State_space_data<Time>& state_space_data,
				const Time next_earliest_release,
				const Time next_certain_source_job_release, // the next time a job without predecessor is certainly released
				const Time next_certain_sequential_source_job_release // the next time a job without predecessor that can execute on a single core is certainly released
			)
			{
#ifdef CONFIG_PARALLEL
				tbb::spin_rw_mutex::scoped_lock lock(states_mutex, true); // write lock
#endif
				states.clear();
				scheduled_jobs.set(from.scheduled_jobs, idx);
				lookup_key = from.next_key(j);
				num_cpus = from.num_cpus;
				num_jobs_scheduled = from.num_jobs_scheduled + 1;
				finish_time = { 0, Time_model::constants<Time>::infinity() };
				a_max = Time_model::constants<Time>::infinity();
				earliest_pending_release = next_earliest_release;
				this->next_certain_source_job_release = next_certain_source_job_release;
				next_certain_successor_jobs_dispatch = Time_model::constants<Time>::infinity();
				this->next_certain_sequential_source_job_release = next_certain_sequential_source_job_release;
				ready_successor_jobs.clear();
				jobs_with_pending_start_succ.clear();
				jobs_with_pending_finish_succ.clear();
				next_certain_gang_source_job_dispatch = Time_model::constants<Time>::infinity();
				update_ready_successors(from, idx, state_space_data.inter_job_constraints, this->scheduled_jobs);
				update_jobs_with_pending_succ(from, idx, state_space_data.inter_job_constraints, this->scheduled_jobs);
			}

			const unsigned int number_of_scheduled_jobs() const
			{
				return num_jobs_scheduled;
			}

			Time earliest_job_release() const
			{
				return earliest_pending_release;
			}

			Time get_next_certain_source_job_release() const
			{
				return next_certain_source_job_release;
			}

			Time get_next_certain_sequential_source_job_release() const
			{
				return next_certain_sequential_source_job_release;
			}

			Time next_certain_job_ready_time() const
			{
				return std::min(next_certain_successor_jobs_dispatch,
					std::min(next_certain_sequential_source_job_release,
						next_certain_gang_source_job_dispatch));
			}

			const std::vector<const Job<Time>*>& get_ready_successor_jobs() const
			{
				return ready_successor_jobs;
			}

			const std::vector<Job_index>& get_jobs_with_pending_start_successors() const
			{
				return jobs_with_pending_start_succ;
			}

			const std::vector<Job_index>& get_jobs_with_pending_finish_successors() const
			{
				return jobs_with_pending_finish_succ;
			}

			hash_value_t get_key() const
			{
				return lookup_key;
			}

			const Job_set& get_scheduled_jobs() const
			{
				return scheduled_jobs;
			}

			const bool job_not_dispatched(Job_index j) const
			{
				return !scheduled_jobs.contains(j);
			}

			const bool job_dispatched(Job_index j) const
			{
				return scheduled_jobs.contains(j);
			}

			bool is_ready(const Job_index j, const Inter_job_constraints& constraints) const
			{
				for (const auto& pred : constraints.predecessors_start_to_start)
				{
					if (!scheduled_jobs.contains(pred.reference_job->get_job_index()))
						return false;
				}
				for (const auto& pred : constraints.predecessors_finish_to_start)
				{
					if (!scheduled_jobs.contains(pred.reference_job->get_job_index()))
						return false;
				}
				return true;
			}

			bool matches(const Schedule_node& other) const
			{
				return lookup_key == other.lookup_key &&
					scheduled_jobs == other.scheduled_jobs;
			}

			hash_value_t next_key(const Job<Time>& j) const
			{
				return get_key() ^ j.get_key();
			}

			//  finish_range / finish_time contains information about the
			//     earliest and latest core availability for core 0.
			//     whenever a state is changed (through merge) or added,
			//     that interval should be adjusted.
			Interval<Time> finish_range() const
			{
				return finish_time;
			}

			Time latest_core_availability() const
			{
				return a_max;
			}


			void add_state(State_ref s)
			{
#ifdef CONFIG_PARALLEL
				tbb::spin_rw_mutex::scoped_lock lock(states_mutex, true); // write lock
#endif
				update_internal_variables(s);
				states.insert(s);
			}

			// export the node information to the stream
			void export_node (std::ostream& stream, const Workload& jobs)
			{
				stream << "=====Node=====\n"
					<< "Ready successors: [[<task_id>,<job_id>], ...]\n"
					<< "[";
                // Ready successor jobs: [<task_id>,<job_id>]
				int i = 0;
                for (const auto* job : ready_successor_jobs) {
                    stream << "[" << job->get_task_id() << "," << job->get_job_id() << "]";
					++i;
					if (i < ready_successor_jobs.size()) 
						stream << ",";
                }
				stream << "]\n"
					<< "Scheduled jobs: [[<task_id>,<job_id>], ...]\n"
					<< "[";
                // Scheduled jobs: <task_id>,<job_id>\n
                // We need to iterate over n.scheduled_jobs.
				i = 0;
				for (int idx = 0; idx < scheduled_jobs.size(); ++idx) {
					if (scheduled_jobs.contains(idx)) {
						const auto& j = jobs[idx];
						stream << "[" << j.get_task_id() << "," << j.get_job_id() << "]";
						i++;
						if (i < num_jobs_scheduled) 
							stream << ",";
					}
                }
				stream << "]\n";
            }

			//return the number of states in the node
			int states_size() const
			{
#ifdef CONFIG_PARALLEL
				tbb::spin_rw_mutex::scoped_lock lock(states_mutex, false); // read lock
#endif
				return states.size();
			}

			const State_ref get_first_state() const
			{
#ifdef CONFIG_PARALLEL
				tbb::spin_rw_mutex::scoped_lock lock(states_mutex, false); // read lock
#endif
				auto first = states.begin();
				return *first;
			}

			const State_ref get_last_state() const
			{
#ifdef CONFIG_PARALLEL
				tbb::spin_rw_mutex::scoped_lock lock(states_mutex, false); // read lock
#endif
				auto last = --(states.end());
				return *last;
			}

			const State_ref_queue* get_states() const
			{
#ifdef CONFIG_PARALLEL
				// Note: This method returns a pointer to the internal container,
				// which is inherently not thread-safe. The caller must ensure
				// proper synchronization when accessing the returned pointer.
				// Consider using states_size() and iterator methods instead.
#endif
				return &states;
			}

			// try to merge state 's' with up to 'budget' states already recorded in this node. 
			// The option 'conservative' allow a merge of two states to happen only if the availability 
			// this_intervals of one state are constained in the availability this_intervals of the other state. If
			// the conservative option is used, the budget parameter is ignored.
			// The option 'use_job_finish_times' controls whether or not the job finish time this_intervals of jobs 
			// with pending successors must overlap to allow two states to merge. Setting it to true should 
			// increase accurracy of the analysis but increases runtime significantly.
			// The 'budget' defines how many states can be merged at once. If 'budget = -1', then there is no limit. 
			// Returns the number of existing states the new state was merged with.
			int merge_states(const Schedule_state<Time>& s, bool conservative, bool use_job_finish_times = false, int budget = 1)
			{
#ifdef CONFIG_PARALLEL
				tbb::spin_rw_mutex::scoped_lock lock(states_mutex, true); // write lock
#endif
				// if we do not use a conservative merge, try to merge with up to 'budget' states if possible.
				int merge_budget = conservative ? 1 : budget;

				State_ref last_state_merged;
				bool result = false;
				for (auto it = states.begin(); it != states.end();)
				{
					State_ref state = *it;
					if (result == false)
					{
						if (state->try_to_merge(s, conservative, use_job_finish_times))
						{
							// Update the node finish_time
							finish_time.widen(s.core_availability());
							a_max = std::max(a_max, s.core_availability(num_cpus).max());
							//update the certain next job ready time
							next_certain_successor_jobs_dispatch = std::max(next_certain_successor_jobs_dispatch, s.next_certain_successor_jobs_dispatch());
							next_certain_gang_source_job_dispatch = std::max(next_certain_gang_source_job_dispatch, s.next_certain_gang_source_job_dispatch());

							result = true;

							// Try to merge with a few more states.
							merge_budget--;
							if (merge_budget == 0)
								break;

							last_state_merged = state;
						}
						++it;
					}
					else // if we already merged with one state at least
					{
						if (last_state_merged->try_to_merge(*state, conservative, use_job_finish_times))
						{
							// the state was merged => we can thus remove the old one from the list of states
							it = states.erase(it);

							// Try to merge with a few more states.
							// std::cerr << "Merged with " << merge_budget << " of " << states.size() << " states left.\n";
							merge_budget--;
							if (merge_budget == 0)
								break;
						}
						else
							++it;
					}
				}

				if (conservative)
					return result ? 1 : 0;
				else
					return (budget - merge_budget);
			}

		private:
			void update_internal_variables(const State_ref& s)
			{
				Interval<Time> ft = s->core_availability();
				if (states.empty()) {
					finish_time = ft;
					a_max = s->core_availability(num_cpus).max();
					next_certain_successor_jobs_dispatch = s->next_certain_successor_jobs_dispatch();
					next_certain_gang_source_job_dispatch = s->next_certain_gang_source_job_dispatch();
				}
				else {
					finish_time.widen(ft);
					a_max = std::max(a_max, s->core_availability(num_cpus).max());
					next_certain_successor_jobs_dispatch = std::max(next_certain_successor_jobs_dispatch, s->next_certain_successor_jobs_dispatch());
					next_certain_gang_source_job_dispatch = std::max(next_certain_gang_source_job_dispatch, s->next_certain_gang_source_job_dispatch());
				}
			}

			// update the list of jobs that have all their predecessors completed and were not dispatched yet
			// Assume: successors_of[j] is sorted in non-increasing priority order
			void update_ready_successors(const Schedule_node& from,
				Job_index j, const Constraints& constraints,
				const Job_set& scheduled_jobs)
			{
				ready_successor_jobs.reserve(from.ready_successor_jobs.size() + constraints[j].start_to_successors_start.size() +
						constraints[j].finish_to_successors_start.size());

				// update the list of ready successor jobs by keeping it sorted in non-increasing priority order
				auto it_old = from.ready_successor_jobs.begin();
				auto it_old_end = from.ready_successor_jobs.end();
				while (it_old != it_old_end && (*it_old)->get_job_index() == j)
					++it_old; // skip if it is the job we just dispatched

				auto it_start_succ = constraints[j].start_to_successors_start.begin();
				auto it_start_succ_end = constraints[j].start_to_successors_start.end();

				auto it_finish_succ = constraints[j].finish_to_successors_start.begin();
				auto it_finish_succ_end = constraints[j].finish_to_successors_start.end();

				auto get_next_ready_job = [&](auto& it, const auto it_end) {
					while (it != it_end)
					{
						if (is_ready(it->reference_job->get_job_index(), constraints[it->reference_job->get_job_index()]))
							break;
						++it;
					}
				};

				get_next_ready_job(it_start_succ, it_start_succ_end);
				get_next_ready_job(it_finish_succ, it_finish_succ_end);

				// merge the three sorted lists of ready jobs
				while (it_old != it_old_end || it_start_succ != it_start_succ_end || it_finish_succ != it_finish_succ_end)
				{
					const Job<Time>* next_job = nullptr;
					unsigned int next_source = 0; // 0 = old, 1 = start_succ, 2 = finish_succ
					if (it_old != it_old_end) {
						next_job = *it_old;
					}
					if (it_start_succ != it_start_succ_end) {
						if (next_job == nullptr || it_start_succ->reference_job->higher_priority_than(*next_job)) {
							next_source = 1;
							next_job = it_start_succ->reference_job;
						}
					}
					if (it_finish_succ != it_finish_succ_end) {
						if (next_job == nullptr || it_finish_succ->reference_job->higher_priority_than(*next_job)) {
							next_source = 2;
							next_job = it_finish_succ->reference_job;
						}
					}

					ready_successor_jobs.push_back(next_job);
					switch (next_source)
					{
						case 0: // old
							++it_old;
							while (it_old != it_old_end && (*it_old)->get_job_index() == j)
								++it_old; // skip if it is the job we just dispatched
							break;
						case 1: // start_succ
							++it_start_succ;
							get_next_ready_job(it_start_succ, it_start_succ_end);
							break;
						case 2: // finish_succ
							++it_finish_succ;
							get_next_ready_job(it_finish_succ, it_finish_succ_end);
					}
				}				
			}

			// update the list of jobs with non-dispatched successors 
			void update_jobs_with_pending_succ(const Schedule_node& from,
				Job_index j, const std::vector<Inter_job_constraints>& constraints,
				const Job_set& scheduled_jobs)
			{
				jobs_with_pending_start_succ.reserve(from.jobs_with_pending_start_succ.size() + 1);
				jobs_with_pending_finish_succ.reserve(from.jobs_with_pending_finish_succ.size() + 1);

				// we only need to add j to jobs_with_pending_start_succ if it has successors with start to start constraints
				bool added_j = constraints[j].start_to_successors_start.empty();
				if (added_j) {
					for (const auto& excl : constraints[j].between_starts) {
						// if it has started then we account for the constraint
						if (!scheduled_jobs.contains(excl.reference_job->get_job_index()))
						{
							added_j = false;
							break;
						}
					}
				}
				for (Job_index job : from.jobs_with_pending_start_succ)
				{
					if (!added_j && job > j)
					{
						jobs_with_pending_start_succ.push_back(j);
						added_j = true;
					}

					bool successor_pending = false;
					for (const auto& succ : constraints[job].start_to_successors_start) {
						auto to_job = succ.reference_job->get_job_index();
						if (!scheduled_jobs.contains(to_job))
						{
							successor_pending = true;
							break;
						}
					}
					if (successor_pending)
						jobs_with_pending_start_succ.push_back(job);
				}

				if (!added_j)
					jobs_with_pending_start_succ.push_back(j);

				// we only need to add j to jobs_with_pending_finish_succ if it has successors with start to start constraints
				added_j = constraints[j].finish_to_successors_start.empty();
				if (added_j) {
					for (const auto& excl : constraints[j].between_executions) {
						// if it has started then we account for the constraint
						if (!scheduled_jobs.contains(excl.reference_job->get_job_index()))
						{
							added_j = false;
							break;
						}
					}
				}
				for (Job_index job : from.jobs_with_pending_finish_succ)
				{
					if (!added_j && job > j)
					{
						jobs_with_pending_finish_succ.push_back(j);
						added_j = true;
					}

					bool successor_pending = false;
					for (const auto& succ : constraints[job].finish_to_successors_start) {
						auto to_job = succ.reference_job->get_job_index();
						if (!scheduled_jobs.contains(to_job))
						{
							successor_pending = true;
							break;
						}
					}
					if (successor_pending)
						jobs_with_pending_finish_succ.push_back(job);
				}

				if (!added_j)
					jobs_with_pending_finish_succ.push_back(j);
			}
		};

	}
}

#endif