// src/smd/forth/interpreter/control_flow_corpus.test.cpp             -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <smd/forth/interpreter/control_flow_corpus.hpp>
#include <smd/forth/interpreter/control_flow_corpus.hpp> // test 2nd include OK

#include <catch2/catch_test_macros.hpp>

TEST_CASE("ControlFlowCorpusTest - HeaderIsIdempotent") { REQUIRE(true); }

// This corpus is data (source-text constants for F27 to consume), not
// behavior: the one thing worth asserting here, at this step, is that every
// entry is present and non-empty -- a guard against a future edit silently
// dropping one of the F26 merge criterion's own named programs (ABS,
// COUNTDOWN, SPIN, SUMTO, FIND5, TENS, SUMEVEN, FIRST) before F27 has a
// chance to consume it.

TEST_CASE("ControlFlowCorpusTest - EveryProgramIsPresent") {
    using namespace smd::forth::interpreter::corpus;
    CHECK_FALSE(abs_program.empty());
    CHECK_FALSE(countdown_program.empty());
    CHECK_FALSE(spin_program.empty());
    CHECK_FALSE(upto3_program.empty());
    CHECK_FALSE(exit_boundary_program.empty());
    CHECK_FALSE(sumto_program.empty());
    CHECK_FALSE(find5_program.empty());
    CHECK_FALSE(tens_program.empty());
    CHECK_FALSE(sumeven_program.empty());
    CHECK_FALSE(first_program.empty());
    CHECK_FALSE(memory_words_program.empty());
    CHECK_FALSE(create_allot_program.empty());
}
