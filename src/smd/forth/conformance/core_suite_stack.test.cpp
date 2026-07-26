// src/smd/forth/conformance/core_suite_stack.test.cpp                -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <smd/forth/conformance/ttester_corpus.hpp>
#include <smd/forth/conformance/ttester_corpus.hpp> // test 2nd include OK

#include <smd/forth/forth.hpp>

#include <catch2/catch_test_macros.hpp>

// Step F32 (docs/forth-plan-2.md), D14/D22/D23: one shard of the Forth-2012
// core word set test battery, under constant evaluation -- data-stack
// manipulation and the return stack. `NIP`/`TUCK` are Programming-Tools
// (core-ext) words, not core proper; tested here alongside the core words
// for regression coverage since this project already implements them.

TEST_CASE("CoreSuiteStackTest - HeaderIsIdempotent") { REQUIRE(true); }

namespace {

using smd::forth::compiled_forth;

constexpr auto stack_suite =
    compiled_forth<SMD_FORTH_TTESTER_SOURCE
                   "T{ 1 DUP -> 1 1 }T "
                   "T{ 1 DROP -> }T "
                   "T{ 1 2 SWAP -> 2 1 }T "
                   "T{ 1 2 OVER -> 1 2 1 }T "
                   "T{ 1 2 3 ROT -> 2 3 1 }T "
                   "T{ 0 ?DUP -> 0 }T "
                   "T{ 5 ?DUP -> 5 5 }T "
                   "T{ 1 2 NIP -> 2 }T "
                   "T{ 1 2 TUCK -> 2 1 2 }T "
                   "T{ DEPTH -> 0 }T "
                   "T{ 1 2 3 DEPTH -> 1 2 3 3 }T "
                   "T{ 5 >R R> -> 5 }T "
                   "T{ 5 >R R@ R> -> 5 5 }T">;

} // namespace

static_assert(stack_suite.output().size() == 0);

TEST_CASE("CoreSuiteStackTest - AllAssertionsPass") {
    CHECK(stack_suite.output().size() == 0);
}
