#ifndef GLOBAL_STATE_HPP
#define GLOBAL_STATE_HPP
#include <algorithm>
#include <cassert>
#include <iostream>
#include <ostream>
#include <numeric>

#include <set>

#include "config.h"
#include "cache.hpp"
#include "index_set.hpp"
#include "jobs.hpp"
#include "statistics.hpp"
#include "util.hpp"
#include "global/state_space_data.hpp"
#include "global/node.hpp"

#ifdef CONFIG_PARALLEL
#include <tbb/mutex.h>
#endif

namespace NP {

	namespace Global {

		typedef Index_set Job_set;
		typedef std::vector<Job_index> Job_precedence_set;

		template<class Time> class Schedule_state
		{
		private:

			typedef const Subtask<Time>* Subtask_ref;
			typedef typename Schedule_node<Time>::Subtask_set Subtask_set;
			typedef typename Task<Time>::Predecessors Predecessors;
			typedef typename Task<Time>::Successors Successors;

			typedef std::vector<std::vector<Interval<Time>>> Subtask_finish_times;
			typedef std::vector<std::vector<Interval<Time>>> Subtask_start_times;
			typedef std::vector<std::vector<Interval<Time>>> Subtask_arrival_times;
			typedef std::vector<Interval<Time>> Core_availability;
			typedef Interval<unsigned int> Parallelism;

			// system availability intervals
			Core_availability core_avail;

			// keeps track of the earliest time a job with at least one predecessor is certainly ready and certainly has enough free cores to start executing
			Time earliest_certain_job_disptach;

			struct Running_subtask {
				Task_index task;
				Subtask_index subtask;
				Parallelism parallelism;
				Interval<Time> finish_time;

				Running_subtask(Task_index task, Subtask_index subtask, Parallelism parallelism, Interval<Time> finish_time)
					: task(task), subtask(subtask), parallelism(parallelism), finish_time(finish_time) {}
			};

			// imprecise set of certainly running subtasks, on how many cores they run, and when they should finish
			std::vector<Running_subtask> certain_subtasks;

			// subtask_finish_times holds the finish times of the last dispatched job of every subtask of every task
			Subtask_finish_times subtask_finish_times;
			// subtask_start_times holds the start times of the last dispatched job of every subtask of every task
			Subtask_start_times subtask_start_times;
			// subtask_release_times holds the release times of the last job released by every subtask
			Subtask_arrival_times subtask_release_times;

			typedef Time Priority;
			// for each job `j` in `jobs_with_pending_succ`, `ready_successor_jobs_prio` contains the highest-priority job that is certainly ready right after `j` completes its execution
			std::vector<Subtask_ref> ready_successor_jobs_prio;
			// the job with a priority at least equal to that of the first job disptached after the current state
			Subtask_ref min_next_prio_job;

		public:

			// initial state -- nothing yet has finished, nothing is running
			Schedule_state(const unsigned int num_processors, const State_space_data<Time>& state_space_data)
				: core_avail{ num_processors, Interval<Time>(Time(0), Time(0)) }
				, certain_subtasks{}
				, earliest_certain_job_disptach{ Time_model::constants<Time>::infinity() }
				, min_next_prio_job{ NULL }
				, subtask_finish_times{ state_space_data.num_tasks() }
				, subtask_start_times{ state_space_data.num_tasks() }
				, subtask_release_times{ state_space_data.num_tasks() }
			{
				assert(core_avail.size() > 0);
				for (int i = 0; i < state_space_data.num_tasks(); i++) {
					const Task<Time>& t = state_space_data.tasks[i];

					subtask_finish_times[i].resize(t.num_subtasks(), Interval<Time>(Time(0), Time(0)));
					subtask_start_times[i].resize(t.num_subtasks(), Interval<Time>(Time(0), Time(0)));
					subtask_release_times[i].reserve(t.num_subtasks());
					Interval<Time> release_offset = t.get_release_offset();
					Interval<Time> release_time = Interval<Time>{ release_offset.min(), release_offset.max() + t.get_release_jitter() };
					for (Subtask_ref s : t.get_subtasks()) {
						subtask_release_times[i].push_back(release_time + s->get_release_offset() );
					}
				}
			}

			// transition: new state by scheduling a subtask 'j' in an existing state 'from'
			Schedule_state(
				const Schedule_state& from,
				const Subtask<Time>& j,
				const Interval<Time>& start_times,
				const Interval<Time>& finish_times,
				const Subtask_set& scheduled_subtasks,
				const std::vector< std::vector<Subtask_ref>>& ready_subtasks,
				const State_space_data<Time>& state_space_data,
				unsigned int ncores = 1)
				: subtask_finish_times{ from.subtask_finish_times }
				, subtask_start_times{ from.subtask_start_times }
				, subtask_release_times{ from.subtask_release_times }
			{
				const Task<Time>& t = state_space_data.tasks[j.task_id()];
				const Successors& successors = t.get_successors();
				const Predecessors& predecessors = t.get_predecessors();
				const Job_precedence_set& predecessors_of_j = t.get_predecessors_of(j.id());
				// update the set of certainly running jobs and
				// get the number of cores certainly used by active predecessors
				int n_prec = update_certainly_running_jobs_and_get_num_prec(from, j, start_times, finish_times, ncores, predecessors_of_j);

				// calculate the cores availability intervals resulting from dispatching `j` on ncores in state 'from'
				update_core_avail(from, j, n_prec, start_times, finish_times, ncores);

				assert(core_avail.size() > 0);

				// save the finish time interval of every dispatched subtask
				update_rel_start_and_finish_times(from, j, t, start_times, finish_times, jobs_with_pending_succ);

				// NOTE: must be done after the finish times and core availabilities have been updated
				update_earliest_certain_job_dispatch(ready_subtasks, predecessors);

				update_ready_successor_jobs_prio(from, j, finish_times, successors, predecessors, scheduled_jobs);

				assert(ready_successor_jobs_prio.size() <= ready_succ_jobs.size());

				DM("*** new state: constructed " << *this << std::endl);
			}

			// initial state -- nothing yet has finished, nothing is running
			void reset(const unsigned int num_processors, const State_space_data<Time>& state_space_data)
			{
				core_avail = Core_availability(num_processors, Interval<Time>(Time(0), Time(0)));
				earliest_certain_job_disptach = Time_model::constants<Time>::infinity();
				min_next_prio_job = NULL;
				ready_successor_jobs_prio.clear();
				assert(core_avail.size() > 0);

				for (int i = 0; i < state_space_data.num_tasks(); i++) {
					const Task<Time>& t = state_space_data.tasks[i];

					subtask_finish_times[i].assign( t.num_subtasks(), Interval<Time>(Time(0), Time(0)) );
					subtask_start_times[i].assign( t.num_subtasks(), Interval<Time>(Time(0), Time(0)) );
					Interval<Time> release_offset = t.get_release_offset();
					Interval<Time> release_time = Interval<Time>{ release_offset.min(), release_offset.max() + t.get_release_jitter() };
					for (Subtask_ref s : t.get_subtasks()) {
						subtask_release_times[i][s->id()] = release_time + s->get_release_offset();
					}
				}
			}

			void reset(
				const Schedule_state& from,
				const Subtask<Time>& j,
				const Interval<Time>& start_times,
				const Interval<Time>& finish_times,
				const Subtask_set& scheduled_subtasks,
				const std::vector< std::vector<Subtask_ref>>& ready_subtasks,
				const State_space_data<Time>& state_space_data,
				unsigned int ncores = 1)
			{
				subtask_finish_times = from.subtask_finish_times;
				subtask_start_times = from.subtask_start_times;
				task_release_times = from.subtask_release_times;

				const Task<Time>& t = state_space_data.tasks[j.task_id()];
				const Successors& successors = t.get_successors();
				const Predecessors& predecessors = t.get_predecessors();
				const Job_precedence_set& predecessors_of_j = t.get_predecessors_of(j.id());
				// update the set of certainly running jobs and
				// get the number of cores certainly used by active predecessors
				certain_jobs.clear();
				int n_prec = update_certainly_running_jobs_and_get_num_prec(from, j, start_times, finish_times, ncores, predecessors_of_j);

				// calculate the cores availability intervals resulting from dispatching job j on ncores in state 'from'
				core_avail.clear();
				update_core_avail(from, j, predecessors_of_j, n_prec, start_times, finish_times, ncores);

				assert(core_avail.size() > 0);

				// save the job finish time of every job with a successor that is not executed yet in the current state
				update_rel_start_and_finish_times(from, j, t, start_times, finish_times, jobs_with_pending_succ);

				// NOTE: must be done after the finish times and core availabilities have been updated
				update_earliest_certain_job_disptach(ready_succ_jobs, predecessors_of);

				ready_successor_jobs_prio.clear();
				update_ready_successor_jobs_prio(from, state_space_data.jobs[j], finish_times, state_space_data.successors_suspensions, state_space_data.predecessors_suspensions, scheduled_jobs);

				assert(ready_successor_jobs_prio.size() <= ready_succ_jobs.size());

				DM("*** new state: constructed " << *this << std::endl);
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
					ftimes = job_finish_times[offset].finish_time;
					return true;
				}
				else {
					ftimes = Interval<Time>{ 0, Time_model::constants<Time>::infinity() };
					return false;
				}
			}

			// return the finish time interval of the last dispatched job of subtask `j` 
			Interval<Time> get_finish_times(Task_index i, Subtask_index j) const
			{
				return subtask_finish_times[i][j];
			}

			// return the start time interval of the last dispatched job of subtask `j` 
			Interval<Time> get_start_times(Task_index i, Subtask_index j) const
			{
				return subtask_start_times[i][j];
			}

			// return the release time interval of the next job of subtask `j` 
			Interval<Time> get_release_times(Task_index i, Subtask_index j) const
			{
				return subtask_release_times[i][j];
			}

			Subtask_ref get_next_dispatched_job_min_priority() const
			{
				return min_next_prio_job;
			}

			Time next_certain_job_disptach() const
			{
				return earliest_certain_job_disptach;
			}

			// returns true if the availability inervals of one state overlaps with the other state.
			// Conservative means that all the availability intervals of one state must be within 
			// the interval of the other state.
			// If conservative is false, the a simple overlap or contiguity between inverals is enough.
			// If conservative is true, then sets `other_in_this` to true if all availability intervals
			// of other are subintervals of this. Otherwise, `other_in_this` is set to false.
			bool core_avail_overlap(const Core_availability& other, bool conservative, bool& other_in_this) const
			{
				assert(core_avail.size() == other.size());
				other_in_this = false;
				// Conservative means that all the availability intervals of one state must be within 
				// the interval of the other state.
				// If conservative is false, the a simple overlap or contiguity between inverals is enough
				if (conservative) {
					bool overlap = true;
					// check if all availability intervals of other are within the intervals of this
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
					// check if all availability intervals of this are within the intervals of other
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
			bool can_merge_with(const Schedule_state<Time>& other, bool conservative, bool use_job_finish_times = false) const
			{
				bool other_in_this;
				if (core_avail_overlap(other.core_avail, conservative, other_in_this))
				{
					if (use_job_finish_times)
						return check_finish_times_overlap(other.subtask_finish_times, conservative, other_in_this);
					else
						return true;
				}
				else
					return false;
			}

			bool can_merge_with(const Core_availability& cav, const Subtask_finish_times& jft, bool conservative, bool use_job_finish_times = false) const
			{
				if (core_avail_overlap(cav, conservative))
				{
					if (use_job_finish_times)
						return check_finish_times_overlap(jft, conservative);
					else
						return true;
				}
				else
					return false;
			}

			// first check if 'other' state can merge with this state, then, if yes, merge 'other' with this state.
			bool try_to_merge(const Schedule_state<Time>& other, bool conservative, bool use_job_finish_times = false)
			{
				if (!can_merge_with(other, conservative, use_job_finish_times))
					return false;

				merge(other.core_avail, other.subtask_finish_times, other.certain_subtasks, other.earliest_certain_job_disptach);

				DM("+++ merged " << other << " into " << *this << std::endl);
				return true;
			}

			void merge(
				const Core_availability& cav,
				const Subtask_finish_times& jft,
				const std::vector<Running_subtask>& cert_j,
				Time ecsj_ready_time)
			{
				for (int i = 0; i < core_avail.size(); i++)
					core_avail[i] |= cav[i];

				// vector to collect joint certain jobs
				std::vector<Running_subtasks> new_cj;

				// walk both sorted job lists to see if we find matches
				auto it = certain_subtasks.begin();
				auto it_end = certain_subtasks.end();
				auto jt = cert_j.begin();
				auto jt_end = cert_j.end();
				while (it != it_end &&
					jt != jt_end) {
					if (it->task == jt->task && it->subtask == jt->subtask) {
						// same job
						new_cj.emplace_back(it->task, it->subtask, it->parallelism | jt->parallelism, it->finish_time | jt->finish_time);
						it++;
						jt++;
					}
					else if (it->task < jt->task || (it->task == jt->task && it->subtask < jt->subtask))
						it++;
					else
						jt++;
				}
				// move new certain jobs into the state
				certain_subtasks.swap(new_cj);

				// merge job_finish_times
				widen_finish_times(jft);

				// update certain ready time of jobs with predecessors
				earliest_certain_job_disptach = std::max(earliest_certain_job_disptach, ecsj_ready_time);

				DM("+++ merged (cav,jft,cert_t) into " << *this << std::endl);
			}

			friend std::ostream& operator<< (std::ostream& stream,
				const Schedule_state<Time>& s)
			{
				stream << "Global::State(";
				for (const auto& a : s.core_avail)
					stream << "[" << a.from() << ", " << a.until() << "] ";
				stream << "(";
				for (const auto& rj : s.certain_subtasks)
					stream << rj.task << "," << rj.subtask << "; ";
				stream << ") " << ")";
				stream << " @ " << &s;
				return stream;
			}

			void print_vertex_label(std::ostream& out,
				const typename Job<Time>::Job_set& jobs) const
			{
				for (const auto& a : core_avail)
					out << "[" << a.from() << ", " << a.until() << "] ";
				out << "\\n";
				bool first = true;
				out << "{";
				for (const auto& rj : certain_subtasks) {
					if (!first)
						out << ", ";
					out << "T" << rj.task
						<< "S" << rj.subtask << ":"
						<< rj.finish_time.min() << "-" << rj.finish_time.max();
					first = false;
				}
				out << "}";
			}

		private:
			// update the list of subtasks that are certainly running in the current system state 
			// and returns the number of predecessors of subtask `j` that were certainly running on cores in the previous system state
			int update_certainly_running_jobs_and_get_num_prec(const Schedule_state& from,
				const Subtask<Time>& j, Interval<Time> start_times,
				Interval<Time> finish_times, unsigned int ncores,
				const Predecessors& predecessors_of_j)
			{
				certain_subtasks.reserve(from.certain_subtasks.size() + 1);

				Time lst = start_times.max();
				int n_prec = 0;

				// update the set of certainly running subtasks
				// keep them sorted to simplify merging
				bool added_j = false;
				for (const Running_subtask& rj : from.certain_subtasks)
				{
					if (rj.task == j.task_id() && predecessors_of_j.contains(rj.subtask))
					{
						n_prec += rj.parallelism.min(); // keep track of the number of predecessors of j that are certainly running
					}
					else if (lst < rj.finish_time.min())
					{
						if (!added_j && 
							(rj.task > j.task_id() 
								|| (rj.task == j.task_id() && rj.subtask > j.id())))
						{
							// right place to add j
							Parallelism p(ncores, ncores);
							certain_subtasks.emplace_back(j.task_id(), j.id(), p, finish_times);
							added_j = true;
						}
						certain_subtasks.emplace_back(rj);
					}
				}
				// if we didn't add it yet, add it at the back
				if (!added_j)
				{
					Parallelism p(ncores, ncores);
					certain_subtasks.emplace_back(j.task_id(), j.id(), p, finish_times);
				}

				return n_prec;
			}

			// update the core availability resulting from scheduling subtask j on m cores in state 'from'
			void update_core_avail(const Schedule_state& from, const Subtask<Time>& j, int n_prec, 
				const Interval<Time> start_times, const Interval<Time> finish_times, const unsigned int m)
			{
				int n_cores = from.core_avail.size();
				core_avail.reserve(n_cores);

				auto est = start_times.min();
				auto lst = start_times.max();
				auto eft = finish_times.min();
				auto lft = finish_times.max();


				// compute the cores availability intervals
				Time* ca = new Time[n_cores];
				Time* pa = new Time[n_cores];
				unsigned int ca_idx = 0, pa_idx = 0;

				// Keep pa and ca sorted, by adding the value at the correct place.
				bool eft_added_to_pa = false;
				bool lft_added_to_ca = false;

				// note, we must skip the first `m` cores elements in `from.core_avail` 
				// because they will be used by subtask `j`
				if (n_prec > m) {
					// if there are n_prec predecessors running, n_prec cores must be available when j starts
					for (int i = m; i < n_prec; i++) {
						pa[pa_idx] = est; pa_idx++; 
						ca[ca_idx] = std::min(lst, std::max(est, from.core_avail[i].max())); ca_idx++; 
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
				delete[] pa;
				delete[] ca;
			}

			// update the list of finish times w.r.t. the previous system state
			void update_rel_start_and_finish_times(const Schedule_state& from,
				const Subtask<Time>& j, const Task<Time>& t, 
				Interval<Time> start_times,	Interval<Time> finish_times)
			{
				Time lst = start_times.max();
				Time lft = finish_times.max();

				bool single_core = (core_avail.size() == 1);

				// update the finish time of the subtask `j` in the new state
				subtask_finish_times[j.task_id()][j.id()] = finish_times;
				// update the start time of the subtask `j` in the new state
				subtask_finish_times[j.task_id()][j.id()] = start_times;
				// update the release time of the subtask `j` based on its start time, i.e., if it started it must be released
				subtask_release_times[j.task_id()][j.id()].upper_bound(lst);
				// record the release time of the next job of 'j'
				subtask_release_times[j.task_id()][j.id()] += t.get_inter_arrival_time();

				// jobs that were disptached in the past must have started 
				// at the latest when our new job starts executing
				for (int i = 0; i++; i < subtask_start_times.size())
				{
					for (const Interval<Time>& st : subtask_start_times[i])
						st.upper_bound(lst);
				}

				// if there is a single core, then we know that 
				// jobs that were disptached in the past cannot have 
				// finished later than when our new job starts executing
				if (core_avail.size() == 1) {
					for (int i = 0; i++; i < subtask_finish_times.size())
					{
						for (const Interval<Time>& ft : subtask_finish_times[i])
							ft.upper_bound(lst);
					}
				}
			}

			//calculate the earliest time a job with precedence constraints will become ready to dispatch
			void update_earliest_certain_job_dispatch(
				const std::vector<Subtask_ref>& ready_subtasks,
				const Predecessors& predecessors_of)
			{
				earliest_certain_job_disptach = Time_model::constants<Time>::infinity();
				// we go through all successor jobs that are ready and update the earliest ready time
				for (Subtask_ref rj : ready_subtasks) {
					Time avail = core_avail[rj->get_min_parallelism() - 1].max();
					Time ready_time = std::max(avail, subtask_arrival_times[rj->task_id()][rj->id].max());
					const Predecessors& predecessors = predecessors_of[rj->id()];
					for (const auto& pred : predecessors.start_before_start)
					{
						Subtask_ref from_job = pred.subtask;
						Time lst = subtask_start_times[from_job->task_id()][from_job->id()].max();
						ready_time = std::max(ready_time, lst + pred.delay.max());
					}
					for (const auto& pred : predecessors.finish_before_start)
					{
						Subtask_ref from_job = pred.subtask;
						Time lft = subtask_finish_times[from_job->task_id()][from_job->id()].max();
						ready_time = std::max(ready_time, lft + pred.delay.max());
					}
					for (const auto& pred : predecessors.exclusions)
					{
						Subtask_ref from_job = pred.subtask;
						Time lft = subtask_finish_times[from_job->task_id()][from_job->id()].max();
						if (lft > 0)
							ready_time = std::max(ready_time, lft + pred.delay.max());
					}
					earliest_certain_job_disptach =
						std::min(earliest_certain_successor_job_disptach, ready_time);
				}
			}

			// checks that `pred` is the only predecessor of `succ` that is not certainly finished
			// or that `succ` is the only successor of all succ's predecessors (i.e., 
			// the sum of all the successors of all predecessors of `succ` is equal to 1)
			/*bool succ_ready_right_after_pred(const Job_index pred, const Job_index succ, const Interval<Time>& finish_times, const Successors& successors_of, const Predecessors& predecessors_of, const Job_set& scheduled_jobs)
			{
				// if there is only one core then there is at most one job that is not certainly finished
				if (core_avail.size() == 1)
					return true;

				// if `succ` has a single predecessors, then at most one predecessor may not be finished
				if (predecessors_of[succ].size() == 1)
					return true;

				// check if all other predecessors of `succ` are certainly finished or that they have no other successors than `succ`
				for (const auto& p : predecessors_of[succ])
				{
					Job_index j_idx = p.first->get_job_index();
					if (j_idx != pred)
					{
						// if `p` was not dispatched yet, then `succ` cannot be ready
						if (!scheduled_jobs.contains(j_idx))
							return false;

						//  if `p` has a single successor and zero delay between the completion of `p` and time `succ` becomes ready, then we disregard `p` 
						if (successors_of[j_idx].size() == 1 && p.second.max() == 0)
							continue;

						// if `p` is certainly finished and the max delay until `succ` is ready is certainly elapsed before `pred` is finished, then we disregard `p`
						Interval<Time> ftimes_p(0, 0);
						if (get_finish_times(j_idx, ftimes_p))
						{
							if (ftimes_p.max() + p.second.max() <= finish_times.min())
								continue;
						}
						else if (p.second.max() > 0)
							return false;

						// If at least one successor of `p` has already been dispatched, then `p` must have finished already.
						bool is_finished = false;
						for (const auto& succ_of_pred : successors_of[j_idx])
						{
							if (scheduled_jobs.contains(succ_of_pred.first->get_job_index())) {
								is_finished = true;
								break;
							}
						}
						if (not is_finished)
							return false;
					}
				}
				return true;
			}*/

			void update_ready_successor_jobs_prio(const Schedule_state& from,
				const Subtask<Time>& j_dispatched,
				const Interval<Time>& j_disp_finish_times,
				const std::vector<Successors>& successors_of,
				const std::vector<Predecessors>& predecessors_of,
				const Subtask_set& scheduled_subtasks)
			{
				/*ready_successor_jobs_prio.reserve(from.ready_successor_jobs_prio.size() + 1);
				Job_index j_dispatched_idx = j_dispatched.get_job_index();

				Job_ref job_to_insert = NULL;

				// find the highest priority successor of `j_dispatched` that will be ready as soon as `j_dispatched` finishes its execution
				for (const auto& s : successors_of[j_dispatched_idx]) {
					Job_index succ_id = s.first->get_job_index();

					// if `s` was not dispatched yet, can execute on a single core, is ready right after `j_dispatched` finishes, and has no other predecessor than `j_dispatched` or all other predecessors certainly finished
					if (!scheduled_jobs.contains(succ_id)
						&& s.first->get_min_parallelism() == 1
						&& s.second.max() == 0 && s.first->latest_arrival() <= j_disp_finish_times.min()
						&& succ_ready_right_after_pred(j_dispatched_idx, succ_id, j_disp_finish_times, successors_of, predecessors_of, scheduled_jobs))
					{
						if (job_to_insert == NULL || s.first->higher_priority_than(*job_to_insert)) {
							job_to_insert = s.first;
						}
					}
				}

				// copy the ready successor jobs priorities, remove the job `j` that was just scheduled and add the prio of the successor job of `j`
				for (Job_ref sp : from.ready_successor_jobs_prio) {
					Job_index sp_idx = sp->get_job_index();
					if (sp_idx != j_dispatched_idx) {
						if (job_to_insert != NULL) {
							// if the job to insert is already in the list, we do not insert it a second time
							if (sp == job_to_insert)
								job_to_insert = NULL;

							if (job_to_insert->higher_priority_than(*sp)) {
								ready_successor_jobs_prio.push_back(job_to_insert);
								job_to_insert = NULL;
							}
						}
						ready_successor_jobs_prio.push_back(sp);
					}
				}

				if (job_to_insert != NULL)
					ready_successor_jobs_prio.push_back(job_to_insert);

				int num_cpus = core_avail.size();

				// calculate the minimum priority of the new job to be dispatched
				if (ready_successor_jobs_prio.size() < num_cpus)
					min_next_prio_job = NULL;
				else
					min_next_prio_job = ready_successor_jobs_prio[num_cpus - 1];*/
			}

			// Check whether the job_finish_times overlap.
			bool check_finish_times_overlap(const Subtask_finish_times& other_ft, bool conservative = false, const bool other_in_this = false) const
			{
				for (int i = 0; i < subtask_finish_times.size(); i++) {
					for (int j = 0; j < subtask_finish_times[i].size(); j++) {
						if (conservative) {
							if (other_in_this == false && !from_pwj[i][j].contains(subtask_finish_times[i][j]))
								return false; // not all the finish time intervals of this are within those of other
							else if (other_in_this == true && !subtask_finish_times[i][j].contains(from_pwj[i][j]))
								return false; // not all the finish time intervals of other are within those of this
						}
						else {
							if (!from_pwj[i][j].intersects(subtask_finish_times[i][j]))
								return false;
						}
					}
				}
				return true;
			}

			void widen_finish_times(const Subtask_finish_times& from_pwj)
			{
				for (int i = 0; i < subtask_finish_times.size(); i++) {
					for (int j = 0; j < subtask_finish_times[i].size(); j++) {
						subtask_finish_times[i][j].widen(from_pwj[i][j]);
					}
				}
			}

			// no accidental copies
			Schedule_state(const Schedule_state& origin) = delete;
		};
	}
}

#endif