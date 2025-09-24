#include "doctest.h"

#include <iostream>

#include "global/space.hpp"

using namespace NP;

static const auto inf = Time_model::constants<dtime_t>::infinity();

TEST_CASE("[NP state space] Find all next jobs") {
	Global::State_space<dtime_t>::Workload jobs{
		Job<dtime_t>{1, Interval<dtime_t>( 0,  0), Interval<dtime_t>(3, 8), 100, 1, 0},
		Job<dtime_t>{2, Interval<dtime_t>( 7,  7), Interval<dtime_t>(5, 5),  100, 2, 1},
		Job<dtime_t>{3, Interval<dtime_t>(10, 10), Interval<dtime_t>(1, 11),  100, 3, 2},
	};

	SUBCASE("Naive exploration") {
		auto space = Global::State_space<dtime_t>::explore_naively(jobs);
		CHECK(space->is_schedulable());

		CHECK(space->get_finish_times(jobs[0]).from()  == 3);
		CHECK(space->get_finish_times(jobs[0]).until() == 8);

		CHECK(space->get_finish_times(jobs[1]).from()  == 12);
		CHECK(space->get_finish_times(jobs[1]).until() == 13);

		CHECK(space->get_finish_times(jobs[2]).from()  == 13);
		CHECK(space->get_finish_times(jobs[2]).until() == 24);

		delete space;
	}

	SUBCASE("Exploration with merging") {
		auto space = Global::State_space<dtime_t>::explore(jobs);
		CHECK(space->is_schedulable());

		CHECK(space->get_finish_times(jobs[0]).from()  == 3);
		CHECK(space->get_finish_times(jobs[0]).until() == 8);

		CHECK(space->get_finish_times(jobs[1]).from()  == 12);
		CHECK(space->get_finish_times(jobs[1]).until() == 13);

		CHECK(space->get_finish_times(jobs[2]).from()  == 13);
		CHECK(space->get_finish_times(jobs[2]).until() == 24);

		delete space;
	}

}

TEST_CASE("[NP state space] Consider large enough interval") {
	Global::State_space<dtime_t>::Workload jobs{
		Job<dtime_t>{1, Interval<dtime_t>( 0,  0), Interval<dtime_t>(3, 10),  100, 3, 0},
		Job<dtime_t>{2, Interval<dtime_t>( 7,  7),  Interval<dtime_t>(5, 5),  100, 2, 1},
		Job<dtime_t>{3, Interval<dtime_t>(10, 10),  Interval<dtime_t>(5, 5),  100, 1, 2},
	};

	auto nspace = Global::State_space<dtime_t>::explore_naively(jobs);
	CHECK(nspace->is_schedulable());

	CHECK(nspace->get_finish_times(jobs[0]).from()  ==  3);
	CHECK(nspace->get_finish_times(jobs[0]).until() == 10);

	CHECK(nspace->get_finish_times(jobs[1]).from()  == 12);
	CHECK(nspace->get_finish_times(jobs[1]).until() == 20);

	CHECK(nspace->get_finish_times(jobs[2]).from()  == 15);
	CHECK(nspace->get_finish_times(jobs[2]).until() == 19);

	auto space = Global::State_space<dtime_t>::explore_naively(jobs);
	CHECK(space->is_schedulable());

	CHECK(space->get_finish_times(jobs[0]).from()  ==  3);
	CHECK(space->get_finish_times(jobs[0]).until() == 10);

	CHECK(space->get_finish_times(jobs[1]).from()  == 12);
	CHECK(space->get_finish_times(jobs[1]).until() == 20);

	CHECK(space->get_finish_times(jobs[2]).from()  == 15);
	CHECK(space->get_finish_times(jobs[2]).until() == 19);

	delete space;
	delete nspace;
}



TEST_CASE("[NP state space] Respect priorities") {
	Global::State_space<dtime_t>::Workload jobs{
		Job<dtime_t>{1, Interval<dtime_t>( 0,  0), Interval<dtime_t>(3, 10),  100, 2, 0},
		Job<dtime_t>{2, Interval<dtime_t>( 0,  0),  Interval<dtime_t>(5, 5),  100, 1, 1},
	};

	auto nspace = Global::State_space<dtime_t>::explore_naively(jobs);
	CHECK(nspace->is_schedulable());

	CHECK(nspace->get_finish_times(jobs[0]).from()  ==  8);
	CHECK(nspace->get_finish_times(jobs[0]).until() == 15);

	CHECK(nspace->get_finish_times(jobs[1]).from()  ==  5);
	CHECK(nspace->get_finish_times(jobs[1]).until() ==  5);

	auto space = Global::State_space<dtime_t>::explore(jobs);
	CHECK(space->is_schedulable());

	CHECK(space->get_finish_times(jobs[0]).from()  ==  8);
	CHECK(space->get_finish_times(jobs[0]).until() == 15);

	CHECK(space->get_finish_times(jobs[1]).from()  ==  5);
	CHECK(space->get_finish_times(jobs[1]).until() ==  5);

	delete space;
	delete nspace;
}

TEST_CASE("[NP state space] Respect jitter") {
	Global::State_space<dtime_t>::Workload jobs{
		Job<dtime_t>{1, Interval<dtime_t>( 0,  1), Interval<dtime_t>(3, 10),  100, 2, 0},
		Job<dtime_t>{2, Interval<dtime_t>( 0,  1),  Interval<dtime_t>(5, 5),  100, 1, 1},
	};

	auto nspace = Global::State_space<dtime_t>::explore_naively(jobs);
	CHECK(nspace->is_schedulable());

	CHECK(nspace->get_finish_times(jobs[0]).from()  ==  3);
	CHECK(nspace->get_finish_times(jobs[0]).until() == 16);

	CHECK(nspace->get_finish_times(jobs[1]).from()  ==  5);
	CHECK(nspace->get_finish_times(jobs[1]).until() == 15);

	auto space = Global::State_space<dtime_t>::explore(jobs);
	CHECK(space->is_schedulable());

	CHECK(space->get_finish_times(jobs[0]).from()  ==  3);
	CHECK(space->get_finish_times(jobs[0]).until() == 16);

	CHECK(space->get_finish_times(jobs[1]).from()  ==  5);
	CHECK(space->get_finish_times(jobs[1]).until() == 15);

	delete space;
	delete nspace;
}

TEST_CASE("[NP state space] Be eager") {
	Global::State_space<dtime_t>::Workload jobs{
		Job<dtime_t>{1, Interval<dtime_t>( 0,  0),  Interval<dtime_t>(1,  5),  100, 2, 0},
		Job<dtime_t>{2, Interval<dtime_t>( 0,  0),  Interval<dtime_t>(1, 20),  100, 3, 1},
		Job<dtime_t>{3, Interval<dtime_t>(10, 10),  Interval<dtime_t>(5,  5),  100, 1, 2},
	};

	auto nspace = Global::State_space<dtime_t>::explore_naively(jobs);
	CHECK(nspace->is_schedulable());

	CHECK(nspace->get_finish_times(jobs[0]).from()  ==  1);
	CHECK(nspace->get_finish_times(jobs[0]).until() ==  5);

	CHECK(nspace->get_finish_times(jobs[1]).from()  ==  2);
	CHECK(nspace->get_finish_times(jobs[1]).until() ==  25);

	CHECK(nspace->get_finish_times(jobs[2]).from()  ==  15);
	CHECK(nspace->get_finish_times(jobs[2]).until() ==  30);

	auto space = Global::State_space<dtime_t>::explore(jobs);
	CHECK(space->is_schedulable());

	CHECK(space->get_finish_times(jobs[0]).from()  ==  1);
	CHECK(space->get_finish_times(jobs[0]).until() ==  5);

	CHECK(space->get_finish_times(jobs[1]).from()  ==  2);
	CHECK(space->get_finish_times(jobs[1]).until() ==  25);

	CHECK(space->get_finish_times(jobs[2]).from()  ==  15);
	CHECK(space->get_finish_times(jobs[2]).until() ==  30);

	delete space;
	delete nspace;
}


TEST_CASE("[NP state space] Be eager, with short deadline") {
	Global::State_space<dtime_t>::Workload jobs{
		Job<dtime_t>{1, Interval<dtime_t>( 0,  0),  Interval<dtime_t>(1,  5),  100, 2, 0},
		Job<dtime_t>{2, Interval<dtime_t>( 9,  9),  Interval<dtime_t>(1, 15),   25, 3, 1},
		Job<dtime_t>{3, Interval<dtime_t>(30, 30),  Interval<dtime_t>(5,  5),  100, 1, 2},
	};

	auto nspace = Global::State_space<dtime_t>::explore_naively(jobs);
	CHECK(nspace->is_schedulable());

	CHECK(nspace->get_finish_times(jobs[0]).from()  ==  1);
	CHECK(nspace->get_finish_times(jobs[0]).until() ==  5);

	CHECK(nspace->get_finish_times(jobs[1]).from()  ==  10);
	CHECK(nspace->get_finish_times(jobs[1]).until() ==  24);

	CHECK(nspace->get_finish_times(jobs[2]).from()  ==  35);
	CHECK(nspace->get_finish_times(jobs[2]).until() ==  35);

	auto space = Global::State_space<dtime_t>::explore(jobs);
	CHECK(space->is_schedulable());

	CHECK(space->get_finish_times(jobs[0]).from()  ==  1);
	CHECK(space->get_finish_times(jobs[0]).until() ==  5);

	CHECK(space->get_finish_times(jobs[1]).from()  ==  10);
	CHECK(space->get_finish_times(jobs[1]).until() ==  24);

	CHECK(space->get_finish_times(jobs[2]).from()  ==  35);
	CHECK(space->get_finish_times(jobs[2]).until() ==  35);

	delete space;
	delete nspace;
}


TEST_CASE("[NP state space] Treat equal-priority jobs correctly") {
	Global::State_space<dtime_t>::Workload jobs{
		Job<dtime_t>{1, Interval<dtime_t>(    0,    10),  Interval<dtime_t>( 2,    50),  2000, 1, 0},
		Job<dtime_t>{2, Interval<dtime_t>(    0,    10),  Interval<dtime_t>(50,  1200),  5000, 2, 1},
		Job<dtime_t>{3, Interval<dtime_t>( 1000,  1010),  Interval<dtime_t>( 2,    50),  3000, 1, 2},
	};

	auto nspace = Global::State_space<dtime_t>::explore_naively(jobs);
	CHECK(nspace->is_schedulable());

	CHECK(nspace->get_finish_times(jobs[0]).from()  ==  2);
	CHECK(nspace->get_finish_times(jobs[0]).until() ==  1259);

	CHECK(nspace->get_finish_times(jobs[1]).from()  ==  50);
	CHECK(nspace->get_finish_times(jobs[1]).until() ==  1260);

	CHECK(nspace->get_finish_times(jobs[2]).from()  ==  1002);
	CHECK(nspace->get_finish_times(jobs[2]).until() ==  1310);

	auto space = Global::State_space<dtime_t>::explore(jobs);
	CHECK(space->is_schedulable());

	CHECK(nspace->get_finish_times(jobs[0]).from()  ==  2);
	CHECK(nspace->get_finish_times(jobs[0]).until() ==  1259);

	CHECK(nspace->get_finish_times(jobs[1]).from()  ==  50);
	CHECK(nspace->get_finish_times(jobs[1]).until() ==  1260);

	CHECK(nspace->get_finish_times(jobs[2]).from()  ==  1002);
	CHECK(nspace->get_finish_times(jobs[2]).until() ==  1310);

	delete space;
	delete nspace;
}

TEST_CASE("[NP state space] Equal-priority simultaneous arrivals") {
	Global::State_space<dtime_t>::Workload jobs{
		Job<dtime_t>{1, Interval<dtime_t>(    0,    10),  Interval<dtime_t>(  2,    50),  2000, 2000, 0},
		Job<dtime_t>{2, Interval<dtime_t>(    0,    10),  Interval<dtime_t>(100,   150),  2000, 2000, 1},
	};

	auto nspace = Global::State_space<dtime_t>::explore_naively(jobs);
	CHECK(nspace->is_schedulable());

	CHECK(nspace->get_finish_times(jobs[0]).from()  ==  2);
	CHECK(nspace->get_finish_times(jobs[0]).until() ==   9 + 150 + 50);

	CHECK(nspace->get_finish_times(jobs[1]).from()  ==  100);
	CHECK(nspace->get_finish_times(jobs[1]).until() ==  10 + 50 + 150);

	auto space = Global::State_space<dtime_t>::explore(jobs);
	CHECK(space->is_schedulable());

	CHECK(nspace->get_finish_times(jobs[0]).from()  ==  2);
	CHECK(nspace->get_finish_times(jobs[0]).until() ==   9 + 150 + 50);

	CHECK(nspace->get_finish_times(jobs[1]).from()  ==  100);
	CHECK(nspace->get_finish_times(jobs[1]).until() ==  10 + 50 + 150);

	delete space;
	delete nspace;
}

TEST_CASE("[NP state space] don't skip over deadline-missing jobs") {
	Global::State_space<dtime_t>::Workload jobs{
		Job<dtime_t>{1, Interval<dtime_t>(  100,   100),  Interval<dtime_t>(   2,    50),   200, 1, 0},
		Job<dtime_t>{2, Interval<dtime_t>(    0,     0),  Interval<dtime_t>(1200,  1200),  5000, 2, 1},
		Job<dtime_t>{3, Interval<dtime_t>(  200,   250),  Interval<dtime_t>( 2,    50),    6000, 3, 2},
		Job<dtime_t>{4, Interval<dtime_t>(  200,   250),  Interval<dtime_t>( 2,    50),    6000, 4, 3},
		Job<dtime_t>{5, Interval<dtime_t>(  200,   250),  Interval<dtime_t>( 2,    50),    6000, 5, 4},
	};

	SUBCASE("Naive exploration") {
		auto nspace = Global::State_space<dtime_t>::explore_naively(jobs);
		CHECK(!nspace->is_schedulable());

		CHECK(nspace->number_of_edges() == 2);
		CHECK(nspace->number_of_states() == 3);
        CHECK(nspace->number_of_nodes() == 3);
		delete nspace;
	}

	SUBCASE("Exploration with state-merging") {
		auto space = Global::State_space<dtime_t>::explore(jobs);
		CHECK(!space->is_schedulable());

		CHECK(space->number_of_edges() == 2);
		CHECK(space->number_of_states() == 3);
        CHECK(space->number_of_nodes() == 3);
		delete space;
	}

	// test removed: exploration after deadline missed is not supported currently
	/*SUBCASE("Exploration after deadline miss") {
		Scheduling_problem<dtime_t> prob{jobs};
		Analysis_options opts;
		opts.early_exit = false;

		auto space = Global::State_space<dtime_t>::explore(prob, opts);
		CHECK(!space->is_schedulable());

		CHECK(space->number_of_edges() == 5);
		CHECK(space->number_of_states() == 6);
		CHECK(space->number_of_nodes() == 6);

		// make sure the analysis continued after the deadline miss
		auto ftimes = space->get_finish_times(prob.jobs[0]);
		CHECK(ftimes.min() == 1202);
		CHECK(ftimes.max() == 1250);

		ftimes = space->get_finish_times(prob.jobs[1]);
		CHECK(ftimes.min() == 1200);
		CHECK(ftimes.max() == 1200);

		ftimes = space->get_finish_times(prob.jobs[2]);
		CHECK(ftimes.min() == 1204);
		CHECK(ftimes.max() == 1300);

		ftimes = space->get_finish_times(prob.jobs[3]);
		CHECK(ftimes.min() == 1206);
		CHECK(ftimes.max() == 1350);

		ftimes = space->get_finish_times(prob.jobs[4]);
		CHECK(ftimes.min() == 1208);
		CHECK(ftimes.max() == 1400);

		delete space;
	}*/
}

// test removed: exploration after deadline missed is not supported currently
/*TEST_CASE("[NP state space] explore all branches with deadline-missing jobs") {
	Global::State_space<dtime_t>::Workload jobs{
		Job<dtime_t>{1, Interval<dtime_t>(  100,   100),  Interval<dtime_t>(   2,    50),   200, 1, 0, 0},
		Job<dtime_t>{2, Interval<dtime_t>(    0,   150),  Interval<dtime_t>(1200,  1200),  5000, 2, 1, 1},
		Job<dtime_t>{3, Interval<dtime_t>(  200,   250),  Interval<dtime_t>( 2,    50),    6000, 3, 2, 2},
		Job<dtime_t>{4, Interval<dtime_t>(  200,   250),  Interval<dtime_t>( 2,    50),    6000, 4, 3, 3},
		Job<dtime_t>{5, Interval<dtime_t>(  200,   250),  Interval<dtime_t>( 2,    50),    6000, 5, 4, 4},
	};
	Scheduling_problem<dtime_t> prob{jobs};
	Analysis_options opts;
	opts.early_exit = false;

	auto space = Global::State_space<dtime_t>::explore(prob, opts);
	CHECK(!space->is_schedulable());

	CHECK(space->number_of_edges() ==  7);
	CHECK(space->number_of_states() == 7);
	CHECK(space->number_of_nodes() == 7);

	// make sure the analysis continued after the deadline miss
	auto ftimes = space->get_finish_times(prob.jobs[0]);
	CHECK(ftimes.min() ==  102);
	CHECK(ftimes.max() == 1349);

	ftimes = space->get_finish_times(prob.jobs[1]);
	CHECK(ftimes.min() == 1200);
	CHECK(ftimes.max() == 1350);

	ftimes = space->get_finish_times(prob.jobs[2]);
	CHECK(ftimes.min() == 1204);
	CHECK(ftimes.max() == 1400);

	ftimes = space->get_finish_times(prob.jobs[3]);
	CHECK(ftimes.min() == 1206);
	CHECK(ftimes.max() == 1450);

	ftimes = space->get_finish_times(prob.jobs[4]);
	CHECK(ftimes.min() == 1208);
	CHECK(ftimes.max() == 1500);

	delete space;
}*/

TEST_CASE("[NP state space] explore across bucket boundaries") {
	Global::State_space<dtime_t>::Workload jobs{
		Job<dtime_t>{1, Interval<dtime_t>(  100,   100),  Interval<dtime_t>(  50,   50),  10000, 1, 0},
		Job<dtime_t>{2, Interval<dtime_t>( 3000,  3000),  Interval<dtime_t>(4000, 4000),  10000, 2, 1},
		Job<dtime_t>{3, Interval<dtime_t>( 6000,  6000),  Interval<dtime_t>(   2,    2),  10000, 3, 2},
	};

	Scheduling_problem<dtime_t> prob{jobs};
	Analysis_options opts;


	opts.be_naive = true;
	auto nspace = Global::State_space<dtime_t>::explore(prob, opts);
	CHECK(nspace->is_schedulable());

	CHECK(nspace->number_of_edges() == 3);

	opts.be_naive = false;
	auto space = Global::State_space<dtime_t>::explore(prob, opts);
	CHECK(space->is_schedulable());

	CHECK(space->number_of_edges() == 3);

	delete space;
	delete nspace;
}

TEST_CASE("[NP state space] start times satisfy work-conserving property")
{
    Job<dtime_t> j0{0, Interval<dtime_t>( 0,  0), Interval<dtime_t>(2, 2), 10, 2, 0};
    Job<dtime_t> j1{1, Interval<dtime_t>(0, 8), Interval<dtime_t>(2, 2), 10, 1, 1};

	Global::State_space<dtime_t>::Workload jobs{j0, j1};

    SUBCASE("naive exploration") {
        auto space = Global::State_space<dtime_t>::explore_naively(jobs);
        CHECK(space->is_schedulable());
        CHECK(space->get_finish_times(j0) == Interval<dtime_t>(2, 4));
        CHECK(space->get_finish_times(j1) == Interval<dtime_t>(2, 10));
		delete space;
    }

    SUBCASE("exploration with state-merging") {
        auto space = Global::State_space<dtime_t>::explore(jobs);
        CHECK(space->is_schedulable());
        CHECK(space->get_finish_times(j0) == Interval<dtime_t>(2, 4));
        CHECK(space->get_finish_times(j1) == Interval<dtime_t>(2, 10));
		delete space;
    }
}

// TEST_CASE("[NP TSN state space] Check the working of TAS") {
// 	TSN::State_space<dtime_t>::Workload jobs{
// 		Job<dtime_t>{1, I( 0,  0), I(1, 2), 10, 0},
// 		Job<dtime_t>{2, I(10, 10), I(1, 2),  20, 0},
// 		Job<dtime_t>{3, I(20, 20), I(1, 2),  30, 0},
// 		Job<dtime_t>{4, I(10, 12), I(5, 7),  30, 1},
// 		Job<dtime_t>{5, I(25, 25), I(15, 19),  60, 2},
// 		Job<dtime_t>{6, I(10, 12), I(5, 7),  30, 0},
// 	};

// 	SUBCASE("Checking finish times") {
// 		auto space = TSN::State_space<dtime_t>::explore(jobs);
// 		//also perhaps needs gates
		
// 		CHECK(!space->is_schedulable());

// 		CHECK(space->get_finish_times(jobs[0]).from()  == 1);
// 		CHECK(space->get_finish_times(jobs[0]).until() == 2);

// 		CHECK(space->get_finish_times(jobs[1]).from()  == 11);
// 		CHECK(space->get_finish_times(jobs[1]).until() == 27);

// 		CHECK(space->get_finish_times(jobs[2]).from()  == 21);
// 		CHECK(space->get_finish_times(jobs[2]).until() == 24);

// 		CHECK(space->get_finish_times(jobs[3]).from()  == 14);
// 		CHECK(space->get_finish_times(jobs[3]).until() == 29);

// 		CHECK(space->get_finish_times(jobs[5]).from()  == 15);
// 		CHECK(space->get_finish_times(jobs[5]).until() == 26);
// 
//		delete space;
// 	}
// }
