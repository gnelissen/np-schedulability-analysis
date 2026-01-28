#include "doctest.h"

#include <iostream>
#include <sstream>
#include <vector>
#include <algorithm>

#include "global/extension/taskchains/taskchains.hpp"
#include "global/extension/taskchains/taskchains_extension.hpp"
#include "global/extension/taskchains/taskchains_problem_extension.hpp"
#include "global/extension/taskchains/taskchains_sp_data_extension.hpp"
#include "global/extension/taskchains/taskchains_state_extension.hpp"
#include "jobs.hpp"
#include "interval.hpp"
#include "global/space.hpp"

#ifdef CONFIG_ANALYSIS_EXTENSIONS

using namespace NP;
using namespace NP::Global::Taskchains_analysis;
using tc_data = NP::Global::Taskchains_analysis::Taskchains_sp_data_extension<dtime_t>;
using tc_prob = NP::Global::Taskchains_analysis::Taskchains_problem_extension<dtime_t>;

static const auto inf = Time_model::constants<dtime_t>::infinity();

// ===========================
// Task_chain class tests
// ===========================

TEST_CASE("Task_chain - Basic construction and getters") {
    std::vector<unsigned long> tasks = {1, 2, 3};
    Task_chain<dtime_t> chain(tasks, true, false, 42);

    CHECK(chain.get_id() == 42);
    CHECK(chain.get_tasks().size() == 3);
    CHECK(chain.get_tasks()[0] == 1);
    CHECK(chain.get_tasks()[1] == 2);
    CHECK(chain.get_tasks()[2] == 3);
    CHECK(chain.uses_event_input() == true);
    CHECK(chain.uses_instantaneous_output() == false);
}

TEST_CASE("Task_chain - Empty task chain") {
    std::vector<unsigned long> tasks = {};
    Task_chain<dtime_t> chain(tasks, false, true, 0);

    CHECK(chain.get_id() == 0);
    CHECK(chain.get_tasks().empty());
    CHECK(chain.uses_event_input() == false);
    CHECK(chain.uses_instantaneous_output() == true);
}

TEST_CASE("Task_chain - Single task chain") {
    std::vector<unsigned long> tasks = {5};
    Task_chain<dtime_t> chain(tasks, false, false, 1);

    CHECK(chain.get_id() == 1);
    CHECK(chain.get_tasks().size() == 1);
    CHECK(chain.get_tasks()[0] == 5);
}

TEST_CASE("Task_chain - Event input and instantaneous output combinations") {
    SUBCASE("Event input, instantaneous output") {
        std::vector<unsigned long> tasks = {1, 2};
        Task_chain<dtime_t> chain(tasks, true, true, 0);
        CHECK(chain.uses_event_input() == true);
        CHECK(chain.uses_instantaneous_output() == true);
    }
    
    SUBCASE("Sensor input, lasting output") {
        std::vector<unsigned long> tasks = {1, 2};
        Task_chain<dtime_t> chain(tasks, false, false, 1);
        CHECK(chain.uses_event_input() == false);
        CHECK(chain.uses_instantaneous_output() == false);
    }
    
    SUBCASE("Event input, lasting output") {
        std::vector<unsigned long> tasks = {1, 2};
        Task_chain<dtime_t> chain(tasks, true, false, 2);
        CHECK(chain.uses_event_input() == true);
        CHECK(chain.uses_instantaneous_output() == false);
    }
    
    SUBCASE("Sensor input, instantaneous output") {
        std::vector<unsigned long> tasks = {1, 2};
        Task_chain<dtime_t> chain(tasks, false, true, 3);
        CHECK(chain.uses_event_input() == false);
        CHECK(chain.uses_instantaneous_output() == true);
    }
}

TEST_CASE("Task_chain - Very long chain") {
    std::vector<unsigned long> tasks;
    for (unsigned long i = 0; i < 1000; ++i) {
        tasks.push_back(i);
    }
    Task_chain<dtime_t> chain(tasks, true, true, 999);

    CHECK(chain.get_tasks().size() == 1000);
    CHECK(chain.get_tasks()[0] == 0);
    CHECK(chain.get_tasks()[500] == 500);
    CHECK(chain.get_tasks()[999] == 999);
}

TEST_CASE("Task_chain - Duplicate task IDs in chain") {
    std::vector<unsigned long> tasks = {1, 2, 1, 1, 3};  // Task 1 appears thrice
    Task_chain<dtime_t> chain(tasks, true, false, 0);

    CHECK(chain.get_tasks().size() == 5);
    CHECK(chain.get_tasks()[0] == 1);
    CHECK(chain.get_tasks()[2] == 1);
    CHECK(chain.get_tasks()[4] == 3);
}

// ===========================
// YAML parsing tests
// ===========================

TEST_CASE("parse_yaml_task_chain_file - Single task chain") {
    std::string yaml_content = R"(
TaskChains:
  - Tasks: [1, 2, 3]
    Input:
        Type: onExternalEvent
    Output:
        Type: instantaneous
)";
    std::istringstream in(yaml_content);
    auto chains = parse_yaml_task_chain_file<dtime_t>(in);

    REQUIRE(chains.size() == 1);
    CHECK(chains[0].get_id() == 0);
    CHECK(chains[0].get_tasks().size() == 3);
    CHECK(chains[0].get_tasks()[0] == 1);
    CHECK(chains[0].get_tasks()[1] == 2);
    CHECK(chains[0].get_tasks()[2] == 3);
    CHECK(chains[0].uses_event_input() == true);
    CHECK(chains[0].uses_instantaneous_output() == true);
}

TEST_CASE("parse_yaml_task_chain_file - Multiple task chains") {
    std::string yaml_content = R"(
TaskChains:
  - Tasks: [1, 2]
    Input:
        Type: onExternalEvent
    Output:
        Type: instantaneous
  - Tasks: [3, 4, 5]
    Input:
        Type: onStart
    Output:
        Type: blackboard
  - Tasks: [6]
    Input:
        Type: onExternalEvent
    Output:
        Type: blackboard
)";
    std::istringstream in(yaml_content);
    auto chains = parse_yaml_task_chain_file<dtime_t>(in);

    REQUIRE(chains.size() == 3);
    
    CHECK(chains[0].get_id() == 0);
    CHECK(chains[0].get_tasks().size() == 2);
    CHECK(chains[0].uses_event_input() == true);
    CHECK(chains[0].uses_instantaneous_output() == true);
    
    CHECK(chains[1].get_id() == 1);
    CHECK(chains[1].get_tasks().size() == 3);
    CHECK(chains[1].uses_event_input() == false);
    CHECK(chains[1].uses_instantaneous_output() == false);
    
    CHECK(chains[2].get_id() == 2);
    CHECK(chains[2].get_tasks().size() == 1);
    CHECK(chains[2].uses_event_input() == true);
    CHECK(chains[2].uses_instantaneous_output() == false);
}

TEST_CASE("parse_yaml_task_chain_file - multi-chain with tasks reuse") {
    std::string yaml_content = R"(
TaskChains:
  - Tasks: [1]
    Input:
        Type: onExternalEvent
    Output:
        Type: instantaneous
  - Tasks: [1, 2, 2, 1]
    Input:
        Type: onStart
    Output:
        Type: blackboard
  - Tasks: [2, 3, 4]
    Input:
        Type: onExternalEvent
    Output:
        Type: instantaneous
  - Tasks: [3, 4, 5, 4, 6]
    Input:
        Type: onStart
    Output:
        Type: blackboard
)";
    std::istringstream in(yaml_content);
    auto chains = parse_yaml_task_chain_file<dtime_t>(in);

    REQUIRE(chains.size() == 4);
    
    CHECK(chains[0].get_tasks().size() == 1);
    CHECK(chains[1].get_tasks().size() == 4);
    CHECK(chains[2].get_tasks().size() == 3);
    CHECK(chains[3].get_tasks().size() == 5);
    
    // Check IDs are sequential
    for (size_t i = 0; i < chains.size(); ++i) {
        CHECK(chains[i].get_id() == i);
    }
}

TEST_CASE("parse_yaml_task_chain_file - Empty file") {
    std::string yaml_content = R"(
TaskChains: []
)";
    std::istringstream in(yaml_content);
    auto chains = parse_yaml_task_chain_file<dtime_t>(in);

    CHECK(chains.empty());
}

TEST_CASE("parse_yaml_task_chain_file - Sensor input types") {
    std::string yaml_content = R"(
TaskChains:
  - Tasks: [10, 20, 30]
    Input:
        Type: onStart
    Output:
        Type: instantaneous
)";
    std::istringstream in(yaml_content);
    auto chains = parse_yaml_task_chain_file<dtime_t>(in);

    REQUIRE(chains.size() == 1);
    CHECK(chains[0].uses_event_input() == false);
    CHECK(chains[0].uses_instantaneous_output() == true);
}

TEST_CASE("parse_yaml_task_chain_file - Lasting output types") {
    std::string yaml_content = R"(
TaskChains:
  - Tasks: [1, 2]
    Input:
        Type: onExternalEvent
    Output:
        Type: blackboard
)";
    std::istringstream in(yaml_content);
    auto chains = parse_yaml_task_chain_file<dtime_t>(in);

    REQUIRE(chains.size() == 1);
    CHECK(chains[0].uses_event_input() == true);
    CHECK(chains[0].uses_instantaneous_output() == false);
}

TEST_CASE("parse_yaml_task_chain_file - Large task IDs") {
    std::string yaml_content = R"(
TaskChains:
  - Tasks: [1000, 2000, 3000]
    Input:
        Type: onExternalEvent
    Output:
        Type: instantaneous
)";
    std::istringstream in(yaml_content);
    auto chains = parse_yaml_task_chain_file<dtime_t>(in);

    REQUIRE(chains.size() == 1);
    CHECK(chains[0].get_tasks()[0] == 1000);
    CHECK(chains[0].get_tasks()[1] == 2000);
    CHECK(chains[0].get_tasks()[2] == 3000);
}

TEST_CASE("parse_yaml_task_chain_file - Single task chains") {
    std::string yaml_content = R"(
TaskChains:
  - Tasks: [1]
    Input:
        Type: onExternalEvent
    Output:
        Type: instantaneous
  - Tasks: [2]
    Input:
        Type: onStart
    Output:
        Type: blackboard
)";
    std::istringstream in(yaml_content);
    auto chains = parse_yaml_task_chain_file<dtime_t>(in);

    REQUIRE(chains.size() == 2);
    CHECK(chains[0].get_tasks().size() == 1);
    CHECK(chains[0].get_tasks()[0] == 1);
    CHECK(chains[1].get_tasks().size() == 1);
    CHECK(chains[1].get_tasks()[0] == 2);
}

TEST_CASE("parse_yaml_task_chain_file - Invalid YAML graceful handling") {
    std::string invalid_yaml = "not valid yaml: {{{";
    std::istringstream in(invalid_yaml);
    
    // The function catches exceptions and returns an empty vector
    auto chains = parse_yaml_task_chain_file<dtime_t>(in);
    CHECK(chains.empty());
}

// ===========================
// Taskchains_problem_extension tests
// ===========================

TEST_CASE("Taskchains_problem_extension - Basic construction") {
    std::vector<Task_chain<dtime_t>> chains;
    chains.push_back(Task_chain<dtime_t>({1, 2, 3}, true, false, 0));
    chains.push_back(Task_chain<dtime_t>({4, 5}, false, true, 1));

    Taskchains_problem_extension<dtime_t> ext(chains);

    const auto& retrieved = ext.get_task_chains();
    REQUIRE(retrieved.size() == 2);
    CHECK(retrieved[0].get_id() == 0);
    CHECK(retrieved[1].get_id() == 1);
    CHECK(retrieved[0].get_tasks().size() == 3);
    CHECK(retrieved[1].get_tasks().size() == 2);
}

TEST_CASE("Taskchains_problem_extension - Empty chains") {
    std::vector<Task_chain<dtime_t>> chains;

    Taskchains_problem_extension<dtime_t> ext(chains);

    const auto& retrieved = ext.get_task_chains();
    CHECK(retrieved.empty());
}

// ===========================
// Taskchains_sp_data_extension tests
// ===========================

TEST_CASE("Taskchains_sp_data_extension - Basic construction") {
    // Create jobs
    typename Scheduling_problem<dtime_t>::Workload jobs;
    jobs.push_back(Job<dtime_t>{1, Interval<dtime_t>(0, 0), Interval<dtime_t>(1, 10), 100, 100, 0, 1});
    jobs.push_back(Job<dtime_t>{2, Interval<dtime_t>(0, 0), Interval<dtime_t>(1, 10), 100, 100, 1, 2});
    jobs.push_back(Job<dtime_t>{3, Interval<dtime_t>(0, 0), Interval<dtime_t>(1, 10), 100, 100, 2, 3});

    // Create task chains
    std::vector<Task_chain<dtime_t>> chains;
    chains.push_back(Task_chain<dtime_t>({1, 2, 3}, true, false, 0));

    Taskchains_sp_data_extension<dtime_t> ext(jobs, chains);

    const auto& retrieved_chains = ext.get_task_chains();
    REQUIRE(retrieved_chains.size() == 1);
    CHECK(retrieved_chains[0].get_id() == 0);
}

TEST_CASE("Taskchains_sp_data_extension - Task to chain mapping") {
    // Create jobs with task IDs 1, 2, 3
    typename Scheduling_problem<dtime_t>::Workload jobs;
    jobs.push_back(Job<dtime_t>{1, Interval<dtime_t>(0, 0), Interval<dtime_t>(1, 10), 100, 100, 0, 1});
    jobs.push_back(Job<dtime_t>{2, Interval<dtime_t>(0, 0), Interval<dtime_t>(1, 10), 100, 100, 1, 2});
    jobs.push_back(Job<dtime_t>{3, Interval<dtime_t>(0, 0), Interval<dtime_t>(1, 10), 100, 100, 2, 3});

    // Create task chains: chain 0 uses tasks [1, 2, 3]
    std::vector<Task_chain<dtime_t>> chains;
    chains.push_back(Task_chain<dtime_t>({1, 2, 3}, true, false, 0));

    Taskchains_sp_data_extension<dtime_t> ext(jobs, chains);

    // Task 1 should be in chain 0 at position 0
    const auto& task1_chains = ext.get_task_chains_of(1);
    REQUIRE(task1_chains.size() == 1);
    CHECK(task1_chains[0].chain_id == 0);
    CHECK(task1_chains[0].position_in_chain == 0);
    CHECK(task1_chains[0].is_sink == false);

    // Task 2 should be in chain 0 at position 1
    const auto& task2_chains = ext.get_task_chains_of(2);
    REQUIRE(task2_chains.size() == 1);
    CHECK(task2_chains[0].chain_id == 0);
    CHECK(task2_chains[0].position_in_chain == 1);
    CHECK(task2_chains[0].is_sink == false);

    // Task 3 should be in chain 0 at position 2 and be a sink
    const auto& task3_chains = ext.get_task_chains_of(3);
    REQUIRE(task3_chains.size() == 1);
    CHECK(task3_chains[0].chain_id == 0);
    CHECK(task3_chains[0].position_in_chain == 2);
    CHECK(task3_chains[0].is_sink == true);
}

TEST_CASE("Taskchains_sp_data_extension - Multiple chains with overlapping tasks") {
    typename Scheduling_problem<dtime_t>::Workload jobs;
    jobs.push_back(Job<dtime_t>{1, Interval<dtime_t>(0, 0), Interval<dtime_t>(1, 10), 100, 100, 0, 1});
    jobs.push_back(Job<dtime_t>{2, Interval<dtime_t>(0, 0), Interval<dtime_t>(1, 10), 100, 100, 1, 2});
    jobs.push_back(Job<dtime_t>{3, Interval<dtime_t>(0, 0), Interval<dtime_t>(1, 10), 100, 100, 2, 3});

    // Chain 0: tasks [1, 2]
    // Chain 1: tasks [2, 3]
    // Task 2 is in both chains
    std::vector<Task_chain<dtime_t>> chains;
    chains.push_back(Task_chain<dtime_t>({1, 2}, true, false, 0));
    chains.push_back(Task_chain<dtime_t>({2, 3}, false, true, 1));

    Taskchains_sp_data_extension<dtime_t> ext(jobs, chains);

    // Task 1 should be in chain 0 only
    const auto& task1_chains = ext.get_task_chains_of(1);
    REQUIRE(task1_chains.size() == 1);
    CHECK(task1_chains[0].chain_id == 0);

    // Task 2 should be in both chains
    const auto& task2_chains = ext.get_task_chains_of(2);
    REQUIRE(task2_chains.size() == 2);
    CHECK(task2_chains[0].chain_id == 0);
    CHECK(task2_chains[0].position_in_chain == 1);
    CHECK(task2_chains[0].is_sink == true);  // sink in chain 0
    CHECK(task2_chains[1].chain_id == 1);
    CHECK(task2_chains[1].position_in_chain == 0);
    CHECK(task2_chains[1].is_sink == false);  // source in chain 1

    // Task 3 should be in chain 1 only
    const auto& task3_chains = ext.get_task_chains_of(3);
    REQUIRE(task3_chains.size() == 1);
    CHECK(task3_chains[0].chain_id == 1);
    CHECK(task3_chains[0].is_sink == true);
}

TEST_CASE("Taskchains_sp_data_extension - Data age and reaction time submission") {
    typename Scheduling_problem<dtime_t>::Workload jobs;
    jobs.push_back(Job<dtime_t>{1, Interval<dtime_t>(0, 0), Interval<dtime_t>(1, 10), 100, 100, 0, 1});
    jobs.push_back(Job<dtime_t>{2, Interval<dtime_t>(0, 0), Interval<dtime_t>(1, 10), 100, 100, 1, 2});

    std::vector<Task_chain<dtime_t>> chains;
    chains.push_back(Task_chain<dtime_t>({1, 2}, true, false, 0));

    Taskchains_sp_data_extension<dtime_t> ext(jobs, chains);

    // Initially, data age and reaction time should be -1 (indicating no data)
    CHECK(ext.get_max_data_age(0) == -1);
    CHECK(ext.get_max_reaction_time(0) == -1);

    // Submit values
    ext.submit_data_age(0,  1, 100);
    ext.submit_reaction_time(0, 1, 50);

    CHECK(ext.get_max_data_age(0) == 100);
    CHECK(ext.get_max_reaction_time(0) == 50);

    // Submit larger values - should update
    ext.submit_data_age(0,  1, 150);
    ext.submit_reaction_time(0, 1, 75);

    CHECK(ext.get_max_data_age(0) == 150);
    CHECK(ext.get_max_reaction_time(0) == 75);

    // Submit smaller values - should not update (we keep max)
    ext.submit_data_age(0, 1, 50);
    ext.submit_reaction_time(0, 1, 25);

    CHECK(ext.get_max_data_age(0) == 150);
    CHECK(ext.get_max_reaction_time(0) == 75);
}

TEST_CASE("Taskchains_sp_data_extension - Multiple chains data tracking") {
    typename Scheduling_problem<dtime_t>::Workload jobs;
    for (unsigned long i = 1; i <= 6; ++i) {
        jobs.push_back(Job<dtime_t>{i, 
                                     Interval<dtime_t>(0, 0), 
                                     Interval<dtime_t>(1, 10), 
                                     100, 100, i - 1, i});
    }

    std::vector<Task_chain<dtime_t>> chains;
    chains.push_back(Task_chain<dtime_t>({1, 2, 3}, true, false, 0));
    chains.push_back(Task_chain<dtime_t>({4, 5, 6}, false, true, 1));

    Taskchains_sp_data_extension<dtime_t> ext(jobs, chains);

    ext.submit_data_age(0,  2, 100);
    ext.submit_data_age(1,  2, 200);
    ext.submit_reaction_time(0, 2, 50);
    ext.submit_reaction_time(1, 2, 75);

    CHECK(ext.get_max_data_age(0) == 100);
    CHECK(ext.get_max_data_age(1) == 200);
    CHECK(ext.get_max_reaction_time(0) == 50);
    CHECK(ext.get_max_reaction_time(1) == 75);
}

TEST_CASE("Taskchains_sp_data_extension - Out of bounds access") {
    typename Scheduling_problem<dtime_t>::Workload jobs;
    jobs.push_back(Job<dtime_t>{1, Interval<dtime_t>(0, 0), Interval<dtime_t>(1, 10), 100, 100, 0, 1});

    std::vector<Task_chain<dtime_t>> chains;
    chains.push_back(Task_chain<dtime_t>({1}, true, false, 0));

    Taskchains_sp_data_extension<dtime_t> ext(jobs, chains);

    // Accessing non-existent chain should return -1
    CHECK(ext.get_max_data_age(10) == -1);
    CHECK(ext.get_max_reaction_time(10) == -1);
}

TEST_CASE("Taskchains_sp_data_extension - Results structure") {
    typename Scheduling_problem<dtime_t>::Workload jobs;
    jobs.push_back(Job<dtime_t>{1, Interval<dtime_t>(0, 0), Interval<dtime_t>(1, 10), 100, 100, 0, 1});
    jobs.push_back(Job<dtime_t>{2, Interval<dtime_t>(0, 0), Interval<dtime_t>(1, 10), 100, 100, 1, 2});

    std::vector<Task_chain<dtime_t>> chains;
    chains.push_back(Task_chain<dtime_t>({1, 2}, true, false, 0));

    Taskchains_sp_data_extension<dtime_t> ext(jobs, chains);

    const auto& results = ext.get_results();
    CHECK(results.size() == 1);
    CHECK(results[0].size() == 2);
    CHECK(results[0][0].max_data_age == -1);
    CHECK(results[0][0].max_reaction_time == -1);
    CHECK(results[0][1].max_data_age == -1);
    CHECK(results[0][1].max_reaction_time == -1);
}

TEST_CASE("Taskchains_sp_data_extension - Large task IDs") {
    typename Scheduling_problem<dtime_t>::Workload jobs;
    jobs.push_back(Job<dtime_t>{1, Interval<dtime_t>(0, 0), Interval<dtime_t>(1, 10), 100, 100, 0, 1000});
    jobs.push_back(Job<dtime_t>{2, Interval<dtime_t>(0, 0), Interval<dtime_t>(1, 10), 100, 100, 1, 2000});

    std::vector<Task_chain<dtime_t>> chains;
    chains.push_back(Task_chain<dtime_t>({1000, 2000}, true, false, 0));

    Taskchains_sp_data_extension<dtime_t> ext(jobs, chains);

    const auto& task1000_chains = ext.get_task_chains_of(1000);
    REQUIRE(task1000_chains.size() == 1);
    CHECK(task1000_chains[0].chain_id == 0);
    CHECK(task1000_chains[0].position_in_chain == 0);
}

TEST_CASE("Taskchains_sp_data_extension - Single task in multiple chains") {
    typename Scheduling_problem<dtime_t>::Workload jobs;
    jobs.push_back(Job<dtime_t>{1, Interval<dtime_t>(0, 0), Interval<dtime_t>(1, 10), 100, 100, 0, 1});
    jobs.push_back(Job<dtime_t>{2, Interval<dtime_t>(0, 0), Interval<dtime_t>(1, 10), 100, 100, 1, 2});
    jobs.push_back(Job<dtime_t>{3, Interval<dtime_t>(0, 0), Interval<dtime_t>(1, 10), 100, 100, 2, 3});

    // Task 2 appears in all three chains at different positions
    std::vector<Task_chain<dtime_t>> chains;
    chains.push_back(Task_chain<dtime_t>({1, 2}, true, false, 0));
    chains.push_back(Task_chain<dtime_t>({2, 3}, false, true, 1));
    chains.push_back(Task_chain<dtime_t>({1, 2, 3}, true, true, 2));

    Taskchains_sp_data_extension<dtime_t> ext(jobs, chains);

    const auto& task2_chains = ext.get_task_chains_of(2);
    REQUIRE(task2_chains.size() == 3);
    
    // Check all three occurrences
    CHECK(task2_chains[0].chain_id == 0);
    CHECK(task2_chains[0].position_in_chain == 1);
    CHECK(task2_chains[0].is_sink == true);
    
    CHECK(task2_chains[1].chain_id == 1);
    CHECK(task2_chains[1].position_in_chain == 0);
    CHECK(task2_chains[1].is_sink == false);
    
    CHECK(task2_chains[2].chain_id == 2);
    CHECK(task2_chains[2].position_in_chain == 1);
    CHECK(task2_chains[2].is_sink == false);
}

TEST_CASE("Taskchains_sp_data_extension - Stress test with many chains") {
    typename Scheduling_problem<dtime_t>::Workload jobs;
    for (unsigned long i = 1; i <= 100; ++i) {
        jobs.push_back(Job<dtime_t>{i, 
                                     Interval<dtime_t>(0, 0), 
                                     Interval<dtime_t>(1, 10), 
                                     100, 100, i - 1, i});
    }

    unsigned long n_chains = 200;
    std::vector<Task_chain<dtime_t>> chains;
    for (int i = 0; i < n_chains; ++i) {
        unsigned long task1 = i * 2 + 1;
        unsigned long task2 = i * 2 + 2;
        chains.push_back(Task_chain<dtime_t>({task1, task2}, i % 2 == 0, i % 2 == 1, i));
    }

    Taskchains_sp_data_extension<dtime_t> ext(jobs, chains);

    CHECK(ext.get_task_chains().size() == n_chains);
    
    // Submit results for all chains
    for (int i = 0; i < n_chains; ++i) {
        ext.submit_data_age(i, 1, i * 10);
        ext.submit_reaction_time(i, 1, i * 5);
    }
    
    // Verify
    for (int i = 0; i < n_chains; ++i) {
        CHECK(ext.get_max_data_age(i) == i * 10);
        CHECK(ext.get_max_reaction_time(i) == i * 5);
    }
}

TEST_CASE("Taskchains_sp_data_extension - Empty workload") {
    typename Scheduling_problem<dtime_t>::Workload jobs;
    // No jobs

    std::vector<Task_chain<dtime_t>> chains;
    chains.push_back(Task_chain<dtime_t>({1, 2}, true, false, 0));

    // This should not crash even with empty jobs
    Taskchains_sp_data_extension<dtime_t> ext(jobs, chains);
    CHECK(ext.get_task_chains().size() == 1);
}

// ===========================
// Check analysis results
// ===========================
const std::string ts1_jobs =
"Task ID, Job ID, Arrival min, Arrival max, Cost min, Cost max, Deadline, Priority\n"
"      0,      0,         0,           0,       10,       10,     200,        2\n"
"      1,      0,         0,           0,       20,       20,     200,        4\n"
"      2,      0,         0,           0,       30,       30,     200,        5\n";

const std::string ts1_edges =
"From TID, From JID,   To TID,   To JID\n"
"       0,        0,        1,        0\n"
"       1,        0,        2,        0\n";

TEST_CASE("One task per core - single job - no offsets, no prec") {
    auto in = std::istringstream(ts1_jobs);
	auto jobs = NP::parse_csv_job_file<dtime_t>(in);
	unsigned int num_cores = 3;
	NP::Scheduling_problem<dtime_t> prob_3{jobs, num_cores};

    std::vector<Task_chain<dtime_t>> chains;
    // event input, instantaneous output
    chains.push_back(Task_chain<dtime_t>({0}, true, true, 0));
    chains.push_back(Task_chain<dtime_t>({1}, true, true, 1));
    chains.push_back(Task_chain<dtime_t>({2}, true, true, 2));
    chains.push_back(Task_chain<dtime_t>({0, 1}, true, true, 3));
    chains.push_back(Task_chain<dtime_t>({0, 1, 2}, true, true, 4));
    // event input, lasting output
    chains.push_back(Task_chain<dtime_t>({0}, true, false, 0));
    chains.push_back(Task_chain<dtime_t>({1}, true, false, 1));
    chains.push_back(Task_chain<dtime_t>({2}, true, false, 2));
    chains.push_back(Task_chain<dtime_t>({0, 1}, true, false, 3));
    chains.push_back(Task_chain<dtime_t>({0, 1, 2}, true, false, 4));
    // sensor input, instantaneous output
    chains.push_back(Task_chain<dtime_t>({0}, false, true, 0));
    chains.push_back(Task_chain<dtime_t>({1}, false, true, 1));
    chains.push_back(Task_chain<dtime_t>({2}, false, true, 2));
    chains.push_back(Task_chain<dtime_t>({0, 1}, false, true, 3));
    chains.push_back(Task_chain<dtime_t>({0, 1, 2}, false, true, 4));
    // sensor input, lasting output
    chains.push_back(Task_chain<dtime_t>({0}, false, false, 0));
    chains.push_back(Task_chain<dtime_t>({1}, false, false, 1));
    chains.push_back(Task_chain<dtime_t>({2}, false, false, 2));
    chains.push_back(Task_chain<dtime_t>({0, 1}, false, false, 3));
    chains.push_back(Task_chain<dtime_t>({0, 1, 2}, false, false, 4));
    prob_3.problem_extensions.register_extension<tc_prob>(chains);
    Analysis_options opts;

    auto space = NP::Global::State_space<dtime_t>::explore(prob_3, opts);
    const auto& results = space->get_results<tc_data>();
    CHECK(results.size() == 20);
    /** for event inputs **/
    // since a single job of each task is executed, task chains should have no data age and reaction time
    for (size_t i = 0; i < 10; ++i) {
        auto p = chains[i].get_tasks().size()-1;
        CHECK(results[i][p].max_data_age == -1);
        CHECK(results[i][p].max_reaction_time == -1);
    }
    /** for sensor inputs, instantaneous output **/
    // task chains of a single task have data age and reaction time equal to their execution time
    CHECK(results[10][0].max_data_age == 10);
    CHECK(results[10][0].max_reaction_time == 10);
    CHECK(results[11][0].max_data_age == 20);
    CHECK(results[11][0].max_reaction_time == 20);
    CHECK(results[12][0].max_data_age == 30);
    CHECK(results[12][0].max_reaction_time == 30);
    for (size_t i = 13; i < 15; ++i) {
        // All jobs execute in parallel => they do not exchange data => no data age and reaction time
        auto p = chains[i].get_tasks().size()-1;
        CHECK(results[i][p].max_data_age == -1);
        CHECK(results[i][p].max_reaction_time == -1);
    }
    /** for sensor inputs, lasting output **/
    // since a single job of each task is executed, task chains should have no data age 
    // but reaction time should be unchanged to instantaneous output case
    for (size_t i = 15; i < 20; ++i) {
        auto p = chains[i].get_tasks().size()-1;
        CHECK(results[i][p].max_data_age == -1);
        CHECK(results[i][p].max_reaction_time == results[i - 5][p].max_reaction_time);
    }

    // same with single core
    auto prob_1 = NP::Scheduling_problem<dtime_t>(jobs, 1);
    prob_1.problem_extensions.template register_extension<NP::Global::Taskchains_analysis::Taskchains_problem_extension<dtime_t>>(chains);

    auto space_1 = NP::Global::State_space<dtime_t>::explore(prob_1, opts);
    const auto& results_1 = space_1->get_results<tc_data>();
    /** for event inputs **/
    // since a single job of each task is executed, task chains should have no data age and reaction time
    for (size_t i = 0; i < 10; ++i) {
        auto p = chains[i].get_tasks().size()-1;
        CHECK(results_1[i][p].max_data_age == -1);
        CHECK(results_1[i][p].max_reaction_time == -1);
    }
    /** for sensor inputs, instantaneous output **/
    // task chains of a single task have data age and reaction time equal to their execution time
    CHECK(results_1[10][0].max_data_age == 10);
    CHECK(results_1[10][0].max_reaction_time == 10);
    CHECK(results_1[11][0].max_data_age == 20);
    CHECK(results_1[11][0].max_reaction_time == 20);
    CHECK(results_1[12][0].max_data_age == 30);
    CHECK(results_1[12][0].max_reaction_time == 30);
    CHECK(results_1[13][1].max_data_age == 30); // chain 1->2: data age = exec time of task 1 + exec time of task 2
    CHECK(results_1[13][1].max_reaction_time == 30);
    CHECK(results_1[14][2].max_data_age == 60); // chain 1->2->3: data age = sum of exec times
    CHECK(results_1[14][2].max_reaction_time == 60);
    /** for sensor inputs, lasting output **/
    // since a single job of each task is executed, task chains should have no data age
    // but reaction time should be unchanged to instantaneous output case
    for (size_t i = 15; i < 20; ++i) {
        auto p = chains[i].get_tasks().size()-1;
        CHECK(results_1[i][p].max_data_age == -1);
        CHECK(results_1[i][p].max_reaction_time == results_1[i - 5][p].max_reaction_time);
    }
}

TEST_CASE("One task per core - single job with prec constraints") {
    auto dag_in = std::istringstream(ts1_edges);
	auto in = std::istringstream(ts1_jobs);
	auto jobs = NP::parse_csv_job_file<dtime_t>(in);
	auto prec = NP::parse_precedence_file<dtime_t>(dag_in, jobs);
	unsigned int num_cores = 3;
	NP::Scheduling_problem<dtime_t> prob_3{jobs, prec, num_cores};

    std::vector<Task_chain<dtime_t>> chains;
    // event input, instantaneous output
    chains.push_back(Task_chain<dtime_t>({0}, true, true, 0));
    chains.push_back(Task_chain<dtime_t>({1}, true, true, 1));
    chains.push_back(Task_chain<dtime_t>({2}, true, true, 2));
    chains.push_back(Task_chain<dtime_t>({0, 1}, true, true, 3));
    chains.push_back(Task_chain<dtime_t>({0, 1, 2}, true, true, 4));
    // event input, lasting output
    chains.push_back(Task_chain<dtime_t>({0}, true, false, 0));
    chains.push_back(Task_chain<dtime_t>({1}, true, false, 1));
    chains.push_back(Task_chain<dtime_t>({2}, true, false, 2));
    chains.push_back(Task_chain<dtime_t>({0, 1}, true, false, 3));
    chains.push_back(Task_chain<dtime_t>({0, 1, 2}, true, false, 4));
    // sensor input, instantaneous output
    chains.push_back(Task_chain<dtime_t>({0}, false, true, 0));
    chains.push_back(Task_chain<dtime_t>({1}, false, true, 1));
    chains.push_back(Task_chain<dtime_t>({2}, false, true, 2));
    chains.push_back(Task_chain<dtime_t>({0, 1}, false, true, 3));
    chains.push_back(Task_chain<dtime_t>({0, 1, 2}, false, true, 4));
    // sensor input, lasting output
    chains.push_back(Task_chain<dtime_t>({0}, false, false, 0));
    chains.push_back(Task_chain<dtime_t>({1}, false, false, 1));
    chains.push_back(Task_chain<dtime_t>({2}, false, false, 2));
    chains.push_back(Task_chain<dtime_t>({0, 1}, false, false, 3));
    chains.push_back(Task_chain<dtime_t>({0, 1, 2}, false, false, 4));
    prob_3.problem_extensions.register_extension<tc_prob>(chains);
    Analysis_options opts;

    auto space = NP::Global::State_space<dtime_t>::explore(prob_3, opts);
    const auto& results = space->get_results<tc_data>();
    CHECK(results.size() == 20);

    /** for event inputs **/
    // since a single job of each task is executed, task chains should have no data age and reaction time
    for (size_t i = 0; i < 10; ++i) {
        auto p = chains[i].get_tasks().size()-1;
        CHECK(results[i][p].max_data_age == -1);
        CHECK(results[i][p].max_reaction_time == -1);
    }
    /** for sensor inputs, instantaneous output **/
    // task chains of a single task have data age and reaction time equal to their execution time
    CHECK(results[10][0].max_data_age == 10);
    CHECK(results[10][0].max_reaction_time == 10);
    CHECK(results[11][0].max_data_age == 20);
    CHECK(results[11][0].max_reaction_time == 20);
    CHECK(results[12][0].max_data_age == 30);
    CHECK(results[12][0].max_reaction_time == 30);
    CHECK(results[13][1].max_data_age == 30); // chain 1->2: data age = exec time of task 1 + exec time of task 2
    CHECK(results[13][1].max_reaction_time == 30);
    CHECK(results[14][2].max_data_age == 60); // chain 1->2->3: data age = sum of exec times
    CHECK(results[14][2].max_reaction_time == 60);
    /** for sensor inputs, lasting output **/
    // since a single job of each task is executed, task chains should have no data age
    // but reaction time should be unchanged to instantaneous output case
    for (size_t i = 15; i < 20; ++i) {
        auto p = chains[i].get_tasks().size()-1;
        CHECK(results[i][p].max_data_age == -1);
        CHECK(results[i][p].max_reaction_time == results[i - 5][p].max_reaction_time);
    }

    // on single core, nothing should change
    auto prob_1 = NP::Scheduling_problem<dtime_t>(jobs, prec, 1);
    prob_1.problem_extensions.register_extension<tc_prob>(chains);

    auto space_1 = NP::Global::State_space<dtime_t>::explore(prob_1, opts);
    const auto& results_1 = space_1->get_results<tc_data>();
    for (size_t i = 0; i < results.size(); ++i) {
        auto p = chains[i].get_tasks().size()-1;
        CHECK(results_1[i][p].max_data_age == results[i][p].max_data_age);
        CHECK(results_1[i][p].max_reaction_time == results[i][p].max_reaction_time);
    }
}

#endif // CONFIG_ANALYSIS_EXTENSIONS