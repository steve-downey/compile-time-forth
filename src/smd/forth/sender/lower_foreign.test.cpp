// src/smd/forth/sender/lower_foreign.test.cpp                       -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <smd/forth/sender/run_and_compare.hpp>

#include <smd/forth/foundation/parse_error.hpp>
#include <smd/forth/foundation/source_pos.hpp>
#include <smd/forth/machine/cell.hpp>
#include <smd/forth/machine/foreign.hpp>
#include <smd/forth/machine/forth_state.hpp>

#include <catch2/catch_test_macros.hpp>

// Step F34 (docs/forth-plan-2.md), D24/DIV-0030: the sender backend's own
// half of this step's merge criteria. D24's final paragraph -- confirmed at
// F33 and recorded again in DIV-0030 -- says `sync_wait` is not
// constexpr-capable, so "a static_assert computing through a constexpr
// foreign word via VM *and sender* backends" is delivered as a `static_assert`
// through the VM (interp.test.cpp, forth.test.cpp) plus this *runtime*
// equivalence check through the sender backend: the identical instruction
// range, run a second way, reaching the identical final state.
//
// One shard, one capacity combination, two programs -- the sharding
// discipline DIV-0026's own measured per-combination compile cost imposes on
// every test in this component.

namespace {

using ffi_state = smd::forth::machine::forth_state<64, 64, 1024, 256>;

/// ( a b -- gcd )
constexpr auto ffi_gcd(ffi_state &state) -> smd::forth::machine::status {
    auto b = state.data().pop();
    if (!b.has_value()) {
        return b.error();
    }
    auto a = state.data().pop();
    if (!a.has_value()) {
        return a.error();
    }
    smd::forth::machine::cell x = a.value() < 0 ? -a.value() : a.value();
    smd::forth::machine::cell y = b.value() < 0 ? -b.value() : b.value();
    while (y != 0) {
        smd::forth::machine::cell const t = x % y;
        x = y;
        y = t;
    }
    return state.data().push(x);
}

/// ( -- ) Fails with a diagnosis both executors map to `THROW -4`.
constexpr auto ffi_boom(ffi_state &state) -> smd::forth::machine::status {
    (void)state;
    return smd::forth::foundation::parse_error{
        smd::forth::foundation::source_pos{}, "stack underflow"};
}

constexpr auto ffi_vocabulary() {
    return smd::forth::machine::default_foreign_dictionary<256, 32, 4, 64, 64,
                                                           1024, 256>()
        .with_foreign("GCD", &ffi_gcd,
                      smd::forth::machine::declared_effect(2, 1))
        .value()
        .with_foreign("BOOM", &ffi_boom)
        .value();
}

} // namespace

TEST_CASE("LowerForeignTest - AForeignWordLowersNativelyAndAgreesWithTheVm") {
    using namespace smd::forth;
    constexpr auto vocab = ffi_vocabulary();
    auto result = sender::testing::compile_and_run_both<64, 64, 1024, 256>(
        ": G ( a b -- c ) GCD ;", "G", {machine::cell{270}, machine::cell{192}},
        100000, &vocab);
    REQUIRE(result.vm_status.has_value());
    REQUIRE(result.sender_status.has_value());
    CHECK(sender::testing::states_agree(result.vm_state, result.sender_state));
    REQUIRE(result.sender_state.data().depth() == 1);
    CHECK(result.sender_state.data().peek().value() == 6);
}

TEST_CASE("LowerForeignTest - CatchOverAForeignFailureAgreesWithTheVm") {
    // The foreign call sits inside a `CATCH`-protected region, so this also
    // covers the DIV-0028-category risk the F34 step-brief named: the
    // sender-level handler bookkeeping and the foreign call's own
    // machine-fault mapping have to compose to the same `-4` the VM
    // produces.
    using namespace smd::forth;
    constexpr auto vocab = ffi_vocabulary();
    auto result = sender::testing::compile_and_run_both<64, 64, 1024, 256>(
        ": SAFE ['] BOOM CATCH ;", "SAFE", {}, 100000, &vocab);
    REQUIRE(result.vm_status.has_value());
    REQUIRE(result.sender_status.has_value());
    CHECK(sender::testing::states_agree(result.vm_state, result.sender_state));
    REQUIRE(result.sender_state.data().depth() == 1);
    CHECK(result.sender_state.data().peek().value() == -4);
}
