// src/smd/forth/sender/lower_if.test.cpp                            -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <smd/forth/sender/run_and_compare.hpp>

#include <smd/forth/interpreter/control_flow_corpus.hpp>

#include <catch2/catch_test_macros.hpp>

// Step F33 (docs/forth-plan-2.md), D24: native `IF` lowering, one of the
// constructs this step's own merge criteria name explicitly. Kept in its
// own shard (D22, "budgets are architecture"; DIV-0026's own measured
// finding) rather than folded into a larger one.

TEST_CASE("LowerIfTest - AbsLowersNativelyAndAgreesWithTheVm") {
    using namespace smd::forth;
    auto result = sender::testing::compile_and_run_both<64, 64, 1024, 256>(
        interpreter::corpus::abs_program, "ABS", {machine::cell{-7}});
    REQUIRE(result.vm_status.has_value());
    REQUIRE(result.sender_status.has_value());
    CHECK(sender::testing::states_agree(result.vm_state, result.sender_state));
    REQUIRE(result.sender_state.data().depth() == 1);
    CHECK(result.sender_state.data().peek().value() == 7);
}
