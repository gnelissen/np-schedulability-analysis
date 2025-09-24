#include "doctest.h"

#include <iostream>
#include <sstream>

#include "io.hpp"
#include "global/space.hpp"

using namespace NP;

#define NOSUSP 0
#define GENERAL_SUSP 1
#define PATHWISE_SUSP 2

inline Interval<dense_t> D(dense_t a, dense_t b)
{
	return Interval<dense_t>{a, b};
}

const std::string uniproc_sn_susp_jobs_file =
"Task ID, Job ID, Arrival min, Arrival max, Cost min, Cost max, Deadline, Priority\n"
"1      , 1      , 0          , 0          , 1        , 2        , 10      , 1      \n"
"1      , 2      , 10         , 10         , 1        , 2        , 20      , 2      \n"
"1      , 3      , 20         , 20         , 1        , 2        , 50      , 3      \n"
"1      , 4      , 30         , 30         , 1        , 2        , 60      , 4      \n"
"1      , 5      , 40         , 40         , 1        , 2        , 70      , 5      \n"
"1      , 6      , 50         , 50         , 1        , 2        , 80      , 6      \n"
"2      , 7      , 0          , 0          , 7        , 7        , 30      , 8      \n"
"2      , 8      , 30         , 30         , 7        , 7        , 100     , 9      \n"
"3      , 9      , 0          , 0          , 3        , 13       , 60      , 7      ";

const std::string uniproc_sn_susp_dag_file =
"Predecessor TID, Predecessor JID, Successor TID, Successor JID, Min_Sus, Max_Sus\n"
"2               , 7              , 1            , 3            , 10     , 12     ";

TEST_CASE("[susp] Uniproc Supernode Self-Suspensions") {
	auto susp_dag_in = std::istringstream(uniproc_sn_susp_dag_file);
	auto in = std::istringstream(uniproc_sn_susp_jobs_file);
	auto dag_in  = std::istringstream("\n");
	auto aborts_in = std::istringstream("\n");

	Scheduling_problem<dtime_t> prob{
		parse_csv_job_file<dtime_t>(in),
		parse_precedence_file<dtime_t>(susp_dag_in)};

	Analysis_options opts;

	opts.be_naive = true;
	auto gen_nspace = Global::State_space<dtime_t>::explore(prob, opts);
	CHECK(gen_nspace->is_schedulable());

	opts.be_naive = false;
	auto gen_space = Global::State_space<dtime_t>::explore(prob, opts);
	CHECK(gen_space->is_schedulable());

	opts.be_naive = true;
	auto pw_nspace = Global::State_space<dtime_t>::explore(prob, opts);
	CHECK(pw_nspace->is_schedulable());

	opts.be_naive = false;
	auto pw_space = Global::State_space<dtime_t>::explore(prob, opts);
	CHECK(pw_space->is_schedulable());

	for (const Job<dtime_t>& j : prob.jobs) {
		CHECK(gen_nspace->get_finish_times(j) == gen_space->get_finish_times(j));
		CHECK(pw_nspace->get_finish_times(j) == pw_space->get_finish_times(j));
		CHECK(pw_nspace->get_finish_times(j).upto() <= gen_space->get_finish_times(j).upto());
	}
	delete gen_nspace;
	delete gen_space;
	delete pw_nspace;
	delete pw_space;
}


const std::string uniproc_sn_susp_jobs_anomaly_file =
"Task ID, Job ID, Arrival min, Arrival max, Cost min, Cost max, Deadline, Priority\n"
"1      , 1      , 0          , 2          , 39       , 39       , 200     , 1      \n"
"1      , 2      , 0          , 2          , 21       , 21       , 200     , 1      \n"
"1      , 3      , 200        , 202        , 39       , 39       , 400     , 1      \n"
"1      , 4      , 200        , 202        , 21       , 21       , 400     , 1      \n"
"1      , 5      , 400        , 402        , 39       , 39       , 600     , 1      \n"
"1      , 6      , 400        , 402        , 21       , 21       , 600     , 1      \n"
"1      , 7      , 600        , 602        , 39       , 39       , 800     , 1      \n"
"1      , 8      , 600        , 602        , 21       , 21       , 800     , 1      \n"
"1      , 9      , 800        , 802        , 39       , 39       , 1000    , 1      \n"
"1      , 10     , 800        , 802        , 21       , 21       , 1000    , 1      \n"
"2      , 1      , 0          , 10         , 49       , 49       , 1000    , 2      \n"
"2      , 2      , 0          , 10         , 144      , 144      , 1000    , 2      ";

const std::string uniproc_sn_susp_dag_anomaly_file =
"Predecessor TID, Predecessor JID, Successor TID, Successor JID, Sus_Min, Sus_Max\n"
"1               , 1              , 1            , 2            , 0      , 0      \n"
"1               , 2              , 1            , 3            , 0      , 0      \n"
"1               , 3              , 1            , 4            , 0      , 0      \n"
"1               , 4              , 1            , 5            , 0      , 0      \n"
"1               , 5              , 1            , 6            , 0      , 0      \n"
"1               , 6              , 1            , 7            , 0      , 0      \n"
"1               , 7              , 1            , 8            , 0      , 0      \n"
"1               , 8              , 1            , 9            , 0      , 0      \n"
"1               , 9              , 1            , 10           , 0      , 0      \n"
"2               , 1              , 2            , 2            , 0      , 0      ";

// test removed: there is no difference between global and uniproc anymore
/*TEST_CASE("[susp] Uniproc Supernode Self-Suspensions Multiset Anomaly") {
	// This test tests if multiple states that have the same earliest finish time are being
	// correctly handled by the state space exploration algorithm. If a state is being replaced
	// instead of being added to the node, then we should see that this taskset is schedulable 
	// with the pathwise implementation, but we see that this is indeed not the case

	auto susp_dag_in = std::istringstream(uniproc_sn_susp_dag_anomaly_file);
	auto in = std::istringstream(uniproc_sn_susp_jobs_anomaly_file);
	auto dag_in  = std::istringstream("\n");
	auto aborts_in = std::istringstream("\n");

	Scheduling_problem<dtime_t> prob{
		parse_file<dtime_t>(in),
		parse_precedence_file<dtime_t>(susp_dag_in)};

	Analysis_options opts;

	opts.be_naive = true;
	auto gen_nspace = Global::State_space<dtime_t>::explore(prob, opts);
	CHECK(!(gen_nspace->is_schedulable()));

	opts.be_naive = false;
	auto gen_space = Global::State_space<dtime_t>::explore(prob, opts);
	CHECK(!(gen_space->is_schedulable()));

	opts.be_naive = true;
	auto pw_nspace = Global::State_space<dtime_t>::explore(prob, opts);
	CHECK(!(pw_nspace->is_schedulable()));

	opts.be_naive = false;
	auto pw_space = Global::State_space<dtime_t>::explore(prob, opts);
	CHECK(!(pw_space->is_schedulable()));

	delete gen_nspace;
	delete gen_space;
	delete pw_nspace;
	delete pw_space;
}*/

const std::string uniproc_sn_susp_jobs_g_pw_diff_file =
"Task ID, Job ID, Arrival min, Arrival max, Cost min, Cost max, Deadline, Priority\n"
"1      , 1      , 0          , 10         , 3        , 6        , 100     , 1      \n"
"1      , 2      , 0          , 10         , 1        , 2        , 100     , 1      \n"
"1      , 3      , 100        , 110        , 3        , 6        , 200     , 1      \n"
"1      , 4      , 100        , 110        , 1        , 2        , 200     , 1      \n"
"2      , 1      , 0          , 10         , 8        , 16       , 100     , 2      \n"
"2      , 2      , 0          , 10         , 10       , 20       , 100     , 2      \n"
"2      , 3      , 100        , 110        , 8        , 16       , 200     , 2      \n"
"2      , 4      , 100        , 110        , 10       , 20       , 200     , 2      \n"
"3      , 1      , 0          , 20         , 7        , 14       , 200     , 3      \n"
"3      , 2      , 0          , 20         , 7        , 14       , 200     , 3      \n"
"4      , 1      , 0          , 20         , 4        , 9        , 200     , 4      \n"
"4      , 2      , 0          , 20         , 2        , 4        , 200     , 4      \n"
"5      , 1      , 0          , 20         , 7        , 14       , 200     , 5      \n"
"5      , 2      , 0          , 20         , 15       , 30       , 200     , 5      ";

const std::string uniproc_sn_susp_g_pw_diff_file =
"Predecessor TID, Predecessor JID, Successor TID, Successor JID, Sus_Min, Sus_Max\n"
"1               , 1              , 1            , 2            , 0      , 0      \n"
"1               , 2              , 1            , 3            , 0      , 0      \n"
"1               , 3              , 1            , 4            , 0      , 0      \n"
"2               , 1              , 2            , 2            , 0      , 0      \n"
"2               , 2              , 2            , 3            , 0      , 0      \n"
"2               , 3              , 2            , 4            , 0      , 0      \n"
"3               , 1              , 3            , 2            , 0      , 0      \n"
"4               , 1              , 4            , 2            , 0      , 0      \n"
"5               , 1              , 5            , 2            , 0      , 0      ";

// test removed: the new code does not differentiate anymore
/*TEST_CASE("[susp] General Pathwise Uniprocessor Difference") {
	auto susp_dag_in = std::istringstream(uniproc_sn_susp_g_pw_diff_file);
	auto in = std::istringstream(uniproc_sn_susp_jobs_g_pw_diff_file);
	auto dag_in  = std::istringstream("\n");
	auto aborts_in = std::istringstream("\n");

	Scheduling_problem<dtime_t> prob{
		parse_file<dtime_t>(in),
		parse_precedence_file<dtime_t>(susp_dag_in)};

	Analysis_options opts;
	auto gen_nspace = Global::State_space<dtime_t>::explore(prob, opts);
	CHECK(!(gen_nspace->is_schedulable()));

	auto pw_nspace = Global::State_space<dtime_t>::explore(prob, opts);
	CHECK(pw_nspace->is_schedulable());

	delete gen_nspace;
	delete pw_nspace;
}*/

TEST_CASE("[susp] Uniproc Global Schedulability Check (sn_susp)") {
	auto susp_dag_in = std::istringstream(uniproc_sn_susp_dag_file);
	auto in = std::istringstream(uniproc_sn_susp_jobs_file);

	Scheduling_problem<dtime_t> prob{
		parse_csv_job_file<dtime_t>(in),
		parse_precedence_file<dtime_t>(susp_dag_in),
		1};

	Analysis_options opts;

	auto uspace = Global::State_space<dtime_t>::explore(prob, opts);
	CHECK(uspace->is_schedulable());
	opts.be_naive = false;

	auto gspace = NP::Global::State_space<dtime_t>::explore(prob, opts);
	CHECK(gspace->is_schedulable());

	for (const Job<dtime_t>& j : prob.jobs) {
		CHECK(uspace->get_finish_times(j) == gspace->get_finish_times(j)); 
	}
}

TEST_CASE("[susp] Uniproc Global Schedulability Check (g_pw_diff)") {
	auto susp_dag_in = std::istringstream(uniproc_sn_susp_g_pw_diff_file);
	auto in = std::istringstream(uniproc_sn_susp_jobs_g_pw_diff_file);

	Scheduling_problem<dtime_t> prob{
		parse_csv_job_file<dtime_t>(in),
		parse_precedence_file<dtime_t>(susp_dag_in), 1};

	Analysis_options opts;

	auto uspace = Global::State_space<dtime_t>::explore(prob, opts);
	CHECK(uspace->is_schedulable());
	opts.be_naive = false;

	auto gspace = NP::Global::State_space<dtime_t>::explore(prob, opts); 
	CHECK(gspace->is_schedulable());

	for (const Job<dtime_t>& j : prob.jobs) {
		CHECK(uspace->get_finish_times(j) == gspace->get_finish_times(j)); 
	}
}

// test removed: the code does not differentiate between uniproc and global anymore
/*TEST_CASE("[susp] Uniproc Global Schedulability Check (anomaly)") {
	auto susp_dag_in = std::istringstream(uniproc_sn_susp_dag_anomaly_file);
	auto in = std::istringstream(uniproc_sn_susp_jobs_anomaly_file);

	Scheduling_problem<dtime_t> prob{
		parse_file<dtime_t>(in),
		parse_precedence_file<dtime_t>(susp_dag_in)};

	Analysis_options opts;

	auto uspace = Global::State_space<dtime_t>::explore(prob, opts);
	CHECK(!(uspace->is_schedulable()));

	prob.num_processors = 1;
	opts.be_naive = false;

	auto gspace = NP::Global::State_space<dtime_t>::explore(prob, opts);
	CHECK(!(gspace->is_schedulable())); 
	delete uspace;
	delete gspace;
}*/

