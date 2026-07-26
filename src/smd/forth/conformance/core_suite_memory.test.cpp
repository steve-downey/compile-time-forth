// src/smd/forth/conformance/core_suite_memory.test.cpp                -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <smd/forth/conformance/ttester_corpus.hpp>
#include <smd/forth/conformance/ttester_corpus.hpp> // test 2nd include OK

#include <smd/forth/forth.hpp>

#include <catch2/catch_test_macros.hpp>

// Step F32 (docs/forth-plan-2.md), D14/D21/D22/D23: one shard of the
// Forth-2012 core word set test battery, under constant evaluation -- data
// space, `VARIABLE`/`CONSTANT`/`CREATE`, and this step's own six D21
// address-unit words (`CELLS CELL+ CHARS CHAR+ C@ C!`). Every `CELLS`/
// `CELL+`/`CHARS`/`CHAR+` assertion below is written against this
// project's own cell-granular convention (DIV-0009: one address unit is
// one cell, so `1 CELLS` is `1`, not a byte count) -- a Forth-2012 program
// assuming byte addressing would assert something different here, by
// design, not by defect (docs/conformance-exclusions.md's own "known
// divergences" section).

TEST_CASE("CoreSuiteMemoryTest - HeaderIsIdempotent") { REQUIRE(true); }

namespace {

using smd::forth::compiled_forth;

constexpr auto memory_suite = compiled_forth<
    SMD_FORTH_TTESTER_SOURCE
    "VARIABLE V1 "
    "7 CONSTANT SEVEN "
    "CREATE BUF 4 ALLOT "
    "T{ 5 V1 ! V1 @ -> 5 }T "
    "T{ 3 V1 +! V1 @ -> 8 }T "
    "T{ SEVEN -> 7 }T "
    "T{ 10 BUF ! 20 BUF 1 CELLS + ! BUF @ BUF 1 CELLS + @ + -> 30 }T "
    "T{ 1 CELLS -> 1 }T "
    "T{ 3 CELLS -> 3 }T "
    "T{ 0 CELL+ -> 1 }T "
    "T{ 5 CELL+ -> 6 }T "
    "T{ 1 CHARS -> 1 }T "
    "T{ 3 CHARS -> 3 }T "
    "T{ 0 CHAR+ -> 1 }T "
    "T{ 5 CHAR+ -> 6 }T "
    "T{ 42 V1 C! V1 C@ -> 42 }T "
    "T{ 99 BUF 2 CELLS + C! BUF 2 CHARS + C@ -> 99 }T">;

} // namespace

static_assert(memory_suite.output().size() == 0);

TEST_CASE("CoreSuiteMemoryTest - AllAssertionsPass") {
    CHECK(memory_suite.output().size() == 0);
}
