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

		void initialise_c_dags() {
			if (prec.empty()) return;

			// Build graph structure once
			DAG_Graph graph = build_graph(jobs, prec);

			// Set conditional siblings for conditional DAGs
			set_conditional_siblings(jobs, graph);
			// Set incompatible jobs for conditional DAGs
			find_all_incompatible_jobs(jobs, graph);
			// Optimize the incompatible jobs for the analysis
			reduce_incompatibilities(jobs, graph);
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

		// Find incompatible jobs for all jobs using reverse topological order
		void find_all_incompatible_jobs(Workload& jobs, const DAG_Graph& graph) {
			const std::size_t n = jobs.size();
			
			// Compute reverse topological order (process sinks first)
			std::vector<std::size_t> out_degree(n);
			for (std::size_t i = 0; i < n; ++i) {
				out_degree[i] = graph.successors[i].size();
			}

			std::vector<Job_index> order;
			order.reserve(n);			
			// Find sink nodes (no successors)
			for (std::size_t i = 0; i < n; ++i) {
				if (out_degree[i] == 0) {
					order.push_back(i);
				}
			}
			// BFS in reverse topological order
			std::size_t head = 0;
			while (head < order.size()) {
				Job_index current = order[head++];
				for (Job_index pred : graph.predecessors[current]) {
					if (--out_degree[pred] == 0) {
						order.push_back(pred);
					}
				}
			}
			// Process jobs in reverse topological order (sinks first)
			for (Job_index idx : order) {
				compute_incompatible_jobs_for(idx, jobs, graph);
			}
		}

		// Compute incompatible jobs for a single job
		void compute_incompatible_jobs_for(Job_index idx, Workload& jobs, const DAG_Graph& graph) {
			Job<Time>& current_job = jobs[idx];
			const auto& successors = graph.successors[idx];
			
			if (successors.empty())
				return;

			std::set<Job_index> incompatible_jobs;
			bool is_fork = (current_job.get_type() == Job<Time>::Job_type::C_FORK);
			bool first_successor = true;

			for (Job_index succ_idx : successors) {
				Job<Time>& successor_job = jobs[succ_idx];
				auto succ_incompatible = successor_job.get_incompatible_jobs_without_itself();

				if (is_fork) {
					// For FORK: intersection of all successors' incompatible jobs
					if (first_successor) {
						incompatible_jobs.insert(succ_incompatible.begin(), succ_incompatible.end());
						first_successor = false;
					} else {
						std::set<Job_index> intersection;
						std::set_intersection(
							incompatible_jobs.begin(), incompatible_jobs.end(),
							succ_incompatible.begin(), succ_incompatible.end(),
							std::inserter(intersection, intersection.begin()));
						incompatible_jobs = std::move(intersection);
					}
				} else if (successor_job.get_type() == Job<Time>::Job_type::C_JOIN) {
					// For JOIN successor: union with special handling
					// if successor is a JOIN then it should be the only successor
					assert(successors.size() == 1);
					auto my_ancestors = get_ancestors(idx, graph);
					auto succ_ancestors = get_ancestors(succ_idx, graph);

					std::set<Job_index> union_set(succ_incompatible.begin(), succ_incompatible.end());
					union_set.insert(succ_ancestors.begin(), succ_ancestors.end());
					
					// Remove my ancestors and myself
					for (Job_index anc : my_ancestors) {
						union_set.erase(anc);
					}
					union_set.erase(idx);

					incompatible_jobs.insert(union_set.begin(), union_set.end());
				} else {
					// Default: union of incompatible jobs
					incompatible_jobs.insert(succ_incompatible.begin(), succ_incompatible.end());
				}
			}

			for (Job_index inc : incompatible_jobs) {
				current_job.add_incompatible_job(inc);
			}
		}

		// Remove unnecessary incompatibilities for optimization
		void reduce_incompatibilities(Workload& jobs, const DAG_Graph& graph) {
			std::set<Job_index> processed;

			for (std::size_t i = 0; i < jobs.size(); ++i) {
				Job<Time>& job = jobs[i];
				Job_index idx = job.get_job_index();

				if (processed.count(idx)) continue;

				if (!job.is_conditional_sibling()) {
					// Non-conditional siblings: remove all incompatibilities
					job.remove_incompatible_jobs();
					processed.insert(idx);
				} else {
					// Conditional sibling: process all siblings together
					Job_index pred_idx = job.get_conditional_predecessor();
					const auto& siblings = graph.successors[pred_idx];

					// Compute intersection of all siblings' incompatible jobs
					std::set<Job_index> common_incompatible;
					bool first = true;

					for (Job_index sib : siblings) {
						auto sib_incompatible = jobs[sib].get_incompatible_jobs_without_itself();
						if (first) {
							common_incompatible.insert(sib_incompatible.begin(), sib_incompatible.end());
							first = false;
						} else {
							std::set<Job_index> intersection;
							std::set_intersection(
								common_incompatible.begin(), common_incompatible.end(),
								sib_incompatible.begin(), sib_incompatible.end(),
								std::inserter(intersection, intersection.begin()));
							common_incompatible = std::move(intersection);
						}
					}
					// Remove common incompatible jobs from all siblings
					for (Job_index sib : siblings) {
						for (Job_index inc : common_incompatible) {
							jobs[sib].remove_incompatible_job(inc);
						}
						processed.insert(sib);
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