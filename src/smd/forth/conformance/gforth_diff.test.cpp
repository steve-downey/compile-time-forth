// src/smd/forth/conformance/gforth_diff.test.cpp                     -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <smd/forth/conformance/gforth_diff.hpp>
#include <smd/forth/conformance/gforth_diff.hpp> // test 2nd include OK

#include <smd/forth/interpreter/session.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string_view>
#include <vector>

TEST_CASE("GforthDiffTest - HeaderIsIdempotent") { REQUIRE(true); }

// -- parse_dot_s_output: pure parsing, no subprocess -------------------------

using smd::forth::conformance::gforth_cell;
using smd::forth::conformance::parse_dot_s_output;

TEST_CASE("GforthDiffTest - ParsesEmptyStack") {
    auto parsed = parse_dot_s_output("<0> ");
    REQUIRE(parsed.has_value());
    CHECK(parsed->empty());
}

TEST_CASE("GforthDiffTest - ParsesPositiveAndNegativeCells") {
    auto parsed = parse_dot_s_output("<3> 1 -2 3 ");
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->size() == 3);
    CHECK((*parsed)[0] == 1);
    CHECK((*parsed)[1] == -2);
    CHECK((*parsed)[2] == 3);
}

TEST_CASE("GforthDiffTest - IgnoresTextBeforeTheMarker") {
    auto parsed = parse_dot_s_output("some program output <2> 5 6 ");
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->size() == 2);
    CHECK((*parsed)[0] == 5);
    CHECK((*parsed)[1] == 6);
}

TEST_CASE("GforthDiffTest - RejectsTextWithNoMarker") {
    CHECK_FALSE(parse_dot_s_output("no marker here").has_value());
}

TEST_CASE("GforthDiffTest - RejectsTruncatedValueList") {
    // Claims 3 values but only 2 are present.
    CHECK_FALSE(parse_dot_s_output("<3> 1 2 ").has_value());
}

// -- Differential battery: this project's own session vs. real gforth ------
//
// D23: "a runtime harness feeds identical programs to the session image
// (runtime side) and to gforth and diffs stacks and output." Every program
// below is drawn from the covered-word list (docs/conformance-
// exclusions.md), deliberately excluding: D21's own address-unit words
// (DIV-0009 -- CELLS/CHARS are *expected* to disagree with byte-addressed
// gforth, so diffing them here would report a known characteristic as a
// fresh failure) and negative-operand `/`/`MOD` (DIV-0022 -- symmetric vs.
// floored division, also expected to disagree). Gated on `gforth` actually
// being reachable (`gforth_version()`); if it is not, this test reports
// that loudly (`WARN` + early return) rather than silently doing nothing,
// per this project's own "silent skipping is the failure mode to avoid."

namespace {

struct battery_case {
    std::string_view name;
    std::string_view program;
};

constexpr std::array<battery_case, 14> battery{{
    {"add", "1 2 +"},
    {"subtract", "5 3 -"},
    {"multiply", "6 7 *"},
    {"divide", "7 2 /"},
    {"mod", "7 2 MOD"},
    {"negate", "5 NEGATE"},
    {"abs", "-5 ABS"},
    {"min", "3 7 MIN"},
    {"max", "3 7 MAX"},
    {"dup", "1 DUP"},
    {"swap", "1 2 SWAP"},
    {"over", "1 2 OVER"},
    {"rot", "1 2 3 ROT"},
    {"do_loop", ": SUMTO 0 SWAP 1+ 0 DO I + LOOP ; 5 SUMTO"},
}};

} // namespace

TEST_CASE("GforthDiffTest - AgreesWithGforthOnTheCoveredWordBattery") {
    auto const version = smd::forth::conformance::gforth_version();
    if (!version.has_value()) {
        WARN("gforth not reachable on PATH -- differential battery not run "
             "(this is reported, not silently skipped)");
        return;
    }
    INFO("gforth --version: " << *version);

    for (auto const &c : battery) {
        INFO("case: " << c.name << " (" << c.program << ")");

        auto gforth_run = smd::forth::conformance::run_via_gforth(c.program);
        REQUIRE(gforth_run.has_value());

        auto session_built =
            smd::forth::interpreter::build_session<4096, 256, 1024, 4096>(
                c.program);
        REQUIRE(session_built.has_value());
        auto const &sess = session_built.value();

        REQUIRE(static_cast<std::size_t>(sess.stack.size()) ==
                gforth_run->stack.size());
        for (int i = 0; i < sess.stack.size(); ++i) {
            CHECK(sess.stack[i] ==
                  gforth_run->stack[static_cast<std::size_t>(i)]);
        }

        std::string_view const session_output{
            sess.output.begin(), static_cast<std::size_t>(sess.output.size())};
        CHECK(session_output == gforth_run->output);
    }
}
