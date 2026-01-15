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

template<class Time>
class Taskchains_state_extension : public State_extension<Time>
{
	// Flattened task chain data container
	template<class T>
	class TCD {
	private:
		// Metadata
		size_t n_chains{ 0 };
		size_t n_total_tasks{ 0 };
		bool multiproc{ false };

		// single vector that contains all task chain data contiguously stored for copy efficiency
		std::vector<T> data;

		// Offsets into 'data' vector
		size_t off_da_max, off_rt_max, off_est_prev, off_eit_age_int, off_eit_reac_int, off_eit_age_out, off_eit_reac_out;
	public:
		// accessors to all data arrays embedded in the flattened data vector
		// DA_max: Max data age per chain
		const T* DA_max() const { return data.data() + off_da_max; }
		T* DA_max() { return data.data() + off_da_max; }
		// RT_max: Max reaction time per chain
		const T* RT_max() const { return data.data() + off_rt_max; }
		T* RT_max() { return data.data() + off_rt_max; }
		// EST_prev: Earliest start time of last dispatched source task per chain
		const T* EST_prev() const { return data.data() + off_est_prev; }
		T* EST_prev() { return data.data() + off_est_prev; }
		// EIT_Age_int: Earliest Input Time tracking unpropagated reaction (task internal)
		const T* EIT_Age_int() const { return data.data() + off_eit_age_int; }
		T* EIT_Age_int() { return data.data() + off_eit_age_int; }
		// EIT_Reac_int: Earliest Input Time tracking unpropagated reaction (task output)
		const T* EIT_Reac_int() const { return data.data() + off_eit_reac_int; }
		T* EIT_Reac_int() { return data.data() + off_eit_reac_int; }
		// EIT_Age_out: Earliest Input Time for data age (task output)
		const T* EIT_Age_out() const { return data.data() + off_eit_age_out; }
		T* EIT_Age_out() { return data.data() + off_eit_age_out; }
		// EIT_Reac_out: Earliest Input Time tracking unpropagated reaction (task output)
		const T* EIT_Reac_out() const { return data.data() + off_eit_reac_out; }
		T* EIT_Reac_out() { return data.data() + off_eit_reac_out; }

		// Metadata
		std::vector<size_t> chain_offset; // start index in flattened arrays for each chain
		std::vector<size_t> chain_length; // number of tasks in each chain

		inline size_t num_chains() const {
			return chain_length.size();
		}

		inline size_t num_tasks() const {
			return n_total_tasks;
		}

		inline size_t idx(size_t chain_id, size_t task_pos) const {
			return chain_offset[chain_id] + task_pos;
		}

		inline bool is_multiproc() const {
			return multiproc;
		}

		void clear() {
			data.clear();
			chain_offset.clear(); chain_length.clear();
			n_chains = 0;
			n_total_tasks = 0;
			multiproc = false;
		}

		inline bool is_cleared() const {
			return n_chains == 0;
		}

		// Initialize / reinitialize while reusing capacity where possible
		void init(const std::vector<Task_chain<T>>& chains, bool mp) {
			multiproc = mp;
			n_chains = chains.size();
			if (n_chains == 0) {
				clear();
				return;
			}
			// Compute offsets & aggregated size
			n_total_tasks = 0;
			chain_offset.resize(n_chains);
			chain_length.resize(n_chains);
			for (size_t i = 0; i < n_chains; ++i) {
				chain_offset[i] = n_total_tasks;
				size_t m = chains[i].get_tasks().size();
				chain_length[i] = m;
				n_total_tasks += m;
			}
			// Compute layout for data
			size_t current_offset = 0;
			off_da_max = current_offset; 
			current_offset += n_chains;
			off_rt_max = current_offset; 
			current_offset += n_chains;
			off_est_prev = current_offset; 
			current_offset += n_chains;
			off_eit_age_int = current_offset; 
			current_offset += n_total_tasks;
			off_eit_reac_int = current_offset; 
			current_offset += n_total_tasks;
			off_eit_age_out = current_offset; 
			current_offset += n_total_tasks;
			off_eit_reac_out = current_offset; 
			current_offset += n_total_tasks;
			data.resize(current_offset);

			// Initialize all arrays to default values
			std::fill(DA_max(), DA_max() + n_chains, T(0));
			std::fill(RT_max(), RT_max() + n_chains, T(0));
			std::fill(EST_prev(), EST_prev() + n_chains, T(0));
			std::fill(EIT_Age_int(), EIT_Age_int() + n_total_tasks, T(0));
			std::fill(EIT_Reac_int(), EIT_Reac_int() + n_total_tasks, T(INVALID));
			std::fill(EIT_Age_out(), EIT_Age_out() + n_total_tasks, T(0));
			std::fill(EIT_Reac_out(), EIT_Reac_out() + n_total_tasks, T(INVALID));
		}

		// Fast full copy (could be replaced with selective copying later)
		void copy_from(const TCD& other) {
			// Check if we can reuse existing capacity. If not resize all vectors.
			if (is_cleared()) {
				*this = other; // trivial copy
			}
			else {
				//data = other.data; // reuse existing capacity
				std::memcpy(data.data(), other.data.data(), other.data.size() * sizeof(T));
			}
		}
	};

	struct Running_job {
		const Job<Time>* j;
		Interval<Time> finish_time;
		Running_job(const Job<Time>* j, Interval<Time> ft) : j(j), finish_time(ft) {}
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
				   const std::vector<Job_index>& jobs_with_pending_start_succ,
				   const std::vector<Job_index>& jobs_with_pending_finish_succ,
				   const std::vector<const Job<Time>*>& ready_succ_jobs,
                   const State_space_data<Time>& state_space_data,
                   Time next_source_job_rel,
                   unsigned int ncores = 1) override
    {
		auto from_ext = from.get_extensions().get<Taskchains_state_extension<Time>>(extension_id);
        if (state_space_data.get_num_cpus() > 1)
            update_possibly_running_jobs(new_state, *from_ext, j, start_times, finish_times, state_space_data);

        update_task_chain_data(*from_ext, j, state_space_data, start_times.min(), start_times.max(),
                               finish_times.min(), finish_times.max(), state_space_data_ext_id, new_state);
	}

	// Reset variants
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
			   const std::vector<Job_index>& jobs_with_pending_start_succ,
			   const std::vector<Job_index>& jobs_with_pending_finish_succ,
			   const std::vector<const Job<Time>*>& ready_succ_jobs,
               const State_space_data<Time>& state_space_data,
               Time next_source_job_rel,
               unsigned int ncores = 1) override
    {
		possible_jobs.clear();
		tasks_with_running_jobs.clear();
		// we do not clear tc_data here, as it will be anyway completely overwritten in update_task_chain_data
        construct(extension_id, state_space_data_ext_id, new_state, from, j, start_times, finish_times,
                  scheduled_jobs, jobs_with_pending_start_succ, jobs_with_pending_finish_succ, ready_succ_jobs, state_space_data, next_source_job_rel, ncores);
	}

	// Merge states conservatively
	void merge(size_t extension_id, const Schedule_state<Time>& this_state, const Schedule_state<Time>& other) override {
		bool multiproc = tc_data.is_multiproc();
		auto other_ext = other.get_extensions().get<Taskchains_state_extension<Time>>(extension_id);
		const auto& other_data = other_ext->tc_data;
		// merge possibly running jobs and tasks (but only if we are analyzing a multiprocessor platform)
		if (multiproc) {
			std::vector<Running_job> merged; 
			merged.reserve(possible_jobs.size() + other_ext->possible_jobs.size());
			auto a = possible_jobs.begin();
			auto b = other_ext->possible_jobs.begin();
			while (a != possible_jobs.end() && b != other_ext->possible_jobs.end()) {
				if (a->j == b->j) {
					merged.emplace_back(a->j, a->finish_time | b->finish_time);
					++a;
					++b;
				}
				else if (a->j->get_job_index() < b->j->get_job_index()) {
					merged.emplace_back(*a);
					++a;
				}
				else {
					merged.emplace_back(*b);
					++b;
					tasks_with_running_jobs.add(b->j->get_task_id());
				}
			}
			merged.insert(merged.end(), a, possible_jobs.end());
			merged.insert(merged.end(), b, other_ext->possible_jobs.end());
			for (; b != other_ext->possible_jobs.end(); ++b)
				tasks_with_running_jobs.add(b->j->get_task_id());

			possible_jobs.swap(merged);
		}
		// merge the data 
		const size_t n = tc_data.num_chains();
		for (size_t c = 0; c < n; ++c) {
            tc_data.DA_max()[c] = std::max(tc_data.DA_max()[c], other_data.DA_max()[c]);
            tc_data.RT_max()[c] = std::max(tc_data.RT_max()[c], other_data.RT_max()[c]);

            if (tc_data.EST_prev()[c] == INVALID && other_data.EST_prev()[c] != INVALID)
                tc_data.EST_prev()[c] = other_data.EST_prev()[c];
            else if (tc_data.EST_prev()[c] != INVALID && other_data.EST_prev()[c] != INVALID)
                tc_data.EST_prev()[c] = std::min(tc_data.EST_prev()[c], other_data.EST_prev()[c]);

			size_t len = tc_data.chain_length[c];
            size_t base = tc_data.chain_offset[c];
			for (size_t t = 0; t < len; ++t) {
				size_t i = base + t;
                // Age_int
                tc_data.EIT_Age_int()[i] = min_star(tc_data.EIT_Age_int()[i], other_data.EIT_Age_int()[i]);
                // Reac_int
                tc_data.EIT_Reac_int()[i] = min_star(tc_data.EIT_Reac_int()[i], other_data.EIT_Reac_int()[i]);                
				// Age_out
                tc_data.EIT_Age_out()[i] = min_star(tc_data.EIT_Age_out()[i], other_data.EIT_Age_out()[i]);
                // Reac_out
                tc_data.EIT_Reac_out()[i] = min_star(tc_data.EIT_Reac_out()[i], other_data.EIT_Reac_out()[i]);
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
		tc_data.clear();
	}

    void update_possibly_running_jobs(
        const Schedule_state<Time>& this_state,
        const Taskchains_state_extension<Time>& from,
        Job_index j, Interval<Time> start_times,
        Interval<Time> finish_times,
        const State_space_data<Time>& ssd)
    {
		possible_jobs.reserve(from.possible_jobs.size() + 1);
		// add the task of the newly scheduled job j to the possibly running tasks
		tasks_with_running_jobs.add(ssd.jobs[j].get_task_id());
		// add all jobs that were possibly running in the previous state and that are not predecessors of j or certainly finished by the time j starts
		const auto& preds = ssd.get_finished_jobs_if_starts(j);
		bool added_j = false;
        for (const auto& pj : from.possible_jobs)
        {
            const auto& running_job = pj.j;
			auto idx = running_job->get_job_index();
			// add pj only if it is not a predecessor of j and it is not certainly finished by the time j starts
            if (std::find(preds.begin(), preds.end(), idx) == preds.end()
                && pj.finish_time.max() >= this_state.core_availability(2).min()
                && pj.finish_time.max() > start_times.min())
            {
				// keep the list sorted by job index
                if (!added_j && idx > j) {
                    possible_jobs.emplace_back(&ssd.jobs[j], finish_times);
                    added_j = true;
                }
				possible_jobs.emplace_back(pj);
				tasks_with_running_jobs.add(running_job->get_task_id());
			}
		}
        if (!added_j) {
            possible_jobs.emplace_back(&ssd.jobs[j], finish_times);
		}
    }

	bool may_have_running_job(unsigned long task_id) const { 
		return tasks_with_running_jobs.contains(task_id);
	}

	bool is_certainly_running(unsigned long task_id, const Schedule_state<Time>& state, const State_space_data<Time>& state_space_data) const {
		const auto& cert_running_jobs = state.get_cert_running_jobs();
		for (const auto& rj : cert_running_jobs) {
			if (state_space_data.jobs[rj.idx].get_task_id() == task_id)
				return true;
		}
		return false;
	}

	Time min_star(const Time& a, const Time& b) const {
		if (a == INVALID) return b;
		if (b == INVALID) return a;
		return std::min(a, b);
	}

	void update_task_chain_data(const Taskchains_state_extension<Time>& from, Job_index& idx,
		const State_space_data<Time>& ssd, const Time& EST, const Time&, const Time&, const Time& LFT,
		const size_t ssd_ext_id, const Schedule_state<Time>& new_state)
	{
		//static int counter = 0;
		auto space_ext = ssd.get_extensions().get<Taskchains_sp_data_extension<Time>>(ssd_ext_id);
		const auto& chains = space_ext->get_task_chains();
		const Job<Time>& job = ssd.jobs[idx];
		const unsigned long tau_j = job.get_task_id();
		const bool singleproc = not tc_data.is_multiproc();

		// Bulk copy previous chain data
		tc_data.copy_from(from.tc_data);

		// update *out* views
		for (const auto& tc : chains) {
			size_t tc_id = tc.get_id();
			size_t pos = tc_data.chain_offset[tc_id];
			const auto& tasks = tc.get_tasks();
			for (size_t k = 0; k < tasks.size(); ++k) {
				unsigned long tau_l = tasks[k];
				// note that `may_have_running_job` does not work for uniprocessor platforms and always returns false
				if (tau_l != tau_j && (singleproc || !may_have_running_job(tau_l))) {
					tc_data.EIT_Age_out()[pos] = from.tc_data.EIT_Age_int()[pos];
					tc_data.EIT_Reac_int()[pos] = INVALID;
					tc_data.EIT_Reac_out()[pos] = min_star(from.tc_data.EIT_Reac_int()[pos], from.tc_data.EIT_Reac_out()[pos]);
				}
				++pos;
			}
		}

		for (const auto& info : space_ext->get_task_chains_of(tau_j)) {
			size_t tc_id = info.chain_id;
			const auto& tc = chains[tc_id];
			size_t index = info.position_in_chain;
			bool is_source = (index == 0);
			bool is_sink = info.is_sink;
			size_t pos = tc_data.idx(tc_id, index);

			if (is_source) {
				tc_data.EST_prev()[tc_id] = EST;
				if (tc.uses_event_input()) {
					Time EST_prev = from.tc_data.EST_prev()[tc_id];
					tc_data.EIT_Age_int()[pos] = EST_prev;
					tc_data.EIT_Reac_int()[pos] = min_star(EST_prev, from.tc_data.EIT_Reac_int()[pos]);
				}
				else {
					tc_data.EIT_Age_int()[pos] = EST;
					tc_data.EIT_Reac_int()[pos] = min_star(EST, from.tc_data.EIT_Reac_int()[pos]);
				}
			}
			else { 
				const auto& tasks = tc.get_tasks();
				size_t pred_pos = pos - 1;
				tc_data.EIT_Reac_out()[pred_pos] = INVALID;

				// note that `pred_pos_running` is always false if the platform is uniprocessor
				bool pred_pos_running = !singleproc && may_have_running_job(tasks[index - 1]);
				if (pred_pos_running)
					tc_data.EIT_Age_int()[pos] = from.tc_data.EIT_Age_out()[pred_pos];
				else
					tc_data.EIT_Age_int()[pos] = from.tc_data.EIT_Age_int()[pred_pos];
				
				// note that `pred_cert_running` is always false if the platform is uniprocessor
				bool pred_cert_running = !singleproc && is_certainly_running(tasks[index - 1], new_state, ssd);
				if (pred_cert_running)
					tc_data.EIT_Reac_int()[pos] = min_star(from.tc_data.EIT_Reac_int()[pos], from.tc_data.EIT_Reac_out()[pred_pos]);
				else
					tc_data.EIT_Reac_int()[pos] = min_star(from.tc_data.EIT_Reac_int()[pos], min_star(from.tc_data.EIT_Reac_out()[pred_pos], from.tc_data.EIT_Reac_int()[pred_pos]));
			}
			
			if (is_sink) {
				Time data_age = tc.uses_instantaneous_output() ? (LFT - tc_data.EIT_Age_int()[pos]) : (LFT - from.tc_data.EIT_Age_int()[pos]);
				space_ext->submit_data_age(tc_id, data_age);
				tc_data.DA_max()[tc_id] = std::max(tc_data.DA_max()[tc_id], data_age);
				if (tc_data.EIT_Reac_int()[pos] != INVALID) {
					Time reaction_time = LFT - tc_data.EIT_Reac_int()[pos];
					//std::cout << counter << "," << reaction_time << "\n";
					//counter++;
					space_ext->submit_reaction_time(tc_id, reaction_time);
					tc_data.RT_max()[tc_id] = std::max(tc_data.RT_max()[tc_id], reaction_time);
					tc_data.EIT_Reac_int()[pos] = INVALID;
				}
			}
		}
	}
};

} // namespace Taskchains_analysis
} // namespace Global
} // namespace NP

#endif // !STATE_EXT_TASKCHAINS_HPP
