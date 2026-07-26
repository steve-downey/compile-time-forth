// src/smd/forth/conformance/core_suite_strings.test.cpp              -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <smd/forth/conformance/ttester_corpus.hpp>
#include <smd/forth/conformance/ttester_corpus.hpp> // test 2nd include OK

#include <smd/forth/forth.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string_view>

// Step F32 (docs/forth-plan-2.md), D14/D22/D23: one shard of the Forth-2012
// core word set test battery, under constant evaluation -- strings and
// parsing (`S" ." [CHAR] WORD PARSE CHAR COUNT TYPE ABORT"`).
//
// Unlike every other shard in this directory, this one cannot gate on
// "output is empty": `TYPE`/`."`/`ABORT"`'s own condition-met path all
// print, by design, and this shard deliberately exercises that (`SAY`/
// `GREET`/`TRYCHK` below). The gate instead checks that the ttester's own
// two failure messages (`ERROR1`'s own text, `ttester_corpus.hpp`) never
// appear, *and* that each word's own expected text actually printed --
// proving the printing words ran, not merely that nothing failed.
//
// `32` stands in for `BL` throughout (the space-character constant is not
// yet a dictionary word here — docs/conformance-exclusions.md).
//
// `CHECK`/`RUNCHECK`'s own shape (the flag `ABORT"` consumes is pushed
// *inside* `RUNCHECK`, not supplied by the caller before `CATCH` runs)
// mirrors `interp.test.cpp`'s own already-passing `AbortQuoteCaughtByCatch`
// deliberately: DIV-0024 (filed this step) records that the seemingly
// equivalent shape "caller pushes the callee's own flag argument, then
// CATCHes it" fails to build here (`CATCH` cannot restore stack items the
// caught word itself popped before throwing) -- this shard avoids that gap
// rather than exercising it.

TEST_CASE("CoreSuiteStringsTest - HeaderIsIdempotent") { REQUIRE(true); }

namespace {

using smd::forth::compiled_forth;

constexpr auto strings_suite = compiled_forth<
    SMD_FORTH_TTESTER_SOURCE
    ": SAY S\" HI\" TYPE ; "
    ": GREET .\" YO\" ; "
    ": FIRSTCHAR [CHAR] A ; "
    ": CHECK ABORT\" BOOM\" ; "
    ": RUNCHECK -1 CHECK 999 ; "
    "T{ S\" HELLO\" NIP -> 5 }T "
    "T{ 32 WORD HELLO COUNT NIP -> 5 }T "
    "T{ 41 PARSE HELLO) NIP -> 5 }T "
    "T{ CHAR B -> 66 }T "
    "T{ FIRSTCHAR -> 65 }T "
    "T{ 0 CHECK -> }T "
    "T{ ' RUNCHECK CATCH -> -2 }T "
    "T{ SAY -> }T "
    "T{ GREET -> }T">;

constexpr auto text_of(auto const &out) -> std::string_view {
    return std::string_view{out.begin(), static_cast<std::size_t>(out.size())};
}

constexpr auto no_ttester_failure(std::string_view text) -> bool {
    return text.find("INCORRECT RESULT:") == std::string_view::npos &&
           text.find("WRONG NUMBER OF RESULTS:") == std::string_view::npos;
}

} // namespace

static_assert(no_ttester_failure(text_of(strings_suite.output())));
static_assert(text_of(strings_suite.output()).find("HI") != std::string_view::npos);
static_assert(text_of(strings_suite.output()).find("YO") != std::string_view::npos);
static_assert(text_of(strings_suite.output()).find("BOOM") != std::string_view::npos);

TEST_CASE("CoreSuiteStringsTest - AllAssertionsPassAndExpectedTextPrinted") {
    // `out` must be a named, scope-lived object: `.output()` returns
    // `static_vector` by value (forth.hpp), so a `text_of(...)` result
    // taken from a *temporary* would dangle the moment this full
    // expression ends -- exactly what the static_asserts above get away
    // with (each is its own complete expression) but a multi-statement
    // TEST_CASE body cannot.
    auto const out = strings_suite.output();
    auto const text = text_of(out);
    CHECK(no_ttester_failure(text));
    CHECK(text.find("HI") != std::string_view::npos);
    CHECK(text.find("YO") != std::string_view::npos);
    CHECK(text.find("BOOM") != std::string_view::npos);
}
