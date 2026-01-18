#include <iostream>
#include <sstream>
#include <fstream>
#include <algorithm>
#include "yaml-cpp/yaml.h"

#include "mem.hpp"
#include "OptionParser.h"
#include "config.h"
#include "problem.hpp"
#include "global/space.hpp"
#include "io.hpp"
#include "clock.hpp"
#include "analysis_config.hpp"
#include "analysis_result.hpp"
#include "problem_builder.hpp"

#ifdef CONFIG_ANALYSIS_EXTENSIONS
#include "global/extension/mk-firm/mk_firm.hpp"
#include "global/extension/mk-firm/mk_extension.hpp"
#endif

#define MAX_PROCESSORS 512

/**
 * @brief Perform the analysis for a given scheduling problem
 * @tparam Time Time type (dense_t or dtime_t)
 * @tparam Space State space type
 * @param jobs_file Path to the job set file
 * @param config Analysis configuration
 * @return Analysis_result structure containing the analysis results
 */
template<class Time, class Space>
Analysis_result analyze(
	std::string jobs_file,
	const Analysis_config& config)
{
	auto problem = NP::problem_builder<Time>(jobs_file, config);

	// Set common analysis options
	NP::Analysis_options opts;
	opts.verbose = config.want_verbose;
	opts.timeout = config.timeout;
	opts.max_memory = config.mem_max;
	opts.max_depth = config.max_depth;
	opts.early_exit = !config.continue_after_dl_miss;
	opts.be_naive = config.want_naive;
	opts.merge_opts.conservative = config.merge_conservative;
	opts.merge_opts.budget = config.merge_depth;
	opts.merge_opts.use_finish_times = config.merge_use_job_finish_times;
#ifdef CONFIG_PARALLEL
	opts.parallel_enabled = config.want_parallel;
	opts.num_threads = config.num_threads;
#endif
#ifdef CONFIG_PRUNING
	// set pruning options if requested
	if (config.want_focus && !config.focus_file.empty()) {
		opts.pruning_active = true;
		std::ifstream focus_in(config.focus_file);
		if (!focus_in) {
			std::cerr << "Error: could not open focus YAML file: " << config.focus_file << std::endl;
			exit(1);
		}
		opts.pruning_cond = NP::parse_focused_expl_spec_yaml<Time>(focus_in, problem->jobs);
	}
#endif
#ifdef CONFIG_COLLECT_SCHEDULE_GRAPH
	NP::Global::Log_options<Time> log_opts;
	log_opts.log = config.want_dot_graph;
	NP::Global::Dot_file_config log_print_config;
	if (not config.dot_file.empty()) {
		auto dot_config_stream = std::ifstream();
		dot_config_stream.open(config.dot_file);
		if (!dot_config_stream) {
			std::cerr << "Error: could not open log config YAML file: " << config.dot_file << std::endl;
			exit(1);
		}
		log_opts.log_cond = NP::parse_log_config_yaml<Time>(dot_config_stream, problem->jobs);
	}
	// Actually call the analysis engine
	auto space = Space::explore(*problem, opts, log_opts);
#else
	// Actually call the analysis engine
	auto space = Space::explore(*problem, opts);
#endif

	// Extract the analysis results
	return get_analysis_result<Time>(space, *problem, config);
}

/**
 * @brief Process a single job set file and perform the analysis
 * @param fname Path to the job set file
 * @param config Analysis configuration
 */
static void process_file(const std::string& fname, const Analysis_config& config)
{
	try {
		Analysis_result result = config.want_dense ?
			analyze<dense_t, NP::Global::State_space<dense_t>>(fname, config) :
			analyze<dtime_t, NP::Global::State_space<dtime_t>>(fname, config);

		save_result_files(fname, result, config);

		if (config.want_header) {
			print_header();
		}

		Memory_monitor mem;
		result.memory_used = mem;

		print_result(fname, result, config);
	} catch (std::ios_base::failure&) {
		std::cerr << fname;
		if (config.want_precedence)
			std::cerr << " + " << config.precedence_file;
		std::cerr <<  ": parse error" << std::endl;
		exit(1);
	} catch (NP::InvalidJobReference& ex) {
		std::cerr << config.precedence_file << ": bad job reference: job "
				  << ex.ref.job << " of task " << ex.ref.task
				  << " is not part of the job set given in "
				  << fname
				  << std::endl;
		exit(3);
	} catch (NP::InvalidAbortParameter& ex) {
		std::cerr << config.aborts_file << ": invalid abort parameter: job "
				  << ex.ref.job << " of task " << ex.ref.task
				  << " has an impossible abort time (abort before release)"
				  << std::endl;
		exit(4);
	} catch (NP::InvalidPrecParameter& ex) {
		std::cerr << config.precedence_file << ": invalid self-suspending parameter: job "
				  << ex.ref.job << " of task " << ex.ref.task
				  << " has an invalid self-suspending time"
				  << std::endl;
		exit(5);
	} catch (std::exception& ex) {
		std::cerr << fname << ": '" << ex.what() << "'" << std::endl;
		exit(1);
	}
}

/**
 * @brief Main function for the NP version of the SAG analysis
 */
int main(int argc, char** argv)
{
    auto parser = optparse::OptionParser();

    parser.description("Exact NP Schedulability Tester");
    parser.usage("usage: %prog [OPTIONS]... [JOB SET FILES]...");

    // add an option to show the version
    parser.add_option("-v", "--version").dest("version")
        .action("store_true").set_default("0")
        .help("show program's version number and exit");

    parser.add_option("--config").dest("config_file")
        .metavar("CONFIG-FILE")
        .help("path to YAML configuration file")
        .set_default("");

    parser.add_option("--merge").dest("merge_opts")
		.metavar("MERGE-LEVEL")
		.choices({ "no", "c1", "c2", "l1", "l2", "l3", "lmax"}).choices({ "no", "c1", "c2","l1","l2","l3","lmax"}).set_default("l1")
		.help("choose type of state merging approach used during the analysis. 'no': no merging, 'c1': conservative level 1, 'c2': conservative level 2, 'lx': lossy with depth=x, 'lmax': lossy with max depth. (default: l1)");

	parser.add_option("-t", "--time").dest("time_model")
	      .metavar("TIME-MODEL")
	      .choices({"dense", "discrete"}).set_default("discrete")
	      .help("choose 'discrete' or 'dense' time (default: discrete)");

	parser.add_option("-l", "--time-limit").dest("timeout")
	      .help("maximum CPU time allowed (e.g., 1d2h15m30s or 3600). \"0\" means no limit. Supported units: d (days), h (hours), m (minutes), s (seconds). Numbers without units are treated as seconds")
	      .set_default("0");

	parser.add_option("--mem-limit").dest("mem_max")
		.help("maximum memory consumption allowed (e.g., \"512M\", \"2G\", \"1024\"). \"0\" means no limit. Supported units: K/k (KiB), M/m (MiB), G/g (GiB). Numbers without units are treated as KiB")
		.set_default("0");

	parser.add_option("-d", "--depth-limit").dest("depth")
	      .help("abort graph exploration after reaching given depth (>= 2)")
	      .set_default("0");

	parser.add_option("-p", "--precedence").dest("precedence_file")
	      .help("name of the file that contains the job set's precedence DAG")
	      .set_default("");

	parser.add_option("--excl").dest("excl_file")
	      .help("[experimental] name of the file that contains the job set's exclusion (mutex) constraints")
	      .set_default("");

	parser.add_option("-a", "--abort-actions").dest("abort_file")
	      .help("name of the file that contains the job set's abort actions")
	      .set_default("");

	parser.add_option("-m", "--multiprocessor").dest("num_processors")
	      .help("set the number of processors of the platform")
	      .set_default("1");

	parser.add_option("--platform").dest("platform_file")
	      .help("name of the file that contains the platform specification (CSV or YAML)")
	      .set_default("");

	parser.add_option("--header").dest("print_header")
	      .help("print a column header")
	      .action("store_const").set_const("1")
	      .set_default("0");

	parser.add_option("--verbose").dest("verbose").set_default("0")
		.action("store_const").set_const("1")
		.help("show the current status of the analysis (default: off)");

	parser.add_option("-r", "--report").dest("rta").set_default("0")
	      .action("store_const").set_const("1")
	      .help("Reporting: store the best- and worst-case response times and store the evolution of the width of the graph (default: off)");

	parser.add_option("--miss_info").dest("miss_info").set_default("0")
		.action("store_const").set_const("1")
		.help("Report information on the deadline miss state if a deadline miss occurs (default: off)");

	parser.add_option("-c", "--continue-after-deadline-miss")
	      .dest("go_on_after_dl").set_default("0")
	      .action("store_const").set_const("1")
	      .help("do not abort the analysis on the first deadline miss "
	            "(default: off)");

	parser.add_option("-g", "--save-graph").dest("want_dot").set_default("0")
		.action("store_const").set_const("1")
		.help("Records the state graph in Graphviz dot format (default: off).");

	parser.add_option("--log_opts").dest("dot_file")
		.help("If 'save-graph is set', allows to pass a YAML file configuring what part of the state graph is recorded and what is printed in the state graph. Default: complete state space is recorded and default info is printed.")
		.set_default("");

	parser.add_option("--mk").dest("mk_file")
		.help("[experimental] name of the file that contains the mk-firm specifications (CSV)")
		.set_default("");

	parser.add_option("--taskchains").dest("task_chains_file").set_default("")
		.help("yaml file containing the task chains specification for the task chains analysis extension");

#ifdef CONFIG_PARALLEL
	parser.add_option("--parallel").dest("parallel").set_default("1")
		.action("store_const").set_const("1")
		.help("enable parallel execution (default: on when compiled with CONFIG_PARALLEL)");

	parser.add_option("--no-parallel").dest("parallel").set_default("1")
		.action("store_const").set_const("0")
		.help("disable parallel execution");

	parser.add_option("--threads").dest("num_threads")
		.help("number of threads to use (0 = auto-detect)")
		.set_default("0");
#endif

	parser.add_option("-f", "--focus").dest("focus_file")
		.help("If 'USE_PRUNING' is set, allows to send a YAML file specifying what part of the state-space to focus on or prune during the exploration.")
		.set_default("");

	auto options = parser.parse_args(argc, argv);

	if(options.get("version")){
		std::cout << parser.prog() <<" version "
				  	<< VERSION_MAJOR << "."
					<< VERSION_MINOR << "."
					<< VERSION_PATCH << std::endl;
		return 0;
	}
	
	Analysis_config analysis_config;
	// Parse config file if provided
	// config options set in the config file will be overridden by command-line options if set by user
	if (options.is_set_by_user("config_file")) {
		std::string config_file = (const std::string&)options.get("config_file");
		if (!config_file.empty()) {
			analysis_config = parse_config_file(config_file);
		}
	}	

	// Override config with command-line options that may have been set by the user
	parse_input_options(options, analysis_config);

	// process the input files and launch the analysis
    if (!parser.args().empty()) {
		auto fname = parser.args()[0];
        process_file(fname, analysis_config);
    } else if (!analysis_config.jobs_file.empty()) {
        process_file(analysis_config.jobs_file, analysis_config);
    } else {
        std::cerr << "Error: no job set file specified" << std::endl;
		exit(1);
    }

	return 0;
}