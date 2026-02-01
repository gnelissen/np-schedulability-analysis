#ifndef CONDITIONAL_CONSTRAINTS_HPP
#define CONDITIONAL_CONSTRAINTS_HPP
#include <vector>
#include <set>
#include <algorithm>
#include <memory>
#include "jobs.hpp"
#include "precedence.hpp"
#include "inter_job_constraints.hpp"

namespace NP {

    template<class Time>
    class Conditional_dispatch_constraints {
		using Workload   = typename Job<Time>::Job_set;
        typedef const Job<Time>* Job_ref;
        typedef std::set<Job_index> Incompatible_jobs_set;
        typedef std::vector<Job_ref> Conditional_siblings;

		// The set of conditional siblings for each job. The set includes the job itself. If the job has no conditional siblings, the set only includes the job itself.
        std::vector<std::shared_ptr<Conditional_siblings>> siblings;
		// The set of jobs that will never be dispatched after the given job is dispatched. The job itself is thus included in the set.
        std::vector<Incompatible_jobs_set> incompatible_jobs;
        // The set of incompatible jobs with pending successors for each job.
        std::vector<Incompatible_jobs_set> incompatible_jobs_with_pending_successors;

    public:
        Conditional_dispatch_constraints(const Workload& jobs, const Precedence_constraints<Time>& precs, const Inter_job_constraints<Time>& inter_job_constraints) 
            : incompatible_jobs(jobs.size())
            , incompatible_jobs_with_pending_successors(jobs.size())
            , siblings(jobs.size(), nullptr)
        {
            DAG_graph dag = build_graph<Time>(jobs.size(), precs);
            build_siblings(jobs, dag);
			find_all_incompatible_jobs(dag, inter_job_constraints);
        }

        /**
         * @brief Check if a job has conditional siblings.
         */
        bool has_conditional_siblings(Job_index j) const {
            assert(siblings[j] != nullptr);
            return siblings[j]->size() > 1;
        }

        /**
         * @brief Get the set of conditional siblings for a job. Note that the set includes the job itself.
         */
        std::shared_ptr<Conditional_siblings> get_conditional_siblings(Job_index j) const {
            return siblings[j];
        }

		/**
		 * @brief Get the set of jobs that are incompatible with the given job.
		 */
		const Incompatible_jobs_set& get_incompatible_jobs(Job_index j) const {
			return incompatible_jobs[j];
		}

        /**
         * @brief Check if two jobs are incompatible (i.e., cannot be dispatched one after the other in the same execution scenario).
         */
        bool are_incompatible(Job_index j1, Job_index j2) const {
            const auto& incomp_set = incompatible_jobs[j1];
            return incomp_set.count(j2) > 0;
        }

        /**
         * @brief Get the set of incompatible jobs with pending successors for a job.
         */
        const Incompatible_jobs_set& get_incompatible_jobs_with_pending_successors(Job_index j) const {
            return incompatible_jobs_with_pending_successors[j];
        }

    private:
        /** 
         * @brief Build the mapping of conditional siblings.
         * Add all successor jobs of each conditional fork to the same set of conditional siblings.
         * @param jobs The set of jobs in the workload.
         * @param precs The inter-job precedence constraints.
         */
        void build_siblings(const Workload& jobs, const DAG_graph& graph) {
            for (Job_index j = 0; j < jobs.size(); ++j) {
                const auto& job = jobs[j];
                if (job.get_type() == Job<Time>::C_FORK) {
                    auto sibling_set = std::make_shared<Conditional_siblings>();
                    // find all siblings
                    const auto& successors = graph.successors[j];
                    for (const auto& succ_index : successors) {
                        sibling_set->push_back(&jobs[succ_index]);
                        siblings[succ_index] = sibling_set;
                    }
                }
                if (siblings[j] == nullptr) {
                    // job has no conditional siblings, so we create a set with only itself
                    auto singleton_set = std::make_shared<Conditional_siblings>();
                    singleton_set->push_back(&jobs[j]);
                    siblings[j] = singleton_set;
                }
            }
        }

		 /**
         * @brief Compute incompatible jobs for all jobs in the workload.
         * @param graph The inter-job precedence constraints graph.
         */
		void find_all_incompatible_jobs(const DAG_graph& graph, const Inter_job_constraints<Time>& inter_job_constraints) {
			const std::size_t n = graph.size();
			incompatible_jobs.resize(n);
			for (std::size_t i = 0; i < n; ++i) {
				// if we did not compute the incompatible jobs for job i yet
				if (incompatible_jobs[i].empty()) {
					// compute incompatible jobs for job i
					if (has_conditional_siblings(i)) {
						// compute incompatible jobs for all siblings at once
						const auto& sibs = *(siblings[i]);
						std::vector<std::set<Job_index>> sib_descendants(sibs.size());
						// union of all descendants of all siblings and siblings themsleves
						std::set<Job_index> union_set;
						for (std::size_t s = 0; s < sibs.size(); ++s) {
							sib_descendants[s] = get_descendants(sibs[s]->get_job_index(), graph);
							union_set.insert(sib_descendants[s].begin(), sib_descendants[s].end());
							union_set.insert(sibs[s]->get_job_index()); // include the sibling itself
						}
						// for each sibling, the incompatible jobs are the union minus its own descendants
						for (std::size_t s = 0; s < sibs.size(); ++s) {
							Job_index sib_index = sibs[s]->get_job_index();
							for (Job_index uj : union_set) {
								if (sib_descendants[s].count(uj) == 0) {
									incompatible_jobs[sib_index].emplace(uj);
                                    // check if uj may have pending successors (i.e., one of the successors of uj is a descendant of sib_index or if uj has mutual exclusion constraints)
                                    const auto& cstr = inter_job_constraints[uj];
                                    if (cstr.between_executions.size() > 0 || cstr.between_starts.size() > 0)
                                        incompatible_jobs_with_pending_successors[sib_index].emplace(uj);
                                    else {
                                        for (Job_index succ : graph.successors[uj]) {
                                            if (sib_descendants[s].count(succ) > 0) {
                                                incompatible_jobs_with_pending_successors[sib_index].emplace(uj);
                                                break;
                                            }
                                        }
                                    }
								}
							}
						}
					} else {
						// non-conditional siblings have only themselves as incompatible
						incompatible_jobs[i].emplace(i);
                        const auto& cstr = inter_job_constraints[i];
                        // check if i may have pending successors (it has successors or mutual exclusion constraints)
                        if (graph.successors[i].size() > 0 || cstr.between_executions.size() > 0 || cstr.between_starts.size() > 0) {
                            incompatible_jobs_with_pending_successors[i].emplace(i);
                        }
					}
				}
			}
		}
    };
        

} // namespace NP

#endif // CONDITIONAL_CONSTRAINTS_HPP