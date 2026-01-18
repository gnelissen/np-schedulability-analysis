#ifndef PROBLEM_BUILDER_HPP
#define PROBLEM_BUILDER_HPP
#include <iostream>
#include <sstream>
#include <fstream>
#include "problem.hpp"
#include "analysis_config.hpp"
#include "io.hpp"

namespace NP {
    /**
	 * @brief Utility function to open a file stream and handle errors
	 * @param filename Name of the file to open
	 * @return Opened ifstream
	 */
    std::ifstream open_file_stream(const std::string& filename) {
        std::ifstream file_stream(filename);
        if (!file_stream) {
            std::cerr << "Error: could not open file: " << filename << std::endl;
            exit(1);
        }
        return file_stream;
    }

	/**
	 * @brief Utility function to determine if a file is in YAML format based on its extension
	 * @param fname Filename to check
	 */
	bool is_yaml(const std::string& fname) {
		std::string ext = fname.substr(fname.find_last_of(".") + 1);
		return (ext == "yaml" || ext == "yml" || ext == "YAML" || ext == "YML");
	}
    
	/**
	 * @brief Build a Scheduling_problem from input files specified in the Analysis_config
	 * @param config Analysis configuration containing input file names and options
	 * @return Pointer to constructed Scheduling_problem
	 */
    template<class Time>
    std::unique_ptr<Scheduling_problem<Time>> problem_builder(
		const std::string& jobs_file, const Analysis_config& config)
	{
        // Parse jobs
		if (jobs_file.empty()) {
			std::cerr << "Error: No job input file specified." << std::endl;
			exit(1);
		}
		auto in = open_file_stream(jobs_file);
		bool jobs_file_is_yaml = is_yaml(jobs_file);
		auto jobs = jobs_file_is_yaml ? NP::parse_yaml_job_file<Time>(in) : NP::parse_csv_job_file<Time>(in);
		auto jobs_lookup_table = NP::make_job_lookup_table<Time>(jobs);
		
		// Parse precedence constraints
		std::vector<NP::Precedence_constraint<Time>> edges;
		if (config.want_precedence) {
			if (config.precedence_file.empty()) {
				std::cerr << "Error: Precedence constraints file not specified." << std::endl;
				exit(1);
			}
			auto prec_in = open_file_stream(config.precedence_file);
			bool dag_is_yaml = is_yaml(config.precedence_file);
			edges = dag_is_yaml ? 
				NP::parse_yaml_dag_file<Time>(prec_in, jobs_lookup_table) :
				NP::parse_precedence_file<Time>(prec_in, jobs_lookup_table);
		}
		else if(jobs_file_is_yaml) {
			edges = NP::parse_yaml_dag_file<Time>(in, jobs_lookup_table);
		}

		// Build DAG (predecessors and successors for each job)
		auto graph = NP::build_graph(jobs.size(), edges);
		// Compute topological order and assign ranks to jobs
		auto topo_order = NP::get_jobs_ranks<Time>(graph);
		if (topo_order.size() != jobs.size()) {
			std::cerr << "Error: The precedence constraints contain a cycle." << std::endl;
			exit(1);
		}
		for (auto& j : jobs) {
			j.set_rank(topo_order[j.get_job_index()]);
		}
		
		// Parse mutex constraints
		std::vector<NP::Exclusion_constraint<Time>> mutexes;
		if (config.want_mutexes) {
			if (config.excl_file.empty()) {
				std::cerr << "Error: Mutex constraints file not specified." << std::endl;
				exit(1);
			}
			auto excl_in = open_file_stream(config.excl_file);
			bool excl_is_yaml = is_yaml(config.excl_file);
			if (excl_is_yaml) {
				std::cerr << "Error: YAML mutex file is not supported yet, use CSV format instead." << std::endl;
				exit(1);
			}
			mutexes = NP::parse_mutex_file<Time>(excl_in, jobs_lookup_table);
		}
		
		// Parse platform specification
		std::vector<Interval<Time>> platform_spec;
		if (config.platform_defined) {
			if (config.platform_file.empty()) {
				std::cerr << "Error: Platform specification file not specified." << std::endl;
				exit(1);
			}
			auto platform_in = open_file_stream(config.platform_file);
			bool plat_is_yaml = is_yaml(config.platform_file);
			platform_spec = plat_is_yaml ? 
				NP::parse_platform_spec_yaml<Time>(platform_in) :
				NP::parse_platform_spec_csv<Time>(platform_in);
			// Update num_processors based on platform specification
			// config.num_processors = platform_spec.size();
		} else {
			platform_spec.resize(config.num_processors, {0, 0});
		}

		// Parse abort actions
		std::vector<NP::Abort_action<Time>> abort_acts;
		if (config.want_aborts) {
			if (config.aborts_file.empty()) {
				std::cerr << "Error: Abort actions file not specified." << std::endl;
				exit(1);
			}
			auto aborts_in = open_file_stream(config.aborts_file);
			bool aborts_is_yaml = is_yaml(config.aborts_file);
			if (aborts_is_yaml) {
				std::cerr << "Error: YAML abort actions file is not supported yet, use CSV format instead." << std::endl;
				exit(1);
			}
			abort_acts = NP::parse_abort_file<Time>(aborts_in);
		}

		// Create scheduling problem
		std::unique_ptr<NP::Scheduling_problem<Time>> problem = std::make_unique<NP::Scheduling_problem<Time>>(
			jobs,
			edges,
			abort_acts,
			mutexes,
			platform_spec
		);
		
#ifdef CONFIG_ANALYSIS_EXTENSIONS
		register_extensions(*problem, config);
#endif
        return problem;
	}
	
#ifdef CONFIG_ANALYSIS_EXTENSIONS
	/**
	 * @brief Register problem extensions based on the Analysis_config
	 * @param problem Scheduling problem to register extensions for
	 * @param config Analysis configuration containing extension options
	 */
	template<class Time>
	void register_extensions(NP::Scheduling_problem<Time>& problem, const Analysis_config& config) {
		if (config.want_mk) {
			if (config.mk_file.empty()) {
				std::cerr << "Error: mk-firm specifications file not specified." << std::endl;
				exit(1);
			}
			bool mk_is_yaml = is_yaml(config.mk_file);
			if (mk_is_yaml) {
				std::cerr << "Error: YAML mk file is not supported yet, use CSV format instead." << std::endl;
				exit(1);
			}
			auto mk_stream = open_file_stream(config.mk_file);
			
			auto mk_constraints = NP::Global::MK_analysis::parse_mk_constraints_csv(mk_stream);
			problem.problem_extensions.template register_extension<NP::Global::MK_analysis::MK_problem_extension>(mk_constraints);
		}
		if (config.want_task_chains) {
			if (config.task_chains_file.empty()) {
				std::cerr << "Error: task chains specifications file not specified." << std::endl;
				exit(1);
			}
			bool tc_is_yaml = is_yaml(config.task_chains_file);
			if (!tc_is_yaml) {
				std::cerr << "Error: task chains file must be in YAML format." << std::endl;
				exit(1);
			}
			auto tc_stream = open_file_stream(config.task_chains_file);
			
			auto task_chains = NP::Global::Taskchains_analysis::template parse_yaml_task_chain_file<Time>(tc_stream);
			problem.problem_extensions.template register_extension<NP::Global::Taskchains_analysis::Taskchains_problem_extension<Time>>(task_chains);
		}
	}
#endif
}

#endif // PROBLEM_BUILDER_HPP