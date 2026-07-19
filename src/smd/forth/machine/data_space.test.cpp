// src/smd/forth/machine/data_space.test.cpp                         -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <smd/forth/machine/data_space.hpp>
#include <smd/forth/machine/data_space.hpp> // test 2nd include OK

#include <catch2/catch_test_macros.hpp>

using smd::forth::machine::addr;
using smd::forth::machine::cell;
using smd::forth::machine::data_space;

// addr <-> cell explicit conversion round-trip.
static_assert([] {
    addr a{cell{42}};
    return static_cast<cell>(a) == 42;
}());

static_assert([] {
    // Default-constructed addr converts to cell 0.
    addr a;
    return static_cast<cell>(a) == 0;
}());

static_assert([] {
    // addr equality is structural.
    return addr{cell{7}} == addr{cell{7}} && addr{cell{7}} != addr{cell{8}};
}());

// allot/fetch/store round-trip.
static_assert([] {
    data_space<8> space;
    auto a = space.allot(1);
    if (!a.has_value())
        return false;
    if (!space.store(a.value(), 99).has_value())
        return false;
    auto v = space.fetch(a.value());
    return v.has_value() && v.value() == 99;
}());

static_assert([] {
    // Multi-cell allot reserves a contiguous run; each cell is independently
    // addressable and starts zero-initialized.
    data_space<8> space;
    auto base = space.allot(3);
    if (!base.has_value())
        return false;
    auto base_cell = static_cast<cell>(base.value());
    addr second{base_cell + 1};
    auto zero = space.fetch(second);
    if (!zero.has_value() || zero.value() != 0)
        return false;
    if (!space.store(second, 5).has_value())
        return false;
    auto first = space.fetch(base.value());
    auto updated = space.fetch(second);
    return first.has_value() && first.value() == 0 && updated.has_value() &&
           updated.value() == 5;
}());

static_assert([] {
    // here() advances by the amount allotted.
    data_space<8> space;
    (void)space.allot(3);
    return static_cast<cell>(space.here()) == 3 && space.size() == 3;
}());

// out-of-bounds fetch/store diagnosed.
static_assert([] {
    data_space<8> space;
    (void)space.allot(1);
    return !space.fetch(addr{cell{1}}).has_value();
}());

static_assert([] {
    data_space<8> space;
    (void)space.allot(1);
    return !space.store(addr{cell{1}}, 1).has_value();
}());

static_assert([] {
    // Negative addresses are diagnosed, not just addresses past HERE.
    data_space<8> space;
    return !space.fetch(addr{cell{-1}}).has_value();
}());

static_assert([] {
    data_space<8> space;
    return !space.store(addr{cell{-1}}, 1).has_value();
}());

// allot exhaustion diagnosed.
static_assert([] {
    data_space<4> space;
    (void)space.allot(4);
    return !space.allot(1).has_value();
}());

static_assert([] {
    data_space<4> space;
    return !space.allot(5).has_value();
}());

static_assert([] {
    // Negative allot count is diagnosed, not just over-capacity.
    data_space<4> space;
    return !space.allot(-1).has_value();
}());

TEST_CASE("DataSpaceTest - HeaderIsIdempotent") { REQUIRE(true); }

TEST_CASE("DataSpaceTest - AllotFetchStoreRoundTrip") {
    data_space<8> space;
    auto a = space.allot(1);
    REQUIRE(a.has_value());
    CHECK(space.store(a.value(), 123).has_value());
    auto v = space.fetch(a.value());
    REQUIRE(v.has_value());
    CHECK(v.value() == 123);
}

TEST_CASE("DataSpaceTest - FreshSpaceHasNothingAllotted") {
    data_space<8> space;
    CHECK(space.size() == 0);
    CHECK(static_cast<cell>(space.here()) == 0);
}

TEST_CASE("DataSpaceTest - OutOfBoundsFetchIsDiagnosed") {
    data_space<8> space;
    (void)space.allot(2);
    auto r = space.fetch(addr{cell{2}});
    CHECK(!r.has_value());
    CHECK(r.error().message != nullptr);
}

TEST_CASE("DataSpaceTest - OutOfBoundsStoreIsDiagnosed") {
    data_space<8> space;
    (void)space.allot(2);
    auto r = space.store(addr{cell{2}}, 1);
    CHECK(!r.has_value());
    CHECK(r.error().message != nullptr);
}

TEST_CASE("DataSpaceTest - AllotExhaustionIsDiagnosed") {
    data_space<4> space;
    CHECK(space.allot(4).has_value());
    auto r = space.allot(1);
    CHECK(!r.has_value());
    CHECK(r.error().message != nullptr);
}

TEST_CASE("DataSpaceTest - AddrConvertsExplicitlyToAndFromCell") {
    addr a{cell{5}};
    cell c = static_cast<cell>(a);
    CHECK(c == 5);
    addr b{c};
    CHECK(a == b);
}
