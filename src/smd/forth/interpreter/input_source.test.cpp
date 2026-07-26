// src/smd/forth/interpreter/input_source.test.cpp                   -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <smd/forth/interpreter/input_source.hpp>
#include <smd/forth/interpreter/input_source.hpp> // test 2nd include OK

#include <catch2/catch_test_macros.hpp>

using smd::forth::interpreter::input_source;

TEST_CASE("InputSourceTest - HeaderIsIdempotent") { REQUIRE(true); }

TEST_CASE("InputSourceTest - StartsAtOffsetZero") {
    input_source src{"dup swap"};
    CHECK(src.text() == "dup swap");
    CHECK(src.in() == 0);
}

TEST_CASE("InputSourceTest - SetInMovesTheScanPoint") {
    input_source src{"dup swap"};
    src.set_in(4);
    CHECK(src.in() == 4);
    CHECK(src.cursor_at_in().remaining() == "swap");
}

TEST_CASE("InputSourceTest - CursorAtInTracksLineAndColumnAcrossNewlines") {
    input_source src{"dup\nswap"};
    src.set_in(4);
    auto cur = src.cursor_at_in();
    auto pos = cur.position();
    CHECK(cur.remaining() == "swap");
    CHECK(pos.offset == 4);
    CHECK(pos.line == 2);
    CHECK(pos.column == 1);
}

TEST_CASE("InputSourceTest - DefaultConstructedIsEmptyAtOffsetZero") {
    input_source src{};
    CHECK(src.text().empty());
    CHECK(src.in() == 0);
    CHECK(src.cursor_at_in().empty());
}
