#ifndef IO_HPP
#define IO_HPP

#include <iostream>
#include <utility>

#include "interval.hpp"
#include "time.hpp"
#include "jobs.hpp"
#include "precedence.hpp"
#include "aborts.hpp"
#include "yaml-cpp/yaml.h"

namespace NP {

	inline void skip_over(std::istream& in, char c)
	{
		while (in.good() && in.get() != (int) c)
			/* skip */;
	}

	inline bool skip_one(std::istream& in, char c)
	{
		if (in.good() && in.peek() == (int) c) {
			in.get(); /* skip */
			return true;
		} else
			return false;
	}

	inline void skip_all(std::istream& in, char c)
	{
		while (skip_one(in, c))
			/* skip */;
	}

	inline bool more_data(std::istream& in)
	{
		in.peek();
		return !in.eof();
	}

	inline void next_field(std::istream& in)
	{
		// eat up any trailing spaces
		skip_all(in, ' ');
		// eat up field separator
		skip_one(in, ',');
	}

	inline void next_line(std::istream& in)
	{
		skip_over(in, '\n');
	}

	inline JobID parse_job_id(std::istream& in)
	{
		unsigned long jid, tid;

		in >> tid;
		next_field(in);
		in >> jid;
		return JobID(jid, tid);
	}

	inline Precedence_constraint parse_precedence_constraint(std::istream &in)
	{
		std::ios_base::iostate state_before = in.exceptions();
		in.exceptions(std::istream::failbit | std::istream::badbit);

		// first two columns
		auto from = parse_job_id(in);

		next_field(in);

		// last two columns
		auto to = parse_job_id(in);

		in.exceptions(state_before);

		return Precedence_constraint(from, to);
	}

	inline Precedence_constraints parse_dag_file(std::istream& in)
	{
		Precedence_constraints edges;

		// skip column headers
		next_line(in);

		// parse all rows
		while (more_data(in)) {
			// each row contains one precedence constraint
			edges.push_back(parse_precedence_constraint(in));
			next_line(in);
		}

		return edges;
	}

	inline Precedence_constraints parse_yaml_dag_file(std::istream& in)
	{
		Precedence_constraints edges;
		// Clear any flags
		in.clear();
		// Move the pointer to the beginning
		in.seekg(0, std::ios::beg);
		try {
			// read the YAML file
			YAML::Node input_job_set = YAML::Load(in);
			auto const js = input_job_set["jobset"];
			// Iterate over each jobset entry
			for (auto const &j : js) {
				// Check if a job has a successor
				if (j["Successors"]) {
					auto from = JobID(j["Job ID"].as<unsigned long>(), j["Task ID"].as<unsigned long>());
					// Iterate over each successor
					for (const auto &succ: j["Successors"]) {
						// first, we need to check to see if it is written
						// in the compact form [TaskID, JobID]
						// or the expanded form
						// - Task ID: Int
						// 	 Job ID: Int
						if (succ.IsSequence()) {
							auto tid = succ[0].as<unsigned long>();
							auto jid = succ[1].as<unsigned long>();
							auto to = JobID(jid, tid);
							edges.push_back(Precedence_constraint(from, to));
						} else {
							auto tid = succ["Task ID"].as<unsigned long>();
							auto jid = succ["Job ID"].as<unsigned long>();
							auto to = JobID(jid, tid);
							edges.push_back(Precedence_constraint(from, to));
						}
					}
				}
			}

		} catch (const YAML::Exception& e) {
			std::cerr << "Error reading YAML file: " << e.what() << std::endl;
		}
		return edges;
	}

	template<class Time> Job<Time> parse_job(std::istream& in)
	{
		unsigned long tid, jid;

		std::ios_base::iostate state_before = in.exceptions();

		Time arr_min, arr_max, cost_min, cost_max, dl, prio;

		in.exceptions(std::istream::failbit | std::istream::badbit);

		in >> tid;
		next_field(in);
		in >> jid;
		next_field(in);
		in >> arr_min;
		next_field(in);
		in >> arr_max;
		next_field(in);
		in >> cost_min;
		next_field(in);
		in >> cost_max;
		next_field(in);
		in >> dl;
		next_field(in);
		in >> prio;

		in.exceptions(state_before);

		return Job<Time>{jid, Interval<Time>{arr_min, arr_max},
						 Interval<Time>{cost_min, cost_max}, dl, prio, tid};
	}

	template<class Time>
	typename Job<Time>::Job_set parse_csv_job_file(std::istream& in)
	{
		// first row contains a comment, just skip it
		next_line(in);

		typename Job<Time>::Job_set jobs;

		while (more_data(in)) {
			jobs.push_back(parse_job<Time>(in));
			// munge any trailing whitespace or extra columns
			next_line(in);
		}

		return jobs;
	}

	template<class Time>
	typename Job<Time>::Job_set parse_yaml_job_file(std::istream& in)
	{
		typename Job<Time>::Job_set jobs;
        unsigned long tid, jid;
        Time arr_min, arr_max, cost_min, cost_max, dl, prio;
		try {
			YAML::Node input_job_set = YAML::Load(in);

			auto const js = input_job_set["jobset"];
			for (auto const &j: js) {
				tid = j["Task ID"].as<unsigned long>();
				jid = j["Job ID"].as<unsigned long>();
				arr_min = j["Arrival min"].as<Time>();
				arr_max = j["Arrival max"].as<Time>();
				cost_min = j["Cost min"].as<Time>();
				cost_max = j["Cost max"].as<Time>();
				dl = j["Deadline"].as<Time>();
				prio = j["Priority"].as<Time>();

				jobs.push_back(Job<Time>{jid, Interval<Time>{arr_min, arr_max},
										 Interval<Time>{cost_min, cost_max}, dl, prio, tid});
			}
		} catch (const YAML::Exception& e) {
			std::cerr << "Error reading YAML file: " << e.what() << std::endl;
		}

		return jobs;
	}

	template<class Time>
	Abort_action<Time> parse_abort_action(std::istream& in)
	{
		unsigned long tid, jid;
		Time trig_min, trig_max, cleanup_min, cleanup_max;

		std::ios_base::iostate state_before = in.exceptions();

		in.exceptions(std::istream::failbit | std::istream::badbit);

		in >> tid;
		next_field(in);
		in >> jid;
		next_field(in);
		in >> trig_min;
		next_field(in);
		in >> trig_max;
		next_field(in);
		in >> cleanup_min;
		next_field(in);
		in >> cleanup_max;

		in.exceptions(state_before);

		return Abort_action<Time>{JobID{jid, tid},
		                          Interval<Time>{trig_min, trig_max},
		                          Interval<Time>{cleanup_min, cleanup_max}};
	}


	template<class Time>
	std::vector<Abort_action<Time>> parse_abort_file(std::istream& in)
	{
		// first row contains a comment, just skip it
		next_line(in);

		std::vector<Abort_action<Time>> abort_actions;

		while (more_data(in)) {
			abort_actions.push_back(parse_abort_action<Time>(in));
			// munge any trailing whitespace or extra columns
			next_line(in);
		}

		return abort_actions;
	}


}

#endif
