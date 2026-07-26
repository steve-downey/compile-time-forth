// src/smd/forth/interpreter/session.test.cpp                       -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <smd/forth/interpreter/session.hpp>
#include <smd/forth/interpreter/session.hpp> // test 2nd include OK

#include <smd/forth/machine/forth_state.hpp>

#include <catch2/catch_test_macros.hpp>

using smd::forth::interpreter::build_session;
using smd::forth::interpreter::call_defined_word;
using smd::forth::interpreter::seed_from_session;
using smd::forth::interpreter::session;

TEST_CASE("SessionTest - HeaderIsIdempotent") { REQUIRE(true); }

// -- Merge criterion (static_assert, immediately-invoked-lambda pattern) ---
//
// A session image round-trips: build at constexpr, run its top-level again
// at runtime from the same object.

namespace {

constexpr auto built_squared =
    build_session<64, 64, 256, 128>(": SQUARED DUP * ;");
static_assert(built_squared.has_value());

/// A session built once, at namespace-scope constexpr initialization --
/// trivially copyable (session.hpp's own doc comment), so this exact object
/// is usable, unmodified, from both a static_assert (compile time) and a
/// Catch2 TEST_CASE (ordinary runtime) below, mirroring F14's own
/// "compiled_program agrees with itself, compile time and runtime" proof
/// pattern (D14a), generalized to a whole session.
inline constexpr auto squared_session = built_squared.value();

} // namespace

static_assert([] {
    auto sess = squared_session; // call_defined_word takes a mutable session;
                                 // squared_session is constexpr, so copy it.
                                 // (It does NOT mutate program_entry --
                                 // call_word goes through machine::run_from
                                 // precisely so that it never has to.)
    smd::forth::machine::forth_state<64, 64, 256, 128> state{};
    if (!seed_from_session(sess, state).has_value()) {
        return false;
    }
    if (!state.data().push(4).has_value()) {
        return false;
    }
    auto r = call_defined_word(sess, state, "SQUARED");
    return r.has_value() && state.data().depth() == 1 &&
           state.data().peek().value() == 16;
}());

TEST_CASE("SessionTest - RoundTripsAtRuntimeFromTheSameConstexprObject") {
    auto sess = squared_session;
    smd::forth::machine::forth_state<64, 64, 256, 128> state{};
    REQUIRE(seed_from_session(sess, state).has_value());
    REQUIRE(state.data().push(4).has_value());
    auto r = call_defined_word(sess, state, "SQUARED");
    REQUIRE(r.has_value());
    REQUIRE(state.data().depth() == 1);
    CHECK(state.data().peek().value() == 16);
}

// -- Other session behavior --------------------------------------------

TEST_CASE("SessionTest - BuildSessionDiagnosesMalformedProgram") {
    auto built = build_session<64, 64, 256, 128>("1 2 + BOGUS");
    REQUIRE_FALSE(built.has_value());
}

TEST_CASE("SessionTest - DataSpaceHighWaterMarkTracksAllotment") {
    // ALLOT is an ordinary primitive (F16). This test drives it directly
    // rather than through VARIABLE/CREATE so what it asserts is the
    // high-water mark alone, independent of the defining words that also
    // advance it. interpret() *does* resolve VARIABLE/CREATE/CONSTANT as of
    // this step (DIV-0014); the memory-word merge criterion covers that path.
    auto built = build_session<64, 64, 256, 128>("3 ALLOT");
    REQUIRE(built.has_value());
    CHECK(built.value().data_space_high_water == 3);
}

TEST_CASE("SessionTest - OutputIsCaptured") {
    auto built = build_session<64, 64, 256, 128>("1 2 + .");
    REQUIRE(built.has_value());
    auto const &out = built.value().output;
    CHECK(std::string_view{out.begin(), static_cast<std::size_t>(out.size())} ==
          "3 ");
}

TEST_CASE("SessionTest - CallDefinedWordMissingNameIsDiagnosed") {
    auto built = build_session<64, 64, 256, 128>(": SQUARED DUP * ;");
    REQUIRE(built.has_value());
    auto sess = built.value();
    smd::forth::machine::forth_state<64, 64, 256, 128> state{};
    auto r = call_defined_word(sess, state, "NOSUCHWORD");
    REQUIRE_FALSE(r.has_value());
}
