#include "doctest.h"

#include <iostream>
#include <sstream>

#include "io.hpp"
#include "global/state.hpp"
#include "global/space.hpp"

using namespace NP;

TEST_CASE("[gang] uniproc vs gang") {
	Global::State_space<dtime_t>::Workload jobs{
			Job<dtime_t>{1, Interval<dtime_t>(0, 0), Interval<dtime_t>(1, 2), 10, 10, 0, 0},
			Job<dtime_t>{2, Interval<dtime_t>(10, 10), Interval<dtime_t>(1, 2), 20, 20, 1, 1},
			Job<dtime_t>{3, Interval<dtime_t>(20, 20), Interval<dtime_t>(1, 2), 30, 30, 2, 2},
			Job<dtime_t>{4, Interval<dtime_t>(30, 30), Interval<dtime_t>(1, 2), 40, 40, 3, 3},
			Job<dtime_t>{5, Interval<dtime_t>(40, 40), Interval<dtime_t>(1, 2), 50, 50, 4, 4},
			Job<dtime_t>{6, Interval<dtime_t>(50, 50), Interval<dtime_t>(1, 2), 60, 60, 5, 5},
			Job<dtime_t>{7, Interval<dtime_t>(0, 0), Interval<dtime_t>(7, 8), 30, 30, 6, 6},
			Job<dtime_t>{8, Interval<dtime_t>(30, 30), Interval<dtime_t>(7, 8), 60, 60, 7, 7},
	};

	Global::State_space<dtime_t>::Workload jobs_gang{
			Job<dtime_t>{1, Interval<dtime_t>(0, 0),
						 std::map<unsigned int, Interval<dtime_t>>{{2, Interval<dtime_t>(1, 2)}}, 10, 10, 0, 0},
			Job<dtime_t>{2, Interval<dtime_t>(10, 10),
						 std::map<unsigned int, Interval<dtime_t>>{{2, Interval<dtime_t>(1, 2)}}, 20, 20, 1, 1},
			Job<dtime_t>{3, Interval<dtime_t>(20, 20),
						 std::map<unsigned int, Interval<dtime_t>>{{2, Interval<dtime_t>(1, 2)}}, 30, 30, 2, 2},
			Job<dtime_t>{4, Interval<dtime_t>(30, 30),
						 std::map<unsigned int, Interval<dtime_t>>{{2, Interval<dtime_t>(1, 2)}}, 40, 40, 3, 3},
			Job<dtime_t>{5, Interval<dtime_t>(40, 40),
						 std::map<unsigned int, Interval<dtime_t>>{{2, Interval<dtime_t>(1, 2)}}, 50, 50, 4, 4},
			Job<dtime_t>{6, Interval<dtime_t>(50, 50),
						 std::map<unsigned int, Interval<dtime_t>>{{2, Interval<dtime_t>(1, 2)}}, 60, 60, 5, 5},
			Job<dtime_t>{7, Interval<dtime_t>(0, 0),
						 std::map<unsigned int, Interval<dtime_t>>{{2, Interval<dtime_t>(7, 8)}}, 30, 30, 6, 6},
			Job<dtime_t>{8, Interval<dtime_t>(30, 30),
						 std::map<unsigned int, Interval<dtime_t>>{{2, Interval<dtime_t>(7, 8)}}, 60, 60, 7, 7},
	};

	// compare the two
	auto space = Global::State_space<dtime_t>::explore(jobs);
	auto space_gang = Global::State_space<dtime_t>::explore(jobs_gang, 2);

	CHECK(space->is_schedulable());
	CHECK(space_gang->is_schedulable());

	// compare the response times
	for (int i = 0; i < jobs.size(); ++i) {
		auto ftimes = space->get_finish_times(jobs[i]);
		auto ftimes_gang = space_gang->get_finish_times(jobs_gang[i]);
		CHECK(ftimes.min() == ftimes_gang.min());
		CHECK(ftimes.max() == ftimes_gang.max());
	}

	delete space;
	delete space_gang;
}

TEST_CASE("[gang] global vs gang") {
	Global::State_space<dtime_t>::Workload jobs{
			Job<dtime_t>{1, Interval<dtime_t>(0, 0), Interval<dtime_t>(1, 2), 10, 10, 0, 0},
			Job<dtime_t>{2, Interval<dtime_t>(10, 10), Interval<dtime_t>(1, 2), 20, 20, 1, 1},
			Job<dtime_t>{3, Interval<dtime_t>(20, 20), Interval<dtime_t>(1, 2), 30, 30, 2, 2},
			Job<dtime_t>{4, Interval<dtime_t>(30, 30), Interval<dtime_t>(1, 2), 40, 40, 3, 3},
			Job<dtime_t>{5, Interval<dtime_t>(40, 40), Interval<dtime_t>(1, 2), 50, 50, 4, 4},
			Job<dtime_t>{6, Interval<dtime_t>(50, 50), Interval<dtime_t>(1, 2), 60, 60, 5, 5},
			Job<dtime_t>{7, Interval<dtime_t>(0, 0), Interval<dtime_t>(7, 8), 30, 30, 6, 6},
			Job<dtime_t>{8, Interval<dtime_t>(30, 30), Interval<dtime_t>(7, 8), 60, 60, 7, 7},
			Job<dtime_t>{9, Interval<dtime_t>(0, 0), Interval<dtime_t>(3, 13), 60, 60, 8, 8}
	};

	Global::State_space<dtime_t>::Workload jobs_rigid_gang{
			Job<dtime_t>{1, Interval<dtime_t>(0, 0),
						 std::map<unsigned int, Interval<dtime_t>>{{2, Interval<dtime_t>(1, 2)}}, 10, 10, 0, 0},
			Job<dtime_t>{2, Interval<dtime_t>(10, 10),
						 std::map<unsigned int, Interval<dtime_t>>{{2, Interval<dtime_t>(1, 2)}}, 20, 20, 1, 1},
			Job<dtime_t>{3, Interval<dtime_t>(20, 20),
						 std::map<unsigned int, Interval<dtime_t>>{{2, Interval<dtime_t>(1, 2)}}, 30, 30, 2, 2},
			Job<dtime_t>{4, Interval<dtime_t>(30, 30),
						 std::map<unsigned int, Interval<dtime_t>>{{2, Interval<dtime_t>(1, 2)}}, 40, 40, 3, 3},
			Job<dtime_t>{5, Interval<dtime_t>(40, 40),
						 std::map<unsigned int, Interval<dtime_t>>{{2, Interval<dtime_t>(1, 2)}}, 50, 50, 4, 4},
			Job<dtime_t>{6, Interval<dtime_t>(50, 50),
						 std::map<unsigned int, Interval<dtime_t>>{{2, Interval<dtime_t>(1, 2)}}, 60, 60, 5, 5},
			Job<dtime_t>{7, Interval<dtime_t>(0, 0),
						 std::map<unsigned int, Interval<dtime_t>>{{2, Interval<dtime_t>(7, 8)}}, 30, 30, 6, 6},
			Job<dtime_t>{8, Interval<dtime_t>(30, 30),
						 std::map<unsigned int, Interval<dtime_t>>{{2, Interval<dtime_t>(7, 8)}}, 60, 60, 7, 7},
			Job<dtime_t>{9, Interval<dtime_t>(0, 0),
						 std::map<unsigned int, Interval<dtime_t>>{{2, Interval<dtime_t>(3, 13)}}, 60, 60, 8, 8}

	};

	Global::State_space<dtime_t>::Workload jobs_moldable_gang{
			Job<dtime_t>{1, Interval<dtime_t>(0, 0),
						 std::map<unsigned int, Interval<dtime_t>>{{1, Interval<dtime_t>(0, 5)},{2, Interval<dtime_t>(1, 2)}}, 10, 10, 0, 0},
			Job<dtime_t>{2, Interval<dtime_t>(10, 10),
						 std::map<unsigned int, Interval<dtime_t>>{{1, Interval<dtime_t>(0, 5)},{2, Interval<dtime_t>(1, 2)}}, 20, 20, 1, 1},
			Job<dtime_t>{3, Interval<dtime_t>(20, 20),
						 std::map<unsigned int, Interval<dtime_t>>{{1, Interval<dtime_t>(0, 5)},{2, Interval<dtime_t>(1, 2)}}, 30, 30, 2, 2},
			Job<dtime_t>{4, Interval<dtime_t>(30, 30),
						 std::map<unsigned int, Interval<dtime_t>>{{1, Interval<dtime_t>(0, 5)},{2, Interval<dtime_t>(1, 2)}}, 40, 40, 3, 3},
			Job<dtime_t>{5, Interval<dtime_t>(40, 40),
						 std::map<unsigned int, Interval<dtime_t>>{{1, Interval<dtime_t>(0, 5)},{2, Interval<dtime_t>(1, 2)}}, 50, 50, 4, 4},
			Job<dtime_t>{6, Interval<dtime_t>(50, 50),
						 std::map<unsigned int, Interval<dtime_t>>{{1, Interval<dtime_t>(0, 5)},{2, Interval<dtime_t>(1, 2)}}, 60, 60, 5, 5},
			Job<dtime_t>{7, Interval<dtime_t>(0, 0),
						 std::map<unsigned int, Interval<dtime_t>>{{1, Interval<dtime_t>(5, 12)},{2, Interval<dtime_t>(7, 8)}}, 30, 30, 6, 6},
			Job<dtime_t>{8, Interval<dtime_t>(30, 30),
						 std::map<unsigned int, Interval<dtime_t>>{{1, Interval<dtime_t>(2, 12)},{2, Interval<dtime_t>(7, 8)}}, 60, 60, 7, 7},
			Job<dtime_t>{9, Interval<dtime_t>(0, 0),
						 std::map<unsigned int, Interval<dtime_t>>{{1, Interval<dtime_t>(3, 25)},{2, Interval<dtime_t>(3, 13)}}, 60, 60, 8, 8}

	};

	// compare the two
	auto space = Global::State_space<dtime_t>::explore(jobs, 2);
	auto space_rigid_gang = Global::State_space<dtime_t>::explore(jobs_rigid_gang, 4);
	auto space_moldable_gang = Global::State_space<dtime_t>::explore(jobs_moldable_gang, 4);

	CHECK(space->is_schedulable());
	CHECK(space_rigid_gang->is_schedulable());
	CHECK(space_moldable_gang->is_schedulable());

	// compare the response times
	for (int i = 0; i < jobs.size(); ++i) {
		auto ftimes = space->get_finish_times(jobs[i]);
		auto ftimes_gang = space_rigid_gang->get_finish_times(jobs_rigid_gang[i]);
		CHECK(ftimes.min() == ftimes_gang.min());
		CHECK(ftimes.max() == ftimes_gang.max());
	}

	for (int i = 0; i < jobs.size()-1; ++i) {
		auto ftimes = space->get_finish_times(jobs[i]);
		auto ftimes_mold_gang = space_moldable_gang->get_finish_times(jobs_moldable_gang[i]);
		CHECK(ftimes.min() == ftimes_mold_gang.min());
		CHECK(ftimes.max() == ftimes_mold_gang.max());
	}

	auto ftimes = space->get_finish_times(jobs[jobs.size() - 1]);
	auto ftimes_mold_gang = space_moldable_gang->get_finish_times(jobs_moldable_gang[jobs.size() - 1]);
	CHECK(ftimes.min() == ftimes_mold_gang.min());
	CHECK(ftimes_mold_gang.max() == 26);

	delete space;
	delete space_rigid_gang;
	delete space_moldable_gang;
}

TEST_CASE("[gang] rigid gang") {
	Global::State_space<dtime_t>::Workload jobs_gang{
			Job<dtime_t>{1, Interval<dtime_t>(0, 0),
						 std::map<unsigned int, Interval<dtime_t>>{{2, Interval<dtime_t>(1, 4)}}, 10, 10, 0, 0},
			Job<dtime_t>{2, Interval<dtime_t>(10, 10),
						 std::map<unsigned int, Interval<dtime_t>>{{2, Interval<dtime_t>(1, 4)}}, 20, 20, 1, 1},
			Job<dtime_t>{3, Interval<dtime_t>(20, 20),
						 std::map<unsigned int, Interval<dtime_t>>{{2, Interval<dtime_t>(1, 4)}}, 30, 30, 2, 2},
			Job<dtime_t>{4, Interval<dtime_t>(30, 30),
						 std::map<unsigned int, Interval<dtime_t>>{{2, Interval<dtime_t>(1, 4)}}, 40, 40, 3, 3},
			Job<dtime_t>{5, Interval<dtime_t>(40, 40),
						 std::map<unsigned int, Interval<dtime_t>>{{2, Interval<dtime_t>(1, 4)}}, 50, 50, 4, 4},
			Job<dtime_t>{6, Interval<dtime_t>(50, 50),
						 std::map<unsigned int, Interval<dtime_t>>{{2, Interval<dtime_t>(1, 4)}}, 60, 60, 5, 5},
			Job<dtime_t>{7, Interval<dtime_t>(0, 0),
						 std::map<unsigned int, Interval<dtime_t>>{{3, Interval<dtime_t>(6, 7)}}, 30, 30, 6, 6},
			Job<dtime_t>{8, Interval<dtime_t>(30, 30),
						 std::map<unsigned int, Interval<dtime_t>>{{3, Interval<dtime_t>(6, 7)}}, 60, 60, 7, 7},
			Job<dtime_t>{9, Interval<dtime_t>(0, 0),
						 std::map<unsigned int, Interval<dtime_t>>{{2, Interval<dtime_t>(3, 13)}}, 60, 60, 8, 8}

	};

	auto space_gang = Global::State_space<dtime_t>::explore(jobs_gang, 4);

	CHECK(space_gang->is_schedulable());

	CHECK(space_gang->get_finish_times(jobs_gang[6]).min() == 9);
	CHECK(space_gang->get_finish_times(jobs_gang[6]).max() == 21);
	CHECK(space_gang->get_finish_times(jobs_gang[7]).min() == 37);
	CHECK(space_gang->get_finish_times(jobs_gang[7]).max() == 41);
	CHECK(space_gang->get_finish_times(jobs_gang[8]).min() == 3);
	CHECK(space_gang->get_finish_times(jobs_gang[8]).max() == 13);

	delete space_gang;
}

TEST_CASE("[gang] moldable gang") {
	Global::State_space<dtime_t>::Workload jobs_gang{
			Job<dtime_t>{1, Interval<dtime_t>(0, 0),
						 std::map<unsigned int, Interval<dtime_t>>{{2, Interval<dtime_t>(1, 4)}}, 10, 10, 0, 0},
			Job<dtime_t>{2, Interval<dtime_t>(10, 10),
						 std::map<unsigned int, Interval<dtime_t>>{{2, Interval<dtime_t>(1, 4)}}, 20, 20, 1, 1},
			Job<dtime_t>{3, Interval<dtime_t>(20, 20),
						 std::map<unsigned int, Interval<dtime_t>>{{2, Interval<dtime_t>(1, 4)}}, 30, 30, 2, 2},
			Job<dtime_t>{4, Interval<dtime_t>(30, 30),
						 std::map<unsigned int, Interval<dtime_t>>{{2, Interval<dtime_t>(1, 4)}}, 40, 40, 3, 3},
			Job<dtime_t>{5, Interval<dtime_t>(40, 40),
						 std::map<unsigned int, Interval<dtime_t>>{{2, Interval<dtime_t>(1, 4)}}, 50, 50, 4, 4},
			Job<dtime_t>{6, Interval<dtime_t>(50, 50),
						 std::map<unsigned int, Interval<dtime_t>>{{2, Interval<dtime_t>(1, 4)}}, 60, 60, 5, 5},
			Job<dtime_t>{7, Interval<dtime_t>(0, 0),
						 std::map<unsigned int, Interval<dtime_t>>{{1, Interval<dtime_t>(15, 17)},{3, Interval<dtime_t>(6, 7)}}, 30, 30, 6, 6},
			Job<dtime_t>{8, Interval<dtime_t>(25, 30),
						 std::map<unsigned int, Interval<dtime_t>>{{1, Interval<dtime_t>(15, 17)},{3, Interval<dtime_t>(6, 7)}}, 60, 60, 7, 7},
			Job<dtime_t>{9, Interval<dtime_t>(0, 0),
						 std::map<unsigned int, Interval<dtime_t>>{{1, Interval<dtime_t>(5, 20)},{2, Interval<dtime_t>(3, 13)}}, 60, 60, 8, 8}

	};

	auto space_gang = Global::State_space<dtime_t>::explore(jobs_gang, 4);

	CHECK(space_gang->is_schedulable());

	CHECK(space_gang->get_finish_times(jobs_gang[3]).min() == 31);
	CHECK(space_gang->get_finish_times(jobs_gang[3]).max() == 40);
	CHECK(space_gang->get_finish_times(jobs_gang[6]).min() == 15);
	CHECK(space_gang->get_finish_times(jobs_gang[6]).max() == 17);
	CHECK(space_gang->get_finish_times(jobs_gang[7]).min() == 31);
	CHECK(space_gang->get_finish_times(jobs_gang[7]).max() == 47);
	CHECK(space_gang->get_finish_times(jobs_gang[8]).min() == 5);
	CHECK(space_gang->get_finish_times(jobs_gang[8]).max() == 20);

	delete space_gang;
}