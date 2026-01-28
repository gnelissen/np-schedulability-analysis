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
                    const Time i_max_interarrival = -1,
                    const Time o_data_availability = -1,
                    const std::string& name = "")
                    : tasks(tasks), event_input(event_input), instantaneous_output(instantaneous_output), id(id), name(name),
                      input_max_interarrival(event_input && i_max_interarrival >= 0 ? i_max_interarrival : Time_model::constants<Time>::infinity()), 
                      output_data_availability(!instantaneous_output && o_data_availability >= 0 ? o_data_availability : Time_model::constants<Time>::infinity())

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
                std::vector<unsigned long> task_ids;
                std::vector<std::string> buffers;

                try {
                    YAML::Node input_tc_set = YAML::Load(in);
                    auto const TCs = input_tc_set["TaskChains"];
                    unsigned long chain_id = 0;
                    for (auto const& tc : TCs) {
                        std::string tc_name = "";
                        if (tc["ID"])
                            tc_name = tc["ID"].as<std::string>();
                        task_ids = tc["Tasks"].as<std::vector<unsigned long>>();
                        // read input type
                        auto inputtype = tc["Input"]["Type"].as<std::string>();
                        if (inputtype != "onExternalEvent" && inputtype != "onStart") {
                            throw std::runtime_error("Invalid Input Type in task chain definition. Must be 'onExternalEvent' or 'onStart'.");
                        }
                        Time input_max_interarrival = -1;
                        if (inputtype == "onExternalEvent") {
                            if (tc["Input"]["MaxInterarrival"]) {
                                input_max_interarrival = tc["Input"]["MaxInterarrival"].as<Time>();
                            }
                        }

                        // read output type
                        auto outputtype = tc["Output"]["Type"].as<std::string>();
                        if (outputtype != "instantaneous" && outputtype != "blackboard") {
                            throw std::runtime_error("Invalid Output Type in task chain definition. Must be 'instantaneous' or 'blackboard'.");
                        }
                        Time output_data_availability = -1;
                        if (outputtype == "blackboard") {
                            if (tc["Output"]["Availability"]) {
                                output_data_availability = tc["Output"]["Availability"].as<Time>();
                            }
                        }

                        taskchains.push_back(Task_chain<Time>(task_ids, inputtype == "onExternalEvent", outputtype != "blackboard", chain_id, input_max_interarrival, output_data_availability, tc_name));
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