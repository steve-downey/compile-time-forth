// src/smd/forth/sender/lower_catch_native.test.cpp                  -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <smd/forth/sender/run_and_compare.hpp>

#include <catch2/catch_test_macros.hpp>

// Step F33 (docs/forth-plan-2.md), D24/D11: native `CATCH`/`THROW` -- this
// step's own merge criterion that "CATCH/THROW demonstrably routes through
// set_error". `TRY`/`BOOM` is this step's own named native-side boundary
// program (docs/compiler_architecture.org's own Phase 16 section): neither
// word touches the return stack as data, so both lower natively, and
// `BOOM`'s own `42 THROW` completes its own recovered block's sender via
// the error channel (`sender::control_error`), adapted back to a value by
// `TRY`'s own `catch_mark` case (`then`/`upon_error`, D24: "CATCH as the
// error-to-value adapter"). Also covers an uncaught `THROW` (no `CATCH`
// anywhere) and a machine fault (division by zero) mapped to its standard
// THROW code and caught -- both from `interp.test.cpp`'s own existing
// CATCH corpus, replayed here through the sender backend.

TEST_CASE("LowerCatchNativeTest - TryBoomLowersNativelyAndAgreesWithTheVm") {
    using namespace smd::forth;
    auto result = sender::testing::compile_and_run_both<64, 64, 1024, 256>(
        ": BOOM 42 THROW ; : TRY ['] BOOM CATCH ;", "TRY");
    REQUIRE(result.vm_status.has_value());
    REQUIRE(result.sender_status.has_value());
    CHECK(sender::testing::states_agree(result.vm_state, result.sender_state));
    REQUIRE(result.sender_state.data().depth() == 1);
    CHECK(result.sender_state.data().peek().value() == 42);
}

TEST_CASE("LowerCatchNativeTest - UncaughtThrowMatchesTheVmsOwnDiagnosis") {
    using namespace smd::forth;
    auto result = sender::testing::compile_and_run_both<64, 64, 1024, 256>(
        ": BOOM2 5 THROW ;", "BOOM2");
    REQUIRE_FALSE(result.vm_status.has_value());
    REQUIRE_FALSE(result.sender_status.has_value());
    CHECK(std::string_view{result.vm_status.error().message} ==
         std::string_view{result.sender_status.error().message});
    CHECK(result.vm_status.error().where.offset ==
         result.sender_status.error().where.offset);
}

TEST_CASE("LowerCatchNativeTest - DivisionByZeroMappedAndCaughtAgreesWithTheVm") {
    using namespace smd::forth;
    auto result = sender::testing::compile_and_run_both<64, 64, 1024, 256>(
        ": DIVZERO 10 0 / ; : GUARDDIV ['] DIVZERO CATCH ;", "GUARDDIV");
    REQUIRE(result.vm_status.has_value());
    REQUIRE(result.sender_status.has_value());
    CHECK(sender::testing::states_agree(result.vm_state, result.sender_state));
    REQUIRE(result.sender_state.data().depth() == 1);
    CHECK(result.sender_state.data().peek().value() == -10);
}
