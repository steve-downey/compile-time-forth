// src/smd/forth/conformance/ttester_corpus.test.cpp                 -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <smd/forth/conformance/ttester_corpus.hpp>
#include <smd/forth/conformance/ttester_corpus.hpp> // test 2nd include OK

#include <smd/forth/forth.hpp>
#include <smd/forth/interpreter/session.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string_view>

TEST_CASE("TtesterCorpusTest - HeaderIsIdempotent") { REQUIRE(true); }

// -- Merge criterion (static_assert, immediately-invoked-lambda pattern) ---
//
// Step F32, D14/D23: "Hayes ttester compiled by the session itself." The
// ttester's own source (smd::forth::conformance::ttester_source,
// SMD_FORTH_TTESTER_SOURCE) is Forth text this project's own text
// interpreter compiles, exactly like any other program -- not C++ imitating
// T{/->/}T. A passing T{ ... -> ... }T assertion produces no output at all
// (upstream's own convention, preserved verbatim by this adaptation, DIV-
// 0020); checking the built session's own captured output is empty is
// therefore a real `static_assert` gate over an *executed* conformance
// check, not merely "this compiled."
namespace {

using smd::forth::compiled_forth;

constexpr auto ttester_self_check =
    compiled_forth<SMD_FORTH_TTESTER_SOURCE
                   "T{ 1 1 + -> 2 }T "
                   "T{ 2 3 SWAP -> 3 2 }T "
                   "T{ 5 DUP * -> 25 }T">;

} // namespace

static_assert(ttester_self_check.output().size() == 0);

TEST_CASE("TtesterCorpusTest - PassingAssertionsProduceNoOutput") {
    CHECK(ttester_self_check.output().size() == 0);
}

// -- Other ttester behavior (ordinary runtime, via build_session) ----------
//
// These deliberately feed the ttester a *wrong* assertion, so they cannot
// be static_assert gates (a static_assert here would fail the whole
// translation unit) -- they instead prove the diagnostic path itself fires,
// which the merge criterion above cannot: a tester that always reports
// success (a no-op }T, say) would also produce empty output on every
// passing test.

TEST_CASE("TtesterCorpusTest - WrongResultIsDiagnosed") {
    auto built = smd::forth::interpreter::build_session<256, 160, 512, 512>(
        std::string_view{SMD_FORTH_TTESTER_SOURCE "T{ 2 2 + -> 5 }T"});
    REQUIRE(built.has_value());
    auto const &out = built.value().output;
    std::string_view text{out.begin(), static_cast<std::size_t>(out.size())};
    CHECK(text.find("INCORRECT RESULT:") != std::string_view::npos);
}

TEST_CASE("TtesterCorpusTest - WrongResultCountIsDiagnosed") {
    auto built = smd::forth::interpreter::build_session<256, 160, 512, 512>(
        std::string_view{SMD_FORTH_TTESTER_SOURCE "T{ 1 2 -> 1 2 3 }T"});
    REQUIRE(built.has_value());
    auto const &out = built.value().output;
    std::string_view text{out.begin(), static_cast<std::size_t>(out.size())};
    CHECK(text.find("WRONG NUMBER OF RESULTS:") != std::string_view::npos);
}

TEST_CASE("TtesterCorpusTest - EmptyStackRecoversAfterAFailure") {
    // ERROR1 clears the data stack (EMPTY-STACK) after reporting, so a
    // failing T{ ... }T does not corrupt whatever runs after it -- the same
    // property the whole sharded battery (core_suite_*.test.cpp) depends on
    // to keep reporting every failure in a shard rather than stopping at
    // the first.
    auto built = smd::forth::interpreter::build_session<256, 160, 512, 512>(
        std::string_view{SMD_FORTH_TTESTER_SOURCE
                         "T{ 2 2 + -> 5 }T "
                         "T{ 3 4 + -> 7 }T"});
    REQUIRE(built.has_value());
    auto const &out = built.value().output;
    std::string_view text{out.begin(), static_cast<std::size_t>(out.size())};
    CHECK(text.find("INCORRECT RESULT:") != std::string_view::npos);
    // Confirm the *second*, passing assertion ran too (the whole point of
    // recovering to an empty stack): if EMPTY-STACK had not cleared the
    // leftover 2 from the failed test, the depth-based bookkeeping in a
    // following T{ ... -> ... }T would misreport a second failure it did
    // not actually have.
    auto const second_count =
        static_cast<std::size_t>(std::count(text.begin(), text.end(), ':'));
    CHECK(second_count == 1); // Exactly one "INCORRECT RESULT:" -- not two.
}
