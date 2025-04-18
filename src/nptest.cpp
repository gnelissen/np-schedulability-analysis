#include <iostream>
#include <sstream>
#include <fstream>
#include <algorithm>

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#endif

#include "OptionParser.h"

#include "config.h"

#ifdef CONFIG_PARALLEL

#include <oneapi/tbb/info.h>
#include <oneapi/tbb/parallel_for.h>
#include <oneapi/tbb/task_arena.h>

#endif

#include "problem.hpp"
#include "global/space.hpp"
#include "io.hpp"
#include "clock.hpp"

#define MAX_PROCESSORS 512

// command line options
static bool want_verbose;
static bool want_naive;
static bool merge_conservative;
static bool merge_use_job_finish_times;
static int merge_depth;
static bool want_dense;

static bool want_multiprocessor = false;
static unsigned int num_processors = 1;

#ifdef CONFIG_COLLECT_SCHEDULE_GRAPH
static bool want_dot_graph;
#endif
static double timeout;
static unsigned int max_depth = 0;
static unsigned int length_obs_window = 0;

static bool want_rta_file;
static bool want_width_file;

static bool continue_after_dl_miss = false;

#ifdef CONFIG_PARALLEL
static unsigned int num_worker_threads = 0;
#endif

struct Analysis_result {
	bool schedulable;
	bool timeout;
	unsigned long long number_of_nodes, number_of_states, number_of_edges, max_width, number_of_jobs;
	double cpu_time;
	std::string graph;
	std::string response_times_csv;
	std::string width_evolution_csv;
};


template<class Time, class Space>
static Analysis_result analyze(
	std::istream &in)
{
#ifdef CONFIG_PARALLEL
	oneapi::tbb::task_arena arena(num_worker_threads ? num_worker_threads : oneapi::tbb::info::default_concurrency());
#endif


	// Parse input files and create NP scheduling problem description
	typename NP::Task<Time>::Task_set tasks = NP::parse_tasks_file<Time>(in);
	NP::Scheduling_problem<Time> problem{
	    tasks,
		num_processors};

	// Set common analysis options
	NP::Analysis_options opts;
	opts.verbose = want_verbose;
	opts.timeout = timeout;
	opts.max_depth = max_depth;
	opts.l_obs_window = length_obs_window;
	opts.early_exit = !continue_after_dl_miss;
	opts.be_naive = want_naive;
	opts.merge_conservative = merge_conservative;
	opts.merge_depth = merge_depth;
	opts.merge_use_job_finish_times = merge_use_job_finish_times;

	// Actually call the analysis engine
	auto space = Space::explore(problem, opts);

	// Extract the analysis results
	auto graph = std::ostringstream();
#ifdef CONFIG_COLLECT_SCHEDULE_GRAPH
	if (want_dot_graph)
		graph << *space;
#endif

	auto rta = std::ostringstream();

	if (want_rta_file) {
		rta << "Segment ID, BCRT, WCRT" << std::endl;
		for (const auto& t : problem.tasks) {
			for (const auto& s : t.get_subtasks()) {
				Interval<Time> rt = space->get_resp_times(s);
				rta << s.get_name() << ", "
					<< rt.min() << ", "
					<< rt.max() << std::endl;
			}
		}
	}

	auto width_stream = std::ostringstream();
	if (want_width_file) {
		width_stream << "Depth, Width (#Nodes), Width (#States)" << std::endl;
		const std::vector<std::pair<unsigned long, unsigned long>>& width = space->evolution_exploration_front_width();
		for (int d = 0; d < width.size(); d++) {
			width_stream << d << ", "
					   << width[d].first
					   << ", "
					   << width[d].second
					   << std::endl;
		}
	}

	Analysis_result results = Analysis_result{
		space->is_schedulable(),
		space->was_timed_out(),
		space->number_of_nodes(),
		space->number_of_states(),
		space->number_of_edges(),
		space->max_exploration_front_width(),
		(unsigned long)(problem.tasks.size()),
		space->get_cpu_time(),
		graph.str(),
		rta.str(),
		width_stream.str()
	};
	delete space;
	return results;
}

static Analysis_result process_stream(
	std::istream &in)
{
	if (want_multiprocessor && want_dense)
		return analyze<dense_t, NP::Global::State_space<dense_t>>(in);
	else if (want_multiprocessor && !want_dense)
		return analyze<dtime_t, NP::Global::State_space<dtime_t>>(in);
	else if (want_dense)
		return analyze<dense_t, NP::Global::State_space<dense_t>>(in);
	else
		return analyze<dtime_t, NP::Global::State_space<dtime_t>>(in);
}

static void process_file(const std::string& fname)
{
	try {
		Analysis_result result;

		if (fname == "-")
		{
			result = process_stream(std::cin);
		}
		else {
            auto in = std::ifstream(fname, std::ios::in);
			result = process_stream(in);

#ifdef CONFIG_COLLECT_SCHEDULE_GRAPH
			if (want_dot_graph) {
				DM("\nDot graph being made\n");
				std::string dot_name = fname;
				auto p = is_yaml ? dot_name.find(".yaml") : dot_name.find(".csv");
				if (p != std::string::npos) {
					dot_name.replace(p, std::string::npos, ".dot");
					auto out  = std::ofstream(dot_name,  std::ios::out);
					out << result.graph;
					out.close();
				}
			}
#endif
			if (want_rta_file) {
				std::string rta_name = fname;
				auto p = rta_name.find_last_of(".");
				if (p != std::string::npos) {
					rta_name.replace(p, std::string::npos, ".rta.csv");
					auto out  = std::ofstream(rta_name,  std::ios::out);
					out << result.response_times_csv;
					out.close();
				}
			}

			if (want_width_file) {
				std::string width_file_name = fname;
				auto p = width_file_name.find_last_of(".");
				if (p != std::string::npos) {
					width_file_name.replace(p, std::string::npos, ".width.csv");
					auto out = std::ofstream(width_file_name, std::ios::out);
					out << result.width_evolution_csv;
					out.close();
				}
			}
		}

#ifdef _WIN32 
		PROCESS_MEMORY_COUNTERS pmc;
		auto currentProcess = GetCurrentProcess();
		GetProcessMemoryInfo(currentProcess, &pmc, sizeof(pmc));
		long mem_used = pmc.PeakWorkingSetSize / 1024; //in kiB
		CloseHandle(currentProcess);
#else
		struct rusage u;
		long mem_used = 0;
		if (getrusage(RUSAGE_SELF, &u) == 0)
			mem_used = u.ru_maxrss;
#endif

		std::cout << fname;

		if (max_depth && max_depth < result.number_of_jobs)
			// mark result as invalid due to debug abort
			std::cout << ",  X";
		else
			std::cout << ",  " << (int) result.schedulable;

		std::cout << ",  " << result.number_of_jobs
			  << ",  " << result.number_of_nodes
		          << ",  " << result.number_of_states
		          << ",  " << result.number_of_edges
		          << ",  " << result.max_width
		          << ",  " << std::fixed << result.cpu_time
		          << ",  " << ((double) mem_used) / (1024.0)
		          << ",  " << (int) result.timeout
		          << ",  " << num_processors
		          << std::endl;
	} catch (NP::InvalidSubtaskReference& ex) {
		std::cerr << "Bad subtask reference in constraints specification: subtask "
				  << ex.ref << " is not part of the task set given in "
				  << fname
				  << std::endl;
		exit(3);
	} catch (std::exception& ex) {
		std::cerr << fname << ": '" << ex.what() << "'" << std::endl;
		exit(1);
	}
}

static void print_header(){
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
	          << ", #CPUs"
	          << std::endl;
}

int main(int argc, char** argv)
{
	auto parser = optparse::OptionParser();

	parser.description("Exact NP Schedulability Tester");
	parser.usage("usage: %prog [OPTIONS]... [JOB SET FILES]...");

	// add an option to show the version
	parser.add_option("-v", "--version").dest("version")
		.action("store_true").set_default("0")
		.help("show program's version number and exit");


	parser.add_option("--merge").dest("merge_opts")
		.metavar("MERGE-LEVEL")
		.choices({ "no", "c1", "c2", "l1", "l2", "l3", "lmax"}).choices({ "no", "c1", "c2","l1","l2","l3","lmax"}).set_default("l1")
		.help("choose type of state merging approach used during the analysis. 'no': no merging, 'c1': conservative level 1, 'c2': conservative level 2, 'lx': lossy with depth=x, 'lmax': lossy with max depth. (default: l1)");

	parser.add_option("-t", "--time").dest("time_model")
	      .metavar("TIME-MODEL")
	      .choices({"dense", "discrete"}).set_default("discrete")
	      .help("choose 'discrete' or 'dense' time (default: discrete)");

	parser.add_option("-l", "--time-limit").dest("timeout")
	      .help("maximum CPU time allowed (in seconds, zero means no limit)")
	      .set_default("0");

	parser.add_option("-d", "--depth-limit").dest("depth")
	      .help("abort graph exploration after reaching given depth (>= 2)")
	      .set_default("0");

	parser.add_option("-w", "--obs-window").dest("obs_window")
		.help("length of the observation window analyzed by the tool (>= 2)")
		.set_default("0");

	parser.add_option("-p", "--precedence").dest("precedence_file")
	      .help("name of the file that contains the job set's precedence DAG")
	      .set_default("");

	parser.add_option("-a", "--abort-actions").dest("abort_file")
	      .help("name of the file that contains the job set's abort actions")
	      .set_default("");

	parser.add_option("-m", "--multiprocessor").dest("num_processors")
	      .help("set the number of processors of the platform")
	      .set_default("1");

	parser.add_option("--threads").dest("num_threads")
	      .help("set the number of worker threads (parallel analysis)")
	      .set_default("0");

	parser.add_option("--header").dest("print_header")
	      .help("print a column header")
	      .action("store_const").set_const("1")
	      .set_default("0");

	parser.add_option("-g", "--save-graph").dest("dot").set_default("0")
	      .action("store_const").set_const("1")
	      .help("store the state graph in Graphviz dot format (default: off)");

	parser.add_option("--verbose").dest("verbose").set_default("0")
		.action("store_const").set_const("1")
		.help("show the current status of the analysis (default: off)");

	parser.add_option("-r", "--report").dest("rta").set_default("0")
	      .action("store_const").set_const("1")
	      .help("Reporting: store the best- and worst-case response times and store the evolution of the width of the graph (default: off)");

	parser.add_option("-c", "--continue-after-deadline-miss")
	      .dest("go_on_after_dl").set_default("0")
	      .action("store_const").set_const("1")
	      .help("do not abort the analysis on the first deadline miss "
	            "(default: off)");


	auto options = parser.parse_args(argc, argv);
	//all the options that could have been entered above are processed below and appropriate variables
	// are assigned their respective values.

	if(options.get("version")){
		std::cout << parser.prog() <<" version "
				  	<< VERSION_MAJOR << "."
					<< VERSION_MINOR << "."
					<< VERSION_PATCH << std::endl;
		return 0;
	}

	std::string merge_opts = (const std::string&)options.get("merge_opts");
#ifdef CONFIG_PARALLEL
	if (merge_opts == "no") {
		std::cerr << "Error: parallel analysis (CONFIG_PARALLEL=ON) is incompatible with naive exploration."
			<< std::endl;
		return 3;
	}
#endif
	want_naive = (merge_opts == "no");
	if (merge_opts == "c1") {
		merge_conservative = true;
		merge_use_job_finish_times = false;
	}
	else if (merge_opts == "c2") {
		merge_conservative = true;
		merge_use_job_finish_times = true;
	}
	else if (merge_opts == "l2")
		merge_depth = 2;
	else if (merge_opts == "l3")
		merge_depth = 3;
	else if (merge_opts == "lmax")
		merge_depth = -1;
	else
		merge_depth = 1;

	std::string time_model = (const std::string&)options.get("time_model");
	want_dense = time_model == "dense";

	timeout = options.get("timeout");

	max_depth = options.get("depth");
	if (options.is_set_by_user("depth")) {
		if (max_depth <= 1) {
			std::cerr << "Error: invalid depth argument\n" << std::endl;
			return 1;
		}
		max_depth -= 1;
	}

	length_obs_window = options.get("obs_window");
	if (options.is_set_by_user("obs_window")) {
		if (length_obs_window <= 1) {
			std::cerr << "Error: invalid observation window length argument\n" << std::endl;
			return 1;
		}
	}

	want_multiprocessor = options.is_set_by_user("num_processors");
	num_processors = options.get("num_processors");
	if (!num_processors || num_processors > MAX_PROCESSORS) {
		std::cerr << "Error: invalid number of processors\n" << std::endl;
		return 1;
	}

	want_rta_file = options.get("rta");
	want_width_file = options.get("rta");

	want_verbose = options.get("verbose");

	continue_after_dl_miss = options.get("go_on_after_dl");

#ifdef CONFIG_COLLECT_SCHEDULE_GRAPH
	want_dot_graph = options.get("dot");
	DM("Dot graph"<<want_dot_graph<<std::endl);
#else
	if (options.is_set_by_user("dot")) {
		std::cerr << "Error: graph collection support must be enabled "
		          << "during compilation (CONFIG_COLLECT_SCHEDULE_GRAPH "
		          << "is not set)." << std::endl;
		return 2;
	}
#endif

#ifdef CONFIG_PARALLEL
	num_worker_threads = options.get("num_threads");
#else
	if (options.is_set_by_user("num_threads")) {
		std::cerr << "Error: parallel analysis must be enabled "
		          << "during compilation (CONFIG_PARALLEL "
		          << "is not set)." << std::endl;
		return 3;
	}
#endif

	// this prints the header in the output on the console
	if (options.get("print_header"))
		print_header();

	// process_file is given the arguments that have been passed
	for (auto f : parser.args())
		process_file(f);

	if (parser.args().empty())
		process_file("-");

	return 0;
}
