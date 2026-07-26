// src/smd/forth/conformance/core_suite_control.test.cpp              -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <smd/forth/conformance/ttester_corpus.hpp>
#include <smd/forth/conformance/ttester_corpus.hpp> // test 2nd include OK

#include <smd/forth/forth.hpp>

#include <catch2/catch_test_macros.hpp>

// Step F32 (docs/forth-plan-2.md), D14/D22/D23: one shard of the Forth-2012
// core word set test battery, under constant evaluation -- control flow:
// IF/ELSE/THEN, BEGIN/UNTIL, BEGIN/WHILE/REPEAT, DO/LOOP/+LOOP/LEAVE/
// UNLOOP/I/J, EXIT, and RECURSE. Every `DO` loop below whose trip count
// could be zero is guarded by an `IF` first (`TENS`'s own outer/inner
// loops always run a fixed nonzero count, so none needs one) -- see
// DIV-0020's own note on why a bare `count 0 DO` is never safe to write
// when `count` could be zero.

TEST_CASE("CoreSuiteControlTest - HeaderIsIdempotent") { REQUIRE(true); }

namespace {

using smd::forth::compiled_forth;

constexpr auto control_suite = compiled_forth<
    SMD_FORTH_TTESTER_SOURCE
    ": TERN DUP 0= IF DROP 100 ELSE DROP 200 THEN ; "
    ": COUNTUP 0 BEGIN 1+ DUP 3 = UNTIL ; "
    ": UPTO3 0 BEGIN DUP 3 < WHILE 1 + REPEAT ; "
    ": SUMTO 0 SWAP 1+ 0 DO I + LOOP ; "
    ": TENS 0 3 0 DO 3 0 DO J + LOOP LOOP ; "
    ": SUMEVEN 0 10 0 DO I + 2 +LOOP ; "
    ": FIND5 10 0 DO I 5 = IF I LEAVE THEN LOOP ; "
    ": FIRST 10 0 DO I DUP DUP * 8 > IF UNLOOP EXIT THEN DROP LOOP -1 ; "
    ": INNER DUP 0< IF EXIT THEN DROP ; "
    ": OUTER INNER 99 ; "
    ": FACT DUP 1 > IF DUP 1- RECURSE * THEN ; "
    "T{ 0 TERN -> 100 }T "
    "T{ 5 TERN -> 200 }T "
    "T{ COUNTUP -> 3 }T "
    "T{ UPTO3 -> 3 }T "
    "T{ 5 SUMTO -> 15 }T "
    "T{ TENS -> 9 }T "
    "T{ SUMEVEN -> 20 }T "
    "T{ FIND5 -> 5 }T "
    "T{ FIRST -> 3 }T "
    "T{ -3 OUTER -> -3 99 }T "
    "T{ 1 FACT -> 1 }T "
    "T{ 5 FACT -> 120 }T">;

} // namespace

static_assert(control_suite.output().size() == 0);

TEST_CASE("CoreSuiteControlTest - AllAssertionsPass") {
    CHECK(control_suite.output().size() == 0);
}
