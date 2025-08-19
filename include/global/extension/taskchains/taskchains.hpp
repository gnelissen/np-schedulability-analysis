#ifndef TASKCHAIN_HPP
#define TASKCHAIN_HPP

#include "jobs.hpp"

namespace NP{

    template<class Time>
    struct Task_chains_result {
        std::vector<Time> data_ages;
        std::vector<Time> reaction_times;
    };

    template<class Time>
    class Task_chain {
    private:
		// unique identifier for the task chain
        const unsigned long id;

		// a task chain is a sequence of tasks
        const std::vector<unsigned long> tasks;
		// whether the task chain uses event-triggered input or sensor-based input 
        const bool event_input;
		// whether the task chain uses active or instantaneous output
        const bool active_output;

    public:
        Task_chain(
            const std::vector<unsigned long>& tasks,
            const bool& event_input,
            const bool& active_output,
            const unsigned long id)
            : tasks(tasks), event_input(event_input), active_output(active_output), id(id)
        {
        }
        const std::vector<unsigned long>& get_tasks()const {
            return tasks;
        }
        bool uses_event_input() const {
            return event_input;
        }
        bool uses_active_output() const {
            return active_output;
        }
        unsigned long get_id() const {
            return id;
        }
    };

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
#endif