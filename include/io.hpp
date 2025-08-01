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
#include "logger.hpp"
#ifdef CONFIG_PRUNING
#include "pruning_cond.hpp"
#endif

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

	inline bool more_fields_in_line(std::istream& in)
	{
		if (!in.good() || in.peek() == (int)'\n' || in.peek() == (int)'\r')
			return false;
		else
			return true;
	}

	inline void next_field(std::istream& in)
	{
	  	while(in.good() && (in.peek() == ',' || in.peek()==' '))
		{
			// eat up any trailing spaces
			skip_all(in, ' ');
			// eat up field separator
			skip_one(in, ',');
		}
	}

	inline void next_field(std::istream& in, char field_delimiter)
	{
		while (in.good() && (in.peek() == field_delimiter || in.peek() == ' '))
		{
			// eat up any trailing spaces
			skip_all(in, ' ');
			// eat up field separator
			skip_one(in, field_delimiter);
		}
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

	template<class Time>
	void parse_job_cost(std::istream& in, std::map<unsigned int, Interval<Time>>& costs)
	{
		Time cost_min, cost_max;
		skip_all(in, ' ');
		if (in.peek() == '{') { // expected format: { paral:cost_min:cost_max; paral:cost_min:cost_max; ... }
			unsigned int paral;

			skip_one(in, '{');
			skip_all(in, ' ');
			while (in.peek() != '}') {
				in >> paral;
				next_field(in, ':');
				in >> cost_min;
				next_field(in, ':');
				in >> cost_max;
				costs.emplace(std::make_pair(paral, Interval<Time>{cost_min, cost_max}));
				skip_all(in, ' ');
				if (in.peek() == ';')
					skip_one(in, ';');
			}
			skip_one(in, '}');
		}
		else { 
			in >> cost_min;
			next_field(in);
			in >> cost_max;
			costs.emplace(std::make_pair(1, Interval<Time>{ cost_min, cost_max }));
		}			
	}

	//Functions that help parse selfsuspending tasks file
	template<class Time>
	Precedence_constraint<Time> parse_precedence_constraint(std::istream &in)
	{
		unsigned long from_tid, from_jid, to_tid, to_jid;
		Time sus_min=0, sus_max=0;

		std::ios_base::iostate state_before = in.exceptions();

		in.exceptions(std::istream::failbit | std::istream::badbit);

		in >> from_tid;
		next_field(in);
		in >> from_jid;
		next_field(in);
		in >> to_tid;
		next_field(in);
		in >> to_jid;
		next_field(in);
		if (more_fields_in_line(in))
		{
			in >> sus_min;
			next_field(in);
			in >> sus_max;
		}

		in.exceptions(state_before);

		return Precedence_constraint<Time>{JobID{from_jid, from_tid},
											JobID{to_jid, to_tid},
		                          			Interval<Time>{sus_min, sus_max}};
	}

	template<class Time>
	std::vector<Precedence_constraint<Time>> parse_precedence_file(std::istream& in)
	{
		// skip column headers
		next_line(in);
		std::vector<Precedence_constraint<Time>> cstr;

		// parse all rows
		while (more_data(in)) {
			// each row contains one self-suspending constraint
			cstr.push_back(parse_precedence_constraint<Time>(in));
			next_line(in);
		}
		return cstr;
	}

	template<class Time>
	inline std::vector<Precedence_constraint<Time>> parse_yaml_dag_file(std::istream& in)
	{
		std::vector<Precedence_constraint<Time>> edges;
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
							edges.push_back(Precedence_constraint<Time>(from, to, {0, 0}));
						} else {
							auto tid = succ["Task ID"].as<unsigned long>();
							auto jid = succ["Job ID"].as<unsigned long>();
							auto to = JobID(jid, tid);
							edges.push_back(Precedence_constraint<Time>(from, to, {0, 0}));
						}
					}
				}
			}

		} catch (const YAML::Exception& e) {
			std::cerr << "Error reading YAML file: " << e.what() << std::endl;
		}
		return edges;
	}

	template<class Time> 
	Job<Time> parse_job(std::istream& in, std::size_t idx)
	{
		unsigned long tid, jid;

		std::ios_base::iostate state_before = in.exceptions();

		Time arr_min, arr_max, dl, prio;
		std::map<unsigned int, Interval<Time>> cost;
		unsigned int affinity = 0;

		in.exceptions(std::istream::failbit | std::istream::badbit);

		in >> tid;
		next_field(in);
		in >> jid;
		next_field(in);
		in >> arr_min;
		next_field(in);
		in >> arr_max;
		next_field(in);
		parse_job_cost(in, cost);
		next_field(in);
		in >> dl;
		next_field(in);
		in >> prio;
		next_field(in);
		if (more_fields_in_line(in))
		{
			in >> affinity;
		}

		in.exceptions(state_before);

		return Job<Time> {jid, Interval<Time>{arr_min, arr_max},
						cost, dl, prio, idx, affinity, tid};
	}

	template<class Time>
	typename Job<Time>::Job_set parse_csv_job_file(std::istream& in)
	{
		// first row contains a comment, just skip it
		next_line(in);

		typename Job<Time>::Job_set jobs;
		std::size_t idx=0;

		while (more_data(in)) {
			jobs.push_back(parse_job<Time>(in, idx));
			idx++;
			// munge any trailing whitespace or extra columns
			next_line(in);
		}

		return jobs;
	}

	//Functions that help parse the abort actions file
	template<class Time>
	typename Job<Time>::Job_set parse_yaml_job_file(std::istream& in)
	{
		typename Job<Time>::Job_set jobs;
        unsigned long tid, jid;
		std::size_t idx = 0;
        Time arr_min, arr_max, cost_min, cost_max, dl, prio;
		unsigned int affinity;
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
				if (j["Affinity"])
					affinity = j["Affinity"].as<int>();
				else
					affinity = 0;

				jobs.push_back(Job<Time>{jid, Interval<Time>{arr_min, arr_max},
										 Interval<Time>{cost_min, cost_max}, dl, prio, idx, tid, affinity});
				idx++;
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

	template<class Time>
	std::vector<Interval<Time>> parse_cluster(std::istream& in)
	{
		// Read mandatory field: number of cores (m)
		unsigned int m;
		in >> m;

		std::vector<Interval<Time>> cluster(m);

		for (unsigned int i = 0; i < m; i++) {
			if (more_fields_in_line(in)) {
				next_field(in);
				Time Amin, Amax;
				in >> Amin;
				next_field(in);
				in >> Amax;
				cluster[i] = Interval<Time>{ Amin, Amax };
			}
			else {
				cluster[i] = Interval<Time>{ 0, 0 };
			}
		}

		if (more_fields_in_line(in))
			std::cerr << "Warning: Extra fields in cluster specification CSV file, ignoring them." << std::endl;

		return cluster;
	}

	template<class Time>
	std::vector<std::vector<Interval<Time>>> parse_platform_spec_csv(std::istream& in)
	{
		// skip header line
		next_line(in);

		std::vector<std::vector<Interval<Time>>> clusters;

		while (more_data(in)) {
			clusters.push_back(parse_cluster<Time>(in));
			// munge any trailing whitespace or extra columns
			next_line(in);
		}

		return clusters;
	}

	template<class Time>
	std::vector<std::vector<Interval<Time>>> parse_platform_spec_yaml(std::istream& in)
	{
		std::vector<std::vector<Interval<Time>>> platform;
		try {
			in.clear();
			in.seekg(0, std::ios::beg);
			YAML::Node data = YAML::Load(in);
			YAML::Node plat = data["platform"];
			platform.resize(plat["clusters"].size());
			for (size_t i = 0; i < plat["clusters"].size(); ++i) {
				YAML::Node clstr = plat["clusters"][i];
				unsigned int m = clstr["cores"].as<unsigned int>();
				platform[i].resize(m, Interval<Time>{0, 0});
				if (clstr["core_availabilities"]) {
					YAML::Node avail = clstr["core_availabilities"];
					for (unsigned int j = 0; j < m && j < avail.size(); j++) {
						YAML::Node core = avail[j];
						if (core.IsSequence()) {
							Time Amin = core[0].as<Time>();
							Time Amax = core[1].as<Time>();
							platform[i][j] = Interval<Time>{ Amin, Amax };
						}
						else if (core.IsMap()) {
							Time Amin = core["Amin"].as<Time>();
							Time Amax = core["Amax"].as<Time>();
							platform[i][j] = Interval<Time>{ Amin, Amax };
						}
					}
				}
			}
		}
		catch (const YAML::Exception& e) {
			std::cerr << "Error reading platform specification YAML file: " << e.what() << std::endl;
		}
		return platform;
	}

	template<class Time>
	Global::Logging_condition<Time> parse_log_config_yaml(std::istream& in, const typename Job<Time>::Job_set& jobset, Global::Dot_file_config& dot_config)
	{		
		// Default values for logging condition parameters
		Interval<Time> time_interval(0, Time_model::constants<Time>::infinity());
		Interval<unsigned long> depth_interval(0, std::numeric_limits<unsigned long>::max());
		std::set<unsigned long> tasks;
		std::set<Job_index> jobs;
		std::set<Job_index> dispatched;
		std::set<Job_index> not_dispatched;
		bool deadline_miss = false;

		try {
			in.clear();
			in.seekg(0, std::ios::beg);
			YAML::Node config = YAML::Load(in);

			// Parse Conditions section
			if (config["Conditions"]) {
				YAML::Node conditions = config["Conditions"];

				// Parse Time interval
				if (conditions["Time"]) {
					YAML::Node time_node = conditions["Time"];
					if (time_node.IsSequence() && time_node.size() == 2) {
						Time time_min = (time_node[0].as<std::string>() == "-") ? 0 : time_node[0].as<Time>();
						Time time_max = (time_node[1].as<std::string>() == "-") ? 
							Time_model::constants<Time>::infinity() : time_node[1].as<Time>();
						time_interval = Interval<Time>(time_min, time_max);
					}
					else
						std::cerr << "Error reading log condition specification on time. This field is going to be ignored." << std::endl;
				}

				// Parse Depth interval
				if (conditions["Depth"]) {
					YAML::Node depth_node = conditions["Depth"];
					if (depth_node.IsSequence() && depth_node.size() == 2) {
						unsigned long depth_min = depth_node[0].as<unsigned long>();
						unsigned long depth_max = depth_node[1].as<unsigned long>();
						depth_interval = Interval<unsigned long>(depth_min, depth_max);
					}
					else
						std::cerr << "Error reading log condition specification on depth. This field is going to be ignored." << std::endl;
				}

				// Parse Tasks
				if (conditions["Tasks"]) {
					for (const auto& task : conditions["Tasks"]) {
						tasks.insert(task.as<unsigned long>());
					}
				}

				// Parse Jobs
				if (conditions["Jobs"]) {
					for (const auto& job : conditions["Jobs"]) {
						if (job.IsSequence() && job.size() == 2) {
							unsigned long task_id = job[0].as<unsigned long>();
							unsigned long job_id = job[1].as<unsigned long>();
							JobID jid(job_id, task_id);
							Job_index idx = std::distance(jobset.begin(), std::find_if(jobset.begin(), jobset.end(), [&task_id, &job_id](const Job<Time>& j) { return j.get_task_id() == task_id && j.get_job_id() == job_id; }));;
							jobs.insert(idx);
						}
						else
							std::cerr << "Error reading log condition specification on monitored jobs. This field is going to be ignored." << std::endl;
					}
				}

				// Parse Dispatched
				if (conditions["Dispatched"]) {
					for (const auto& job : conditions["Dispatched"]) {
						if (job.IsSequence() && job.size() == 2) {
							unsigned long task_id = job[0].as<unsigned long>();
							unsigned long job_id = job[1].as<unsigned long>();
							Job_index idx = std::distance(jobset.begin(), std::find_if(jobset.begin(), jobset.end(), [&task_id, &job_id](const Job<Time>& j) { return j.get_task_id() == task_id && j.get_job_id() == job_id; }));;
							dispatched.insert(idx);
						}
						else
							std::cerr << "Error reading log condition specification on monitored dispatched jobs. This field is going to be ignored." << std::endl;
					}
				}

				// Parse NotDispatched
				if (conditions["NotDispatched"]) {
					for (const auto& job : conditions["NotDispatched"]) {
						if (job.IsSequence() && job.size() == 2) {
							unsigned long task_id = job[0].as<unsigned long>();
							unsigned long job_id = job[1].as<unsigned long>();
							Job_index idx = std::distance(jobset.begin(), std::find_if(jobset.begin(), jobset.end(), [&task_id, &job_id](const Job<Time>& j) { return j.get_task_id() == task_id && j.get_job_id() == job_id; }));;
							not_dispatched.insert(idx);
						}
						else
							std::cerr << "Error reading log condition specification on monitored non-dispatched jobs. This field is going to be ignored." << std::endl;
					}
				}

				// Parse DeadlineMiss
				if (conditions["DeadlineMiss"]) {
					deadline_miss = conditions["DeadlineMiss"].as<std::string>() == "yes";
				}
			}

			// Parse LoggedInfo section
			if (config["LoggedInfo"]) {
				YAML::Node logged_info = config["LoggedInfo"];

				if (logged_info["DispatchedJob"]) {
					dot_config.print_dispatched_job = logged_info["DispatchedJob"].as<std::string>() == "yes";
				}
				if (logged_info["StartTime"]) {
					dot_config.print_start_times = logged_info["StartTime"].as<std::string>() == "yes";
				}
				if (logged_info["FinishTime"]) {
					dot_config.print_finish_times = logged_info["FinishTime"].as<std::string>() == "yes";
				}
				if (logged_info["JobParallelism"]) {
					dot_config.print_parallelism = logged_info["JobParallelism"].as<std::string>() == "yes";
				}
				if (logged_info["CoresAvailability"]) {
					dot_config.print_core_availability = logged_info["CoresAvailability"].as<std::string>() == "yes";
				}
				if (logged_info["CertRunningJobs"]) {
					dot_config.print_certainly_running_jobs = logged_info["CertRunningJobs"].as<std::string>() == "yes";
				}
				if (logged_info["PredFinishTimes"]) {
					dot_config.print_pred_finish_times = logged_info["PredFinishTimes"].as<std::string>() == "yes";
				}
				if (logged_info["ReadySuccessors"]) {
					dot_config.print_ready_successors = logged_info["ReadySuccessors"].as<std::string>() == "yes";
				}
				if (logged_info["NodeKey"]) {
					dot_config.print_node_key = logged_info["NodeKey"].as<std::string>() == "yes";
				}
			}

		} catch (const YAML::Exception& e) {
			std::cerr << "Error reading log configuration YAML file: " << e.what() << std::endl;
		}

		// Create logging condition with parsed parameters
		Global::Logging_condition<Time> logging_condition(
			time_interval, depth_interval, tasks, jobs, dispatched, not_dispatched, deadline_miss);

		return logging_condition;
	}

#ifdef CONFIG_PRUNING
    // Parse a Focused_expl_spec.yaml file and return a Pruning_condition
    // The YAML file must be loaded from the input stream 'in'.
    // The function receives the jobset (vector of Job<Time>) and returns a Pruning_condition.
    template<class Time>
    Pruning_condition parse_focused_expl_spec_yaml(std::istream& in, const std::vector<Job<Time>>& jobset)
    {
     // Parse YAML
     YAML::Node config;
     try {
         in.clear();
         in.seekg(0, std::ios::beg);
         config = YAML::Load(in);
     } catch (const YAML::Exception& e) {
         std::cerr << "Error reading YAML file: " << e.what() << std::endl;
         return Pruning_condition(jobset.size());
     }

	 Pruning_condition cond(jobset.size());

     // Parse Focus::jobset and Focus::taskset
     std::set<std::pair<unsigned long, unsigned long>> focus_jobset;
     std::set<unsigned long> focus_taskset;
     if (config["Focus"]) {
         auto focus = config["Focus"];
         if (focus["jobset"]) {
             for (const auto& job : focus["jobset"]) {
                 if (job.IsSequence() && job.size() == 2) {
                     focus_jobset.emplace(job[0].as<unsigned long>(), job[1].as<unsigned long>());
                 }
					else {
						std::cerr << "Error reading Focus::jobset. Expected a sequence of [TaskID, JobID]. This field is going to be ignored." << std::endl;
					}
             }
         }
         if (focus["taskset"]) {
             for (const auto& task : focus["taskset"]) {
                 focus_taskset.insert(task.as<unsigned long>());
             }
         }
     }

     // Parse Prune::tasks and Prune::jobs
     std::set<unsigned long> prune_tasks;
     std::set<std::pair<unsigned long, unsigned long>> prune_jobs;
     if (config["Prune"]) {
         auto prune = config["Prune"];
         if (prune["tasks"]) {
             for (const auto& task : prune["tasks"]) {
                 prune_tasks.insert(task.as<unsigned long>());
             }
         }
         if (prune["jobs"]) {
             for (const auto& job : prune["jobs"]) {
                 if (job.IsSequence() && job.size() == 2) {
                     prune_jobs.emplace(job[0].as<unsigned long>(), job[1].as<unsigned long>());
                 }
					else {
						std::cerr << "Error reading Prune::jobs. Expected a sequence of [TaskID, JobID]. This field is going to be ignored." << std::endl;
					}
             }
         }
     }

     // Parse Prune::jobsets and add to cond.stop_job_sets
     if (config["Prune"]) {
         auto prune = config["Prune"];
         if (prune["jobsets"]) {
             for (const auto& jobset_node : prune["jobsets"]) {
                 std::vector<Job_index> job_indices;
                 // Each jobset_node should be a sequence of [TaskID, JobID] pairs
                 if (jobset_node.IsSequence()) {
                     for (const auto& job : jobset_node) {
                         if (job.IsSequence() && job.size() == 2) {
                             unsigned long task_id = job[0].as<unsigned long>();
                             unsigned long job_id = job[1].as<unsigned long>();
                             // Find the index of this job in jobset
                             auto it = std::find_if(jobset.begin(), jobset.end(),
                                 [&task_id, &job_id](const Job<Time>& j) {
                                     return j.get_task_id() == task_id && j.get_job_id() == job_id;
                                 });
                             if (it != jobset.end()) {
                                 Job_index idx = it->get_job_index();
                                 job_indices.push_back(idx);
                             } else {
                                 std::cerr << "Warning: Prune::jobsets contains job [TaskID=" << task_id << ", JobID=" << job_id << "] not found in jobset." << std::endl;
                             }
                         } else {
                             std::cerr << "Error reading Prune::jobsets. Expected a sequence of [TaskID, JobID]. This entry is going to be ignored." << std::endl;
                         }
                     }
                     // Add the set to stop_job_sets for every job in the set
                     for (const auto& idx : job_indices) {
                         cond.stop_job_sets.emplace(idx, job_indices);
                     }
                 } else {
                     std::cerr << "Error reading Prune::jobsets. Expected a sequence of job sets. This entry is going to be ignored." << std::endl;
                 }
             }
         }
     }

	 // populate cond.prune_jobs based on prune_tasks, prune_jobs, focus_jobset, and focus_taskset
     for (size_t idx = 0; idx < jobset.size(); ++idx) {
         const auto& job = jobset[idx];
         unsigned long task_id = job.get_task_id();
         unsigned long job_id = job.get_job_id();
         // Prune if job is in Prune::jobs
         if (prune_jobs.count({task_id, job_id})) {
             cond.prune_jobs.add(idx);
             continue;
         }
         // Prune if job's task is in Prune::tasks
         if (prune_tasks.count(task_id)) {
             cond.prune_jobs.add(idx);
             continue;
         }
         // Prune if Focus::jobset is not empty and job is not in it
         if (!focus_jobset.empty() && !focus_jobset.count({task_id, job_id})) {
             cond.prune_jobs.add(idx);
             continue;
         }
         // Prune if Focus::taskset is not empty and job's task is not in it
         if (!focus_taskset.empty() && !focus_taskset.count(task_id)) {
             cond.prune_jobs.add(idx);
             continue;
         }
     }

     return cond;
    }
#endif
}

#endif
