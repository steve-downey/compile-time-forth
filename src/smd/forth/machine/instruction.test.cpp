// src/smd/forth/machine/instruction.test.cpp                       -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <smd/forth/machine/instruction.hpp>
#include <smd/forth/machine/instruction.hpp> // test 2nd include OK

#include <catch2/catch_test_macros.hpp>

using smd::forth::machine::compiled_program;
using smd::forth::machine::instr;
using smd::forth::machine::op;

TEST_CASE("InstructionTest - HeaderIsIdempotent") { REQUIRE(true); }

namespace {

static_assert([] {
    instr i{.code = op::push, .operand = 42};
    return i.code == op::push && i.operand == 42;
}());

static_assert([] {
    compiled_program<> p{};
    return p.code.size() == 0 && p.entry_points.size() == 0 &&
           p.program_entry == 0 && p.data_space_size == 0 &&
           p.required_stack_depth == -1 && p.required_return_depth == -1;
}());

// compiled_program is usable at runtime too: pushing instructions and
// entries works exactly like it does at compile time.
static_assert([] {
    compiled_program<8, 4> p{};
    p.code.push_back(instr{.code = op::halt, .operand = 0});
    p.entry_points.push_back(-1);
    return p.code.size() == 1 && p.code[0].code == op::halt &&
           p.entry_points.size() == 1;
}());

} // namespace

TEST_CASE("InstructionTest - PlainAggregateConstruction") {
    smd::forth::machine::instr i{.code = op::branch0, .operand = 7};
    CHECK(i.code == op::branch0);
    CHECK(i.operand == 7);
}

TEST_CASE("InstructionTest - CompiledProgramDefaultsAreEmpty") {
    compiled_program<> p{};
    CHECK(p.code.size() == 0);
    CHECK(p.entry_points.size() == 0);
    CHECK(p.program_entry == 0);
    CHECK(p.data_space_size == 0);
    CHECK(p.required_stack_depth == -1);
    CHECK(p.required_return_depth == -1);
}
