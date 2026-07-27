// src/smd/forth/sender/lower_loops2.test.cpp                        -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <smd/forth/sender/run_and_compare.hpp>

#include <smd/forth/interpreter/control_flow_corpus.hpp>

#include <catch2/catch_test_macros.hpp>

// Step F33 (docs/forth-plan-2.md), D24: native `DO ... LOOP` lowering with
// `I`/`J`, including a nested loop -- one of the constructs this step's own
// merge criteria name explicitly.

TEST_CASE("LowerLoops2Test - SumtoLowersNativelyAndAgreesWithTheVm") {
    using namespace smd::forth;
    auto result = sender::testing::compile_and_run_both<64, 64, 1024, 256>(
        interpreter::corpus::sumto_program, "SUMTO", {machine::cell{5}});
    REQUIRE(result.vm_status.has_value());
    REQUIRE(result.sender_status.has_value());
    CHECK(sender::testing::states_agree(result.vm_state, result.sender_state));
    REQUIRE(result.sender_state.data().depth() == 1);
    CHECK(result.sender_state.data().peek().value() == 15);
}

TEST_CASE("LowerLoops2Test - TensLowersNativelyAndAgreesWithTheVm") {
    using namespace smd::forth;
    auto result = sender::testing::compile_and_run_both<64, 64, 1024, 256>(
        interpreter::corpus::tens_program, "TENS");
    REQUIRE(result.vm_status.has_value());
    REQUIRE(result.sender_status.has_value());
    CHECK(sender::testing::states_agree(result.vm_state, result.sender_state));
    REQUIRE(result.sender_state.data().depth() == 1);
    CHECK(result.sender_state.data().peek().value() == 9);
}
