#ifndef ANALYSIS_RESULT_HPP
#define ANALYSIS_RESULT_HPP

#include <string>
#include <fstream>
#include <iostream>
#include <memory>
#include "analysis_config.hpp"
#include "global/space.hpp"
#include "problem.hpp"

/**
 * @brief Structure to hold the results of the analysis
 */
struct Analysis_result {
	bool schedulable;
	bool timeout;
	bool out_of_memory;
	unsigned long long number_of_nodes, number_of_states, number_of_edges, max_width, number_of_jobs;
	double cpu_time;
	long memory_used; // in KiB
#ifdef CONFIG_COLLECT_SCHEDULE_GRAPH
	std::string graph;
#endif
	std::string response_times_csv;
	std::string width_evolution_csv;
	std::string deadline_miss_info;
#ifdef CONFIG_ANALYSIS_EXTENSIONS
	// mk-firm analysis results
	std::string mk_results;
	// task chains analysis results
	std::string task_chains_results;
#endif
};

/**
 * @brief print the header for the analysis result output
 */
void print_header() {
	std::cout << "# file name"
		<< ", schedulable?(1Y/0N)"
		<< ", #jobs"
		<< ", #nodes"
		<< ", #states"
		<< ", #edges"
		<< ", max width"
		<< ", CPU time"
		<< ", memory"
		<< ", timeout"
		<< ", mem_out"
		<< ", #CPUs"
		<< std::endl;
}

/**
 * @brief print the analysis result
 * @param fname Name of the analyzed job file
 * @param result Analysis result to print
 * @param config Analysis configuration used
 */
void print_result(const std::string& fname, const Analysis_result& result, 
				  const Analysis_config& config){
	std::cout << fname;
	
	if (!result.schedulable)
		std::cout << ",  0";
	else if (config.max_depth && config.max_depth < result.number_of_jobs)
		std::cout << ",  X";
	else
		std::cout << ",  1";
	
	std::cout << ",  " << result.number_of_jobs
				<< ",  " << result.number_of_nodes
				<< ",  " << result.number_of_states
				<< ",  " << result.number_of_edges
				<< ",  " << result.max_width
				<< ",  " << std::fixed << result.cpu_time
				<< ",  " << ((double) result.memory_used) / (1024.0)
				<< ",  " << (int) result.timeout
				<< ",  " << (int) result.out_of_memory
				<< ",  " << config.num_processors
				<< std::endl;
}

/**
 * @brief Save content to a file with a specific extension
 * @param base_fname Base filename without extension
 * @param extension Extension to append to the base filename
 * @param content Content to write to the file
 */
void save_file_with_extension(const std::string& base_fname, 
										const std::string& extension, 
										const std::string& content) {
	if (content.empty() || base_fname.empty()) 
		return;
	
	std::string output_name = base_fname;
	output_name.append(extension);
	std::ofstream out(output_name, std::ios::out);
	out << content;
	out.close();
}

/**
 * @brief save analysis result files (RTA, width evolution, deadline miss info, graph, etc.)
 * @param fname Base name for the output files
 * @param result Analysis result containing the data to save
 * @param config Analysis configuration used
 */
void save_result_files(const std::string& fname, const Analysis_result& result, const Analysis_config& config) {
	if (config.want_rta_file) {
		save_file_with_extension(fname, ".rta.csv", result.response_times_csv);
	}
	if (config.want_width_file) {
		save_file_with_extension(fname, ".width.csv", result.width_evolution_csv);
	}		
	if (config.want_deadline_miss_info && !result.schedulable && !result.deadline_miss_info.empty()) {
		save_file_with_extension(fname, ".dl_miss.txt", result.deadline_miss_info);
	}		
#ifdef CONFIG_COLLECT_SCHEDULE_GRAPH
	if (config.want_dot_graph && !result.graph.empty()) {
		save_file_with_extension(fname, ".dot", result.graph);
	}
#endif
#ifdef CONFIG_ANALYSIS_EXTENSIONS
	if (config.want_mk && !result.mk_results.empty()) {
		save_file_with_extension(fname, ".mk.csv", result.mk_results);
	}
	if (config.want_task_chains && !result.task_chains_results.empty()) {
		save_file_with_extension(fname, ".tchains.csv", result.task_chains_results);
	}
#endif
}

/**
 * @brief Generate the complettion times and response times in CSV format
 * @param space Explored state space
 * @param problem Scheduling problem that was analyzed
 * @return CSV string containing the completion times and response times of all jobs
 */
template<class Time>
std::string generate_rta_csv(const std::unique_ptr<NP::Global::State_space<Time>>& space, const NP::Scheduling_problem<Time>& problem) {
    std::ostringstream rta;
    rta << "Task ID, Job ID, BCCT, WCCT, BCRT, WCRT" << std::endl;
    
    for (const auto& j : problem.jobs) {
        Interval<Time> finish = space->get_finish_times(j);
        rta << j.get_task_id() << ", "
            << j.get_job_id() << ", "
            << finish.from() << ", "
            << finish.until() << ", "
            << std::max<Time>(0, finish.from() - j.earliest_arrival()) << ", "
            << (finish.until() - j.earliest_arrival())
            << std::endl;
    }		
    return rta.str();
}

/**
 * @brief Generate the SAG width evolution in CSV format
 * @param space Explored state space
 * @param problem Scheduling problem that was analyzed
 * @return CSV string containing the SAG width evolution
 */
template<class Time>
std::string generate_width_csv(const std::unique_ptr<NP::Global::State_space<Time>>& space, const NP::Scheduling_problem<Time>& problem) {
    std::ostringstream width_stream;
    width_stream << "Depth, Width (#Nodes), Width (#States)" << std::endl;
    
    const auto& width = space->evolution_exploration_front_width();
    for (size_t d = 0; d < problem.jobs.size(); d++) {
        width_stream << d << ", "
                    << width[d].first << ", "
                    << width[d].second
                    << std::endl;
    }		
    return width_stream.str();
}

/**
 * @brief Generate the deadline miss information
 * @param space Explored state space
 * @param problem Scheduling problem that was analyzed
 * @return String containing the deadline miss information
 */
template<class Time>
std::string generate_deadline_miss_info(const std::unique_ptr<NP::Global::State_space<Time>>& space, const NP::Scheduling_problem<Time>& problem) {
    std::ostringstream deadline_miss_stream;
    auto deadline_miss_state = space->get_deadline_miss_state();
    
    if (deadline_miss_state.first != nullptr) {
        deadline_miss_state.first->export_node(deadline_miss_stream, problem.jobs);
    }
    if (deadline_miss_state.second != nullptr) {
        deadline_miss_state.second->export_state(deadline_miss_stream, problem.jobs);
    }		
    return deadline_miss_stream.str();
}

#ifdef CONFIG_COLLECT_SCHEDULE_GRAPH
/**
 * @brief Generate the DOT graph representation of the explored state space
 * @param space Explored state space
 * @param config Configuration used for the analysis
 * @return String containing the DOT graph representation
 */
template<class Time>
std::string generate_dot_graph(const std::unique_ptr<NP::Global::State_space<Time>>& space, const Analysis_config& config) {
    NP::Global::Dot_file_config dot_config;
    if (!config.dot_file.empty()) {
        std::ifstream dot_config_stream(config.dot_file);
        if (dot_config_stream) {
            dot_config = NP::parse_dot_file_config_yaml<Time>(dot_config_stream);
        } else {
            std::cerr << "Error: could not open log config YAML file: " << config.dot_file << std::endl;
        }
    }

    std::ostringstream graph;    
    space->print_dot_file(graph, dot_config);
    return graph.str();
}
#endif

/**
 * @brief Extract the analysis result from the explored state space
 * @param space Explored state space
 * @param config Analysis configuration used
 * @return Analysis_result structure containing the analysis results
 */
template<class Time>
Analysis_result get_analysis_result(const std::unique_ptr<NP::Global::State_space<Time>>& space, const NP::Scheduling_problem<Time>& problem, const Analysis_config& config) {
	Analysis_result result;
	result.schedulable = space->is_schedulable();
	result.timeout = space->was_timed_out();
	result.out_of_memory = space->out_of_memory();
	result.number_of_nodes = space->number_of_nodes();
	result.number_of_states = space->number_of_states();
	result.number_of_edges = space->number_of_edges();
	result.max_width = space->max_exploration_front_width();
	result.number_of_jobs = space-> get_number_of_jobs_analyzed();
	result.cpu_time = space->get_cpu_time();
	
	if (config.want_rta_file) {
		result.response_times_csv = generate_rta_csv<Time>(space, problem);
	}
	
	if (config.want_width_file) {
		result.width_evolution_csv = generate_width_csv<Time>(space, problem);
	}
	
	if (config.want_deadline_miss_info && !result.schedulable) {
		result.deadline_miss_info = generate_deadline_miss_info<Time>(space, problem);
	}	
#ifdef CONFIG_COLLECT_SCHEDULE_GRAPH
	if (config.want_dot_graph) {
		result.graph = generate_dot_graph<Time>(space, config);
	}
#endif
#ifdef CONFIG_ANALYSIS_EXTENSIONS
	if (config.want_mk) {
		auto mk_stream = space->template export_results<NP::Global::MK_analysis::MK_sp_data_extension<Time>>();
		result.mk_results = mk_stream.str();
	}
	if (config.want_task_chains) {
		auto tc_stream = space->template export_results<NP::Global::Taskchains_analysis::template Taskchains_sp_data_extension<Time>>();
		result.task_chains_results = tc_stream.str();
	}
#endif		
	return result;
}

#endif // ANALYSIS_RESULT_HPP