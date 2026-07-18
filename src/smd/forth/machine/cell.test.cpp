// src/smd/forth/machine/cell.test.cpp                               -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <smd/forth/machine/cell.hpp>
#include <smd/forth/machine/cell.hpp> // test 2nd include OK

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <type_traits>

using smd::forth::machine::cell;
using smd::forth::machine::flag_false;
using smd::forth::machine::flag_true;
using smd::forth::machine::status;

static_assert(std::is_same_v<cell, std::int64_t>);
static_assert(flag_true == -1);
static_assert(flag_false == 0);
static_assert(status{std::monostate{}}.has_value());
static_assert(!status{smd::forth::foundation::parse_error{}}.has_value());

TEST_CASE("CellTest - HeaderIsIdempotent") { REQUIRE(true); }

TEST_CASE("CellTest - FlagConstants") {
    CHECK(flag_true == -1);
    CHECK(flag_false == 0);
}

TEST_CASE("CellTest - AllBitsSet") {
    CHECK(static_cast<std::uint64_t>(flag_true) == ~std::uint64_t{0});
}

TEST_CASE("CellTest - StatusRoundTrip") {
    status ok{std::monostate{}};
    CHECK(ok.has_value());

    status err{smd::forth::foundation::parse_error{{}, "boom"}};
    CHECK(!err.has_value());
}
