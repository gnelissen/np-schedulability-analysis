#ifndef JOBS_HPP
#define JOBS_HPP

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
	typedef std::size_t Job_index;

	struct JobID {
		unsigned long job;
		unsigned long task;

		JobID(unsigned long j_id, unsigned long t_id)
		: job(j_id), task(t_id)
		{
		}

		bool operator==(const JobID& other) const
		{
			return this->task == other.task && this->job == other.job;
		}

		friend std::ostream& operator<< (std::ostream& stream, const JobID& id)
		{
			stream << "T" << id.task << "J" << id.job;
			return stream;
		}
	};

	
	template<class Time> class Job {

	public:
		typedef std::vector<Job<Time>> Job_set;
		typedef Time Priority; // Make it a time value to support EDF
		typedef std::map<unsigned int, Interval<Time>> Cost;

	private:
		Interval<Time> arrival;
		Interval<unsigned int> parallelism; // on which range of core numbers can it run in parallel
		Cost exec_time; // execution time range depending on the number of cores assigned to the job to execute
		Time bcet; // least execution time for the job among all its parallelism levels
		Time deadline;
		Priority priority;
		JobID id;
		unsigned int affinity; // which cluster of processors this job is pinned to
		hash_value_t key;
		Job_index index;  // RV: index in the jobs array of the workload.

		void compute_hash() {
			auto h = std::hash<Time>{};
			//RV: added index to the hash key, which seems to prevent collisions in state/node lookup keys.
			key = h(index);
			key = (key << 4) ^ h(arrival.from());
			key = (key << 4) ^ h(id.task);
			key = (key << 4) ^ h(arrival.until());
			key = (key << 4) ^ h(exec_time.begin()->second.from());
			key = (key << 4) ^ h(deadline);
			key = (key << 4) ^ h(exec_time.begin()->second.upto());
			key = (key << 4) ^ h(id.job);
			key = (key << 4) ^ h(priority);
		}

	public:

		Job(unsigned long id,
			Interval<Time> arr, const Cost& costs,
			Time dl, Priority prio,
			Job_index idx,
			unsigned int affinity = 0,
			unsigned long tid = 0)
		: arrival(arr), exec_time(costs), parallelism(costs.begin()->first, costs.rbegin()->first),
		  deadline(dl), priority(prio), id(id, tid), index(idx), affinity(affinity)
		{
			compute_hash();
			if (exec_time.empty()) {
				throw std::invalid_argument("Job must have at least one execution time defined.");
			}
			// compute the least execution time (bcet) for the job
			bcet = Time_model::constants<Time>::infinity();
			for (const auto& cost : costs) {
				bcet = std::min(bcet, cost.second.min());
			}
		}

		Job(unsigned long id,
			Interval<Time> arr, Interval<Time> cost,
			Time dl, Priority prio,
			Job_index idx,
			unsigned int affinity = 0,
			unsigned long tid = 0)
			: arrival(arr), parallelism(Interval<unsigned int>{ 1, 1 }),
			deadline(dl), priority(prio), id(id, tid), index(idx), affinity(affinity)
		{
			exec_time.emplace(1, cost);
			bcet = cost.min();
			compute_hash();
		}

		hash_value_t get_key() const
		{
			return key;
		}

		Time earliest_arrival() const
		{
			return arrival.from();
		}

		Time latest_arrival() const
		{
			return arrival.until();
		}

		const Interval<Time>& arrival_window() const
		{
			return arrival;
		}

		Time least_exec_time(unsigned int ncores = 1) const
		{
			assert(ncores >= parallelism.min() && ncores <= parallelism.max());
			auto cost = exec_time.find(ncores);
			if (cost == exec_time.end())
				return Time_model::constants<Time>::infinity();
			else
				return cost->second.min();
		}

		Time maximal_exec_time(unsigned int ncores = 1) const
		{
			assert(ncores >= parallelism.min() && ncores <= parallelism.max());
			auto cost = exec_time.find(ncores);
			if (cost == exec_time.end())
				return Time_model::constants<Time>::infinity();
			else
				return cost->second.max();
		}

		Time get_bcet() const
		{
			return bcet;
		}


		// return the execution time bounds for a given level of parallelism
		Interval<Time> get_cost(unsigned int ncores = 1) const
		{
			assert(ncores >= parallelism.min() && ncores <= parallelism.max());
			auto cost = exec_time.find(ncores);
			if (cost == exec_time.end())
				return Interval<Time>{Time_model::constants<Time>::infinity(), Time_model::constants<Time>::infinity()};
			else
				return cost->second;
		}

		// return all possible levels of parallelism and associated execution time bounds
		const Cost& get_all_costs() const
		{
			return exec_time;
		}

		int get_next_parallelism(unsigned int ncores) const
		{
			assert(ncores < parallelism.max());
			auto it = exec_time.upper_bound(ncores);
			if (it == exec_time.end())
				return -1;
			else
				return it->first;

		}

		Priority get_priority() const
		{
			return priority;
		}

		unsigned int get_min_parallelism() const
		{
			return parallelism.min();
		}

		unsigned int get_max_parallelism() const
		{
			return parallelism.max();
		}

		const Interval<unsigned int>& get_parallelism() const
		{
			return parallelism;
		}

		Time get_deadline() const
		{
			return deadline;
		}

		bool exceeds_deadline(Time t) const
		{
			return t > deadline
			       && (t - deadline) >
			          Time_model::constants<Time>::deadline_miss_tolerance();
		}

		JobID get_id() const
		{
			return id;
		}

		unsigned long get_job_id() const
		{
			return id.job;
		}

		unsigned long get_task_id() const
		{
			return id.task;
		}

		bool is(const JobID& search_id) const
		{
			return this->id == search_id;
		}

				Job_index get_job_index() const
		{
			return index;
		}

		unsigned int get_affinity() const
		{
			return affinity;
		}

		bool higher_priority_than(const Job &other) const
		{
			return priority < other.priority
			       // first tie-break by task ID
			       || (priority == other.priority
			           && id.task < other.id.task)
			       // second, tie-break by job ID
			       || (priority == other.priority
			           && id.task == other.id.task
			           && id.job < other.id.job);
		}

		bool priority_at_least_that_of(const Job &other) const
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

		Interval<Time> scheduling_window() const
		{
			// inclusive interval, so take off one epsilon
			return Interval<Time>{
			                earliest_arrival(),
			                deadline - Time_model::constants<Time>::epsilon()};
		}

		static Interval<Time> scheduling_window(const Job& j)
		{
			return j.scheduling_window();
		}

		friend std::ostream& operator<< (std::ostream& stream, const Job& j)
		{
			stream << "Job{" << j.id.task << ", " << j.id.job << ", " << j.arrival << ", ";
			for (auto i : j.exec_time)
				stream << i.first << " cores: " << i.second << ", ";
			stream << j.deadline << ", " << j.priority << "}";
			return stream;
		}

	};

	template<class Time>
	bool contains_job_with_id(const typename Job<Time>::Job_set& jobs,
	                          const JobID& id)
	{
		auto pos = std::find_if(jobs.begin(), jobs.end(),
		                        [id] (const Job<Time>& j) { return j.is(id); } );
		return pos != jobs.end();
	}

	class InvalidJobReference : public std::exception
	{
		public:

		InvalidJobReference(const JobID& bad_id)
		: ref(bad_id)
		{}

		const JobID ref;

		virtual const char* what() const noexcept override
		{
			return "invalid job reference";
		}

	};

	template<class Time>
	const Job<Time>& lookup(const typename Job<Time>::Job_set& jobs,
	                                 const JobID& id)
	{
		auto pos = std::find_if(jobs.begin(), jobs.end(),
		                        [id] (const Job<Time>& j) { return j.is(id); } );
		if (pos == jobs.end())
			throw InvalidJobReference(id);
		return *pos;
	}

}

namespace std {
	template<class T> struct hash<NP::Job<T>>
	{
		std::size_t operator()(NP::Job<T> const& j) const
		{
			return j.get_key();
		}
	};

	template<> struct hash<NP::JobID>
	{
		std::size_t operator()(NP::JobID const& id) const
		{
			hash<unsigned long> h;
			return (h(id.job) << 4) ^ h(id.task);
		}

	};
}

#endif
