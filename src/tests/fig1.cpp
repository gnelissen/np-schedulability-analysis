#include "doctest.h"

#include <iostream>


#include "global/space.hpp"

using namespace NP;

static const auto inf = Time_model::constants<dtime_t>::infinity();

TEST_CASE("Example in Figure 1(a,b)") {
	Global::State_space<dtime_t>::Workload jobs{
		// high-frequency task
		Job<dtime_t>{1, Interval<dtime_t>( 0,  0), Interval<dtime_t>(1, 2), 10, 10, 0},
		Job<dtime_t>{2, Interval<dtime_t>(10, 10), Interval<dtime_t>(1, 2), 20, 20, 1},
		Job<dtime_t>{3, Interval<dtime_t>(20, 20), Interval<dtime_t>(1, 2), 30, 30, 2},
		Job<dtime_t>{4, Interval<dtime_t>(30, 30), Interval<dtime_t>(1, 2), 40, 40, 3},
		Job<dtime_t>{5, Interval<dtime_t>(40, 40), Interval<dtime_t>(1, 2), 50, 50, 4},
		Job<dtime_t>{6, Interval<dtime_t>(50, 50), Interval<dtime_t>(1, 2), 60, 60, 5},

		// middle task
		Job<dtime_t>{7, Interval<dtime_t>( 0,  0), Interval<dtime_t>(7, 8), 30, 30, 6},
		Job<dtime_t>{8, Interval<dtime_t>(30, 30), Interval<dtime_t>(7, 8), 60, 60, 7},

		// the long task
		Job<dtime_t>{9, Interval<dtime_t>( 0,  0), Interval<dtime_t>(3, 13), 60, 60, 8}
	};

	SUBCASE("Naive exploration") {
		auto space = Global::State_space<dtime_t>::explore_naively(jobs);
		CHECK(!space->is_schedulable());

		// make sure we saw the right deadline miss
		auto ftimes = space->get_finish_times(jobs[1]);
		CHECK(ftimes.min() == 11);
		CHECK(ftimes.max() == 24);

		delete space;
	}

	SUBCASE("Exploration with state-merging") {
		auto space = Global::State_space<dtime_t>::explore(jobs);
		CHECK(!space->is_schedulable());

		// make sure we saw the right deadline miss
		auto ftimes = space->get_finish_times(jobs[1]);
		CHECK(ftimes.min() == 11);
		CHECK(ftimes.max() == 24);

		delete space;
	}

	/*SUBCASE("Exploration after deadline miss") {
		// explore with early_exit = false
		Scheduling_problem<dtime_t> prob{jobs};
		Analysis_options opts;
		opts.early_exit = false;
		auto space = Global::State_space<dtime_t>::explore(prob, opts);
		CHECK(!space->is_schedulable());

		// make sure the analysis continued after the deadline miss
		auto ftimes = space->get_finish_times(jobs[5]);
		CHECK(ftimes.min() == 51);
		CHECK(ftimes.max() == 52);

		ftimes = space->get_finish_times(jobs[4]);
		CHECK(ftimes.min() == 41);
		CHECK(ftimes.max() == 42);

		ftimes = space->get_finish_times(jobs[3]);
		CHECK(ftimes.min() == 31);
		CHECK(ftimes.max() == 32);

		ftimes = space->get_finish_times(jobs[7]);
		CHECK(ftimes.min() == 38);
		CHECK(ftimes.max() == 40);

		delete space;
	}*/
}


TEST_CASE("Example in Figure 1(c)") {
	Global::State_space<dtime_t>::Workload jobs{
		// high-frequency task
		Job<dtime_t>{1, Interval<dtime_t>( 0,  0), Interval<dtime_t>(1, 2), 10, 1, 0},
		Job<dtime_t>{2, Interval<dtime_t>(10, 10), Interval<dtime_t>(1, 2), 20, 2, 1},
		Job<dtime_t>{3, Interval<dtime_t>(20, 20), Interval<dtime_t>(1, 2), 30, 3, 2},
		Job<dtime_t>{4, Interval<dtime_t>(30, 30), Interval<dtime_t>(1, 2), 40, 4, 3},
		Job<dtime_t>{5, Interval<dtime_t>(40, 40), Interval<dtime_t>(1, 2), 50, 5, 4},
		Job<dtime_t>{6, Interval<dtime_t>(50, 50), Interval<dtime_t>(1, 2), 60, 6, 5},

		// the long task
		Job<dtime_t>{9, Interval<dtime_t>( 0,  0), Interval<dtime_t>(3, 13), 60, 7, 6},

		// middle task
		Job<dtime_t>{7, Interval<dtime_t>( 0,  0), Interval<dtime_t>(7, 8), 30, 8, 7},
		Job<dtime_t>{8, Interval<dtime_t>(30, 30), Interval<dtime_t>(7, 7), 60, 9, 8},
	};

	auto nspace = Global::State_space<dtime_t>::explore_naively(jobs);
	CHECK(nspace->is_schedulable());

	auto space = Global::State_space<dtime_t>::explore(jobs);
	CHECK(space->is_schedulable());

	for (const Job<dtime_t>& j : jobs) {
		CHECK(nspace->get_finish_times(j) == space->get_finish_times(j));
		CHECK(nspace->get_finish_times(j).from() != 0);
	}
	delete space;
	delete nspace;
}
