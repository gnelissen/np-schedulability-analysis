#include "doctest.h"

#include <iostream>
#include <sstream>

#include "io.hpp"
#include "global/space.hpp"
#include "affinity.hpp"
#include "jobs.hpp"


/*TEST_CASE("[partitioned] basic state evolution") {
	NP::Global::Schedule_state<dtime_t> init({ 2, 1, 3 });

	CHECK(init.cluster(0).core_availability().min() == 0);
	CHECK(init.cluster(0).core_availability().max() == 0);

	NP::Job<dtime_t> J0(0, { 0,0 }, Interval<dtime_t>(0, 0), 10, 10, 0, 0, 0);
	NP::Global::Schedule_state<dtime_t> v1(init, { &J0, NULL,NULL }, { {0, 0}, {0,0}, {5,10} }, { {5, 15}, {15, 20}, {30,40} }, { 1,1,1 }, {}, {}, { {},{},{} }, { {},{},{} }, { 0,0,0 });

	CHECK(v1.cluster(0).core_availability().min() == 0);
	CHECK(v1.cluster(0).core_availability().max() == 0);
	CHECK(v1.cluster(1).core_availability().min() == 0);
	CHECK(v1.cluster(1).core_availability().max() == 0);

	NP::Job<dtime_t> J1(1, { 0,0 }, Interval<dtime_t>(0, 0), 10, 10, 1, 0, 0);
	NP::Global::Schedule_state<dtime_t> vp(v1, { &J1, NULL,NULL }, { {0, 0}, {0,0}, {0,0} }, { {12, 30}, {15, 20}, {0,0} }, { 1,1,1 }, {}, {}, { {},{},{} }, { {},{},{} }, { 0,0,0 });

	CHECK(vp.cluster(0).core_availability().min() == 5);
	CHECK(vp.cluster(0).core_availability().max() == 15);

	CHECK(!vp.can_merge_with(init));
	CHECK(!vp.can_merge_with(v1));

	NP::Job<dtime_t> J2(2, { 0,0 }, Interval<dtime_t>(0, 0), 10, 10, 2, 0, 0);
	NP::Global::Schedule_state<dtime_t> v2(init, { &J2, NULL,NULL }, { {0, 0}, {0,0}, {0,0} }, { {10, 25}, {15, 20}, {0,0} }, { 1,1,1 }, {}, {}, { {},{},{} }, { {},{},{} }, { 0,0,0 });

	CHECK(v2.cluster(0).core_availability().min() == 0);
	CHECK(v2.cluster(0).core_availability().max() == 0);

	CHECK(v2.can_merge_with(v1));
	CHECK(v2.try_to_merge(v1));

	CHECK(v2.cluster(0).core_availability(2).min() == 5);
	CHECK(v2.cluster(0).core_availability(2).max() == 25);

	NP::Job<dtime_t> J3(3, { 0,0 }, Interval<dtime_t>(0, 0), 10, 10, 0, 0, 0);
	NP::Job<dtime_t> J4(4, { 0,0 }, Interval<dtime_t>(0, 0), 10, 10, 0, 0, 1);
	NP::Global::Schedule_state<dtime_t> vs(v1, { &J3, &J4, NULL }, { {0, 0}, {0,0}, {0,0} }, { {12, 30}, {15, 20}, {0,0} }, { 1,1,1 }, {}, {}, { {},{},{} }, { {},{},{} }, { 0,0,0 });

	CHECK(vs.cluster(0).core_availability().min() == 5);
	CHECK(vs.cluster(0).core_availability().max() == 15);
	CHECK(vs.cluster(1).core_availability().min() == 15);
	CHECK(vs.cluster(1).core_availability().max() == 20);

	CHECK(!vs.can_merge_with(init));
	CHECK(!vs.can_merge_with(v1));

	NP::Job<dtime_t> J5(5, { 0,0 }, Interval<dtime_t>(0, 0), 10, 10, 0, 0, 0);
	NP::Job<dtime_t> J6(6, { 0,0 }, Interval<dtime_t>(0, 0), 10, 10, 0, 0, 1);
	NP::Global::Schedule_state<dtime_t> v3(init, { &J5, &J6, NULL }, { {0, 0}, {0,0}, {0,0} }, { {10, 25}, {5, 20}, {0,0} }, { 1,1,1 }, {}, {}, { {},{},{} }, { {},{},{} }, { 0,0,0 });

	CHECK(v3.cluster(0).core_availability().min() == 0);
	CHECK(v3.cluster(0).core_availability().max() == 0);
	CHECK(v3.cluster(1).core_availability().min() == 5);
	CHECK(v3.cluster(1).core_availability().max() == 20);

	CHECK(!v3.can_merge_with(v1));
	CHECK(!v3.try_to_merge(v1));
	CHECK(!v3.can_merge_with(vs));
	CHECK(!v3.try_to_merge(vs));

	NP::Job<dtime_t> J7(7, { 0,0 }, Interval<dtime_t>(0, 0), 10, 10, 0, 0, 0);
	NP::Global::Schedule_state<dtime_t> vq(v2, { &J7, NULL,NULL }, { {0, 0}, {0,0}, {0,0} }, { {8, 20}, {15, 20}, {0,0} }, { 1,1,1 }, {}, {}, { {},{},{} }, { {},{},{} }, { 0,0,0 });

	CHECK(vq.cluster(0).core_availability().min() == 5);
	CHECK(vq.cluster(0).core_availability().max() == 20);

	CHECK(vq.can_merge_with(vp));
	CHECK(vp.can_merge_with(vq));
	CHECK(vp.try_to_merge(vq));

	CHECK(vq.cluster(0).core_availability().min() == 5);
	CHECK(vq.cluster(0).core_availability().max() == 20);

	CHECK(vp.cluster(0).core_availability().min() == 5);
	CHECK(vp.cluster(0).core_availability().max() == 20);
}*/

const std::string part1_file =
"Task ID, Job ID, Arrival min, Arrival max, Cost min, Cost max, Deadline, Priority, Affinity\n"
"1, 1, 0, 0, 2, 4, 7, 1, 0\n"
"2, 1, 0, 0, 10, 15, 20, 2, 1\n"
"3, 1, 5, 5, 1, 7, 15, 3, 0\n"
"4, 1, 8, 8, 2, 3, 20, 4, 1\n"
"5, 1, 8, 8, 1, 1, 19, 5, 1\n";

const std::string platform_file =
"Num_processors\n"
"1\n"
"1\n";


TEST_CASE("[partitioned] schedulable partitioning") {
	auto in = std::istringstream(part1_file);
	auto jobs = NP::parse_csv_job_file<dtime_t>(in);
	auto in_platform = std::istringstream(platform_file);
	auto platform = NP::parse_platform_spec_csv<dtime_t>(in_platform);

	auto nspace = NP::Global::State_space<dtime_t>::explore_naively(jobs,
		platform);

	CHECK(nspace->is_schedulable());

	auto space = NP::Global::State_space<dtime_t>::explore(jobs,
		platform);

	CHECK(space->is_schedulable());
}

const std::string part2_file =
"Task ID, Job ID, Arrival min, Arrival max, Cost min, Cost max, Deadline, Priority, Affinity\n"
"1, 1, 0, 0, 2, 4, 7, 1, 1\n"
"2, 1, 0, 0, 10, 15, 20, 2, 0\n"
"3, 1, 5, 5, 1, 7, 15, 3, 0\n"
"4, 1, 8, 8, 2, 3, 20, 4, 1\n"
"5, 1, 8, 8, 1, 1, 19, 5, 1\n";

TEST_CASE("[partitioned] unschedulable partitioning") {
	auto in = std::istringstream(part2_file);
	auto jobs = NP::parse_csv_job_file<dtime_t>(in);
	auto in_platform = std::istringstream(platform_file);
	auto platform = NP::parse_platform_spec_csv<dtime_t>(in_platform);

	auto nspace = NP::Global::State_space<dtime_t>::explore_naively(jobs,
		platform);

	CHECK_FALSE(nspace->is_schedulable());

	auto space = NP::Global::State_space<dtime_t>::explore(jobs,
		platform);

	CHECK_FALSE(space->is_schedulable());
}

const std::string part_identical_file =
"Task ID, Job ID, Arrival min, Arrival max, Cost min, Cost max, Deadline, Priority, Affinity\n"
"1, 1, 0, 0, 2, 4, 7, 1, 0\n"
"2, 1, 0, 0, 10, 15, 20, 2, 0\n"
"3, 1, 5, 5, 1, 7, 15, 3, 0\n"
"4, 1, 8, 8, 2, 3, 20, 4, 0\n"
"5, 1, 8, 8, 1, 1, 19, 5, 0\n"
"11, 1, 0, 0, 2, 4, 7, 1, 1\n"
"12, 1, 0, 0, 10, 15, 20, 2, 1\n"
"13, 1, 5, 5, 1, 7, 15, 3, 1\n"
"14, 1, 8, 8, 2, 3, 20, 4, 1\n"
"15, 1, 8, 8, 1, 1, 19, 5, 1\n";

TEST_CASE("[partitioned] unschedulable identical partitioning") {
	auto in = std::istringstream(part_identical_file);
	auto jobs = NP::parse_csv_job_file<dtime_t>(in);
	auto in_platform = std::istringstream(platform_file);
	auto platform = NP::parse_platform_spec_csv<dtime_t>(in_platform);

	auto space = NP::Global::State_space<dtime_t>::explore(jobs,
		platform);

	CHECK_FALSE(space->is_schedulable());
}

const std::string platform_file_2x2_cores =
"Num_processors\n"
"2\n"
"2\n";

TEST_CASE("[partitioned] schedulable identical partitioning") {
	auto in = std::istringstream(part_identical_file);
	auto jobs = NP::parse_csv_job_file<dtime_t>(in);
	auto in_platform = std::istringstream(platform_file_2x2_cores);
	auto platform = NP::parse_platform_spec_csv<dtime_t>(in_platform);

	auto space = NP::Global::State_space<dtime_t>::explore(jobs,
		platform);

	CHECK(space->is_schedulable());

	for (int i = 0; i < 5; i++) {
		CHECK(space->get_finish_times(jobs[i]) == space->get_finish_times(jobs[i + 5]));
	}
	CHECK(space->number_of_nodes() == 11);
	CHECK(space->number_of_states() == 11);
}

const std::string part_dependent_jobs_file =
"Task ID, Job ID, Arrival min, Arrival max, Cost min, Cost max, Deadline, Priority, Affinity\n"
"1, 1, 0, 0, 2, 4, 25, 1, 0\n"
"1, 2, 0, 0, 1, 10, 25, 3, 0\n"
"1, 3, 0, 0, 1, 7, 25, 2, 0\n"
"2, 1, 0, 0, 3, 3, 20, 2, 1\n"
"2, 2, 6, 6, 1, 1, 20, 1, 1\n";

const std::string part_prec_file =
"Predecessor TID,	Predecessor JID,	Successor TID, Successor JID\n"
"1, 1,    1, 2\n"
"1, 1,    2, 1\n"
"2, 1,    1, 3\n"
"2, 1,    2, 2\n";

TEST_CASE("[partitioned] partitioned with prec constraints") {
	auto in = std::istringstream(part_dependent_jobs_file);
	auto jobs = NP::parse_csv_job_file<dtime_t>(in);
	auto in_platform = std::istringstream(platform_file);
	auto platform = NP::parse_platform_spec_csv<dtime_t>(in_platform);
	auto in_prec = std::istringstream(part_prec_file);
	auto prec = NP::parse_precedence_file<dtime_t>(in_prec);

	NP::Scheduling_problem<dtime_t> prob(jobs, prec, platform);
	NP::Analysis_options opts;

	auto space = NP::Global::State_space<dtime_t>::explore(prob, opts);

	CHECK(space->is_schedulable());

	CHECK(space->number_of_nodes() == 6);
	CHECK(space->number_of_states() == 6);

	CHECK(space->get_finish_times(jobs[0]).min() == 2);
	CHECK(space->get_finish_times(jobs[0]).max() == 4);
	CHECK(space->get_finish_times(jobs[1]).min() == 3);
	CHECK(space->get_finish_times(jobs[1]).max() == 14);
	CHECK(space->get_finish_times(jobs[2]).min() == 6);
	CHECK(space->get_finish_times(jobs[2]).max() == 21);
	CHECK(space->get_finish_times(jobs[3]).min() == 5);
	CHECK(space->get_finish_times(jobs[3]).max() == 7);
	CHECK(space->get_finish_times(jobs[4]).min() == 7);
	CHECK(space->get_finish_times(jobs[4]).max() == 8);
}

const std::string inv_part_file =
"Task ID, Job ID, Arrival min, Arrival max, Cost min, Cost max, Deadline, Priority, Affinity\n"
"1, 1, 0, 0, 2, 4, 7, 1, 1\n"
"2, 1, 0, 0, 10, 15, 20, 2, 2\n";

TEST_CASE("[partitioned] error affinity") {
	auto in = std::istringstream(inv_part_file);
	auto jobs = NP::parse_csv_job_file<dtime_t>(in);
	auto in_platform = std::istringstream(platform_file);
	auto platform = NP::parse_platform_spec_csv<dtime_t>(in_platform);

	CHECK_THROWS_AS(NP::Global::State_space<dtime_t>::explore(jobs, platform),
		NP::InvalidAffinity);
}