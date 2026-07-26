// src/smd/forth/conformance/core_suite_defining.test.cpp             -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <smd/forth/conformance/ttester_corpus.hpp>
#include <smd/forth/conformance/ttester_corpus.hpp> // test 2nd include OK

#include <smd/forth/forth.hpp>

#include <catch2/catch_test_macros.hpp>

// Step F32 (docs/forth-plan-2.md), D14/D22/D23: one shard of the Forth-2012
// core word set test battery, under constant evaluation -- the colon
// compiler's execution-token surface (`'`, `EXECUTE`, `[']`, `DEFER`/`IS`,
// `VALUE`/`TO`, `DOES>`) and exception handling (`CATCH`/`THROW`/`ABORT`).
// Every syntax shape below is exercised elsewhere in this project's own
// `interp.test.cpp` (F28/F31's own merge criteria); this shard's own job is
// running the identical shapes through the ttester, as
// `T{ ... -> ... }T` assertions, per D14's own leg (c).
//
// `COMPILE,` is deliberately not stress-tested here: DIV-0023 (filed by
// this same step) records that this project's own `COMPILE,` has no
// currently-reachable valid usage. The textbook idiom (`['] TARGET
// COMPILE,` inside an *immediate* helper word, used from a third
// definition) needs `COMPILE,` itself to be non-immediate, deferring its
// own append until the helper actually runs; this project's own
// `COMPILE,` is installed immediate instead, so the append happens while
// the helper is still being *defined* -- before `[']`'s own deferred
// literal-push has ever taken effect -- and underflows. See DIV-0023 for
// the full analysis and why fixing it is out of this shard's own scope.

TEST_CASE("CoreSuiteDefiningTest - HeaderIsIdempotent") { REQUIRE(true); }

namespace {

using smd::forth::compiled_forth;

constexpr auto defining_suite =
    compiled_forth<SMD_FORTH_TTESTER_SOURCE ": DBL DUP + ; "
                                            ": CALLDBL ['] DBL EXECUTE ; "
                                            "DEFER DEFOP "
                                            "' DBL IS DEFOP "
                                            "5 VALUE VAL1 "
                                            ": CONSTANT2 CREATE , DOES> @ ; "
                                            "42 CONSTANT2 LIFE "
                                            ": SAFEW 99 ; "
                                            ": BOOMW 42 THROW ; "
                                            ": MAYBEABORT ABORT ; "
                                            "T{ 5 ' DBL EXECUTE -> 10 }T "
                                            "T{ 5 CALLDBL -> 10 }T "
                                            "T{ 5 DEFOP -> 10 }T "
                                            "T{ VAL1 -> 5 }T "
                                            "10 TO VAL1 "
                                            "T{ VAL1 -> 10 }T "
                                            "T{ LIFE -> 42 }T "
                                            "T{ ' SAFEW CATCH -> 99 0 }T "
                                            "T{ ' BOOMW CATCH -> 42 }T "
                                            "T{ 5 6 ' BOOMW CATCH -> 5 6 42 }T "
                                            "T{ ' MAYBEABORT CATCH -> -1 }T "
                                            "T{ 1 2 0 THROW + -> 3 }T">;

} // namespace

static_assert(defining_suite.output().size() == 0);

TEST_CASE("CoreSuiteDefiningTest - AllAssertionsPass") {
    CHECK(defining_suite.output().size() == 0);
}
