#ifndef TASKS_HPP
#define TASKS_HPP

#include <ostream>
#include <vector>
#include <map>
#include <algorithm> // for find
#include <functional> // for hash
#include <exception>

#include "time.hpp"
#include "interval.hpp"

namespace NP {

	typedef std::size_t hash_value_t;
	typedef std::size_t Task_index;
    typedef std::size_t Subtask_index;
	
	template <class Time>
    class Subtask {
	   public:
	    typedef Time Priority;  // Make it a time value to support EDF;
	    typedef std::map<unsigned int, Interval<Time>> Exec_times;

	   private:
	    Interval<Time> release_offset;  // release offset w.r.t. the release of the task
	    Interval<unsigned int>
	        parallelism;       // on which range of core numbers can it run in parallel
	    Exec_times exec_time;  // execution time range w.r.t.the number of cores assigned to execute
	                           // a job of the task
	    Priority priority;     // subtask own priority
	    Time deadline;
	    Subtask_index index; // index in the set of subtasks of the task
	    Task_index task;

		hash_value_t key;

		void compute_hash() {
			auto h = std::hash<Time>{};
			//RV: added index to the hash key, which seems to prevent collisions in state/node lookup keys.
			key = h(index);
			key = (key << 4) ^ h(release_offset.from());
			key = (key << 4) ^ h(task);
			key = (key << 4) ^ h(release_offset.until());
			key = (key << 4) ^ h(exec_time.begin()->second.from());
			key = (key << 4) ^ h(deadline);
			key = (key << 4) ^ h(exec_time.begin()->second.upto());
			key = (key << 4) ^ h(priority);
		}

	   public:
	    Subtask(Interval<Time> rel_offset, const Exec_times& costs, Time dl,
	         Priority prio, Task_index tsk, Subtask_index idx)
	        : release_offset(rel_offset),
	          exec_time(costs),
	          parallelism(costs.begin()->first, costs.rbegin()->first),
	          deadline(dl),
	          priority(prio),
	          index(idx),
	          task(tsk)
		{
			compute_hash();
	    }

	    Subtask(Interval<Time> rel_offset, Interval<Time> cost, Time dl, Priority prio,
	         Task_index tsk, Subtask_index idx)
	        : release_offset(rel_offset),
	          parallelism(Interval<unsigned int>{1, 1}),
	          deadline(dl),
	          priority(prio),
	          index(idx),
	          task(tsk)
		{
		    exec_time.emplace(1, cost);
			compute_hash();
	    }

		hash_value_t get_key() const
		{
			return key;
		}

	    Time smallest_offset() const {
		    return release_offset.min();
	    }

	    Time longest_offset() const {
		    return release_offset.max();
	    }

		Time get_deadline() const {
		    return deadline;
	    }

	    bool exceeds_deadline(Time arrival, Time t) const {
		    return t > arrival + deadline &&
		           (t - (arrival + deadline)) >
		               Time_model::constants<Time>::deadline_miss_tolerance();
	    }

	    Time least_exec_time(unsigned int ncores = 1) const {
		    assert(ncores >= parallelism.min() && ncores <= parallelism.max());
		    auto cost = exec_time.find(ncores);
		    if (cost == exec_time.end())
			    return Time_model::constants<Time>::infinity();
		    else
			    return cost->second.min();
	    }

	    Time maximal_exec_time(unsigned int ncores = 1) const {
		    assert(ncores >= parallelism.min() && ncores <= parallelism.max());
		    auto cost = exec_time.find(ncores);
		    if (cost == exec_time.end())
			    return Time_model::constants<Time>::infinity();
		    else
			    return cost->second.max();
	    }

	    // return the execution time bounds for a given level of parallelism
	    Interval<Time> get_cost(unsigned int ncores = 1) const {
		    assert(ncores >= parallelism.min() && ncores <= parallelism.max());
		    auto cost = exec_time.find(ncores);
		    if (cost == exec_time.end())
			    return Interval<Time>{Time_model::constants<Time>::infinity(),
			                          Time_model::constants<Time>::infinity()};
		    else
			    return cost->second;
	    }

	    // return all possible levels of parallelism and associated execution time bounds
	    const Exec_times& get_all_costs() const {
		    return exec_time;
	    }

	    int get_next_parallelism(unsigned int ncores) const {
		    assert(ncores < parallelism.max());
		    auto it = exec_time.upper_bound(ncores);
		    if (it == exec_time.end())
			    return -1;
		    else
			    return it->first;
	    }

	    Priority get_priority() const {
		    return priority;
	    }

	    unsigned int get_min_parallelism() const {
		    return parallelism.min();
	    }

	    unsigned int get_max_parallelism() const {
		    return parallelism.max();
	    }

	    const Interval<unsigned int>& get_parallelism() const {
		    return parallelism;
	    }

		Subtask_index id() const {
		    return index;
	    }

		Task_index task_id() const {
		    return task;
	    }

	    bool higher_priority_than(const Subtask& other) const {
		    return priority < other.priority
		           // tie-break by task ID
		           || (priority == other.priority && index < other.index);
	    }

	    bool priority_at_least_that_of(const Subtask& other) const {
		    return priority <= other.priority;
	    }

	    bool priority_exceeds(Priority prio_level) const {
		    return priority < prio_level;
	    }

	    bool priority_at_least(Priority prio_level) const {
		    return priority <= prio_level;
	    }

	    friend std::ostream& operator<<(std::ostream& stream, const Subtask& t) {
		    stream << "Sub-task{" << t.index << ", " << t.release_offset << ", ";
		    for (auto i : t.exec_time) stream << i.first << " cores: " << i.second << ", ";
		    << t.priority << "}";
		    return stream;
	    }
    };

	template<class Time> 
	class Task {

	public:
		typedef std::vector<Task<Time>> Task_set;
		typedef const Subtask<Time>* Subtask_ref;
	    typedef Time Priority;

		struct Precedence_cstr {
		    Subtask_ref subtask;
		    Interval<Time> delay;
		};
		struct Successors {
		    std::vector<Precedence_cstr> start_after_start; // set of jobs that must start after j starts
		    std::vector<Precedence_cstr> start_after_finish; // set of jobs that must start after j finishes
		    std::vector<Precedence_cstr> exclusions; // set of jobs that must not execute in parallel to j, i.e., it must either precede or succede j

			void add_after_start_successor(Subtask_ref subtask, Interval<Time> delay) {
			    Precedence_cstr cstr;
			    cstr.subtask = subtask;
			    cstr.delay = delay;
			    start_after_start.push_back(cstr);
		    }

			void add_after_finish_successor(Subtask_ref subtask, Interval<Time> delay) {
			    Precedence_cstr cstr;
			    cstr.subtask = subtask;
			    cstr.delay = delay;
			    start_after_finish.push_back(cstr);
		    }

			void add_execlusion_cstr(Subtask_ref subtask, Interval<Time> delay) {
			    Precedence_cstr cstr;
			    cstr.subtask = subtask;
			    cstr.delay = delay;
				exclusions.push_back(cstr);
		    }
		};
	    struct Predecessors {
		    std::vector<Precedence_cstr> start_before_start; // set of jobs that must start before j starts
		    std::vector<Precedence_cstr> finish_before_start; // set of jobs that must finish before j starts
		    std::vector<Precedence_cstr> exclusions; // set of jobs that must not execute in parallel to j, i.e., it must either precede or succede j

			void add_after_start_predecessor(Subtask_ref subtask, Interval<Time> delay) {
			    Precedence_cstr cstr;
			    cstr.subtask = subtask;
			    cstr.delay = delay;
			    start_before_start.push_back(cstr);
		    }

		    void add_after_finish_predecessor(Subtask_ref subtask, Interval<Time> delay) {
			    Precedence_cstr cstr;
			    cstr.subtask = subtask;
			    cstr.delay = delay;
			    finish_before_start.push_back(cstr);
		    }

		    void add_execlusion_cstr(Subtask_ref subtask, Interval<Time> delay) {
			    Precedence_cstr cstr;
			    cstr.subtask = subtask;
			    cstr.delay = delay;
			    exclusions.push_back(cstr);
		    }
	    };
		
	private:
	    Interval<Time> release_offset; // release offset of the first job of the task
		Time release_jitter;
	    Interval<Time> inter_arrival;  // minimum and maximum inter-arrival time between consecutive jobs
		Time deadline;
		hash_value_t key;
		Task_index index;  // index in the task set.
	    std::vector<Subtask_ref> subtasks; // list of subtasks 

		// list of precedence constraints between subtasks
	    std::vector<Successors> successors_of;
	    std::vector<Predecessors> predecessors_of; 

		void compute_hash() {
			auto h = std::hash<Time>{};
			//RV: added index to the hash key, which seems to prevent collisions in state/node lookup keys.
			key = h(index);
		    key = (key << 4) ^ h(inter_arrival.from());
		    key = (key << 4) ^ h(index);
		    key = (key << 4) ^ h(inter_arrival.until());
			key = (key << 4) ^ h(release_offset.from());
			key = (key << 4) ^ h(deadline);
			key = (key << 4) ^ h(release_offset.upto());
		    key = (key << 4) ^ h(release_jitter);
		}

	public:

		Task(Interval<Time> inter_arr, Time rel_jitter, Interval<Time> rel_offset, Time dl,
	      Priority prio, Task_index idx,
	      const std::vector<Subtask_ref> subtasks, const std::vector<Successors>& successors_of,
	      const std::vector<Predecessors>& predecessors_of)
		  : inter_arrival(inter_arr), release_jitter(rel_jitter), exec_time(costs),
	       release_offset(rel_offset),
	       deadline(dl),
	       priority(prio),
	       index(idx),
	       subtasks(subtasks),
	       successors_of(successors_of),
	       predecessors_of(predecessors_of)
		{
			compute_hash();
		}

		hash_value_t get_key() const
		{
			return key;
		}

		Time earliest_next_arrival(Time last_arrival) const
		{
		    return last_arrival + inter_arrival.min();
		}

		Time latest_next_arrival(Time last_arrival) const
		{
		    return last_arrival + inter_arrival.max();
		}

		Time latest_next_release(Time last_arrival) const {
		    return last_arrival + inter_arrival.max() + release_jitter;
	    }

		Priority get_priority() const
		{
			return priority;
		}

		Time get_deadline() const
		{
			return deadline;
		}

		bool exceeds_deadline(Time arrival, Time t) const
		{
			return t > arrival + deadline
			       && (t - (arrival + deadline)) >
			          Time_model::constants<Time>::deadline_miss_tolerance();
		}

		Task_index id() const
		{
			return index;
		}

		const std::vector<Subtask_ref>& get_subtasks() const {
		    return subtasks;
	    }

		Subtask_ref get_subtask(Subtask_index i) const {
		    assert(i < subtasks.size());
			return subtasks[i];
	    }

		const std::vector<Successors>& get_successors() const {
		    return successors_of;
	    }

		const std::vector<Predecessors>& get_predecessors() const {
		    return predecessors_of;
	    }

		const Predecessors& get_predecessors_of(Subtask_index subtask) const {
		    assert(subtask < subtasks.size());
		    return predecessors_of[subtask];
	    }

		const Successors& get_successors_of(Subtask_index subtask) const {
		    assert(subtask < subtasks.size());
		    return successors_of[subtask];
	    }

		bool higher_priority_than(const Task& other) const
		{
			return priority < other.priority
			       // tie-break by task ID
			       || (priority == other.priority
			           && index < other.index);
		}

		bool priority_at_least_that_of(const Task& other) const
		{
			return priority <= other.priority;
		}

		bool priority_exceeds(Priority prio_level) const
		{
			return priority < prio_level;
		}

		bool priority_at_least(Priority prio_level) const
		{
			return priority <= prio_level;
		}

		friend std::ostream& operator<< (std::ostream& stream, const Task& t)
		{
		    stream << "Task{" << t.index << ", " << t.release_offset << ", " << t.inter_arrival
		           << ", " << t.release_jitter << ", ";
			stream << t.deadline << ", " << t.priority << "}";
			return stream;
		}
	};

}

namespace std {
	template<class T> struct hash<NP::Task<T>>
	{
		std::size_t operator()(NP::Task<T> const& t) const
		{
			return t.get_key();
		}
	};
}

#endif
