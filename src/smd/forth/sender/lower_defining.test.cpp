// src/smd/forth/sender/lower_defining.test.cpp                      -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <smd/forth/sender/run_and_compare.hpp>

#include <catch2/catch_test_macros.hpp>

// Step F33 (docs/forth-plan-2.md), D24: two shapes not named directly by
// this step's own merge criteria, but exercised here as a regression guard
// specifically because both are exactly the kind of shape that has already
// broken this component once during development (DIV-0025's own block-
// recovery fix, and the catch_mark-inline-in-a-block fix it enabled): a
// defining word (`CREATE ... DOES>`, F28) that reaches `word_sender::run`'s
// own `does_enter` terminator case, and a nested `CATCH` (one handler's own
// protected xt itself establishing another handler), which stresses
// `handler_depth()` save/restore nesting through two levels of the same
// `then`/`upon_error` composition.

TEST_CASE("LowerDefiningTest - DoesEnterLowersNativelyAndAgreesWithTheVm") {
    // Not routed through sender::testing::compile_and_run_both: that
    // helper's own fresh, args-seeded forth_state is the right model for a
    // plain colon word, but wrong here. "42 CONST2 FOO" runs `CREATE`
    // followed by `,` *during* interpret() (compile time), storing `42` in
    // the compile-time forth_state's own data space; a later run of
    // `USEFOO` (whose own compiled body is `push <FOO's own address>` then
    // `call <DOES>'s own does_entry>`, which fetches from that exact
    // address) depends on that stored value still being there, not merely
    // on the data space being *sized* correctly (D15's own "session image"
    // is exactly this: the data space's own content, not only its extent,
    // is part of what a session carries forward). So both runs below start
    // from a *copy* of the same already-populated forth_state instead.
    using namespace smd::forth;
    machine::forth_state<64, 64, 1024, 256> defst{
        ": CONST2 CREATE , DOES> @ ; 42 CONST2 FOO : USEFOO FOO ;"};
    auto dict = machine::default_dictionary<>();
    interpreter::compile_buffer<> buf;
    auto compiled = interpreter::interpret(defst, dict, buf);
    REQUIRE(compiled.has_value());
    auto const *entry = dict.lookup("USEFOO");
    REQUIRE(entry != nullptr);
    auto const *cw = std::get_if<machine::compiled_colon_word>(&entry->binding);
    REQUIRE(cw != nullptr);

    auto vm_state = defst;
    int vm_fuel = 1000;
    auto vm_status =
        interpreter::call_word(buf, vm_state, cw->entry_point, vm_fuel, &dict);

    auto sender_state = defst;
    auto sender_status = sender::run_from_via_senders(
        buf.program(), sender_state, cw->entry_point, 1000, &dict);

    REQUIRE(vm_status.has_value());
    REQUIRE(sender_status.has_value());
    CHECK(sender::testing::states_agree(vm_state, sender_state));
    REQUIRE(sender_state.data().depth() == 1);
    CHECK(sender_state.data().peek().value() == 42);
}

TEST_CASE("LowerDefiningTest - NestedCatchLowersNativelyAndAgreesWithTheVm") {
    using namespace smd::forth;
    auto result = sender::testing::compile_and_run_both<64, 64, 1024, 256>(
        ": INNER 1 THROW ; "
        ": OUTER ['] INNER CATCH DUP IF DROP 2 THROW ELSE DROP THEN ; "
        ": OUTERTRY ['] OUTER CATCH ;",
        "OUTERTRY");
    REQUIRE(result.vm_status.has_value());
    REQUIRE(result.sender_status.has_value());
    CHECK(sender::testing::states_agree(result.vm_state, result.sender_state));
    REQUIRE(result.sender_state.data().depth() == 1);
    CHECK(result.sender_state.data().peek().value() == 2);
}
