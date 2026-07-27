// src/smd/forth/sender/lower_catch_fallback.test.cpp                -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <smd/forth/sender/run_and_compare.hpp>

#include <catch2/catch_test_macros.hpp>

// Step F33 (docs/forth-plan-2.md), D24: the fallback boundary. `DEEP`'s own
// body uses `>R`/`R>` (`sender::word_uses_return_stack_data`'s own
// conservative, whole-word trigger, this component's own top comment in
// `lower.hpp`), so it falls back to `sender::run_word_via_vm` for its
// *whole* body; `GUARD`'s own `CATCH` around it does not fall back (`GUARD`
// itself touches neither), so this program exercises a genuine mixed case:
// a natively-lowered `CATCH` whose own protected xt runs via the VM-in-a-
// sender fallback. This is this step's own named fallback-side boundary
// program (docs/compiler_architecture.org's own Phase 16 section pairs it
// with `TRY`/`BOOM`, the native-side one, `lower_catch_native.test.cpp`).
// Getting this case right needed two fixes beyond the first working draft
// of `run_word_via_vm` -- see DIV-0027 for the full record (a manufactured
// return address, and hiding the enclosing sender-level `handler_depth()`
// for the duration of the fallback call, both confirmed necessary by this
// exact program failing without them).
//
// Source reused verbatim from `interpreter::interp.test.cpp`'s own existing
// `CatchUnwindsThroughToRDoLoopAndCallFrames`-shaped corpus (not imported
// directly to avoid a new cross-directory include for one string; kept
// textually identical here on purpose).

TEST_CASE("LowerCatchFallbackTest - GuardDeepFallsBackAndAgreesWithTheVm") {
    using namespace smd::forth;
    auto result = sender::testing::compile_and_run_both<64, 64, 1024, 256>(
        ": DEEP 42 >R 10 0 DO 5 THROW LOOP R> DROP ; "
        ": GUARD ['] DEEP CATCH ;",
        "GUARD");
    REQUIRE(result.vm_status.has_value());
    REQUIRE(result.sender_status.has_value());
    CHECK(sender::testing::states_agree(result.vm_state, result.sender_state));
    REQUIRE(result.sender_state.data().depth() == 1);
    CHECK(result.sender_state.data().peek().value() == 5);
}
