#ifndef TASKCHAINS_HPP
#define TASKCHAINS_HPP

#include <vector>
#include "io.hpp"
#include "jobs.hpp"

namespace NP{
    namespace Global {
        namespace Taskchains_analysis {

            // Class representing a task chain
            template<class Time>
            class Task_chain {
            private:
                // unique identifier for the task chain
                const unsigned long id;
                // name of the task chain
                const std::string name;

                // a task chain is a sequence of tasks
                const std::vector<unsigned long> tasks;
                // whether the task chain uses event-triggered input (i.e., task-unrelated events that may happen anytime) or sensor-based input (i.e., input is periodically sensed at the arrival time of the source task) 
                const bool event_input;
                // maximum interarrival time of events triggering the input of the chain (only relevant if event_input is true)
                const Time input_max_interarrival;
                // whether the task chain uses instantaneous (i.e., valid only when generated) or lasting/blackboard (i.e, valid until output is overridden) output
                const bool instantaneous_output;
                // maximum data availability for lasting output (only relevant if instantaneous_output is false)
                const Time output_data_availability;
                // interval of time AFTER a job of the i^th task in the chain is dispatched within which the input data is read
                const std::vector<Interval<Time>> read_interval;
                // interval of time BEFORE a job of the i^th task in the chain is completed within which the output data is written
                const std::vector<Interval<Time>> write_interval;

                // Requirements
                // Max data age
                const Time req_max_data_age;
                // Max reaction time
                const Time req_max_reaction_time;
            public:
                /**
                 * @brief Constructor
                 * @param tasks Vector of the task IDs of the tasks in the task chain
                 * @param event_input Boolean indicating if the chain uses event-triggered input
                 * @param instantaneous_output Boolean indicating if the chain uses instantaneous output
                 * @param id Unique identifier for the task chain
                 * @param name Name of the task chain
                 * @param i_max_interarrival Maximum interarrival time of events triggering the input of the chain (only relevant if event_input is true)
                 * @param o_data_availability Maximum data availability for lasting output (only relevant if instantaneous_output is false)
                 */
                Task_chain(
                    const std::vector<unsigned long>& tasks,
                    const bool& event_input,
                    const bool& instantaneous_output,
                    const unsigned long id,
                    const Time req_max_reaction_time = -1,
                    const Time req_max_data_age = -1,
                    const std::vector<Interval<Time>>& read_interval = {},
                    const std::vector<Interval<Time>>& write_interval = {},
                    const Time i_max_interarrival = -1,
                    const Time o_data_availability = -1,
                    const std::string& name = "")
                    : tasks(tasks), event_input(event_input), instantaneous_output(instantaneous_output), id(id), name(name), 
                      req_max_data_age(req_max_data_age), req_max_reaction_time(req_max_reaction_time),
                      input_max_interarrival(event_input && i_max_interarrival >= 0 ? i_max_interarrival : Time_model::constants<Time>::infinity()), 
                      output_data_availability(!instantaneous_output && o_data_availability >= 0 ? o_data_availability : Time_model::constants<Time>::infinity()),
                      read_interval(read_interval.size() == tasks.size() ? read_interval : std::vector<Interval<Time>>(tasks.size(), Interval<Time>(0,0))),
                      write_interval(write_interval.size() == tasks.size() ? write_interval : std::vector<Interval<Time>>(tasks.size(), Interval<Time>(0,0)))
                {}
                /**
                 * @brief Get the tasks in the task chain
                 */
                const std::vector<unsigned long>& get_tasks()const {
                    return tasks;
                }
                /**
                 * @brief Check if the task chain uses event-triggered input
                 */
                bool uses_event_input() const {
                    return event_input;
                }
                /**
                 * @brief Get the maximum interarrival time of events triggering the input of the chain
                 */
                Time get_input_max_interarrival() const {
                    return input_max_interarrival;
                }
                /**
                 * @brief Check if the task chain uses instantaneous output
                 */
                bool uses_instantaneous_output() const {
                    return instantaneous_output;
                }
                /**
                 * @brief Get the maximum data validity for lasting output
                 */
                Time get_output_data_availability() const {
                    return output_data_availability;
                }
                /**
                 * @brief Get the read interval for the task at position task_pos in the chain
                 * @param task_pos Position of the task in the chain
                 */
                Interval<Time> get_read_interval(size_t task_pos) const {
                    assert(task_pos < tasks.size());
                    return read_interval[task_pos];
                }
                /**
                 * @brief Get the write interval for the task at position task_pos in the chain
                 * @param task_pos Position of the task in the chain
                 */
                Interval<Time> get_write_interval(size_t task_pos) const {
                    assert(task_pos < tasks.size());
                    return write_interval[task_pos];
                }
                /**
                 * @brief Get the unique identifier of the task chain
                 */
                unsigned long get_id() const {
                    return id;
                }
                /**
                 * @brief Get the name of the task chain
                 */
                const std::string& get_name() const {
                    return name;
                }
                /**
                 * @brief Get the max data age requirement of the task chain
                 */
                Time get_max_data_age() const {
                    return req_max_data_age;
                }
                /**
                 * @brief Get the max reaction time requirement of the task chain
                 */
                Time get_max_reaction_time() const {
                    return req_max_reaction_time;
                }
            };

            /**
             * @brief Parse a YAML file containing task chain definitions
             * @param in Input stream of the YAML file
             * @return Vector of Task_chain objects representing the parsed task chains
             */
            template<class Time>
            std::vector<Task_chain<Time>> parse_yaml_task_chain_file(std::istream& in)
            {
                std::vector<Task_chain<Time>> taskchains;
                try {
                    YAML::Node input_tc_set = YAML::Load(in);
                    auto const TCs = input_tc_set["TaskChains"];
                    unsigned long chain_id = 0;
                    for (auto const& tc : TCs) {
                        std::vector<unsigned long> task_ids;
                        std::vector<Interval<Time>> read_intervals;
                        std::vector<Interval<Time>> write_intervals;
                        std::string tc_name = "";
                        if (tc["ID"])
                            tc_name = tc["ID"].as<std::string>();
                        // parse tasks in the chain
                        for (auto& t : tc["Tasks"]) {
                            if (t.IsScalar()) {
                                task_ids.push_back(t.as<unsigned long>());
                            }
                            else {
                                task_ids.push_back(t["ID"].as<unsigned long>());
                                read_intervals.push_back(Interval<Time>(
                                    t["ReadInterval"][0].as<Time>(),
                                    t["ReadInterval"][1].as<Time>()));
                                write_intervals.push_back(Interval<Time>(
                                    t["WriteInterval"][0].as<Time>(),
                                    t["WriteInterval"][1].as<Time>()));
                            }                                
                        }
                        // read input properties
                        auto in = tc["Input"];
                        auto inputtype = in["Type"].as<std::string>();
                        if (inputtype != "onExternalEvent" && inputtype != "onStart") {
                            throw std::runtime_error("Invalid Input Type in task chain definition. Must be 'onExternalEvent' or 'onStart'.");
                        }
                        Time input_max_interarrival = -1;
                        if (inputtype == "onExternalEvent") {
                            if (in["MaxInterarrival"]) {
                                input_max_interarrival = in["MaxInterarrival"].as<Time>();
                            }
                        }
                        read_intervals.push_back(in["ReadInterval"] ? Interval<Time>(
                            in["ReadInterval"][0].as<Time>(),
                            in["ReadInterval"][1].as<Time>()) : Interval<Time>(0,0));

                        // read output properties
                        auto out = tc["Output"];
                        auto outputtype = out["Type"].as<std::string>();
                        if (outputtype != "instantaneous" && outputtype != "blackboard") {
                            throw std::runtime_error("Invalid Output Type in task chain definition. Must be 'instantaneous' or 'blackboard'.");
                        }
                        Time output_data_availability = -1;
                        if (outputtype == "blackboard") {
                            if (out["Availability"]) {
                                output_data_availability = out["Availability"].as<Time>();
                            }
                        }
                        write_intervals.push_back(out["WriteInterval"] ? Interval<Time>(
                            out["WriteInterval"][0].as<Time>(),
                            out["WriteInterval"][1].as<Time>()) : Interval<Time>(0,0));

                        // read requirements
                        Time req_max_data_age = -1;
                        Time req_max_reaction_time = -1;
                        if (tc["Requirements"]) {
                            auto req = tc["Requirements"];
                            if (req["MaxDataAge"]) {
                                req_max_data_age = req["MaxDataAge"].as<Time>();
                            }
                            if (req["MaxReactionTime"]) {
                                req_max_reaction_time = req["MaxReactionTime"].as<Time>();
                            }
                        }
                        taskchains.push_back(Task_chain<Time>(task_ids, inputtype == "onExternalEvent", outputtype != "blackboard", chain_id, req_max_reaction_time, req_max_data_age, read_intervals, write_intervals, input_max_interarrival, output_data_availability, tc_name));
                        chain_id++;
                    }
                }
                catch (const YAML::Exception& e) {
                    std::cerr << "Error reading task chain YAML file: " << e.what() << std::endl;
                }

                return taskchains;
            }
        }
    }
}
#endif