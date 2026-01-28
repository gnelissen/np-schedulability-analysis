#ifndef NP_PROBLEM_HPP
#define NP_PROBLEM_HPP

#include "jobs.hpp"
#include "precedence.hpp"
#include "aborts.hpp"
#include "affinity.hpp"
#ifdef CONFIG_PRUNING
#include "pruning_cond.hpp"
#endif

namespace NP {

	// Description of a non-preemptive scheduling problem
	template<class Time>
	class Scheduling_problem {
	public:
		typedef typename Job<Time>::Job_set Workload;
		typedef typename std::vector<Abort_action<Time>> Abort_actions;
		typedef typename std::vector<Precedence_constraint<Time>> Precedence_constraints;

		// ** Description of the workload:
		// (1) a set of jobs
		Workload jobs;
		// (2) a set of precedence constraints among the jobs
		Precedence_constraints prec;
		// (3) abort actions for (some of) the jobs
		Abort_actions aborts;

		// ** Platform model:
		// initial state (availability intervals) of the identical processors 
		// on which the jobs are being dispatched (globally, in priority order)
		std::vector<std::vector<Interval<Time>>> processors_initial_state;

		// Classic default setup: no abort actions
		Scheduling_problem(const Workload& jobs, const Precedence_constraints& prec,
		                   unsigned int num_processors = 1)
		: jobs(jobs)
		, prec(prec)
		{
			processors_initial_state.emplace_back(num_processors, Interval<Time>(0, 0));
			assert(num_processors > 0);
			validate_prec_cstrnts<Time>(this->prec);
			validate_affinities<Time>(this->jobs, 1);
			validate_jobs<Time>(this->jobs, this->processors_initial_state);
			initialise_c_dags();
		}

		Scheduling_problem(const Workload& jobs, const Precedence_constraints& prec,
			const std::vector<std::vector<Interval<Time>>>& proc_init_state)
		: jobs(jobs)
		, prec(prec)
		, processors_initial_state(proc_init_state)
		{
			assert(processors_initial_state.size() > 0);
			validate_prec_cstrnts<Time>(this->prec);
			validate_affinities<Time>(this->jobs, proc_init_state.size());
			validate_jobs<Time>(this->jobs, this->processors_initial_state);
			initialise_c_dags();
		}

		// Constructor with abort actions and precedence constraints
		Scheduling_problem(const Workload& jobs, const Precedence_constraints& prec,
		                   const Abort_actions& aborts,
		                   unsigned int num_processors)
		: jobs(jobs)
		, prec(prec)
		, aborts(aborts)
		{
			processors_initial_state.emplace_back(num_processors, Interval<Time>(0, 0));
			assert(num_processors > 0);
			validate_prec_cstrnts<Time>(this->prec);
			validate_abort_refs<Time>(aborts, jobs);
			validate_affinities<Time>(this->jobs, 1);
			validate_jobs<Time>(this->jobs, this->processors_initial_state);
			initialise_c_dags();
		}

		Scheduling_problem(const Workload& jobs, const Precedence_constraints& prec,
			const Abort_actions& aborts,
			const std::vector<std::vector<Interval<Time>>>& proc_init_state)
			: jobs(jobs)
			, prec(prec)
			, aborts(aborts)
			, processors_initial_state(proc_init_state)
		{
			assert(processors_initial_state.size() > 0);
			validate_prec_cstrnts<Time>(this->prec);
			validate_abort_refs<Time>(aborts, jobs);
			validate_affinities<Time>(this->jobs, proc_init_state.size());
			validate_jobs<Time>(this->jobs, this->processors_initial_state);
			initialise_c_dags();
		}

		// Convenience constructor: no DAG, no abort actions
		Scheduling_problem(const Workload& jobs,
		                   unsigned int num_processors = 1)
		: jobs(jobs)
		{
			processors_initial_state.emplace_back(num_processors, Interval<Time>(0, 0));
			assert(num_processors > 0);
			validate_affinities<Time>(this->jobs, 1);
			validate_jobs<Time>(this->jobs, this->processors_initial_state);
		}

		Scheduling_problem(const Workload& jobs,
			const std::vector<unsigned int>& num_processors)
			: jobs(jobs)
		{
			assert(num_processors.size() > 0);
			for (auto n : num_processors) {
				processors_initial_state.emplace_back(n, Interval<Time>(0, 0));
				assert(n > 0);
			}
			validate_affinities<Time>(this->jobs, num_processors.size());
			validate_jobs<Time>(this->jobs, this->processors_initial_state);
		}

		Scheduling_problem(const Workload& jobs,
			const std::vector<std::vector<Interval<Time>>>& proc_init_state)
			: jobs(jobs)
			, processors_initial_state(proc_init_state)
		{
			assert(processors_initial_state.size() > 0);
			validate_affinities<Time>(this->jobs, proc_init_state.size());
			validate_jobs<Time>(this->jobs, this->processors_initial_state);
		}

	private:
		// Helper structure for precomputed graph data
		struct DAG_Graph {
			Job_lookup id_to_index;  // JobID -> Job_index map
			std::vector<std::vector<Job_index>> successors;  // successors[job_index] = list of successor indices
			std::vector<std::vector<Job_index>> predecessors; // predecessors[job_index] = list of predecessor indices
		};

		// Build lookup maps and adjacency lists once for efficient access
		DAG_Graph build_graph(Workload& jobs, const Precedence_constraints& edges) {
			DAG_Graph graph;
			graph.successors.resize(jobs.size());
			graph.predecessors.resize(jobs.size());

			// Build job_id -> job_index map
			for (std::size_t i = 0; i < jobs.size(); ++i) {
				graph.id_to_index[jobs[i].get_id()] = jobs[i].get_job_index();
			}

			// Build adjacency lists
			for (const auto& edge : edges) {
				auto from_it = graph.id_to_index.find(edge.get_fromID());
				auto to_it = graph.id_to_index.find(edge.get_toID());
				if (from_it != graph.id_to_index.end() && to_it != graph.id_to_index.end()) {
					graph.successors[from_it->second].push_back(to_it->second);
					graph.predecessors[to_it->second].push_back(from_it->second);
				}
			}
			return graph;
		}

		// Get all descendants of a job using precomputed graph
		std::set<Job_index> get_descendants(Job_index job_index, const DAG_Graph& graph) {
			std::set<Job_index> descendants;
			std::vector<Job_index> stack;
			stack.push_back(job_index);
			while (!stack.empty()) {
				Job_index current = stack.back();
				stack.pop_back();
				for (Job_index succ : graph.successors[current]) {
					if (descendants.insert(succ).second) {
						stack.push_back(succ);
					}
				}
			}
			return descendants;
		}

		void initialise_c_dags() {
			if (prec.empty()) return;

			// Build graph structure once
			DAG_Graph graph = build_graph(jobs, prec);

			// Set conditional siblings for conditional DAGs
			set_conditional_siblings(jobs, graph);
			// Set incompatible jobs for conditional DAGs
			find_all_incompatible_jobs(jobs, graph);
			// Set the topological order for each job
			set_rank_order(jobs, graph);
		}

		// Get all ancestors of a job using precomputed graph
		std::set<Job_index> get_ancestors(Job_index job_index, const DAG_Graph& graph) {
			std::set<Job_index> ancestors;
			std::vector<Job_index> stack;
			stack.push_back(job_index);

			while (!stack.empty()) {
				Job_index current = stack.back();
				stack.pop_back();

				for (Job_index pred : graph.predecessors[current]) {
					if (ancestors.insert(pred).second) {
						stack.push_back(pred);
					}
				}
			}
			return ancestors;
		}

		// Set rank order using Kahn's algorithm (topological sort)
		void set_rank_order(Workload& jobs, const DAG_Graph& graph) {
			const std::size_t n = jobs.size();
			std::vector<std::size_t> in_degree(n, 0);
			std::vector<std::size_t> rank(n, 0);

			// Calculate in-degrees
			for (std::size_t i = 0; i < n; ++i) {
				in_degree[i] = graph.predecessors[i].size();
			}
			// Find all source nodes (no predecessors)
			std::vector<Job_index> queue;
			for (std::size_t i = 0; i < n; ++i) {
				if (in_degree[i] == 0) {
					queue.push_back(i);
					rank[i] = 1;
				}
			}
			// Process nodes in topological order
			std::size_t head = 0;
			while (head < queue.size()) {
				Job_index current = queue[head++];
				for (Job_index succ : graph.successors[current]) {
					// Update rank: max of all predecessors + 1
					rank[succ] = std::max(rank[succ], rank[current] + 1);
					
					if (--in_degree[succ] == 0) {
						queue.push_back(succ);
					}
				}
			}
			// Set the order for all jobs
			for (std::size_t i = 0; i < n; ++i) {
				jobs[i].set_order(rank[i]);
			}
		}

		void find_all_incompatible_jobs(Workload& jobs, const DAG_Graph& graph) {
			const std::size_t n = graph.id_to_index.size();
			std::vector<bool> calculated(n, false);
			for (std::size_t i = 0; i < n; ++i) {
				// if we did not compute the incompatible jobs for job i yet
				if (calculated[i] == false) {
					Job<Time>& job = jobs[i];
					// compute incompatible jobs for job i
					if (job.is_conditional_sibling()) {
						// compute incompatible jobs for all siblings at once
						const auto& preds = graph.predecessors[i];
						assert(preds.size() == 1); // conditional siblings have exactly one conditional predecessor
						const auto& sibs = graph.successors[preds[0]];
						assert(sibs.size() >= 2); // must be at least two siblings
						std::vector<std::set<Job_index>> sib_descendants(sibs.size());
						// union of all descendants of all siblings and siblings themsleves
						std::set<Job_index> union_set;
						for (std::size_t s = 0; s < sibs.size(); ++s) {
							sib_descendants[s] = get_descendants(sibs[s], graph);
							union_set.insert(sib_descendants[s].begin(), sib_descendants[s].end());
							union_set.insert(sibs[s]); // include the sibling itself
						}
						// for each sibling, the incompatible jobs are the union minus its own descendants
						for (std::size_t s = 0; s < sibs.size(); ++s) {
							Job_index sib_index = sibs[s];
							for (Job_index uj : union_set) {
								if (sib_descendants[s].count(uj) == 0) {
									jobs[sib_index].add_incompatible_job(uj);
								}
							}
						}
					}
					else {
						// non-conditional siblings have only themselves as incompatible
						job.add_incompatible_job(i);
					}
				}
			}
		}

		// Set conditional siblings using precomputed graph
		void set_conditional_siblings(Workload& jobs, const DAG_Graph& graph) {
			for (std::size_t i = 0; i < jobs.size(); ++i) {
				if (jobs[i].get_type() == Job<Time>::Job_type::C_FORK) {
					for (Job_index succ_idx : graph.successors[i]) {
						jobs[succ_idx].set_conditional_sibling(true);
						jobs[succ_idx].set_conditional_predecessor(i);
					}
				}
			}
		}
	};

	// Common options to pass to the analysis engines
	struct Analysis_options {
		// After how many seconds of CPU time should we give up?
		// Zero means unlimited.
		double timeout;

		long max_memory_usage = 0; // in KiB

		// After how many scheduling decisions (i.e., depth of the
		// schedule graph) should we terminate the analysis?
		// Zero means unlimited.
		unsigned int max_depth;

		// Should we terminate the analysis upon encountering the first
		// deadline miss?
		bool early_exit;

		// Should we use state-merging techniques or naively explore the
		// whole state space in a brute-force manner (only useful as a
		// baseline).
		bool be_naive;

		// If we use state merging, defines options to use
		bool merge_conservative;
		bool merge_use_job_finish_times;
		int merge_depth;

		// Should we write where we are in the analysis?
		bool verbose;

#ifdef CONFIG_PARALLEL
		// Parallel execution options
		bool parallel_enabled = true;
		unsigned int num_threads = 0;  // 0 = auto-detect
		unsigned int min_nodes_per_thread = 4;  // Minimum nodes per thread for load balancing
#endif

#ifdef CONFIG_PRUNING
		// Pruning options
		bool pruning_active = false;
		Pruning_condition pruning_cond;
#endif

		Analysis_options()
		: timeout(0)
		, max_depth(0)
		, early_exit(true)
		, be_naive(false)
		, merge_conservative(false)
		, merge_use_job_finish_times(false)
		, merge_depth(1)
		, verbose(false)
#ifdef CONFIG_PARALLEL
		, parallel_enabled(true)
		, num_threads(0)
		, min_nodes_per_thread(4)
#endif
#ifdef CONFIG_PRUNING
		, pruning_active(false)
		, pruning_cond()
#endif
		{
		}
	};
}

#endif