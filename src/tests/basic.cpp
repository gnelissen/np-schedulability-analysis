#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <algorithm>
#include <iostream>

#include "index_set.hpp"
#include "jobs.hpp"
#include "global/space.hpp"

using namespace NP;

static const auto inf = Time_model::constants<dtime_t>::infinity();

TEST_CASE("Intervals") {
	auto i1 = Interval<dtime_t>{10, 20};
	auto i2 = Interval<dtime_t>{15, 25};
	//to check if i1 and  i3 are not disjoint as they are consecutive intervals
	auto i3 = Interval<dtime_t>{21, 30};
	auto i4 = Interval<dtime_t>{5,  45};
	//to check if i1 and i5 are disjoint as they are not consecutive nor are they joint
	auto i5 = Interval<dtime_t>{22, 30};

	Interval<dtime_t> ivals[]{i1, i2, i3, i4};

	CHECK(i1.intersects(i2));
	CHECK(i2.intersects(i3));
	//to check if i1 and  i3 are not disjoint as they are consecutive intervals
	CHECK(i1.intersects(i3));
	//to check if i1 and i5 are disjoint as they are not consecutive nor are they joint
	CHECK(i1.disjoint(i5));

	for (auto i : ivals)
		CHECK(i.intersects(i4));

	CHECK(i1.merge(i2).merge(i3) == Interval<dtime_t>(10, 30));

	//to check if i1 and  i3 are not disjoint as they are consecutive intervals
	CHECK(i1.merge(i3) == Interval<dtime_t>(10,30));	

	CHECK(Interval<dtime_t>(10, 20).intersects(Interval<dtime_t>(10, 20)));
}



TEST_CASE("Job hashes work") {
	Job<dtime_t> j1{8,  Interval<dtime_t>(0, 0), Interval<dtime_t>(3, 13), 60, 60, 0, 0, 0};
	Job<dtime_t> j2{9,  Interval<dtime_t>(0, 0), Interval<dtime_t>(3, 13), 60, 60, 0, 0, 0};
	Job<dtime_t> j3{10, Interval<dtime_t>(0, 0), Interval<dtime_t>(3, 13), 60, 60, 1, 0, 0};

	CHECK(j1.get_key() == j2.get_key()); // The job index is used to make the hash uniq.
    CHECK(j3.get_key() != j1.get_key());
}



TEST_CASE("state space") {

	NP::Global::Schedule_node<dtime_t> n0{ {1} };

	CHECK(n0.finish_range(0).from() == 0);
	CHECK(n0.finish_range(0).until() == 0);

	Job<dtime_t> j1{10, Interval<dtime_t>(0, 0), Interval<dtime_t>(3, 13), 60, 60, 0};

	CHECK(j1.least_exec_time() == 3);
	CHECK(j1.maximal_exec_time() == 13);
	CHECK(j1.earliest_arrival() == 0);
	CHECK(j1.latest_arrival() == 0);
}


TEST_CASE("bool vector assumptions") {
	std::vector<bool> v1(100);

	CHECK(v1.size() == 100);
	CHECK(!v1[10]);
	v1[10] = true;

	std::vector<bool> v2(400);

	v2 = v1;
	CHECK(v2.size() == 100);
	CHECK(v2.capacity() > v1.capacity());

	v1.resize(150);
	CHECK(v1.size() == 150);
	CHECK(!v1[149]);

	std::vector<bool> v3(400);
	std::copy(v1.begin(), v1.end(), v3.begin());
	CHECK(v3.size() == 400);
	CHECK(v3.capacity() > v1.capacity());
	CHECK(v3[10]);
}


using NP::Index_set;

TEST_CASE("Index_set: Basic Construction and State") {
    Index_set s1;
    CHECK(s1.size() == 0);
    CHECK_FALSE(s1.contains(0));
    CHECK_FALSE(s1.contains(100));

    Index_set s2(s1);
    CHECK(s1 == s2);

    Index_set s3;
    s3.add(5);
    CHECK(s1 != s3);
    s3 = s1;
    CHECK(s1 == s3);

    s3.add(10);
    CHECK(s3.size() == NP::Block_Manager::BLOCK_SIZE * 64);
    s3.clear();
    CHECK(s3.size() == 0);
    CHECK(s3 == s1);
}

TEST_CASE("Index_set: Adding Elements, `contains`, and `size`") {
    Index_set s;
    s.add(0);
    CHECK(s.contains(0));
    CHECK_FALSE(s.contains(1));
    CHECK(s.size() == NP::Block_Manager::BLOCK_SIZE * 64);

    s.add(63);
    CHECK(s.contains(63));
    CHECK(s.size() == NP::Block_Manager::BLOCK_SIZE * 64);

    // Cross a u64 boundary
    s.add(64);
    CHECK(s.contains(64));
    CHECK(s.size() == NP::Block_Manager::BLOCK_SIZE * 64);

    // Cross a block boundary (BLOCK_SIZE = 32) -> 32 * 64 = 2048
    s.add(2048);
    CHECK(s.contains(2048));
    CHECK(s.size() == 2 * NP::Block_Manager::BLOCK_SIZE * 64);

    // Add an existing element
    s.add(64);
    CHECK(s.size() == 2 * NP::Block_Manager::BLOCK_SIZE * 64);

    CHECK_FALSE(s.contains(9999));
}

TEST_CASE("Index_set: Derivation Constructor and Immutability") {
    Index_set s1;
    s1.add(10);
    s1.add(20);

    Index_set s2(s1, 30);

    // Original set should be unchanged
    //CHECK(s1.size() == 2);
    CHECK(s1.contains(10));
    CHECK(s1.contains(20));
    CHECK_FALSE(s1.contains(30));

    // New set should have all old bits plus the new one
    //CHECK(s2.size() == 3);
    CHECK(s2.contains(10));
    CHECK(s2.contains(20));
    CHECK(s2.contains(30));

    CHECK(s1 != s2);
}

TEST_CASE("Index_set: Equality and Canonicalization") {
    Index_set s1, s2;
    s1.add(10);
    s1.add(500);
    s1.add(3000);

    s2.add(3000);
    s2.add(10);
    s2.add(500);

    // Sets with the same elements added in different orders must be equal
    CHECK(s1 == s2);
    //CHECK(s1.size() == 3);
    //CHECK(s2.size() == 3);

    s2.add(4000);
    CHECK(s1 != s2);
}

TEST_CASE("Index_set: `matches` method correctness") {
    Index_set s_empty;

    SUBCASE("Matching from an empty set") {
        Index_set s_derived(s_empty, 123);
        CHECK(s_derived.matches(s_empty, 123));
        CHECK_FALSE(s_empty.matches(s_derived, 123));
    }

    SUBCASE("Matching from a non-empty set") {
        Index_set s_base;
        s_base.add(50);
        s_base.add(100);

        Index_set s_derived(s_base, 200);
        CHECK(s_derived.matches(s_base, 200));

        // Check false negatives
        CHECK_FALSE(s_derived.matches(s_base, 201)); // Wrong index
        CHECK_FALSE(s_base.matches(s_derived, 200)); // Wrong direction

        Index_set s_unrelated;
        s_unrelated.add(50);
        s_unrelated.add(101); // Different base
        CHECK_FALSE(s_derived.matches(s_unrelated, 200));
    }

    SUBCASE("Matching across block boundaries") {
        Index_set s_base;
        s_base.add(1);
        Index_set s_derived(s_base, 2050); // idx=2050 crosses block boundary
        CHECK(s_derived.matches(s_base, 2050));
    }

    SUBCASE("Matching should fail if sets are identical") {
        Index_set s1;
        s1.add(10);
        CHECK_FALSE(s1.matches(s1, 10));
    }
}

TEST_CASE("Index_set: `first_non_full_block` optimization") {
    Index_set s;
    // Fill the first block completely (BLOCK_SIZE * 64 bits)
    for (size_t i = 0; i < NP::Block_Manager::BLOCK_SIZE * 64; ++i) {
        s.add(i);
    }

    CHECK(s.size() == NP::Block_Manager::BLOCK_SIZE * 64);

    Index_set s_copy = s;
    CHECK(s == s_copy); // Equality check should be fast

    s.add(NP::Block_Manager::BLOCK_SIZE * 64); // Add one more bit
    CHECK(s != s_copy);
    CHECK(s.size() == 2 * NP::Block_Manager::BLOCK_SIZE * 64);
}