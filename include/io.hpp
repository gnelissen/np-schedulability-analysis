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

	//Functions that help parse the abort actions file
	template<class Time>
	typename Task<Time>::Task_set parse_tasks_file(std::istream& in)
	{
		typename Task<Time>::Task_set tasks;
        // task parameters
		std::string tid;
		Time arr_min, arr_max, interarr_min, interarr_max, jitter, dl;
		// subtask parameters
		std::string sid;
		Time off_min, off_max, cost_min, cost_max, sdeadline, prio;
		unsigned int parall;
		// predecessor parameters
		Time delay_min, delay_max;
		std::string pred_id;

		try {
			YAML::Node input_task_set = YAML::Load(in);

			// extract tasks parameters
			auto const ts = input_task_set["taskset"];
			unsigned int tidx = 0;
			for (auto const &t: ts) {
				tid = t["id"].as<std::string>();
				if(t["arrival"]) {
					auto const arr = t["arrival"];
					arr_min = arr["min"].as<Time>();
					arr_max = arr["max"].as<Time>();
				}
				else {
					arr_min = 0;
					arr_max = 0;
				}
				dl = t["deadline"].as<Time>();
				auto const inter_arr = t["interarrival"];
				interarr_min = inter_arr["min"].as<Time>();
				interarr_max = inter_arr["max"].as<Time>();
				if(t["releaseJitter"])
					jitter = t["releaseJitter"].as<Time>();
				else
					jitter = 0;

				// extract the subtasks parameters
				std::vector<Subtask<Time>> subtasks;
				auto const stsk = t["segments"];
				unsigned int stidx = 0;
				for (const auto& s : stsk) {
					sid = s["id"].as<std::string>();
					typename Subtask<Time>::Exec_times costs;
					for (const auto& c : s["execTime"]) {
						if (c["parallelism"])
							parall = c["parallelism"].as<unsigned int>();
						else
							parall = 1;
						cost_min = c["bcet"].as<Time>();
						cost_max = c["wcet"].as<Time>();
						costs.emplace(parall, Interval<Time>{cost_min, cost_max});
					}
					prio = s["priority"].as<Time>();
					if (s["deadline"])
						sdeadline = s["deadline"].as<Time>();
					else
						sdeadline = dl;
					if (s["offset"]) {
						const auto offset = s["offset"];
						off_min = offset["min"].as<Time>();
						off_max = offset["max"].as<Time>();
					}
					else {
						off_min = 0;
						off_max = 0;
					}

					// create the subtask
					subtasks.emplace_back(tid + ":" + sid, tidx, stidx, Interval<Time>{off_min, off_max}, costs, sdeadline, prio);

					++stidx;
				}

				// extract precedence constraints
				std::vector<typename Task<Time>::Predecessors> predecessors(subtasks.size());
				std::vector<typename Task<Time>::Successors> successors(subtasks.size());
				for (int i = 0; i < subtasks.size(); i++) {
					if (stsk[i]["predecessors"]) {
						const auto pred_cstr = stsk[i]["predecessors"];
						if (pred_cstr["startBefore"]) {
							for (const auto& p : pred_cstr["startBefore"]) {
								pred_id = p["id"].as<std::string>();
								if (p["delay"]) {
									const auto delay = p["delay"];
									delay_min = delay["min"].as<Time>();
									delay_max = delay["max"].as<Time>();
								}
								else {
									delay_min = 0;
									delay_max = 0;
								}

								std::string predname = tid + ":" + pred_id;
								const Subtask_index pred = lookup(subtasks, predname);
								predecessors[i].add_start_before(pred, Interval<Time>{delay_min, delay_max});
								successors[pred].add_after_start(i, Interval<Time>{delay_min, delay_max});
							}
						}
						if (pred_cstr["finishBefore"]) {
							for (const auto& p : pred_cstr["finishBefore"]) {
								pred_id = p["id"].as<std::string>();
								if (p["delay"]) {
									const auto delay = p["delay"];
									delay_min = delay["min"].as<Time>();
									delay_max = delay["max"].as<Time>();
								}
								else {
									delay_min = 0;
									delay_max = 0;
								}

								std::string predname = tid + ":" + pred_id;
								const Subtask_index pred = lookup(subtasks, predname);
								predecessors[i].add_finish_before(pred, Interval<Time>{delay_min, delay_max});
								successors[pred].add_after_finish(i, Interval<Time>{delay_min, delay_max});
							}
						}
					}
				}

				if (t["exclusionConstraints"]) {
					for (const auto& e : t["exclusionConstraints"]) {
						auto const subtsks = e["segments"];
						std::string sa = subtsks[0].as<std::string>();
						std::string sb = subtsks[1].as<std::string>();
						Time sep = e["minSeparation"].as<Time>();

						std::string nameSa = tid + ":" + sa;
						std::string nameSb = tid + ":" + sb;
						const Subtask_index sbtska = lookup(subtasks, nameSa);
						const Subtask_index sbtskb = lookup(subtasks, nameSb);
						successors[sbtska].add_exclusion_cstr(sbtskb, Interval<Time>{sep, sep});
						successors[sbtskb].add_exclusion_cstr(sbtska, Interval<Time>{sep, sep});
						predecessors[sbtska].add_exclusion_cstr(sbtskb, Interval<Time>{sep, sep});
						predecessors[sbtskb].add_exclusion_cstr(sbtska, Interval<Time>{sep, sep});
					}
				}
				tasks.emplace_back(tidx, Interval<Time>{interarr_min, interarr_max}, jitter, Interval<Time>{arr_min, arr_max}, dl, subtasks, successors, predecessors);
				++tidx;
			}
		} catch (const YAML::Exception& e) {
			std::cerr << "Error reading YAML file: " << e.what() << std::endl;
		}

		return tasks;
	}
}

#endif
