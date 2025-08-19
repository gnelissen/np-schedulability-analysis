#ifndef STATE_EXT_TASKCHAINS_HPP
#define STATE_EXT_TASKCHAINS_HPP
#include "global/extension/state_extension.hpp"
#include "global/extension/taskchains/taskchains_sp_data_extension.hpp"
#include "global/extension/taskchains/taskchains.hpp"
#include "jobs.hpp"
#include "index_set.hpp"

#include <iostream>
#include <vector>

namespace NP {
	namespace Global {
		namespace Taskchains_analysis {

#define INVALID -1

			template<class Time>
			class Taskchains_state_extension : public State_extension<Time>
			{
				template<class Time> struct TCD {
					std::vector<Time> DA_max, RT_max, EST_prev; // per task chain
					std::vector<std::vector<Time>> EIT_Reac_int, EIT_Age_int, LIT_int; // per task per task chain
					std::vector<std::vector<Time>> EIT_Reac_out, EIT_Age_out, LIT_out; // per task per task chain
				};

				struct Running_job {
					Job_index idx;
					Interval<Time> finish_time;

					Running_job(
						Job_index idx,
						Interval<Time> finish_time
					)
						: idx(idx),
						finish_time(finish_time)
					{
					}
				};
				// set of possibly running jobs, used for taskchain analysis
				std::vector<Running_job> possible_jobs;
				Index_set tasks_with_running_jobs;

				// task chain analysis
				TCD<Time> tc_data;

			public:
				Taskchains_state_extension() = default;
				
				// default constructor for initial state
				void construct(const size_t extension_id,
					const size_t state_space_data_ext_id,
					const Schedule_state<Time>& new_state,
					const unsigned int num_processors,
					const State_space_data<Time>& state_space_data) override
				{
					// initialize the task chain data structure
					auto spd_ext = state_space_data.get_extensions().get<Taskchains_sp_data_extension<Time>>(state_space_data_ext_id);
					init_task_chain_data_structure(spd_ext->get_task_chains(), state_space_data.get_num_cpus() > 1);
				}

				void construct(const size_t extension_id,
					const size_t state_space_data_ext_id,
					const Schedule_state<Time>& new_state,
					const std::vector<Interval<Time>>& proc_initial_state,
					const State_space_data<Time>& state_space_data) override
				{
					// initialize the task chain data structure
					const auto& exts = state_space_data.get_extensions();
					auto spd_ext = exts.get<Taskchains_sp_data_extension<Time>>(extension_id);
					init_task_chain_data_structure(spd_ext->get_task_chains(), state_space_data.get_num_cpus() > 1);
				}

				void construct(const size_t extension_id,
					const size_t state_space_data_ext_id,
					const Schedule_state<Time>& new_state,
					const Schedule_state<Time>& from,
					Job_index j,
					const Interval<Time>& start_times,
					const Interval<Time>& finish_times,
					const Job_set& scheduled_jobs,
					const std::vector<Job_index>& jobs_with_pending_succ,
					const std::vector<const Job<Time>*>& ready_succ_jobs,
					const State_space_data<Time>& state_space_data,
					Time next_source_job_rel,
					unsigned int ncores = 1) override
				{
					auto from_ext = from.get_extensions().get<Taskchains_state_extension<Time>>(extension_id);
					// If unicore, no need to keep track of possibly running jobs and leave set empty
					if(state_space_data.get_num_cpus() > 1)
						update_possibly_running_jobs(new_state, *from_ext, j, start_times, finish_times, state_space_data);

					update_task_chain_data(*from_ext, j, state_space_data, start_times.min(), start_times.max(),
						finish_times.min(), finish_times.max(), state_space_data_ext_id);
				}

				void reset(const size_t extension_id,
					const size_t state_space_data_ext_id,
					const Schedule_state<Time>& new_state,
					const unsigned int num_processors,
					const State_space_data<Time>& state_space_data) override
				{
					clear();
					construct(extension_id, state_space_data_ext_id, new_state, num_processors, state_space_data);
				}

				void reset(const size_t extension_id,
					const size_t state_space_data_ext_id,
					const Schedule_state<Time>& new_state,
					const std::vector<Interval<Time>>& proc_initial_state,
					const State_space_data<Time>& state_space_data) override
				{
					clear();
					construct(extension_id, state_space_data_ext_id, new_state, proc_initial_state, state_space_data);
				}

				void reset(const size_t extension_id,
					const size_t state_space_data_ext_id,
					const Schedule_state<Time>& new_state,
					const Schedule_state<Time>& from,
					Job_index j,
					const Interval<Time>& start_times,
					const Interval<Time>& finish_times,
					const Job_set& scheduled_jobs,
					const std::vector<Job_index>& jobs_with_pending_succ,
					const std::vector<const Job<Time>*>& ready_succ_jobs,
					const State_space_data<Time>& state_space_data,
					Time next_source_job_rel,
					unsigned int ncores = 1) override
				{
					clear();
					construct(extension_id, state_space_data_ext_id, new_state, from, j, start_times, finish_times, scheduled_jobs, jobs_with_pending_succ, ready_succ_jobs, state_space_data, next_source_job_rel, ncores);
				}

				void merge(size_t extension_id, const Schedule_state<Time>& this_state, const Schedule_state<Time>& other) override
				{
					bool multiproc = (tc_data.EIT_Age_out.size() > 0);
					auto tc_ext_other = other.get_extensions().get<Taskchains_state_extension<Time>>(extension_id);

					if (multiproc) {
						// vector to collect joint possible jobs
						std::vector<Running_job> new_pj;
						// walk both sorted job lists to see if we find matches
						auto pit = possible_jobs.begin();
						auto pit_end = possible_jobs.end();
						auto pjt = tc_ext_other->possible_jobs.begin();
						auto pjt_end = tc_ext_other->possible_jobs.end();
						while (pit != pit_end &&
							pjt != pjt_end) {
							if (pit->idx == pjt->idx) {
								// same job
								new_pj.emplace_back(pit->idx, pit->finish_time | pjt->finish_time);
								pit++;
								pjt++;
							}
							else if (pit->idx < pjt->idx) {
								new_pj.emplace_back(pit->idx, pit->finish_time);
								pit++;
							}
							else {
								new_pj.emplace_back(pjt->idx, pjt->finish_time);
								pjt++;
							}
							// move new certain jobs into the state
							possible_jobs.swap(new_pj);
						}
					}
					
					// merge task chain data
					const auto& tc_data_other = tc_ext_other->tc_data;
					for (int i = 0; i < tc_data.EST_prev.size(); i++) {
						tc_data.DA_max[i] = std::max(tc_data.DA_max[i], tc_data_other.DA_max[i]);
						tc_data.RT_max[i] = std::max(tc_data.RT_max[i], tc_data_other.RT_max[i]);

						if (tc_data.EST_prev[i] == INVALID && tc_data_other.EST_prev[i] != INVALID) tc_data.EST_prev[i] = tc_data_other.EST_prev[i];
						if (tc_data.EST_prev[i] != INVALID && tc_data_other.EST_prev[i] != INVALID) tc_data.EST_prev[i] = std::min(tc_data.EST_prev[i], tc_data_other.EST_prev[i]);

						for (int j = 0; j < tc_data.EIT_Age_int[i].size(); j++) {
							if (tc_data.EIT_Age_int[i][j] != INVALID && tc_data_other.EIT_Age_int[i][j] != INVALID) tc_data.EIT_Age_int[i][j] = std::min(tc_data.EIT_Age_int[i][j], tc_data_other.EIT_Age_int[i][j]);
							if (tc_data.EIT_Age_int[i][j] == INVALID && tc_data_other.EIT_Age_int[i][j] != INVALID) tc_data.EIT_Age_int[i][j] = tc_data_other.EIT_Age_int[i][j];

							if (tc_data.EIT_Reac_int[i][j] != INVALID && tc_data_other.EIT_Reac_int[i][j] != INVALID) tc_data.EIT_Reac_int[i][j] = std::min(tc_data.EIT_Reac_int[i][j], tc_data_other.EIT_Reac_int[i][j]);
							if (tc_data.EIT_Reac_int[i][j] == INVALID && tc_data_other.EIT_Reac_int[i][j] != INVALID) tc_data.EIT_Reac_int[i][j] = tc_data_other.EIT_Reac_int[i][j];

							if (multiproc) {
								if (tc_data.EIT_Age_out[i][j] == INVALID && tc_data_other.EIT_Age_out[i][j] != INVALID) tc_data.EIT_Age_out[i][j] = tc_data_other.EIT_Age_out[i][j];
								if (tc_data.EIT_Age_out[i][j] != INVALID && tc_data_other.EIT_Age_out[i][j] != INVALID) tc_data.EIT_Age_out[i][j] = std::min(tc_data.EIT_Age_out[i][j], tc_data_other.EIT_Age_out[i][j]);

								if (tc_data.EIT_Reac_out[i][j] != INVALID && tc_data_other.EIT_Reac_out[i][j] != INVALID) tc_data.EIT_Reac_out[i][j] = std::min(tc_data.EIT_Reac_out[i][j], tc_data_other.EIT_Reac_out[i][j]);
								if (tc_data.EIT_Reac_out[i][j] == INVALID && tc_data_other.EIT_Reac_out[i][j] != INVALID) tc_data.EIT_Reac_out[i][j] = tc_data_other.EIT_Reac_out[i][j];
							}

							//if(tc_data_other.LIT[i][j]==INVALID || tc_data.LIT[i][j]==INVALID) tc_data.LIT[i][j]==INVALID;
							//else tc_data.LIT[i][j] = std::max(tc_data.LIT[i][j],tc_data_other.LIT[i][j]);
						}
					}
				}

				void print_vertex_label(std::ostream& out,
					const typename Job<Time>::Job_set& jobs) const
				{
					out << "Task Chains: ";
					for (const auto& tc : jobs) {
						out << tc.get_id() << " ";
					}
					out << std::endl;
				}

			private:
				void init_task_chain_data_structure(std::vector<Task_chain<Time>> TC_set, bool multiproc) 
				{
					auto n = TC_set.size();
					tc_data.DA_max.resize(n, 0);
					tc_data.RT_max.resize(n, 0);
					tc_data.EST_prev.resize(n, 0);
					for (const auto& tc : TC_set) {
						tc_data.EIT_Age_int.emplace_back(tc.get_tasks().size(), 0);
						tc_data.EIT_Reac_int.emplace_back(tc.get_tasks().size(), INVALID);
					
						if (multiproc) {
							tc_data.EIT_Age_out.emplace_back(tc.get_tasks().size(), 0);
							tc_data.EIT_Reac_out.emplace_back(tc.get_tasks().size(), INVALID);
						}
					}
				}

				void clear()
				{
					possible_jobs.clear();
					tasks_with_running_jobs.clear();
					tc_data.DA_max.clear();
					tc_data.RT_max.clear();
					tc_data.EST_prev.clear();
					tc_data.EIT_Age_int.clear();
					tc_data.EIT_Reac_int.clear();
					tc_data.EIT_Age_out.clear();
					tc_data.EIT_Reac_out.clear();
				}

				void update_possibly_running_jobs(
					const Schedule_state<Time>& this_state,
					const Taskchains_state_extension<Time>& from,
					Job_index j, Interval<Time> start_times,
					Interval<Time> finish_times, 
					const State_space_data<Time>& ssd)
				{
					// update the set of possibly running jobs
					// keep them sorted to simplify merging				

					possible_jobs.reserve(from.possible_jobs.size() + 1);

					// add the task of job j to the set of tasks with possibly running jobs
					tasks_with_running_jobs.add(ssd.jobs[j].get_task_id());

					const std::vector<Job_index>& preds = ssd.predecessors_of(j);

					bool added_j = false;
					for (const auto& pj : from.possible_jobs)
					{
						auto running_job = pj.idx;
						if (std::find(preds.begin(), preds.end(), running_job) == preds.end()
							&& pj.finish_time.max() >= this_state.core_availability(2).min()
							&& pj.finish_time.max() > start_times.min())
						{
							if (!added_j && running_job > j)
							{
								// right place to add j
								possible_jobs.emplace_back(j, finish_times);
								added_j = true;
							}
							// add pj to the set of possibly running jobs and the task of job pj to 
							// the set of tasks with possibly running jobs
							possible_jobs.emplace_back(pj);
							tasks_with_running_jobs.add(ssd.jobs[pj.idx].get_task_id());
						}
					}
					// if we didn't add it yet, add it at the back
					if (!added_j)
					{
						possible_jobs.emplace_back(j, finish_times);
					}
				}

				bool may_have_running_job(unsigned long task_id) {
					return tasks_with_running_jobs.contains(task_id);
				}

				void update_task_chain_data(const Taskchains_state_extension<Time>& from,
					Job_index& idx, const State_space_data<Time>& state_space_data, 
					const Time& EST, const Time& LST, const Time& EFT, const Time& LFT,
					const size_t state_space_data_ext_id)
				{
					auto space_data_ext_taskchains = state_space_data.get_extensions().get<Taskchains_sp_data_extension<Time>>(state_space_data_ext_id);
					const std::vector<Job<Time>>& jobs = state_space_data.jobs;
					const std::vector<Task_chain<Time>>& TC_set = space_data_ext_taskchains->get_task_chains();
					const Job<Time>& j = jobs[idx];
					const unsigned long& tau_j = j.get_id().task;
					tc_data = from.tc_data;

					if (state_space_data.get_num_cpus() == 1) { // Single core
						for (const auto& tc_info : space_data_ext_taskchains->get_task_chains_of(tau_j)) 
						{
							auto tc_id = tc_info.chain_id;
							const auto& tc = TC_set[tc_id];
							auto index = tc_info.position_in_chain;
							bool is_source = (index == 0);
							bool is_sink = tc_info.is_sink;

							if (is_source) {
								tc_data.EST_prev[tc_id] = EST;

								// EIT_Age_int is the time when the input was read by tau_j
								if (tc.uses_event_input())
									tc_data.EIT_Age_int[tc_id][index] = from.tc_data.EST_prev[tc_id];
								else
									tc_data.EIT_Age_int[tc_id][index] = EST;

								// EIT_Reac_int is the time when the input was read by tau_j for which no reaction propagated yet
								if (from.tc_data.EIT_Reac_int[tc_id][index] == INVALID) {
									// If the previous input was already read tau_j's successor task, i.e., reaction propagated already 
									// => we record the EIT of the newest input read by tau_j for which no reaction propagated yet
									if (tc.uses_event_input())
										tc_data.EIT_Reac_int[tc_id][index] = from.tc_data.EST_prev[tc_id];
									else
										tc_data.EIT_Reac_int[tc_id][index] = EST;
								}
								// else the successor task of tau_j has not read the previous input of tau_j yet 
								// => reaction did not propagate along the task chain yet and we keep the record as is
							}
							else {
								auto pred_index = index - 1;

								// EIT_Age_int is the time when the last input used by the predecessor task was read by the source task of the task chain
								tc_data.EIT_Age_int[tc_id][index] = from.tc_data.EIT_Age_int[tc_id][pred_index];

								// EIT_Reac_int is the time when the last input used by the predecessor task for which 
								// no reaction propagated yet beyond tau_j was read by the source task of the task chain
								if (from.tc_data.EIT_Reac_int[tc_id][index] == INVALID)
									tc_data.EIT_Reac_int[tc_id][index] = from.tc_data.EIT_Reac_int[tc_id][pred_index];
								else if (from.tc_data.EIT_Reac_int[tc_id][pred_index] != INVALID)
									tc_data.EIT_Reac_int[tc_id][index] = std::min(from.tc_data.EIT_Reac_int[tc_id][pred_index], from.tc_data.EIT_Reac_int[tc_id][index]);
							
								// If tau_j is not a source task, we set the EIT_Reac_int of the predecessor task to INVALID 
								// since the reaction propagated to tau_j now that just executed it
								tc_data.EIT_Reac_int[tc_id][pred_index] = INVALID;
							}

							// if we just dispatched a job of the sink task, we can calculate the data age and reaction time for the input 
							// used by that job (or the previous one depending on whether the task chain uses active outputs or not)
							if (is_sink) {
								Time data_age = tc.uses_active_output() ?
									LFT - tc_data.EIT_Age_int[tc_id][index] : LFT - from.tc_data.EIT_Age_int[tc_id][index];
								space_data_ext_taskchains->submit_data_age(tc_id, data_age);
								//std::cout<<"Data age found: "<<data_age<<std::endl;
								tc_data.DA_max[tc_id] = std::max(tc_data.DA_max.at(tc_id), data_age);
								if (tc_data.EIT_Reac_int[tc_id][index] != INVALID) {
									Time reaction_time = LFT - tc_data.EIT_Reac_int[tc_id][index];
									space_data_ext_taskchains->submit_reaction_time(tc_id, reaction_time);
									//std::cout<<"Reaction time found: "<<reaction_time<<std::endl;
									tc_data.RT_max[tc_id] = std::max(tc_data.RT_max.at(tc_id), reaction_time);
									tc_data.EIT_Reac_int[tc_id][index] = INVALID;
								}
							}
						}
					}
					else { // Multicore
						for (const auto& tc : TC_set) {
							const auto& t = tc.get_tasks();
							auto tc_id = tc.get_id();

							for (unsigned int tau_l_index = 0; tau_l_index < t.size(); tau_l_index++) { // for all tasks in the chain
								const unsigned long& tau_l = t[tau_l_index];

								//tc_data.EIT_Age_out[tc.get_id()][tau_l_index] = !may_have_running_job(tau_l_index, state_space_data) ?
								//		from.tc_data.EIT_Age_int[tc.get_id()][tau_l_index] : from.tc_data.EIT_Age_out[tc.get_id()][tau_l_index];
								if (!may_have_running_job(tau_l))
									tc_data.EIT_Age_out[tc_id][tau_l_index] = from.tc_data.EIT_Age_int[tc_id][tau_l_index];

								//tc_data.EIT_Reac_out[tc.get_id()][tau_l_index] = (pred_index==tau_l_index) ? INVALID :
								//tc_data.EIT_Reac_out[tc_id][tau_l_index] = (may_have_running_job(tau_l, state_space_data) || from.tc_data.EIT_Reac_int[tc_id][tau_l_index] == INVALID || from.tc_data.EIT_Reac_out[tc_id][tau_l_index] != INVALID) ?
								//		from.tc_data.EIT_Reac_out[tc_id][tau_l_index] : from.tc_data.EIT_Reac_int[tc_id][tau_l_index];
								if (from.tc_data.EIT_Reac_int[tc_id][tau_l_index] != INVALID && from.tc_data.EIT_Reac_out[tc_id][tau_l_index] == INVALID && !may_have_running_job(tau_l))
									tc_data.EIT_Reac_out[tc_id][tau_l_index] = from.tc_data.EIT_Reac_int[tc_id][tau_l_index];

								if (tau_j != tau_l)
									tc_data.EIT_Reac_int[tc.get_id()][tau_l_index] = may_have_running_job(tau_l) ?
									from.tc_data.EIT_Reac_int[tc.get_id()][tau_l_index] : INVALID;
							}
						}

						for (const auto& tc_info : space_data_ext_taskchains->get_task_chains_of(tau_j)) {
							auto tc_id = tc_info.chain_id;
							const auto& tc = TC_set[tc_id];
							const auto& t = tc.get_tasks();

							auto index = tc_info.position_in_chain; // index (in chain) of task that was just dispatched
							bool is_source = (index == 0); // if tau_j is source task
							bool is_sink = tc_info.is_sink;
							auto pred_index = index - 1;

							if (is_source)
								tc_data.EST_prev[tc_id] = EST; // else from.tc_data.EST_prev[tc.get_id()];

							if (!is_source)
								tc_data.EIT_Reac_out[tc_id][pred_index] = INVALID;

							tc_data.EIT_Age_int[tc_id][index] = is_source ?
								(tc.uses_event_input() ? from.tc_data.EST_prev[tc_id] : EST)
								: (may_have_running_job(t[pred_index]) ? from.tc_data.EIT_Age_int[tc_id][pred_index] : from.tc_data.EIT_Age_int[tc_id][pred_index]);

							tc_data.EIT_Reac_int[tc_id][index] = from.tc_data.EIT_Reac_int[tc_id][index] == INVALID ?
								(is_source ?
									(tc.uses_event_input() ?
										from.tc_data.EST_prev[tc_id] : EST)
									: (may_have_running_job(t[pred_index]) || from.tc_data.EIT_Reac_int[tc_id][pred_index] == INVALID ?
										from.tc_data.EIT_Reac_out[tc_id][pred_index] : from.tc_data.EIT_Reac_int[tc_id][pred_index]))
								: from.tc_data.EIT_Reac_int[tc_id][index];

							if (is_sink) {
								Time data_age = tc.uses_active_output() ?
									LFT - tc_data.EIT_Age_int[tc_id][index] : LFT - from.tc_data.EIT_Age_int[tc_id][index];
								space_data_ext_taskchains->submit_data_age(tc_id, data_age);
								//std::cout<<"Data age found: "<<data_age<<std::endl;
								tc_data.DA_max[tc_id] = std::max(tc_data.DA_max.at(tc_id), data_age);
								if (tc_data.EIT_Reac_int[tc_id][index] != INVALID) {
									Time reaction_time = LFT - tc_data.EIT_Reac_int[tc_id][index];
									space_data_ext_taskchains->submit_reaction_time(tc_id, reaction_time);
									//std::cout<<"Reaction time found: "<<reaction_time<<std::endl;
									tc_data.RT_max[tc_id] = std::max(tc_data.RT_max.at(tc_id), reaction_time);
									tc_data.EIT_Reac_int[tc_id][index] = INVALID;
								}

							}
						}
					}
				}
			};
		}
	}
}

#endif // !STATE_EXT_TASKCHAINS_HPP
