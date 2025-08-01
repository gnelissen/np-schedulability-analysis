#ifndef PRUNING_COND_HPP
#define PRUNING_COND_HPP

#include <map>
#include <vector>

#include "jobs.hpp"
#include "index_set.hpp"

namespace NP {

    struct Pruning_condition
    {
    public:
        Pruning_condition() {}

        Pruning_condition(int num_jobs)
            : prune_jobs(num_jobs)
        {
        }

        Pruning_condition(const Index_set& prune_jobs,
            const std::multimap<Job_index, std::vector<Job_index>>& stop_job_sets)
            : prune_jobs(prune_jobs),
            stop_job_sets(stop_job_sets)
        {
        }

        Index_set prune_jobs;
        std::multimap<Job_index, std::vector<Job_index>> stop_job_sets;
    };
}
#endif // PRUNING_COND_HPP