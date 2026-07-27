// src/smd/forth/sender/lower.test.cpp                               -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <smd/forth/sender/lower.hpp>
#include <smd/forth/sender/lower.hpp> // test 2nd include OK

#include <catch2/catch_test_macros.hpp>

// Step F33 (docs/forth-plan-2.md), D24: this shard covers only what does not
// require instantiating the sender machinery itself (word_sender, drive,
// run_from_via_senders) -- deliberately, to keep this the cheapest shard in
// the component (DIV-0026's own measured finding: instantiating the sender
// machinery costs hundreds of MB of peak compiler memory even after the
// type-erasure fix, per distinct forth_state/capacity combination). Every
// other shard (lower_*.test.cpp) exercises an actual run and is measured
// separately; see docs/compiler_architecture.org's own Phase 16 section for
// the full per-shard table.

TEST_CASE("LowerTest - HeaderIsIdempotent") { REQUIRE(true); }

// -- word_body_end -----------------------------------------------------

static_assert([] {
    // No later entry_points value: bound is program.code.size().
    smd::forth::machine::compiled_program<32, 8> program{};
    program.code.push_back(
        smd::forth::machine::instr{.code = smd::forth::machine::op::halt});
    program.code.push_back(
        smd::forth::machine::instr{.code = smd::forth::machine::op::ret});
    program.entry_points.push_back(1);
    return smd::forth::sender::word_body_end(program, 1) ==
           program.code.size();
}());

static_assert([] {
    // A later colon word's own entry point bounds this one.
    smd::forth::machine::compiled_program<32, 8> program{};
    for (int i = 0; i < 6; ++i) {
        program.code.push_back(
            smd::forth::machine::instr{.code = smd::forth::machine::op::ret});
    }
    program.entry_points.push_back(1); // this word
    program.entry_points.push_back(4); // a later one
    return smd::forth::sender::word_body_end(program, 1) == 4;
}());

// -- word_uses_return_stack_data -----------------------------------------

static_assert([] {
    using smd::forth::machine::instr;
    using smd::forth::machine::op;
    using smd::forth::machine::primitive;
    smd::forth::machine::compiled_program<32, 8> program{};
    program.code.push_back(instr{.code = op::prim,
                                 .operand = static_cast<smd::forth::machine::cell>(
                                     primitive::dup)});
    program.code.push_back(instr{.code = op::ret});
    return !smd::forth::sender::word_uses_return_stack_data(program, 0, 2);
}());

static_assert([] {
    using smd::forth::machine::instr;
    using smd::forth::machine::op;
    using smd::forth::machine::primitive;
    smd::forth::machine::compiled_program<32, 8> program{};
    program.code.push_back(instr{.code = op::prim,
                                 .operand = static_cast<smd::forth::machine::cell>(
                                     primitive::to_r)});
    program.code.push_back(instr{.code = op::ret});
    return smd::forth::sender::word_uses_return_stack_data(program, 0, 2);
}());

static_assert([] {
    using smd::forth::machine::instr;
    using smd::forth::machine::op;
    using smd::forth::machine::primitive;
    smd::forth::machine::compiled_program<32, 8> program{};
    program.code.push_back(instr{.code = op::prim,
                                 .operand = static_cast<smd::forth::machine::cell>(
                                     primitive::r_fetch)});
    program.code.push_back(instr{.code = op::ret});
    return smd::forth::sender::word_uses_return_stack_data(program, 0, 2);
}());

// -- to_status ------------------------------------------------------------

static_assert([] {
    using error_type = smd::forth::sender::control_error<8, 8, 8, 32>;
    auto const status = smd::forth::sender::to_status(
        error_type{.numbered = true, .n = smd::forth::machine::cell{-2}});
    return !status.has_value() &&
           status.error().where.offset == -2 &&
           std::string_view{status.error().message} ==
               "uncaught THROW (code in foundation::parse_error::"
               "where.offset)";
}());

static_assert([] {
    using error_type = smd::forth::sender::control_error<8, 8, 8, 32>;
    auto const status = smd::forth::sender::to_status(error_type{
        .numbered = false,
        .diag = smd::forth::foundation::parse_error{
            smd::forth::foundation::source_pos{}, "stack underflow"}});
    return !status.has_value() &&
           std::string_view{status.error().message} == "stack underflow";
}());
