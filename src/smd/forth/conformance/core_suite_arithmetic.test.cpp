// src/smd/forth/conformance/core_suite_arithmetic.test.cpp           -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <smd/forth/conformance/ttester_corpus.hpp>
#include <smd/forth/conformance/ttester_corpus.hpp> // test 2nd include OK

#include <smd/forth/forth.hpp>

#include <catch2/catch_test_macros.hpp>

// Step F32 (docs/forth-plan-2.md), D14/D22/D23: one shard of the Forth-2012
// core word set test battery, under constant evaluation (D14's own leg
// (c)) -- arithmetic, logic, and comparison. Sharded into its own
// translation unit (D22: "the conformance suite is sharded across
// translation units so per-TU evaluation cost stays bounded") rather than
// combined with the other shards in this directory.
//
// `<=`/`>=` are this project's own additions, not Forth-2012 core words;
// they are tested here for regression coverage alongside the words
// Forth-2012 actually specifies, not as a conformance claim.
//
// `/`/`MOD` here use only non-negative operands: DIV-0022 records that this
// project's own symmetric (truncating) division legitimately disagrees with
// gforth's own floored division for a negative operand with an inexact
// result -- both are Forth-2012-legal, so this shard's own compile-time
// gate checks this project's *own* declared convention, not a claim that
// gforth would agree on every input.

TEST_CASE("CoreSuiteArithmeticTest - HeaderIsIdempotent") { REQUIRE(true); }

namespace {

using smd::forth::compiled_forth;

constexpr auto arithmetic_suite =
    compiled_forth<SMD_FORTH_TTESTER_SOURCE "T{ 1 2 + -> 3 }T "
                                            "T{ 5 3 - -> 2 }T "
                                            "T{ 2 3 * -> 6 }T "
                                            "T{ 7 2 / -> 3 }T "
                                            "T{ 7 2 MOD -> 1 }T "
                                            "T{ 6 3 / -> 2 }T "
                                            "T{ 6 3 MOD -> 0 }T "
                                            "T{ 5 NEGATE -> -5 }T "
                                            "T{ -5 NEGATE -> 5 }T "
                                            "T{ -5 ABS -> 5 }T "
                                            "T{ 5 ABS -> 5 }T "
                                            "T{ 3 7 MIN -> 3 }T "
                                            "T{ 7 3 MIN -> 3 }T "
                                            "T{ 3 7 MAX -> 7 }T "
                                            "T{ 7 3 MAX -> 7 }T "
                                            "T{ 12 10 AND -> 8 }T "
                                            "T{ 12 10 OR -> 14 }T "
                                            "T{ 12 10 XOR -> 6 }T "
                                            "T{ 0 INVERT -> -1 }T "
                                            "T{ 1 4 LSHIFT -> 16 }T "
                                            "T{ 16 4 RSHIFT -> 1 }T "
                                            "T{ 5 1- -> 4 }T "
                                            "T{ 5 1+ -> 6 }T "
                                            "T{ 0 0= -> -1 }T "
                                            "T{ 1 0= -> 0 }T "
                                            "T{ -1 0< -> -1 }T "
                                            "T{ 0 0< -> 0 }T "
                                            "T{ 3 3 = -> -1 }T "
                                            "T{ 3 4 = -> 0 }T "
                                            "T{ 3 4 <> -> -1 }T "
                                            "T{ 3 3 <> -> 0 }T "
                                            "T{ 3 4 < -> -1 }T "
                                            "T{ 4 3 < -> 0 }T "
                                            "T{ 4 3 > -> -1 }T "
                                            "T{ 3 4 > -> 0 }T "
                                            "T{ 3 3 <= -> -1 }T "
                                            "T{ 4 3 <= -> 0 }T "
                                            "T{ 3 3 >= -> -1 }T "
                                            "T{ 3 4 >= -> 0 }T">;

} // namespace

// 4d8b6f2a-3e7c-4a9d-8f1b-6c3e9a2d7f5b
static_assert(arithmetic_suite.output().size() == 0);

TEST_CASE("CoreSuiteArithmeticTest - AllAssertionsPass") {
    CHECK(arithmetic_suite.output().size() == 0);
}
// 4d8b6f2a-3e7c-4a9d-8f1b-6c3e9a2d7f5b end
