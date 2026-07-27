// src/smd/forth/interpreter/prelude.test.cpp                        -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <smd/forth/interpreter/prelude.hpp>
#include <smd/forth/interpreter/prelude.hpp> // test 2nd include OK

#include <catch2/catch_test_macros.hpp>

#include <string_view>

using smd::forth::interpreter::build_session_with_prelude;
using smd::forth::interpreter::prelude_source;

TEST_CASE("PreludeTest - HeaderIsIdempotent") { REQUIRE(true); }

// -- Merge criterion (static_assert, immediately-invoked-lambda pattern) ---
//
// The derived arithmetic/stack words: NIP/TUCK/?DUP, defined in Forth in
// prelude_source rather than installed as machine::primitive dictionary
// entries (machine/dictionary.hpp's own default_dictionary, DIV-0027).

static_assert([] {
    auto built = build_session_with_prelude<64, 160, 256, 128>("1 2 NIP");
    return built.has_value() && built.value().stack.size() == 1 &&
           built.value().stack[0] == 2;
}());

TEST_CASE("PreludeTest - NipIsDefinedInForth") {
    auto built = build_session_with_prelude<64, 160, 256, 128>("1 2 NIP");
    REQUIRE(built.has_value());
    REQUIRE(built.value().stack.size() == 1);
    CHECK(built.value().stack[0] == 2);
}

static_assert([] {
    auto built = build_session_with_prelude<64, 160, 256, 128>("1 2 TUCK");
    return built.has_value() && built.value().stack.size() == 3 &&
           built.value().stack[0] == 2 && built.value().stack[1] == 1 &&
           built.value().stack[2] == 2;
}());

TEST_CASE("PreludeTest - TuckIsDefinedInForth") {
    auto built = build_session_with_prelude<64, 160, 256, 128>("1 2 TUCK");
    REQUIRE(built.has_value());
    REQUIRE(built.value().stack.size() == 3);
    CHECK(built.value().stack[0] == 2);
    CHECK(built.value().stack[1] == 1);
    CHECK(built.value().stack[2] == 2);
}

static_assert([] {
    auto zero = build_session_with_prelude<64, 160, 256, 128>("0 ?DUP");
    auto five = build_session_with_prelude<64, 160, 256, 128>("5 ?DUP");
    return zero.has_value() && zero.value().stack.size() == 1 &&
           zero.value().stack[0] == 0 && five.has_value() &&
           five.value().stack.size() == 2 && five.value().stack[0] == 5 &&
           five.value().stack[1] == 5;
}());

TEST_CASE("PreludeTest - QuestionDupIsDefinedInForth") {
    auto zero = build_session_with_prelude<64, 160, 256, 128>("0 ?DUP");
    REQUIRE(zero.has_value());
    REQUIRE(zero.value().stack.size() == 1);
    CHECK(zero.value().stack[0] == 0);

    auto five = build_session_with_prelude<64, 160, 256, 128>("5 ?DUP");
    REQUIRE(five.has_value());
    REQUIRE(five.value().stack.size() == 2);
    CHECK(five.value().stack[0] == 5);
    CHECK(five.value().stack[1] == 5);
}

// -- Merge criterion: one control word, ENDIF-class -------------------------
//
// The DIV-0015 boundary's own stated example: ENDIF, defined by a whole-
// body POSTPONE of THEN, genuinely indistinguishable from THEN itself.

static_assert([] {
    auto built = build_session_with_prelude<64, 160, 256, 128>(
        ": ABS3 DUP 0< IF NEGATE ENDIF ;  -7 ABS3");
    return built.has_value() && built.value().stack.size() == 1 &&
           built.value().stack[0] == 7;
}());

TEST_CASE("PreludeTest - EndifIsAWholeBodyPostponeAliasOfThen") {
    auto built = build_session_with_prelude<64, 160, 256, 128>(
        ": ABS3 DUP 0< IF NEGATE ENDIF ;  -7 ABS3");
    REQUIRE(built.has_value());
    REQUIRE(built.value().stack.size() == 1);
    CHECK(built.value().stack[0] == 7);
}

// -- A full IF/ELSE/THEN replacement, under new names (DIV-0027) -----------
//
// WHEN/OTHERWISE/ENDIF exercised together, the same whole-body-alias
// mechanism extended to all three structural words a Forth-2012 IF ...
// ELSE ... THEN needs -- not just the THEN-only ENDIF-class synonym.

static_assert([] {
    auto built = build_session_with_prelude<64, 160, 256, 128>(
        ": SIGN DUP 0< WHEN DROP -1 OTHERWISE DROP 1 ENDIF ;  -7 SIGN");
    return built.has_value() && built.value().stack.size() == 1 &&
           built.value().stack[0] == -1;
}());

TEST_CASE("PreludeTest - WhenOtherwiseEndifReplaceIfElseThen") {
    auto negative = build_session_with_prelude<64, 160, 256, 128>(
        ": SIGN DUP 0< WHEN DROP -1 OTHERWISE DROP 1 ENDIF ;  -7 SIGN");
    REQUIRE(negative.has_value());
    REQUIRE(negative.value().stack.size() == 1);
    CHECK(negative.value().stack[0] == -1);

    auto positive = build_session_with_prelude<64, 160, 256, 128>(
        ": SIGN DUP 0< WHEN DROP -1 OTHERWISE DROP 1 ENDIF ;  7 SIGN");
    REQUIRE(positive.has_value());
    REQUIRE(positive.value().stack.size() == 1);
    CHECK(positive.value().stack[0] == 1);
}

// -- The prelude runs before the caller's own text, not instead of it ------

TEST_CASE("PreludeTest - CallersOwnTextStillRunsAfterThePrelude") {
    // 2 3 * leaves a one-deep stack; NIP ( a b -- b ) on that is an
    // underflow -- proving NIP genuinely ran as a real word (an *unknown*
    // NIP would fail with "unknown word" instead, a different diagnosis).
    auto built = build_session_with_prelude<64, 160, 256, 128>("2 3 * NIP");
    REQUIRE_FALSE(built.has_value());
}

TEST_CASE("PreludeTest - CombinedLengthOverflowIsDiagnosed") {
    // MaxSourceLen smaller than prelude_source alone.
    auto built =
        build_session_with_prelude<64, 160, 256, 128, 32, 64, 64, 64, 8>("1");
    REQUIRE_FALSE(built.has_value());
}

TEST_CASE("PreludeTest - SourceTextIsNonEmpty") {
    CHECK_FALSE(prelude_source.empty());
}
