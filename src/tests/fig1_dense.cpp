#include "doctest.h"

#include <iostream>


#include "global/space.hpp"

using namespace NP;

static const auto inf = Time_model::constants<dense_t>::infinity();

inline Interval<dense_t> D(dense_t a, dense_t b)
{
	return Interval<dense_t>{a, b};
}

TEST_CASE("[dense time] Example in Figure 1(a,b)") {
	Global::State_space<dense_t>::Workload jobs{
		// high-frequency task
		Job<dense_t>{1, D( 0,  0), D(1, 2), 10, 10, 0},
		Job<dense_t>{2, D(10, 10), D(1, 2), 20, 20, 1},
		Job<dense_t>{3, D(20, 20), D(1, 2), 30, 30, 2},
		Job<dense_t>{4, D(30, 30), D(1, 2), 40, 40, 3},
		Job<dense_t>{5, D(40, 40), D(1, 2), 50, 50, 4},
		Job<dense_t>{6, D(50, 50), D(1, 2), 60, 60, 5},

		// middle task
		Job<dense_t>{7, D( 0,  0), D(7, 8), 30, 30, 6},
		Job<dense_t>{8, D(30, 30), D(7, 7), 60, 60, 7},

		// the long task
		Job<dense_t>{9, D( 0,  0), D(3, 13), 60, 60, 8}
	};

	SUBCASE("Naive exploration") {
		auto space = Global::State_space<dense_t>::explore_naively(jobs);
		CHECK(!space->is_schedulable());
		delete space;
	}

	SUBCASE("Exploration with state-merging") {
		auto space = Global::State_space<dense_t>::explore(jobs);
		CHECK(!space->is_schedulable());
		delete space;
	}
}


TEST_CASE("[dense time] Example in Figure 1(c)") {
	Global::State_space<dense_t>::Workload jobs{
		// high-frequency task
		Job<dense_t>{1, D( 0,  0), D(1, 2), 10, 1, 0},
		Job<dense_t>{2, D(10, 10), D(1, 2), 20, 2, 1},
		Job<dense_t>{3, D(20, 20), D(1, 2), 30, 3, 2},
		Job<dense_t>{4, D(30, 30), D(1, 2), 40, 4, 3},
		Job<dense_t>{5, D(40, 40), D(1, 2), 50, 5, 4},
		Job<dense_t>{6, D(50, 50), D(1, 2), 60, 6, 5},

		// the long task
		Job<dense_t>{9, D( 0,  0), D(3, 13), 60, 7, 6},

		// middle task
		Job<dense_t>{7, D( 0,  0), D(7, 8), 30, 8, 7},
		Job<dense_t>{8, D(30, 30), D(7, 7), 60, 9, 8},
	};

	auto nspace = Global::State_space<dense_t>::explore_naively(jobs);
	CHECK(nspace->is_schedulable());

	auto space = Global::State_space<dense_t>::explore(jobs);
	CHECK(space->is_schedulable());

	for (const Job<dense_t>& j : jobs) {
		CHECK(nspace->get_finish_times(j) == space->get_finish_times(j));
		CHECK(nspace->get_finish_times(j).from() != 0);
	}
	delete space;
	delete nspace;
}
