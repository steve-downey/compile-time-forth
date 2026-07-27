// src/smd/forth/sender/lower_exit.test.cpp                          -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <smd/forth/sender/run_and_compare.hpp>

#include <smd/forth/interpreter/control_flow_corpus.hpp>

#include <catch2/catch_test_macros.hpp>

// Step F33 (docs/forth-plan-2.md), D24: native `EXIT` lowering -- one of the
// constructs this step's own merge criteria name explicitly ("EXIT/ret/
// LEAVE as value completions"). `EXIT` compiles to a bare `op::ret`
// (`vm.hpp`'s own top comment), so this is the same value-channel
// completion @ref smd::forth::sender::word_sender::run gives a plain `;`,
// just reached early -- @ref smd::forth::interpreter::corpus::exit_boundary_program
// (`INNER`/`OUTER`) is F30's own regression case for "EXIT unwinds only to
// its own call boundary," replayed here through the sender backend.
// FIRST additionally exercises `UNLOOP EXIT` reached from inside a `DO`
// loop.

TEST_CASE("LowerExitTest - ExitBoundaryLowersNativelyAndAgreesWithTheVm") {
    using namespace smd::forth;
    auto result = sender::testing::compile_and_run_both<64, 64, 1024, 256>(
        interpreter::corpus::exit_boundary_program, "OUTER",
        {machine::cell{-3}});
    REQUIRE(result.vm_status.has_value());
    REQUIRE(result.sender_status.has_value());
    CHECK(sender::testing::states_agree(result.vm_state, result.sender_state));
    REQUIRE(result.sender_state.data().depth() == 2);
    CHECK(result.sender_state.data().peek(1).value() == -3);
    CHECK(result.sender_state.data().peek(0).value() == 99);
}

TEST_CASE("LowerExitTest - FirstLowersNativelyAndAgreesWithTheVm") {
    using namespace smd::forth;
    auto result = sender::testing::compile_and_run_both<64, 64, 1024, 256>(
        interpreter::corpus::first_program, "FIRST");
    REQUIRE(result.vm_status.has_value());
    REQUIRE(result.sender_status.has_value());
    CHECK(sender::testing::states_agree(result.vm_state, result.sender_state));
    REQUIRE(result.sender_state.data().depth() == 1);
    CHECK(result.sender_state.data().peek().value() == 3);
}
