#ifndef STATE_EXT_TASKCHAINS_HPP
#define STATE_EXT_TASKCHAINS_HPP
#include "global/extension/state_extension.hpp"
#include "global/extension/taskchains/taskchains_sp_data_extension.hpp"
#include "global/extension/taskchains/taskchains.hpp"
#include "jobs.hpp"
#include "index_set.hpp"

#include <iostream>
#include <vector>
#include <cstring>
#include <type_traits>
#include <algorithm>

namespace NP {
namespace Global {
namespace Taskchains_analysis {

#define INVALID -1

/*
 Optimized Taskchains_state_extension
 -----------------------------------
 - Flattened contiguous storage for per-task per-chain arrays
 - Reuse of allocated capacity (avoid repeated allocations)
 - Bulk copy via memcpy when Time is trivially copyable
 - Detailed comments retained for clarity
*/

template<class Time>
class Taskchains_state_extension : public State_extension<Time>
{
	// Flattened task chain data container
	template<class T>
	struct TCD {
		// Per-chain scalar metrics
		std::vector<T> DA_max;    // Max data age per chain
		std::vector<T> RT_max;    // Max reaction time per chain
		std::vector<T> EST_prev;  // Earliest start time of last dispatched source task per chain

		// Flattened per (chain, task-position) arrays
		std::vector<T> EIT_Reac_int; // Earliest Input Time tracking unpropagated reaction (task internal)
		std::vector<T> EIT_Age_int;  // Earliest Input Time for data age (task internal)
		//std::vector<T> LIT_int;      // Placeholder / future use
		std::vector<T> EIT_Reac_out; // Earliest Input Time tracking unpropagated reaction (task output)
		std::vector<T> EIT_Age_out;  // Earliest Input Time for data age (task output)
		//std::vector<T> LIT_out;      // Placeholder / future use

		// Metadata
		std::vector<size_t> chain_offset; // start index in flattened arrays for each chain
		std::vector<size_t> chain_length; // number of tasks in each chain
		bool multiproc{false};

		inline size_t num_chains() const {
			return chain_length.size();
		}

		inline size_t idx(size_t chain_id, size_t task_pos) const {
			return chain_offset[chain_id] + task_pos;
		}

		void clear() {
			DA_max.clear(); RT_max.clear(); EST_prev.clear();
			EIT_Reac_int.clear(); EIT_Age_int.clear(); LIT_int.clear();
			EIT_Reac_out.clear(); EIT_Age_out.clear(); LIT_out.clear();
			chain_offset.clear(); chain_length.clear();
			multiproc = false;
		}

		inline bool is_cleared() const {
			return chain_offset.empty();
		}

		// Initialize / reinitialize while reusing capacity where possible
		void init(const std::vector<Task_chain<T>>& chains, bool mp) {
			multiproc = mp;
			const size_t n = chains.size();
			// Compute offsets & aggregated size
			size_t total_tasks = 0;
			chain_offset.resize(n);
			chain_length.resize(n);
			for (size_t i = 0; i < n; ++i) {
				chain_offset[i] = total_tasks;
				size_t m = chains[i].get_tasks().size();
				chain_length[i] = m;
				total_tasks += m;
			}
			DA_max.assign(n, T(0));
			RT_max.assign(n, T(0));
			EST_prev.assign(n, T(0));
			EIT_Age_int.assign(total_tasks, T(0));
			EIT_Reac_int.assign(total_tasks, T(INVALID));
			//LIT_int.assign(total_tasks, T(0));
			if (multiproc) {
				EIT_Age_out.assign(total_tasks, T(0));
				EIT_Reac_out.assign(total_tasks, T(INVALID));
				//LIT_out.assign(total_tasks, T(0));
			} else {
				EIT_Age_out.clear();
				EIT_Reac_out.clear();
				//LIT_out.clear();
			}
		}

		// Fast full copy (could be replaced with selective copying later)
		void copy_from(const TCD& other) {
			const size_t n = other.DA_max.size();
			const size_t total_tasks = other.EIT_Age_int.size();

			// Check if we can reuse existing capacity. If not resize all vectors.
			if (is_cleared()) {
				multiproc = other.multiproc;
				chain_offset = other.chain_offset;
				chain_length = other.chain_length;				
				DA_max.resize(n); RT_max.resize(n); EST_prev.resize(n);				
				EIT_Age_int.resize(total_tasks);
				EIT_Reac_int.resize(total_tasks);
				//LIT_int.resize(total_tasks);
				if (multiproc) {
					EIT_Age_out.resize(total_tasks);
					EIT_Reac_out.resize(total_tasks);
					//LIT_out.resize(total_tasks);
				}
			}

			if constexpr (std::is_trivially_copyable<T>::value) {
				if (n > 0) {
					std::memcpy(DA_max.data(), other.DA_max.data(), n * sizeof(T));
					std::memcpy(RT_max.data(), other.RT_max.data(), n * sizeof(T));
					std::memcpy(EST_prev.data(), other.EST_prev.data(), n * sizeof(T));
				}
				if (total_tasks > 0) {
					std::memcpy(EIT_Age_int.data(), other.EIT_Age_int.data(), total_tasks * sizeof(T));
					std::memcpy(EIT_Reac_int.data(), other.EIT_Reac_int.data(), total_tasks * sizeof(T));
					//std::memcpy(LIT_int.data(), other.LIT_int.data(), total_tasks * sizeof(T));
					if (multiproc) {
						std::memcpy(EIT_Age_out.data(), other.EIT_Age_out.data(), total_tasks * sizeof(T));
						std::memcpy(EIT_Reac_out.data(), other.EIT_Reac_out.data(), total_tasks * sizeof(T));
						//std::memcpy(LIT_out.data(), other.LIT_out.data(), total_tasks * sizeof(T));
					}
				}
			}
			else {
				DA_max = other.DA_max; RT_max = other.RT_max; EST_prev = other.EST_prev;
				EIT_Age_int = other.EIT_Age_int; EIT_Reac_int = other.EIT_Reac_int; //LIT_int = other.LIT_int;
				if (multiproc) {
					EIT_Age_out = other.EIT_Age_out; EIT_Reac_out = other.EIT_Reac_out; //LIT_out = other.LIT_out;
				}
			}
		}
	};

	struct Running_job {
		Job_index idx;
		Interval<Time> finish_time;
		Running_job(Job_index idx, Interval<Time> ft) : idx(idx), finish_time(ft) {}
	};

	std::vector<Running_job> possible_jobs;  // sorted set of jobs that may be running
	Index_set tasks_with_running_jobs;       // task ids that may have a running job
	TCD<Time> tc_data;                       // task chain data

public:
	Taskchains_state_extension() = default;

	// Initial state construction
    void construct(const size_t extension_id,
                   const size_t state_space_data_ext_id,
                   const Schedule_state<Time>& new_state,
                   const unsigned int num_processors,
                   const State_space_data<Time>& state_space_data) override
    {
        auto spd_ext = state_space_data.get_extensions().get<Taskchains_sp_data_extension<Time>>(state_space_data_ext_id);
		tc_data.init(spd_ext->get_task_chains(), state_space_data.get_num_cpus() > 1);
	}

    void construct(const size_t extension_id,
                   const size_t state_space_data_ext_id,
                   const Schedule_state<Time>& new_state,
                   const std::vector<Interval<Time>>& proc_initial_state,
                   const State_space_data<Time>& state_space_data) override
    {
        auto spd_ext = state_space_data.get_extensions().get<Taskchains_sp_data_extension<Time>>(extension_id);
		tc_data.init(spd_ext->get_task_chains(), state_space_data.get_num_cpus() > 1);
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
        if (state_space_data.get_num_cpus() > 1)
            update_possibly_running_jobs(new_state, *from_ext, j, start_times, finish_times, state_space_data);

        update_task_chain_data(*from_ext, j, state_space_data, start_times.min(), start_times.max(),
                               finish_times.min(), finish_times.max(), state_space_data_ext_id);
	}

	// Reset variants
    void reset(const size_t extension_id,
               const size_t state_space_data_ext_id,
               const Schedule_state<Time>& new_state,
               const unsigned int num_processors,
               const State_space_data<Time>& state_space_data) override
    {
		clear();
	}

    void reset(const size_t extension_id,
               const size_t state_space_data_ext_id,
               const Schedule_state<Time>& new_state,
               const std::vector<Interval<Time>>& proc_initial_state,
               const State_space_data<Time>& state_space_data) override
    {
		clear();
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
		possible_jobs.clear();
		tasks_with_running_jobs.clear();
		// we do not clear tc_data here, as it will be anyway completely overwritten in update_task_chain_data
        construct(extension_id, state_space_data_ext_id, new_state, from, j, start_times, finish_times,
                  scheduled_jobs, jobs_with_pending_succ, ready_succ_jobs, state_space_data, next_source_job_rel, ncores);
	}

	// Merge states conservatively
	void merge(size_t extension_id, const Schedule_state<Time>& this_state, const Schedule_state<Time>& other) override {
		bool multiproc = tc_data.multiproc;
		auto other_ext = other.get_extensions().get<Taskchains_state_extension<Time>>(extension_id);
		const auto& other_data = other_ext->tc_data;
		if (multiproc) {
			std::vector<Running_job> merged; 
			merged.reserve(possible_jobs.size() + other_ext->possible_jobs.size());
			auto a = possible_jobs.begin();
			auto b = other_ext->possible_jobs.begin();
			while (a != possible_jobs.end() && b != other_ext->possible_jobs.end()) {
				if (a->idx == b->idx) {
					merged.emplace_back(a->idx, a->finish_time | b->finish_time);
					++a;
					++b;
				}
				else if (a->idx < b->idx) {
					merged.emplace_back(*a);
					++a;
				}
				else {
					merged.emplace_back(*b);
					++b;
				}
			}
			merged.insert(merged.end(), a, possible_jobs.end());
			merged.insert(merged.end(), b, other_ext->possible_jobs.end());
			possible_jobs.swap(merged);
		}
		const size_t n = tc_data.num_chains();
		for (size_t c = 0; c < n; ++c) {
            tc_data.DA_max[c] = std::max(tc_data.DA_max[c], other_data.DA_max[c]);
            tc_data.RT_max[c] = std::max(tc_data.RT_max[c], other_data.RT_max[c]);

            if (tc_data.EST_prev[c] == INVALID && other_data.EST_prev[c] != INVALID)
                tc_data.EST_prev[c] = other_data.EST_prev[c];
            else if (tc_data.EST_prev[c] != INVALID && other_data.EST_prev[c] != INVALID)
                tc_data.EST_prev[c] = std::min(tc_data.EST_prev[c], other_data.EST_prev[c]);

			size_t len = tc_data.chain_length[c];
            size_t base = tc_data.chain_offset[c];
			for (size_t t = 0; t < len; ++t) {
				size_t i = base + t;

                // Age_int
                if (tc_data.EIT_Age_int[i] != INVALID && other_data.EIT_Age_int[i] != INVALID)
                    tc_data.EIT_Age_int[i] = std::min(tc_data.EIT_Age_int[i], other_data.EIT_Age_int[i]);
                else if (tc_data.EIT_Age_int[i] == INVALID && other_data.EIT_Age_int[i] != INVALID)
                    tc_data.EIT_Age_int[i] = other_data.EIT_Age_int[i];

                // Reac_int
                if (tc_data.EIT_Reac_int[i] != INVALID && other_data.EIT_Reac_int[i] != INVALID)
                    tc_data.EIT_Reac_int[i] = std::min(tc_data.EIT_Reac_int[i], other_data.EIT_Reac_int[i]);
                else if (tc_data.EIT_Reac_int[i] == INVALID && other_data.EIT_Reac_int[i] != INVALID)
                    tc_data.EIT_Reac_int[i] = other_data.EIT_Reac_int[i];

				if (multiproc) {
                    // Age_out
                    if (tc_data.EIT_Age_out[i] == INVALID && other_data.EIT_Age_out[i] != INVALID)
                        tc_data.EIT_Age_out[i] = other_data.EIT_Age_out[i];
                    else if (tc_data.EIT_Age_out[i] != INVALID && other_data.EIT_Age_out[i] != INVALID)
                        tc_data.EIT_Age_out[i] = std::min(tc_data.EIT_Age_out[i], other_data.EIT_Age_out[i]);

                    // Reac_out
                    if (tc_data.EIT_Reac_out[i] != INVALID && other_data.EIT_Reac_out[i] != INVALID)
                        tc_data.EIT_Reac_out[i] = std::min(tc_data.EIT_Reac_out[i], other_data.EIT_Reac_out[i]);
                    else if (tc_data.EIT_Reac_out[i] == INVALID && other_data.EIT_Reac_out[i] != INVALID)
                        tc_data.EIT_Reac_out[i] = other_data.EIT_Reac_out[i];
				}
			}
		}
	}

	void print_vertex_label(std::ostream& out, const typename Job<Time>::Job_set& jobs) const {
		out << "Task Chains: ";
		for (const auto& tc : jobs) out << tc.get_id() << ' ';
		out << '\n';
	}

private:
	void clear() {
		possible_jobs.clear();
		tasks_with_running_jobs.clear();
		// tc_data capacity retained; reset values to default if needed
		// Instead of deallocating, just reset metrics to neutral to avoid growth on reuse
		// Provide a lightweight reset of scalar arrays if already initialized
		if (!tc_data.chain_length.empty()) {
			std::fill(tc_data.DA_max.begin(), tc_data.DA_max.end(), Time(0));
			std::fill(tc_data.RT_max.begin(), tc_data.RT_max.end(), Time(0));
			std::fill(tc_data.EST_prev.begin(), tc_data.EST_prev.end(), Time(0));
			std::fill(tc_data.EIT_Age_int.begin(), tc_data.EIT_Age_int.end(), Time(0));
			std::fill(tc_data.EIT_Reac_int.begin(), tc_data.EIT_Reac_int.end(), Time(INVALID));
			if (tc_data.multiproc) {
				std::fill(tc_data.EIT_Age_out.begin(), tc_data.EIT_Age_out.end(), Time(0));
				std::fill(tc_data.EIT_Reac_out.begin(), tc_data.EIT_Reac_out.end(), Time(INVALID));
			}
		}
	}

    void update_possibly_running_jobs(
        const Schedule_state<Time>& this_state,
        const Taskchains_state_extension<Time>& from,
        Job_index j, Interval<Time> start_times,
        Interval<Time> finish_times,
        const State_space_data<Time>& ssd)
    {
		possible_jobs.reserve(from.possible_jobs.size() + 1);
		tasks_with_running_jobs.add(ssd.jobs[j].get_task_id());
		const auto& preds = ssd.predecessors_of(j);
		bool added_j = false;
        for (const auto& pj : from.possible_jobs)
        {
            auto running_job = pj.idx;
            if (std::find(preds.begin(), preds.end(), running_job) == preds.end()
                && pj.finish_time.max() >= this_state.core_availability(2).min()
                && pj.finish_time.max() > start_times.min())
            {
                if (!added_j && running_job > j) {
                    possible_jobs.emplace_back(j, finish_times);
                    added_j = true;
                }
				possible_jobs.emplace_back(pj);
				tasks_with_running_jobs.add(ssd.jobs[pj.idx].get_task_id());
			}
		}
        if (!added_j) {
            possible_jobs.emplace_back(j, finish_times);
		}
    }

	bool may_have_running_job(unsigned long task_id) { 
		return tasks_with_running_jobs.contains(task_id);
	}

	void update_task_chain_data(const Taskchains_state_extension<Time>& from, Job_index& idx,
		const State_space_data<Time>& ssd, const Time& EST, const Time&, const Time&, const Time& LFT,
		const size_t ssd_ext_id) 
	{
		auto space_ext = ssd.get_extensions().get<Taskchains_sp_data_extension<Time>>(ssd_ext_id);
		const auto& chains = space_ext->get_task_chains();
		const Job<Time>& job = ssd.jobs[idx];
		const unsigned long tau_j = job.get_task_id();

		// Bulk copy previous chain data
		tc_data.copy_from(from.tc_data);

		if (ssd.get_num_cpus() == 1) { // single-core
			for (const auto& info : space_ext->get_task_chains_of(tau_j)) {
				size_t tc_id = info.chain_id;
				const auto& tc = chains[tc_id];
				size_t index = info.position_in_chain;
				bool is_source = (index == 0);
				bool is_sink = info.is_sink;
				size_t pos = tc_data.idx(tc_id, index);
				if (is_source) {
					tc_data.EST_prev[tc_id] = EST;
					tc_data.EIT_Age_int[pos] = tc.uses_event_input() ? from.tc_data.EST_prev[tc_id] : EST;
					if (from.tc_data.EIT_Reac_int[pos] == INVALID)
						tc_data.EIT_Reac_int[pos] = tc.uses_event_input() ? from.tc_data.EST_prev[tc_id] : EST;
				} else {
					size_t pred_pos = pos - 1;
					tc_data.EIT_Age_int[pos] = from.tc_data.EIT_Age_int[pred_pos];
					if (from.tc_data.EIT_Reac_int[pos] == INVALID)
						tc_data.EIT_Reac_int[pos] = from.tc_data.EIT_Reac_int[pred_pos];
					else if (from.tc_data.EIT_Reac_int[pred_pos] != INVALID)
						tc_data.EIT_Reac_int[pos] = std::min(from.tc_data.EIT_Reac_int[pred_pos], from.tc_data.EIT_Reac_int[pos]);
					tc_data.EIT_Reac_int[pred_pos] = INVALID;
				}
				if (is_sink) {
					Time data_age = tc.uses_active_output() ? (LFT - tc_data.EIT_Age_int[pos]) : (LFT - from.tc_data.EIT_Age_int[pos]);
					space_ext->submit_data_age(tc_id, data_age);
					tc_data.DA_max[tc_id] = std::max(tc_data.DA_max[tc_id], data_age);
					if (tc_data.EIT_Reac_int[pos] != INVALID) {
						Time reaction_time = LFT - tc_data.EIT_Reac_int[pos];
						space_ext->submit_reaction_time(tc_id, reaction_time);
						tc_data.RT_max[tc_id] = std::max(tc_data.RT_max[tc_id], reaction_time);
						tc_data.EIT_Reac_int[pos] = INVALID;
					}
				}
			}
		} else { // multicore
			// Pass 1: update *out* views
			for (const auto& tc : chains) {
				size_t tc_id = tc.get_id();
				size_t base = tc_data.chain_offset[tc_id];
				const auto& tasks = tc.get_tasks();
				for (size_t k = 0; k < tasks.size(); ++k) {
					unsigned long tau_l = tasks[k];
					size_t pos = base + k;
					if (!may_have_running_job(tau_l)) tc_data.EIT_Age_out[pos] = from.tc_data.EIT_Age_int[pos];
					if (from.tc_data.EIT_Reac_int[pos] != INVALID && from.tc_data.EIT_Reac_out[pos] == INVALID && !may_have_running_job(tau_l))
						tc_data.EIT_Reac_out[pos] = from.tc_data.EIT_Reac_int[pos];
					if (job.get_id().task != tau_l)
						tc_data.EIT_Reac_int[pos] = may_have_running_job(tau_l) ? from.tc_data.EIT_Reac_int[pos] : INVALID;
				}
			}
			// Pass 2: update chain(s) containing current job
			for (const auto& info : space_ext->get_task_chains_of(tau_j)) {
				size_t tc_id = info.chain_id;
				const auto& tc = chains[tc_id];
				size_t index = info.position_in_chain;
				bool is_source = (index == 0);
				bool is_sink = info.is_sink;
				size_t pos = tc_data.idx(tc_id, index);

				if (is_source) {
					tc_data.EST_prev[tc_id] = EST;
					tc_data.EIT_Age_int[pos] = tc.uses_event_input() ? from.tc_data.EST_prev[tc_id] : EST;
					if (from.tc_data.EIT_Reac_int[pos] == INVALID)
						tc_data.EIT_Reac_int[pos] = tc.uses_event_input() ? from.tc_data.EST_prev[tc_id] : EST;
				}
				else {
					size_t pred_pos = pos - 1;
					tc_data.EIT_Reac_out[pred_pos] = INVALID;
					tc_data.EIT_Age_int[pos] = from.tc_data.EIT_Age_int[pred_pos];
					if (from.tc_data.EIT_Reac_int[pos] == INVALID) {
						 const auto& tasks = tc.get_tasks();
						if (may_have_running_job(tasks[index - 1]) || from.tc_data.EIT_Reac_int[pred_pos] == INVALID)
							tc_data.EIT_Reac_int[pos] = from.tc_data.EIT_Reac_out[pred_pos];
						else
							tc_data.EIT_Reac_int[pos] = from.tc_data.EIT_Reac_int[pred_pos];
					}
				}

				if (is_sink) {
					Time data_age = tc.uses_active_output() ? (LFT - tc_data.EIT_Age_int[pos]) : (LFT - from.tc_data.EIT_Age_int[pos]);
					space_ext->submit_data_age(tc_id, data_age);
					tc_data.DA_max[tc_id] = std::max(tc_data.DA_max[tc_id], data_age);
					if (tc_data.EIT_Reac_int[pos] != INVALID) {
						Time reaction_time = LFT - tc_data.EIT_Reac_int[pos];
						space_ext->submit_reaction_time(tc_id, reaction_time);
						tc_data.RT_max[tc_id] = std::max(tc_data.RT_max[tc_id], reaction_time);
						tc_data.EIT_Reac_int[pos] = INVALID;
					}
				}
			}
		}
	}
};

} // namespace Taskchains_analysis
} // namespace Global
} // namespace NP

#endif // !STATE_EXT_TASKCHAINS_HPP
