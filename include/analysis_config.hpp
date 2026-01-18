#ifndef ANALYSIS_CONFIG_HPP
#define ANALYSIS_CONFIG_HPP

#include <string>
#include <fstream>
#include <iostream>
#include "yaml-cpp/yaml.h"
#include "OptionParser.h"

/**
 * @brief Structure to hold analysis configuration options
 */
struct Analysis_config {
	// Merge options
	bool want_naive = false;
	bool merge_conservative = false;
	bool merge_use_job_finish_times = false;
	int merge_depth = 1;
	
	// Time model
	bool want_dense = false;
	
	// Output options
	bool want_verbose = false;
	bool want_header = false;
	bool want_rta_file = false;
	bool want_width_file = false;
	bool want_deadline_miss_info = false;
	
	// Input files
	std::string jobs_file;
	bool want_precedence = false;
	std::string precedence_file;
	bool want_mutexes = false;
	std::string excl_file;
	bool want_aborts = false;
	std::string aborts_file;
	
	// Platform configuration
	bool want_multiprocessor = false;
	unsigned int num_processors = 1;
	bool platform_defined = false;
	std::string platform_file;
	
	// Analysis limits
	unsigned long long timeout = 0; //in seconds
	unsigned long long mem_max = 0; // in KiB
	unsigned long long max_depth = 0;
	bool continue_after_dl_miss = false;
	
#ifdef CONFIG_PARALLEL
	bool want_parallel = true;
	unsigned int num_threads = 0;
#endif

#ifdef CONFIG_PRUNING
	bool want_focus = false;
	std::string focus_file;
#endif

#ifdef CONFIG_ANALYSIS_EXTENSIONS
	// MK-firm extension
	bool want_mk = false;
	std::string mk_file;
	// Task Chains extension
	bool want_task_chains = false;
	std::string task_chains_file;
#endif

#ifdef CONFIG_COLLECT_SCHEDULE_GRAPH
	bool want_dot_graph = false;
	std::string dot_file;
#endif
};


// Forward Declarations
void update_merge_options(const std::string& merge_opts, Analysis_config& analysis_config);
unsigned long long parse_time_limit(const std::string& time_str);
unsigned long long parse_memory_limit(const std::string& mem_str);
Analysis_config parse_config_file(const std::string& config_file);
void parse_input_options(const optparse::Values& options, Analysis_config& analysis_config);

// Utility Functions

/**
 * @brief Parse time limit string and convert to seconds
 * @param time_str Time limit string (e.g., "1d2h15m30s" or "3600")
 *                 Supported units: d (days), h (hours), m (minutes), s (seconds)
 *                 Numbers without units are treated as seconds
 * @return Time limit in seconds, or 0 if empty/invalid
 * @note Logs error to stderr for unknown time units
 */
unsigned long long parse_time_limit(const std::string& time_str) {
	if (time_str.empty()) {
		return 0;
	}
	
	unsigned long long total_seconds = 0;
	std::string num_str;
	num_str.reserve(16); // Reserve space for efficiency
	
	for (char c : time_str) {
		if (isdigit(c) || c == '.') {
			num_str += c;
		} else {
			if (!num_str.empty()) {
				try {
					double value = std::stod(num_str);
					switch (c) {
						case 'd': total_seconds += static_cast<unsigned long long>(value * 86400); break;
						case 'h': total_seconds += static_cast<unsigned long long>(value * 3600); break;
						case 'm': total_seconds += static_cast<unsigned long long>(value * 60); break;
						case 's': total_seconds += static_cast<unsigned long long>(value); break;
						default:
							std::cerr << "Error: Unknown time unit '" << c << "' in time limit string" << std::endl;
							return 0;
					}
					num_str.clear();
				} catch (const std::exception& e) {
					std::cerr << "Error: Invalid numeric value in time limit string: " << e.what() << std::endl;
					return 0;
				}
			}
		}
	}
	
	// If there's a remaining number without unit, treat it as seconds
	if (!num_str.empty()) {
		try {
			total_seconds += static_cast<unsigned long long>(std::stod(num_str));
		} catch (const std::exception& e) {
			std::cerr << "Error: Invalid numeric value in time limit string: " << e.what() << std::endl;
			return 0;
		}
	}
	
	return total_seconds;
}

/**
 * @brief Parse memory limit string and convert to KiB
 * @param mem_str Memory limit string (e.g., "512M", "2G", "1024")
 *                Supported units: K/k (KiB), M/m (MiB), G/g (GiB)
 *                Numbers without units are treated as KiB
 * @return Memory limit in KiB, or 0 if empty/invalid
 * @note Logs error to stderr for invalid numeric values
 */
unsigned long long parse_memory_limit(const std::string& mem_str) {
	if (mem_str.empty()) {
		return 0;
	}
	
	std::string num_str;
	char unit = 0;
	
	for (char c : mem_str) {
		if (isdigit(c) || c == '.') {
			num_str += c;
		} else {
			unit = c;
			break;
		}
	}
	
	if (num_str.empty()) {
		std::cerr << "Error: No numeric value found in memory limit string" << std::endl;
		return 0;
	}

	try {
		double value = std::stod(num_str);
		
		switch (unit) {
			case 'K': case 'k': return static_cast<unsigned long long>(value);
			case 'M': case 'm': return static_cast<unsigned long long>(value * 1024);
			case 'G': case 'g': return static_cast<unsigned long long>(value * 1024 * 1024);
			case 0: return static_cast<unsigned long long>(value); // No unit, assume KiB
			default:
				std::cerr << "Error: Unknown memory unit '" << unit << "' (use K, M, or G)" << std::endl;
				return 0;
		}
	} catch (const std::exception& e) {
		std::cerr << "Error: Invalid numeric value in memory limit string: " << e.what() << std::endl;
		return 0;
	}
}

/**
 * @brief Parse configuration YAML file and set analysis config options accordingly
 * @param config_file Path to the configuration YAML file
 * @return Analysis_config structure with parsed options
 */
Analysis_config parse_config_file(const std::string& config_file) {
    try {
        YAML::Node config = YAML::LoadFile(config_file);
		Analysis_config analysis_config;

        // Parse analysis_config section
        if (config["analysis_config"]) {
            auto analysis = config["analysis_config"];
            // Time model
            if (analysis["time_model"]) {
                analysis_config.want_dense = (analysis["time_model"].as<std::string>() == "dense");
            }
            // Stop at first deadline miss
            if (analysis["stop_at_first_deadline_miss"]) {
                analysis_config.continue_after_dl_miss = analysis["stop_at_first_deadline_miss"].as<bool>();
            }
            // Merge strategy
            if (analysis["merge_strategy"]) {
                std::string merge_opts = analysis["merge_strategy"].as<std::string>();
				update_merge_options(merge_opts, analysis_config);
            }
			#ifdef CONFIG_PRUNING
            // Focused exploration
            if (analysis["focused_exploration"]) {
                bool want_focus_config = analysis["focused_exploration"].as<bool>();
                if (want_focus_config && config["input"] && config["input"]["focused_expl_config"]) {
					analysis_config.focus_file = config["input"]["focused_expl_config"].as<std::string>();
                }
				analysis_config.want_focus = want_focus_config;
            }
			#endif
			#ifdef CONFIG_ANALYSIS_EXTENSIONS
            // Extensions (prepare but don't set anything yet)
            if (analysis["extensions"]) {
                auto ext = analysis["extensions"];
				if (config["input"] && config["input"]["extensions"]) {
					auto ext_input = config["input"]["extensions"];
					// mk_constraints
					if (ext["activate_mk_analysis"]) {
						bool want_mk = ext["activate_mk_analysis"].as<bool>();
						if (want_mk ) {
							if (ext_input["mk_constraints"])
								analysis_config.mk_file = ext_input["mk_constraints"].as<std::string>();
							else
								std::cerr << "A mk constraint file must be specified if the mk analysis is activated.";
						}
						analysis_config.want_mk = want_mk;
					}
					// task chains
					if (ext["activate_task_chains"]) {
						bool want_tc = ext["activate_task_chains"].as<bool>();
						if (want_tc) {
							if (ext_input["task_chains"])
								analysis_config.task_chains_file = ext_input["task_chains"].as<std::string>();
							else
								std::cerr << "A task chains file must be specified if the task chains analysis is activated.";
						}
						analysis_config.want_task_chains = want_tc;
					}
				}
			}
			#endif
		}

        // Parse input section
        if (config["input"]) {
            auto input = config["input"];
            // Jobs input
            if (input["jobs"]) {
                std::string jobs = input["jobs"].as<std::string>();
                if (!jobs.empty() && jobs != "none") {
                    analysis_config.jobs_file = jobs;
                }
            }
            // Precedence constraints
            if (input["precedence_constraints"]) {
                std::string prec = input["precedence_constraints"].as<std::string>();
                if (prec != "none" && !prec.empty()) {
                    analysis_config.precedence_file = prec;
					analysis_config.want_precedence = true;
                }
				else {
					analysis_config.want_precedence = false;
                }
            }
            // Exclusion constraints
            if (input["exclusion_constraints"]) {
                std::string excl = input["exclusion_constraints"].as<std::string>();
                if (excl != "none" && !excl.empty()) {
                    analysis_config.excl_file = excl;
					analysis_config.want_mutexes = true;
                }
				else {
					analysis_config.want_mutexes = false;
				}
            }
            // Abort actions
            if (input["abort_actions"]) {
                std::string aborts = input["abort_actions"].as<std::string>();
                if (aborts != "none" && !aborts.empty()) {
                    analysis_config.aborts_file = aborts;
					analysis_config.want_aborts = true;
                }
				else {
					analysis_config.want_aborts = false;
                }
            }
            // Platform
            if (input["platform"]) {
                std::string platform = input["platform"].as<std::string>();
                if (platform != "none" && !platform.empty()) {
                    analysis_config.platform_file = platform;
					analysis_config.platform_defined = true;
					analysis_config.want_multiprocessor = true;
                }
				else {
					analysis_config.platform_defined = false;
					analysis_config.want_multiprocessor = false;
				}
            }
		}

        // Parse output section
        if (config["output"]) {
            auto output = config["output"];            
            // Response time
            if (output["response_time"]) {
                analysis_config.want_rta_file = output["response_time"].as<bool>();
            }            
            // Graph width
            if (output["graph_width"]) {
                analysis_config.want_width_file = output["graph_width"].as<bool>();
            }            
            // Miss state info
            if (output["miss_state_info"]) {
                analysis_config.want_deadline_miss_info = output["miss_state_info"].as<bool>();
            }            
			#ifdef CONFIG_COLLECT_SCHEDULE_GRAPH
            // Save SAG
            if (output["save_sag"]) {
                auto sag = output["save_sag"];
                if (sag["activate"]) {
                    bool want_graph = sag["activate"].as<bool>();
                    analysis_config.want_dot_graph = want_graph;
                }
                if (sag["config"]) {
					std::string dot_cfg = sag["config"].as<std::string>();
					if (dot_cfg != "none" && !dot_cfg.empty()) {
                        analysis_config.dot_file = dot_cfg;
                    }
                }
            }
			#endif
        }
        
        // Parse limits section
        if (config["limits"]) {
            auto limits = config["limits"];            
            // Memory usage
            if (limits["memory_usage"]) {
                std::string mem_limit = limits["memory_usage"].as<std::string>();
				if (mem_limit != "none") {
					analysis_config.mem_max = parse_memory_limit(mem_limit);
				}
            }            
            // Runtime
            if (limits["runtime"]) {
                std::string time_limit = limits["runtime"].as<std::string>();
				if (time_limit != "none") {
					unsigned long long time_sec = parse_time_limit(time_limit);
					analysis_config.timeout = time_sec;
				}
            }            
            // Depth
            if (limits["depth"]) {
				std::string depth_limit = limits["depth"].as<std::string>();
				if (depth_limit != "none") {
					analysis_config.max_depth = limits["depth"].as<long long>();
				}
            }
        }
        
        // Parse pretty_printing section
        if (config["pretty_printing"]) {
            auto pp = config["pretty_printing"];            
            // Header
            if (pp["header"]) {
                bool want_header = pp["header"].as<bool>();
                analysis_config.want_header = want_header;
            }            
            // Verbose
            if (pp["verbose"]) {
                bool want_verbose = pp["verbose"].as<bool>();
                analysis_config.want_verbose = want_verbose;
            }
        }
        
		#ifdef CONFIG_PARALLEL
        // Parse parallel_exploration section
        if (config["parallel_exploration"]) {
            auto parallel = config["parallel_exploration"];
            
            // Active
            if (parallel["active"]) {
                bool want_parallel = parallel["active"].as<bool>();
                analysis_config.want_parallel = want_parallel;
            }
            // Threads
            if (parallel["threads"]) {
                analysis_config.num_threads = parallel["threads"].as<int>();
            }
        }
		#endif

		return analysis_config;
		
    } catch (const YAML::Exception& e) {
        std::cerr << "Error parsing config file " << config_file << ": " << e.what() << std::endl;
        exit(1);
    }
}

/**
 * @brief Parse command-line options and set analysis config accordingly
 * @param options Parsed command-line options
 * @param analysis_config Analysis_config structure to populate
 */
void parse_input_options(const optparse::Values& options, Analysis_config& analysis_config) {
	// Time model
	if (options.is_set_by_user("time_model")) {
		std::string time_model = (const std::string&)options.get("time_model");
		if (time_model == "dense") {
			analysis_config.want_dense = true;
		}
		else if (time_model == "discrete") {
			analysis_config.want_dense = false;
		}
		else {
			std::cerr << "Error: invalid time model specified (use 'dense' or 'discrete')." << std::endl;
			exit(1);
		}
	}
	
	// Precedence constraints
	if (options.is_set_by_user("precedence_file")) {
		analysis_config.want_precedence = true;
		std::string prec_file = (const std::string&)options.get("precedence_file");
		if (!prec_file.empty()) {
			analysis_config.precedence_file = prec_file;
		}
		else {
			std::cerr << "Error: precedence constraints file not specified" << std::endl;
			exit(1);
		}
	}
	
	// Exclusion constraints
	if (options.is_set_by_user("excl_file")) {
		analysis_config.want_mutexes = true;
		if (options.is_set_by_user("excl_file")) {
			std::string excl_file = (const std::string&)options.get("excl_file");
			if (!excl_file.empty()) {
				analysis_config.excl_file = excl_file;
				std::cout << "[**] Note: support for mutex constraints is only experimental for now." << std::endl;
			}
			else {
				std::cerr << "Error: exclusion constraints file not specified" << std::endl;
				exit(1);
			}
		}
	}
	
	// Abort actions
	if (options.is_set_by_user("abort_file")) {
		analysis_config.want_aborts = true;
		std::string aborts_file = (const std::string&)options.get("abort_file");
		if (!aborts_file.empty()) {
			analysis_config.aborts_file = aborts_file;
		}
		else {
			std::cerr << "Error: abort actions file not specified" << std::endl;
			exit(1);
		}
	}

	// Platform specification
	if (options.is_set_by_user("platform_file")) {
		analysis_config.platform_defined = true;
		analysis_config.want_multiprocessor = true;
		std::string platform_file = (const std::string&)options.get("platform_file");
		if (!platform_file.empty()) {
			analysis_config.platform_file = platform_file;
		}
		else {
			std::cerr << "Error: platform specification file not specified" << std::endl;
			exit(1);
		}
	}
	else if (options.is_set_by_user("num_processors")) {
		analysis_config.want_multiprocessor = true;
		unsigned int num_processors = options.get("num_processors");
		if (!num_processors) {
			std::cerr << "Error: invalid number of processors\n" << std::endl;
			exit(1);
		}
		analysis_config.num_processors = num_processors;
	}

	#ifdef CONFIG_ANALYSIS_EXTENSIONS
	// Mk-firm analysis
	if (options.is_set_by_user("mk_file")) {
		analysis_config.want_mk = true;
		std::string mk_file = (const std::string&)options.get("mk_file");
		if (!mk_file.empty()) {
			analysis_config.mk_file = mk_file;
		}
		else {
			std::cerr << "Error: mk-firm specifications file not specified" << std::endl;
			exit(1);
		}
	}
	// Task Chains analysis
	if (options.is_set_by_user("task_chains_file")) {
		analysis_config.want_task_chains = true;
		std::string tc_file = (const std::string&)options.get("task_chains_file");
		if (!tc_file.empty()) {
			analysis_config.task_chains_file = tc_file;
		}
		else {
			std::cerr << "Error: task chains specifications file not specified" << std::endl;
			exit(1);
		}
	}
	#else
	if (options.is_set_by_user("mk_file")) {
		std::cerr << "Error: mk-firm analysis support must be enabled "
		          << "during compilation (ANALYSIS_EXTENSIONS must be set)."
		          << std::endl;
		exit(1);
	}
	if (options.is_set_by_user("task_chains_file")) {
		std::cerr << "Error: task chains analysis support must be enabled "
		          << "during compilation (ANALYSIS_EXTENSIONS must be set)."
		          << std::endl;
		exit(1);
	}
	#endif

	// Analysis limits
	if (options.is_set_by_user("timeout")) {
		analysis_config.timeout = parse_time_limit(options.get("timeout"));
	}
	if (options.is_set_by_user("mem_max")) {
		analysis_config.mem_max = parse_memory_limit(options.get("mem_max"));
	}
	if (options.is_set_by_user("depth")) {
		analysis_config.max_depth = static_cast<unsigned long>(options.get("depth"));
	}

	// Continue after deadline miss
	if (options.is_set_by_user("go_on_after_dl")) {
		analysis_config.continue_after_dl_miss = true;
	}

	// Merge options
	if (options.is_set_by_user("merge_strategy")) {
		std::string merge_opts = (const std::string&)options.get("merge_strategy");
		update_merge_options(merge_opts, analysis_config);
	}

	// Focused exploration
	#ifdef CONFIG_PRUNING
	if (options.is_set_by_user("focus_file")) {
		analysis_config.want_focus = true;
		std::string focus_file = (const std::string&)options.get("focus_file");
		if (!focus_file.empty()) {
			analysis_config.focus_file = focus_file;
		}
		else {
			std::cerr << "Error: focused exploration configuration file not specified" << std::endl;
			exit(1);
		}
	}
	#endif

	#ifdef CONFIG_PARALLEL
	// Parallel exploration
	if (options.is_set_by_user("parallel")) {
		analysis_config.want_parallel = true;
	}
	if (options.is_set_by_user("num_threads")) {
		analysis_config.want_parallel = true;
		analysis_config.num_threads = options.get("num_threads");
	}
	#ifdef CONFIG_COLLECT_SCHEDULE_GRAPH
	if (analysis_config.want_parallel && analysis_config.want_dot_graph) {
		std::cerr << "Warning: Parallel execution is not compatible with "
		          << "schedule graph collection. Disabling parallel execution." 
		          << std::endl;
		analysis_config.want_parallel = false;
	}
	#endif
	#else
	if (options.is_set_by_user("parallel") || options.is_set_by_user("num_threads")) {
		std::cerr << "Error: parallel exploration support must be enabled "
		          << "during compilation (CONFIG_PARALLEL is not set)."
		          << std::endl;
		exit(1);
	}
	#endif

	// output options
	if (options.is_set_by_user("rta")) {
		analysis_config.want_rta_file = true;
	}
	if (options.is_set_by_user("rta")) {
		analysis_config.want_width_file = true;
	}
	if (options.is_set_by_user("miss_info")) {
		analysis_config.want_deadline_miss_info = true;
	}
	if (options.is_set_by_user("print_header")) {
		analysis_config.want_header = true;
	}
	if (options.is_set_by_user("verbose")) {
		analysis_config.want_verbose = true;
	}
	// dot file options
	#ifdef CONFIG_COLLECT_SCHEDULE_GRAPH
	if (options.is_set_by_user("want_dot")) {
		analysis_config.want_dot_graph = true;
		std::string dot_file = (const std::string&)options.get("dot_file");
		if (!dot_file.empty()) {
			analysis_config.dot_file = dot_file;
		}
		else {
			std::cerr << "Error: log options file not specified" << std::endl;
			exit(1);
		}
	}
	else if (options.is_set_by_user("dot_file")) {
		std::cerr << "Warning: log options are set but log of state graph is not activated ('-g' or '--save-graph' not set). "
			<< "The content of the file passed in argument with '--log_opts' will be ignored." << std::endl;
	}
	#else
	if (options.is_set_by_user("want_dot") || options.is_set_by_user("dot_file")) {
		std::cerr << "Error: graph collection support must be enabled "
			<< "during compilation (CONFIG_COLLECT_SCHEDULE_GRAPH "
			<< "is not set)." << std::endl;
		exit(1);
	}
	#endif
}


/**
 * @brief Configure state merging strategy for schedule graph exploration
 * @param merge_opts Merge strategy identifier:
 *                   - "no"   : Disable merging (naive exploration)
 *                   - "c1"   : Conservative merge without job finish times
 *                   - "c2"   : Conservative merge with job finish times
 *                   - "l2"   : Merge with depth 2
 *                   - "l3"   : Merge with depth 3
 *                   - "lmax" : Merge with maximum depth (-1)
 *                   - default: Merge with depth 1
 * @param analysis_config Analysis_config structure to update
 */
void update_merge_options(const std::string& merge_opts, Analysis_config& analysis_config) {
	// Reset merge-related flags to defaults
	analysis_config.want_naive = false;
	analysis_config.merge_conservative = false;
	analysis_config.merge_use_job_finish_times = false;
	analysis_config.merge_depth = 1;
	
	// Set merge strategy based on input
	if (merge_opts == "no") {
		analysis_config.want_naive = true;
	} else if (merge_opts == "c1") {
		analysis_config.merge_conservative = true;
	} else if (merge_opts == "c2") {
		analysis_config.merge_conservative = true;
		analysis_config.merge_use_job_finish_times = true;
	} else if (merge_opts == "l2") {
		analysis_config.merge_depth = 2;
	} else if (merge_opts == "l3") {
		analysis_config.merge_depth = 3;
	} else if (merge_opts == "lmax") {
		analysis_config.merge_depth = -1;
	} else if (merge_opts != "l1" && !merge_opts.empty()) {
		std::cerr << "Warning: Unknown merge strategy '" << merge_opts << "', using default (l1)" << std::endl;
	}
	// Default (empty, "l1", or unknown) keeps merge_depth = 1
}

#endif // ANALYSIS_CONFIG_HPP