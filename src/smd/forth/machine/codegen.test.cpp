// src/smd/forth/machine/codegen.test.cpp                            -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <smd/forth/machine/codegen.hpp>
#include <smd/forth/machine/codegen.hpp> // test 2nd include OK

#include <smd/forth/elaborator/elaborate.hpp>
#include <smd/forth/machine/instruction.hpp>
#include <smd/forth/reader/read_program.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string_view>

using smd::forth::elaborator::elaborate;
using smd::forth::machine::codegen;
using smd::forth::machine::op;
using smd::forth::reader::read_program;

TEST_CASE("CodegenTest - HeaderIsIdempotent") { REQUIRE(true); }

namespace {

// Small capacities: these tests never need the header's production defaults.
constexpr int test_max_code = 512;
constexpr int test_max_nodes = 256;
constexpr int test_max_body = 32;
constexpr int test_max_name = 16;
constexpr int test_max_parse_depth = 8;
constexpr int test_max_words = 64;
constexpr int test_max_data = 16;
constexpr int test_max_warnings = 8;

constexpr auto compile(std::string_view source) {
    auto tree = read_program<test_max_nodes, test_max_body, test_max_name,
                             test_max_parse_depth>(source);
    auto unit =
        elaborate<test_max_nodes, test_max_body, test_max_name, test_max_words,
                  test_max_data, test_max_warnings>(tree.value(), source);
    return codegen<test_max_code, test_max_nodes, test_max_body, test_max_name,
                   test_max_words, test_max_data, test_max_warnings>(
        unit.value());
}

} // namespace

// -- codegen produces a flat instruction array shaped as documented --------

// A trivial definition's body ends with `ret`; the top-level program (which
// only ever pushes a literal and calls it) ends with `halt`.
static_assert([] {
    auto result = compile(": SQUARED DUP * ;  4 SQUARED");
    if (!result.has_value()) {
        return false;
    }
    auto const &program = result.value();
    if (program.code.size() == 0) {
        return false;
    }
    auto const &last = program.code[program.code.size() - 1];
    return last.code == op::halt;
}());

// The colon word's entry point is recorded before the top-level program
// starts (colon definitions are laid out first), and a `call` to it carries
// that same instruction index as its operand.
static_assert([] {
    auto result = compile(": SQUARED DUP * ;  4 SQUARED");
    if (!result.has_value()) {
        return false;
    }
    auto const &program = result.value();
    // SQUARED is the first colon word defined after the 46 built-in
    // primitives, so its dictionary index is 46.
    int const squared_index = 46;
    int const entry = program.entry_points[squared_index];
    if (entry < 0 || entry >= program.program_entry) {
        return false;
    }
    // Somewhere in the top-level tail, a `call` targets that entry point.
    bool found_call = false;
    for (int i = program.program_entry; i < program.code.size(); ++i) {
        if (program.code[i].code == op::call &&
            program.code[i].operand == entry) {
            found_call = true;
        }
    }
    return found_call;
}());

// Primitive/variable/constant dictionary entries never get an entry point:
// they are inlined at each reference site instead of called.
static_assert([] {
    auto result = compile("1 2 +");
    if (!result.has_value()) {
        return false;
    }
    auto const &program = result.value();
    // `+` is the very first primitive in the default dictionary.
    return program.entry_points[0] == -1;
}());

// -- DO...LOOP is deferred to F17, mirroring F13's own choice ---------------

static_assert([] {
    auto result = compile("5 0 DO LOOP");
    return !result.has_value();
}());

TEST_CASE("CodegenTest - SquaredEndsWithHalt") {
    auto result = compile(": SQUARED DUP * ;  4 SQUARED");
    REQUIRE(result.has_value());
    auto const &program = result.value();
    REQUIRE(program.code.size() > 0);
    CHECK(program.code[program.code.size() - 1].code == op::halt);
}

TEST_CASE("CodegenTest - ColonWordGetsAnEntryPointCalledFromTopLevel") {
    auto result = compile(": SQUARED DUP * ;  4 SQUARED");
    REQUIRE(result.has_value());
    auto const &program = result.value();
    int const squared_index = 46;
    int const entry = program.entry_points[squared_index];
    REQUIRE(entry >= 0);
    REQUIRE(entry < program.program_entry);
    bool found_call = false;
    for (int i = program.program_entry; i < program.code.size(); ++i) {
        if (program.code[i].code == op::call &&
            program.code[i].operand == entry) {
            found_call = true;
        }
    }
    CHECK(found_call);
}

TEST_CASE("CodegenTest - PrimitivesHaveNoEntryPoint") {
    auto result = compile("1 2 +");
    REQUIRE(result.has_value());
    CHECK(result.value().entry_points[0] == -1);
}

TEST_CASE("CodegenTest - DoLoopIsDiagnosedNotImplemented") {
    auto result = compile("5 0 DO LOOP");
    CHECK_FALSE(result.has_value());
}

TEST_CASE("CodegenTest - IfWithoutElseBranchesPastThenBody") {
    auto result = compile(": ABS DUP 0< IF NEGATE THEN ;  -7 ABS");
    REQUIRE(result.has_value());
    auto const &program = result.value();
    // There must be at least one branch0 whose target is in-range and does
    // not point back at (or before) itself -- a forward branch, as IF's own
    // conditional skip always is.
    bool found_forward_branch0 = false;
    for (int i = 0; i < program.code.size(); ++i) {
        if (program.code[i].code == op::branch0 &&
            program.code[i].operand > i) {
            found_forward_branch0 = true;
        }
    }
    CHECK(found_forward_branch0);
}
