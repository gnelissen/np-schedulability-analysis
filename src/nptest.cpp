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

static bool want_precedence = false;
static std::string precedence_file;

static bool want_aborts = false;
static std::string aborts_file;

static bool want_global = false;
static unsigned int num_processors = 1;

static bool want_partitioned = false;
static std::string platform_file;

#ifdef CONFIG_COLLECT_SCHEDULE_GRAPH
static bool want_dot_graph;
#endif
static double timeout;
static unsigned int max_depth = 0;

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
	std::istream &in,
	std::istream &prec_in,
	std::istream &aborts_in,
	std::istream& platform_in,
    bool &is_yaml)
{
#ifdef CONFIG_PARALLEL
	oneapi::tbb::task_arena arena(num_worker_threads ? num_worker_threads : oneapi::tbb::info::default_concurrency());
#endif


	// Parse input files and create NP scheduling problem description
    typename NP::Job<Time>::Job_set jobs = is_yaml ? NP::parse_yaml_job_file<Time>(in) : NP::parse_csv_job_file<Time>(in);
	// Parse precedence constraints
	std::vector<NP::Precedence_constraint<Time>> edges = is_yaml ? NP::parse_yaml_dag_file<Time>(prec_in) : NP::parse_precedence_file<Time>(prec_in);
	std::vector<unsigned int> clusters = want_partitioned ?
		(is_yaml ? NP::parse_yaml_platform_file(platform_in) : NP::parse_csv_platform_file(platform_in))
		: std::vector<unsigned int>{ num_processors };

	NP::Scheduling_problem<Time> problem{
        jobs,
		edges,
		NP::parse_abort_file<Time>(aborts_in),
		clusters };

	// Set common analysis options
	NP::Analysis_options opts;
	opts.verbose = want_verbose;
	opts.timeout = timeout;
	opts.max_depth = max_depth;
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
		rta << "Task ID, Job ID, BCCT, WCCT, BCRT, WCRT" << std::endl;
		for (const auto& j : problem.jobs) {
			Interval<Time> finish = space->get_finish_times(j);
			rta << j.get_task_id() << ", "
			    << j.get_job_id() << ", "
			    << finish.from() << ", "
			    << finish.until() << ", "
			    << std::max<long long>(0,
			                           (finish.from() - j.earliest_arrival()))
			    << ", "
			    << (finish.until() - j.earliest_arrival())
			    << std::endl;
		}
	}

	auto width_stream = std::ostringstream();
	if (want_width_file) {
		width_stream << "Depth, Width (#Nodes), Width (#States)" << std::endl;
		const std::vector<std::pair<unsigned long, unsigned long>>& width = space->evolution_exploration_front_width();
		for (int d = 0; d < problem.jobs.size(); d++) {
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
		(unsigned long)(problem.jobs.size()),
		space->get_cpu_time(),
		graph.str(),
		rta.str(),
		width_stream.str()
	};
	delete space;
	return results;
}

static Analysis_result process_stream(
	std::istream &in,
	std::istream &prec_in,
	std::istream &aborts_in,
	std::istream& platform_in,
    bool is_yaml)
{
	if (want_dense)
		return analyze<dense_t, NP::Global::State_space<dense_t>>(in, prec_in, aborts_in, platform_in, is_yaml);
	else
		return analyze<dtime_t, NP::Global::State_space<dtime_t>>(in, prec_in, aborts_in, platform_in, is_yaml);
}

static void process_file(const std::string& fname)
{
	try {
		Analysis_result result;

		auto empty_dag_stream = std::istringstream("\n");
		auto empty_aborts_stream = std::istringstream("\n");
		auto empty_platform_stream = std::istringstream("\n");
		auto dag_stream = std::ifstream();
		auto aborts_stream = std::ifstream();
		auto platform_stream = std::ifstream();

		if (want_precedence)
			dag_stream.open(precedence_file);

		if (want_aborts)
			aborts_stream.open(aborts_file);

		if (want_partitioned)
			platform_stream.open(platform_file);

		std::istream &dag_in = want_precedence ?
			static_cast<std::istream&>(dag_stream) :
			static_cast<std::istream&>(empty_dag_stream);

		std::istream &aborts_in = want_aborts ?
			static_cast<std::istream&>(aborts_stream) :
			static_cast<std::istream&>(empty_aborts_stream);

		std::istream& platform_in = want_partitioned ?
			static_cast<std::istream&>(platform_stream) :
			static_cast<std::istream&>(empty_platform_stream);

		if (fname == "-")
		{
			result = process_stream(std::cin, dag_in, aborts_in, platform_in, false);
		}
		else {
            // check the extension of the file
            std::string ext = fname.substr(fname.find_last_of(".") + 1);
            bool is_yaml = false;
            if (ext == "yaml" || ext == "yml") {
                is_yaml = true;
            }

			auto in = std::ifstream(fname, std::ios::in);
			result = process_stream(in, dag_in, aborts_in, platform_in, is_yaml);

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
				auto p = is_yaml ? rta_name.find(".yaml") : rta_name.find(".csv");
				if (p != std::string::npos) {
					rta_name.replace(p, std::string::npos, ".rta.csv");
					auto out  = std::ofstream(rta_name,  std::ios::out);
					out << result.response_times_csv;
					out.close();
				}
			}

			if (want_width_file) {
				std::string width_file_name = fname;
				auto p = is_yaml ? width_file_name.find(".yaml") : width_file_name.find(".csv");
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
			<< ",  " << ((double)mem_used) / (1024.0)
			<< ",  " << (int)result.timeout;
		if (want_partitioned)
		{
			std::cout << ",  clustered"
				<< std::endl;
		}
		else
		{
			std::cout << ",  " << num_processors
				<< std::endl;
		}
	} catch (std::ios_base::failure& ex) {
		std::cerr << fname;
		if (want_precedence)
			std::cerr << " + " << precedence_file;
		std::cerr <<  ": parse error" << std::endl;
		exit(1);
	} catch (NP::InvalidJobReference& ex) {
		std::cerr << precedence_file << ": bad job reference: job "
				  << ex.ref.job << " of task " << ex.ref.task
				  << " is not part of the job set given in "
				  << fname
				  << std::endl;
		exit(3);
	} catch (NP::InvalidAbortParameter& ex) {
		std::cerr << aborts_file << ": invalid abort parameter: job "
				  << ex.ref.job << " of task " << ex.ref.task
				  << " has an impossible abort time (abort before release)"
				  << std::endl;
		exit(4);
	} catch (NP::InvalidPrecParameter& ex) {
		std::cerr << precedence_file << ": invalid self-suspending parameter: job "
				  << ex.ref.job << " of task " << ex.ref.task
				  << " has an invalid self-suspending time"
				  << std::endl;
		exit(5);
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

	/*parser.add_option("-n", "--naive").dest("naive").set_default("0")
	      .action("store_const").set_const("1")
	      .help("use the naive exploration method (default: merging)");*/

	parser.add_option("-p", "--precedence").dest("precedence_file")
	      .help("name of the file that contains the job set's precedence DAG")
	      .set_default("");

	parser.add_option("-a", "--abort-actions").dest("abort_file")
	      .help("name of the file that contains the job set's abort actions")
	      .set_default("");

	parser.add_option("-m", "--multiprocessor").dest("num_processors")
	      .help("set the number of processors of the platform. Mutually exclusive with --platform.")
	      .set_default("1");

	parser.add_option("--platform").dest("platform_file")
		.help("name of the file that contains the platform architecture. Mutually exclusive with --multiprocessor.")
		.set_default("");

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

	if (options.get("version")) {
		std::cout << parser.prog() <<" version "
				  	<< VERSION_MAJOR << "."
					<< VERSION_MINOR << "."
					<< VERSION_PATCH << std::endl;
		return 0;
	}

	std::string merge_opts = (const std::string&)options.get("merge_opts");
	want_naive = (merge_opts == "no");
	if (merge_opts == "c1") {
		merge_conservative = true;
		merge_use_job_finish_times = false;
	}
	else if (merge_opts == "c2") {
		merge_conservative = true;
		merge_use_job_finish_times = true;
	}
	if (merge_opts == "l2")
		merge_depth = 2;
	else if (merge_opts == "l3")
		merge_depth = 3;
	else if (merge_opts == "lmax")
		merge_depth = -1;
	else
		merge_depth = 1;

	std::string time_model = (const std::string&)options.get("time_model");
	want_dense = time_model == "dense";

	//want_naive = options.get("naive");

	timeout = options.get("timeout");

	max_depth = options.get("depth");
	if (options.is_set_by_user("depth")) {
		if (max_depth <= 1) {
			std::cerr << "Error: invalid depth argument\n" << std::endl;
			return 1;
		}
		max_depth -= 1;
	}

	want_precedence = options.is_set_by_user("precedence_file");
	if (want_precedence && parser.args().size() > 1) {
		std::cerr << "[!!] Warning: multiple job sets "
		          << "with a single precedence DAG specified."
		          << std::endl;
	}
	precedence_file = (const std::string&) options.get("precedence_file");

	want_aborts = options.is_set_by_user("abort_file");
	if (want_aborts && parser.args().size() > 1) {
		std::cerr << "[!!] Warning: multiple job sets "
		          << "with a single abort action list specified."
		          << std::endl;
	}
	aborts_file = (const std::string&) options.get("abort_file");

	want_global = options.is_set_by_user("num_processors");
	num_processors = options.get("num_processors");
	if (!num_processors || num_processors > MAX_PROCESSORS) {
		std::cerr << "Error: invalid number of processors\n" << std::endl;
		return 1;
	}

	want_partitioned = options.is_set_by_user("platform_file");
	platform_file = (const std::string&)options.get("platform_file");
	if (want_global && want_partitioned) {
		std::cerr << "Error: options --platform and --multiprocessor are mutually exclusive.\n" << std::endl;
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
