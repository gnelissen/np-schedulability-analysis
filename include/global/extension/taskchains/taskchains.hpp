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

                // a task chain is a sequence of tasks
                const std::vector<unsigned long> tasks;
                // whether the task chain uses event-triggered input (i.e., task-unrelated events that may happen anytime) or sensor-based input (i.e., input is periodically sensed at the arrival time of the source task) 
                const bool event_input;
                // whether the task chain uses instantaneous (i.e., valid only when generated) or lasting/blackboard (i.e, valid until output is overridden) output
                const bool instantaneous_output;

            public:
                /**
                 * @brief Constructor
                 * @param tasks Vector of the task IDs of the tasks in the task chain
                 * @param event_input Boolean indicating if the chain uses event-triggered input
                 * @param instantaneous_output Boolean indicating if the chain uses instantaneous output
                 * @param id Unique identifier for the task chain
                 */
                Task_chain(
                    const std::vector<unsigned long>& tasks,
                    const bool& event_input,
                    const bool& instantaneous_output,
                    const unsigned long id)
                    : tasks(tasks), event_input(event_input), instantaneous_output(instantaneous_output), id(id)
                {
                }
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
                 * @brief Check if the task chain uses instantaneous output
                 */
                bool uses_instantaneous_output() const {
                    return instantaneous_output;
                }
                /**
                 * @brief Get the unique identifier of the task chain
                 */
                unsigned long get_id() const {
                    return id;
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
                    auto const TCs = input_tc_set["taskchains"];
                    unsigned long chain_id = 0;
                    for (auto const& tc : TCs) {
                        task_ids = tc["Tasks"].as<std::vector<unsigned long>>();
                        auto inputtype = tc["InputType"].as<std::string>();
                        auto outputtype = tc["OutputType"].as<std::string>();
                        //std::cout<<"InputType inited"<<std::endl;

                        taskchains.push_back(Task_chain<Time>(task_ids, inputtype == "event", outputtype == "active", chain_id));
                        //std::cout<<"Task chain pushed to vector"<<std::endl;
                        chain_id++;
                    }
                }
                catch (const YAML::Exception& e) {
                    std::cerr << "Error reading YAML file: " << e.what() << std::endl;
                }

                return taskchains;
            }
        }
    }
}
#endif