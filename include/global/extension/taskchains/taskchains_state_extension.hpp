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

template<class Time>
class Taskchains_state_extension : public State_extension<Time>
{
	// NOTE: INVALID must be equal to infinity. Otherwise, the logic for Taskchains_data initialization and for min_star will fail
	static constexpr Time INVALID = Time_model::constants<Time>::infinity();

	// Flattened task chain data container
	template<class T>
	class Taskchains_data {
	private:
		// number of task chains 
		size_t n_chains{ 0 };
		// total number of tasks across all chains
		size_t n_total_tasks{ 0 };
		// whether the platform is multiprocessor
		bool multiproc{ false };

		// single vector that contains all task chain data contiguously stored for copy efficiency
		std::vector<T> data;

		// Offsets into 'data' vector
		size_t off_est_prev, off_avail, off_eit_age_int, off_eit_reac_int, off_eit_age_out, off_eit_reac_out;
	public:
		// accessors to all data arrays embedded in the flattened data vector
		// EST_prev: Earliest start time of last dispatched job of the source task per chain
		const T* EST_prev() const { return data.data() + off_est_prev; }
		T* EST_prev() { return data.data() + off_est_prev; }
		// out_availability: Latest time the data published by the sink task of a chain will be available to use
		const T* out_availability() const { return data.data() + off_avail; }
		T* out_availability() { return data.data() + off_avail; }
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

		// start index in flattened arrays for each chain
		std::vector<size_t> chain_offset;
		// number of tasks in each task chain
		std::vector<size_t> chain_length;

		/**
		 * @brief Get the number of task chains
		 */
		inline size_t num_chains() const {
			return chain_length.size();
		}
		/**
		 * @brief Get the total number of tasks across all chains
		 */
		inline size_t num_tasks() const {
			return n_total_tasks;
		}
		/**
		 * @brief Get the index in the flattened arrays for a given chain and task position in the chain
		 * @param chain_id Task chain ID
		 * @param task_pos Position of the task in the chain
		 */
		inline size_t get_idx(size_t chain_id, size_t task_pos) const {
			return chain_offset[chain_id] + task_pos;
		}
		/**
		 * @brief Check if the platform is multiprocessor
		 */
		inline bool is_multiproc() const {
			return multiproc;
		}
		/**
		 * @brief Clear all data
		 */
		void clear() {
			data.clear();
			chain_offset.clear(); 
			chain_length.clear();
			n_chains = 0;
			n_total_tasks = 0;
			multiproc = false;
		}
		/**
		 * @brief Check if the data container is cleared
		 */
		inline bool is_cleared() const {
			return n_chains == 0;
		}

		/**
		 * @brief Initialize the task chain data container
		 * @param chains Vector of task chains
		 * @param mp Boolean indicating if the platform is multiprocessor
		 */
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
			off_est_prev = current_offset; 
			current_offset += n_chains;
			off_avail = current_offset; 
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
			std::fill(data.begin(), data.end(), INVALID);
		}

		/**
		 * @brief Copy data from another Taskchains_data instance
		 * @param other The other Taskchains_data instance to copy from
		 */
		void copy_from(const Taskchains_data& other) {
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

	// Structure saving the finish time of a job
	struct Running_job {
		const Job<Time>* j;
		Interval<Time> finish_time;
		Running_job(const Job<Time>* j, Interval<Time> ft) : j(j), finish_time(ft) {}
	};

	// Sorted set of jobs that may be running
	std::vector<Running_job> possibly_running_jobs;
	// Task ids that may have a running job
	Index_set tasks_with_possibly_running_jobs;
	// Task chains data
	Taskchains_data<Time> tc_data;

public:
	// Default constructor
	Taskchains_state_extension() = default;

	/**
	 * @brief Initial state construction with number of processors
	 */
    void construct(const size_t extension_id,
                   const size_t state_space_data_ext_id,
                   const Schedule_state<Time>& new_state,
                   const unsigned int num_processors,
                   const State_space_data<Time>& state_space_data) override
    {
        auto spd_ext = state_space_data.get_extensions().template get<Taskchains_sp_data_extension<Time>>(state_space_data_ext_id);
		tc_data.init(spd_ext->get_task_chains(), state_space_data.get_num_cpus() > 1);
	}

	/**
	 * @brief Initial state construction with processors initial state
	 */
    void construct(const size_t extension_id,
                   const size_t state_space_data_ext_id,
                   const Schedule_state<Time>& new_state,
                   const std::vector<Interval<Time>>& proc_initial_state,
                   const State_space_data<Time>& state_space_data) override
    {
        auto spd_ext = state_space_data.get_extensions().template get<Taskchains_sp_data_extension<Time>>(extension_id);
		tc_data.init(spd_ext->get_task_chains(), state_space_data.get_num_cpus() > 1);
	}

	/**
	 * @brief Construct a new state from an existing one by dispatching a job
	 */
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
		auto from_ext = from.get_extensions().template get<Taskchains_state_extension<Time>>(extension_id);
        if (state_space_data.get_num_cpus() > 1)
            update_possibly_running_jobs(new_state, *from_ext, j, start_times, finish_times, state_space_data);

        update_task_chain_data(*from_ext, j, state_space_data, start_times.min(), start_times.max(),
                               finish_times.min(), finish_times.max(), state_space_data_ext_id, new_state);
	}

	/**
	 * @brief Reset the state extension to a new initial state
	 */
    void reset(const size_t extension_id,
               const size_t state_space_data_ext_id,
               const Schedule_state<Time>& new_state,
               const unsigned int num_processors,
               const State_space_data<Time>& state_space_data) override
    {
		clear();
		construct(extension_id, state_space_data_ext_id, new_state, num_processors, state_space_data);
	}

	/**
	 * @brief Reset the state extension to a new initial state with known processors initial state
	 */
    void reset(const size_t extension_id,
               const size_t state_space_data_ext_id,
               const Schedule_state<Time>& new_state,
               const std::vector<Interval<Time>>& proc_initial_state,
               const State_space_data<Time>& state_space_data) override
    {
		clear();
		construct(extension_id, state_space_data_ext_id, new_state, proc_initial_state, state_space_data);
	}

	/**
	 * @brief Calculate a new state resulting from dispatching a job in an existing state
	 */
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
		possibly_running_jobs.clear();
		tasks_with_possibly_running_jobs.clear();
		// we do not clear tc_data here, as it will be anyway completely overwritten in update_task_chain_data
        construct(extension_id, state_space_data_ext_id, new_state, from, j, start_times, finish_times,
                  scheduled_jobs, jobs_with_pending_start_succ, jobs_with_pending_finish_succ, ready_succ_jobs, state_space_data, next_source_job_rel, ncores);
	}

	/**
	 * @brief Merge this state extension with another one of the same type
	 */
	void merge(size_t extension_id, const Schedule_state<Time>& this_state, const Schedule_state<Time>& other) override {
		bool multiproc = tc_data.is_multiproc();
		auto other_ext = other.get_extensions().template get<Taskchains_state_extension<Time>>(extension_id);
		const auto& other_data = other_ext->tc_data;
		// merge possibly running jobs and tasks (but only if we are analyzing a multiprocessor platform)
		if (multiproc) {
			std::vector<Running_job> merged; 
			merged.reserve(possibly_running_jobs.size() + other_ext->possibly_running_jobs.size());
			auto a = possibly_running_jobs.begin();
			auto b = other_ext->possibly_running_jobs.begin();
			while (a != possibly_running_jobs.end() && b != other_ext->possibly_running_jobs.end()) {
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
					tasks_with_possibly_running_jobs.add(b->j->get_task_id());
					++b;
				}
			}
			merged.insert(merged.end(), a, possibly_running_jobs.end());
			merged.insert(merged.end(), b, other_ext->possibly_running_jobs.end());
			for (; b != other_ext->possibly_running_jobs.end(); ++b)
				tasks_with_possibly_running_jobs.add(b->j->get_task_id());

			possibly_running_jobs.swap(merged);
		}
		// merge the data 
		const size_t n = tc_data.num_chains();
		for (size_t c = 0; c < n; ++c) {
			// merge chain-level data
            tc_data.EST_prev()[c] = min_star(tc_data.EST_prev()[c], other_data.EST_prev()[c]);
			tc_data.out_availability()[c] = std::max(tc_data.out_availability()[c], other_data.out_availability()[c]);
			
			// merge task-level data in the chain
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

	/**
	 * @brief Print a label for the state extension (for graph visualization)
	 */
	void print_vertex_label(std::ostream& out, const typename Job<Time>::Job_set& jobs) const {
		out << "Task Chains: ";
		for (const auto& tc : jobs) out << tc.get_id() << ' ';
		out << '\n';
	}

private:
	/**
	 * @brief Clear all data in the state extension
	 */
	void clear() {
		possibly_running_jobs.clear();
		tasks_with_possibly_running_jobs.clear();
		tc_data.clear();
	}
	/**
	 * @brief Update the list of possibly running jobs after dispatching a new job
	 * @param this_state The new schedule state
	 * @param from The previous taskchains state extension
	 * @param j The index of the newly dispatched job
	 * @param start_times The start time interval of the newly dispatched job
	 * @param finish_times The finish time interval of the newly dispatched job
	 * @param ssd The state space data
	 */
    void update_possibly_running_jobs(
        const Schedule_state<Time>& this_state,
        const Taskchains_state_extension<Time>& from,
        Job_index j, Interval<Time> start_times,
        Interval<Time> finish_times,
        const State_space_data<Time>& ssd)
    {
		possibly_running_jobs.reserve(from.possibly_running_jobs.size() + 1);
		// add the task of the newly scheduled job j to the possibly running tasks
		tasks_with_possibly_running_jobs.add(ssd.jobs[j].get_task_id());
		// add all jobs that were possibly running in the previous state and that are not predecessors of j or have certainly finished by the time j starts
		//const auto& preds = ssd.get_finished_jobs_if_starts(j);
		const auto& job_cstr = ssd.inter_job_constraints[j];
		bool added_j = false;
        for (const auto& pj : from.possibly_running_jobs)
        {
            const auto& running_job = pj.j;
			auto idx = running_job->get_job_index();
			// add pj only if it has no precedence or mutual exclusion constraint with j and it is not certainly finished by the time j starts
            if (job_cstr.get_min_delay_after_finish_of(idx) == -1
                && pj.finish_time.max() >= this_state.core_availability(2).min()
                && pj.finish_time.max() > start_times.min())
            {
				// keep the list sorted by job index
                if (!added_j && idx > j) {
                    possibly_running_jobs.emplace_back(&ssd.jobs[j], finish_times);
                    added_j = true;
                }
				possibly_running_jobs.emplace_back(pj);
				tasks_with_possibly_running_jobs.add(running_job->get_task_id());
			}
		}
        if (!added_j) {
            possibly_running_jobs.emplace_back(&ssd.jobs[j], finish_times);
		}
    }
	/**
	 * @brief Check if a task may have a running job
	 * @param task_id The task ID to check
	 */
	bool may_have_running_job(unsigned long task_id) const { 
		return tasks_with_possibly_running_jobs.contains(task_id);
	}

	/**
	 * @brief Check if a task is certainly running in the given state
	 * @param task_id The task ID to check
	 * @param state The schedule state to check
	 * @param state_space_data The state space data
	 */
	bool is_certainly_running(unsigned long task_id, const Schedule_state<Time>& state, const State_space_data<Time>& state_space_data) const {
		const auto& cert_running_jobs = state.get_cert_running_jobs();
		for (const auto& rj : cert_running_jobs) {
			if (state_space_data.jobs[rj.idx].get_task_id() == task_id)
				return true;
		}
		return false;
	}

	/**
	 * @brief Check if a job did certainly not publish its output by time a certain time in the given state
	 * @param at The time to check against
	 * @param j The job index to check
	 * @param tc The task chain the job belongs to
	 * @param pos_in_chain The position of the job's task in the task chain
	 * @param state The schedule state to check
	 * @param state_space_data The state space data
	 */
	bool did_certainly_not_publish_output(Time at, Job_index j, const Task_chain<Time>& tc, size_t pos_in_chain, const Schedule_state<Time>& state, const State_space_data<Time>& state_space_data) const {
		// get finish time of job j
		Interval<Time> ft;
		state.get_finish_times(j, ft);
		// get output write interval of j's task in the task chain
		Interval<Time> output_interval = tc.get_write_interval(pos_in_chain);
		// check if the output could have been published by time 'at', if yes, return false
		Time earliest_pub_time = ft.min() - output_interval.max();
		if (earliest_pub_time <= at)
			return false;
		return true;
	}

	/**
	 * @brief Compute the minimum of two time values if they are both valid, otherwise return the only valid one, or INVALID if both are invalid
	 * @param a The first time value
	 * @param b The second time value
	 * @return The minimum of a and b, or the valid one if the other is INVALID
	 */
	inline Time min_star(const Time& a, const Time& b) const {
		/* Since INVALID is represented by infinity, std::min is equivalent to
		if (a == INVALID) return b;
		if (b == INVALID) return a;
		return std::min(a, b);*/
		return std::min(a, b);
	}

	/**
	 * @brief Update the task chains data after dispatching a new job
	 * @param from The previous taskchains state extension
	 * @param idx The index of the newly dispatched job
	 * @param ssd The state space data
	 * @param EST The earliest start time of the newly dispatched job
	 * @param LFT The latest finish time of the newly dispatched job
	 * @param ssd_ext_id The ID of the state space data extension
	 * @param new_state The new schedule state
	 */
	void update_task_chain_data(const Taskchains_state_extension<Time>& from, Job_index& idx,
		const State_space_data<Time>& ssd, const Time& EST, const Time&, const Time&, const Time& LFT,
		const size_t ssd_ext_id, const Schedule_state<Time>& new_state)
	{
		auto space_ext = ssd.get_extensions().get<Taskchains_sp_data_extension<Time>>(ssd_ext_id);
		const auto& chains = space_ext->get_task_chains();
		const Job<Time>& job = ssd.jobs[idx];
		const unsigned long tau_j = job.get_task_id();
		// Bulk copy previous chain data
		tc_data.copy_from(from.tc_data);

		const bool multiproc = tc_data.is_multiproc();

		// update *out* views
		for (const auto& tc : chains) {
			size_t tc_id = tc.get_id();
			size_t pos = tc_data.chain_offset[tc_id];
			const auto& tasks = tc.get_tasks();
			for (size_t k = 0; k < tasks.size(); ++k) {
				unsigned long tau_l = tasks[k];
				// NOTE: `may_have_running_job` does not work for uniprocessor platforms
				if (tau_l != tau_j && (!multiproc || !may_have_running_job(tau_l))) {
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
			size_t pos_in_chain = info.position_in_chain;
			bool is_source = (pos_in_chain == 0);
			bool is_sink = info.is_sink;
			size_t task_idx = tc_data.get_idx(tc_id, pos_in_chain);
			bool event_input = tc.uses_event_input();
			bool inst_output = tc.uses_instantaneous_output();

			if (is_source) {
				tc_data.EST_prev()[tc_id] = EST;
				if (event_input) {
					Time EST_prev = from.tc_data.EST_prev()[tc_id];
					Time earliest_possible_event = std::max(EST_prev, EST - tc.get_input_max_interarrival());
					tc_data.EIT_Age_int()[task_idx] = earliest_possible_event;
					tc_data.EIT_Reac_int()[task_idx] = min_star(earliest_possible_event, from.tc_data.EIT_Reac_int()[task_idx]);
				}
				else {
					tc_data.EIT_Age_int()[task_idx] = EST;
					tc_data.EIT_Reac_int()[task_idx] = min_star(EST, from.tc_data.EIT_Reac_int()[task_idx]);
				}
				// calculate max data age for the task
				Time data_age;
				if (inst_output)
					data_age = LFT - tc_data.EIT_Age_int()[task_idx];
				else {
					// time when the output is either erased or overridden by the new output
					Time max_availability_time = std::min(LFT, tc_data.out_availability()[tc_id]);
					data_age = max_availability_time - from.tc_data.EIT_Age_int()[task_idx];
				}
				space_ext->submit_data_age(tc_id, pos_in_chain, data_age);
				// calculate max reaction time for the task
				Time reaction_time = LFT - tc_data.EIT_Reac_int()[task_idx];
				space_ext->submit_reaction_time(tc_id, pos_in_chain, reaction_time);
			}
			else { 
				const auto& tasks = tc.get_tasks();
				size_t pred_task_idx = task_idx - 1;
				tc_data.EIT_Reac_out()[pred_task_idx] = INVALID;

				// NOTE: `pred_pos_running` is always false if the platform is uniprocessor since nothing else than tau_j can be running on the single available core
				bool pred_pos_running = multiproc && may_have_running_job(tasks[pos_in_chain - 1]);
				if (pred_pos_running)
					tc_data.EIT_Age_int()[task_idx] = from.tc_data.EIT_Age_out()[pred_task_idx];
				else
					tc_data.EIT_Age_int()[task_idx] = from.tc_data.EIT_Age_int()[pred_task_idx];
				
				// calculate max data age for the task
				Time data_age;
				if (inst_output)
					data_age = LFT - tc_data.EIT_Age_int()[task_idx];
				else {
					// time when the output is either erased or overridden by the new output
					Time max_availability_time = std::min(LFT, tc_data.out_availability()[tc_id]);
					data_age = max_availability_time - from.tc_data.EIT_Age_int()[task_idx];
				}
				space_ext->submit_data_age(tc_id, pos_in_chain, data_age);
				
				// NOTE: `pred_cert_running` is always false if the platform is uniprocessor since nothing else than tau_j can be running on the single available core
				bool pred_cert_running = multiproc && is_certainly_running(tasks[pos_in_chain - 1], new_state, ssd);
				if (pred_cert_running)
					tc_data.EIT_Reac_int()[task_idx] = min_star(from.tc_data.EIT_Reac_int()[task_idx], from.tc_data.EIT_Reac_out()[pred_task_idx]);
				else
					tc_data.EIT_Reac_int()[task_idx] = min_star(from.tc_data.EIT_Reac_int()[task_idx], min_star(from.tc_data.EIT_Reac_out()[pred_task_idx], from.tc_data.EIT_Reac_int()[pred_task_idx]));
				
				// calculate max reaction time for the task
				Time reaction_time = LFT - tc_data.EIT_Reac_int()[task_idx];
				space_ext->submit_reaction_time(tc_id, pos_in_chain, reaction_time);
			}
			
			if (is_sink) {
				tc_data.EIT_Reac_int()[task_idx] = INVALID;
				Time max_availability_time = LFT + tc.get_output_data_availability();
				if (max_availability_time < 0) // output data is available until the end of time
					max_availability_time = Time_model::constants<Time>::infinity();				
				tc_data.out_availability()[tc_id] = max_availability_time;
			}
		}
	}
};

} // namespace Taskchains_analysis
} // namespace Global
} // namespace NP

#endif // !STATE_EXT_TASKCHAINS_HPP
