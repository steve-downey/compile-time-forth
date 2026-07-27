// src/smd/forth/sender/lower_loops1.test.cpp                        -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <smd/forth/sender/run_and_compare.hpp>

#include <smd/forth/interpreter/control_flow_corpus.hpp>

#include <catch2/catch_test_macros.hpp>

// Step F33 (docs/forth-plan-2.md), D24: native `BEGIN ... UNTIL` and
// `BEGIN ... WHILE ... REPEAT` lowering -- both `BEGIN` forms this step's
// own merge criteria name explicitly, "loops as repeat-until composition"
// via word_sender::run's own trampoline loop.

TEST_CASE("LowerLoopsTest - CountdownLowersNativelyAndAgreesWithTheVm") {
    using namespace smd::forth;
    auto result = sender::testing::compile_and_run_both<64, 64, 1024, 256>(
        interpreter::corpus::countdown_program, "COUNTDOWN",
        {machine::cell{3}});
    REQUIRE(result.vm_status.has_value());
    REQUIRE(result.sender_status.has_value());
    CHECK(sender::testing::states_agree(result.vm_state, result.sender_state));
    CHECK(result.sender_state.data().depth() == 0);
    std::string_view out(result.sender_state.output().begin(),
                         result.sender_state.output().size());
    CHECK(out == "3 2 1 ");
}

TEST_CASE("LowerLoopsTest - Upto3LowersNativelyAndAgreesWithTheVm") {
    using namespace smd::forth;
    auto result = sender::testing::compile_and_run_both<64, 64, 1024, 256>(
        interpreter::corpus::upto3_program, "UPTO3");
    REQUIRE(result.vm_status.has_value());
    REQUIRE(result.sender_status.has_value());
    CHECK(sender::testing::states_agree(result.vm_state, result.sender_state));
    REQUIRE(result.sender_state.data().depth() == 1);
    CHECK(result.sender_state.data().peek().value() == 3);
}
