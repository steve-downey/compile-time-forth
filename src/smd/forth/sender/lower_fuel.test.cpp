// src/smd/forth/sender/lower_fuel.test.cpp                          -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <smd/forth/sender/run_and_compare.hpp>

#include <catch2/catch_test_macros.hpp>

// Step F33 (docs/forth-plan-2.md), D22/D24: fuel exhaustion via the stop
// channel -- D22's own "budgets are architecture" demonstrated at the
// sender level, not just the VM's. `word_sender::run`'s own trampoline loop
// checks the shared fuel counter once per pass (once per recovered block or
// per fallback-word step) and completes via `set_stopped` rather than
// looping forever; `run_from_via_senders`'s own top-level `upon_stopped`
// adapter renders that as the same "vm execution budget exhausted" message
// `vm.hpp`'s own `consume_vm_fuel` uses, so an exhausted run is diagnosed
// identically either way even though the two executors spend the budget at
// different granularities (per VM instruction vs. per sender step) and so
// are not expected to exhaust at the exact same fuel value -- this shard
// asserts only that a small fuel budget is diagnosed, not agreement with
// the VM on which specific fuel value it exhausts at.

TEST_CASE("LowerFuelTest - SpinExhaustsFuelViaTheStopChannel") {
    using namespace smd::forth;
    // Definition only, deliberately not interpreter::corpus::spin_program
    // verbatim: that constant's own trailing top-level "SPIN" would run via
    // interpret()'s own dispatch (through machine::run_from) at *compile*
    // time, with a generous default fuel -- exhausting that budget hanging
    // interpret() itself before this test ever reaches the sender backend
    // at all, rather than the sender-level fuel exhaustion this test means
    // to demonstrate. Only SPIN's own definition needs to compile here;
    // running it is this test's own job, against the small fuel below.
    machine::forth_state<64, 64, 1024, 256> defst{
        ": SPIN BEGIN FALSE UNTIL ;"};
    auto dict = machine::default_dictionary<>();
    interpreter::compile_buffer<> buf;
    auto compiled = interpreter::interpret(defst, dict, buf);
    REQUIRE(compiled.has_value());
    auto const *entry = dict.lookup("SPIN");
    REQUIRE(entry != nullptr);
    auto const *cw = std::get_if<machine::compiled_colon_word>(&entry->binding);
    REQUIRE(cw != nullptr);

    machine::forth_state<64, 64, 1024, 256> sst{};
    auto sr = sender::run_from_via_senders(buf.program(), sst, cw->entry_point,
                                           /*fuel=*/25, &dict);
    REQUIRE_FALSE(sr.has_value());
    CHECK(std::string_view{sr.error().message} ==
         "vm execution budget exhausted");
}
