// src/smd/forth/machine/foreign.test.cpp                            -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <smd/forth/machine/foreign.hpp>
#include <smd/forth/machine/foreign.hpp> // test 2nd include OK

#include <smd/forth/foundation/parse_error.hpp>
#include <smd/forth/foundation/source_pos.hpp>
#include <smd/forth/machine/cell.hpp>
#include <smd/forth/machine/dictionary.hpp>
#include <smd/forth/machine/forth_state.hpp>
#include <smd/forth/machine/instruction.hpp>
#include <smd/forth/machine/vm.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string_view>
#include <variant>

using smd::forth::machine::cell;
using smd::forth::machine::compiled_program;
using smd::forth::machine::declared_effect;
using smd::forth::machine::default_dictionary;
using smd::forth::machine::default_foreign_dictionary;
using smd::forth::machine::foreign_vocabulary;
using smd::forth::machine::foreign_word;
using smd::forth::machine::forth_state;
using smd::forth::machine::instr;
using smd::forth::machine::op;
using smd::forth::machine::status;

namespace {

using test_state = forth_state<64, 64, 1024, 256>;
using test_vocabulary = foreign_vocabulary<4, 64, 64, 1024, 256>;

/// ( a b -- a*b+1 ): an arbitrary but distinctive pure-stack foreign word.
constexpr auto mul_add_one(test_state &state) -> status {
    auto b = state.data().pop();
    if (!b.has_value()) {
        return b.error();
    }
    auto a = state.data().pop();
    if (!a.has_value()) {
        return a.error();
    }
    return state.data().push(a.value() * b.value() + 1);
}

/// ( -- ): writes straight into the output buffer, showing that a foreign
/// word reaches every part of the state a primitive does, not only the data
/// stack.
constexpr auto shout(test_state &state) -> status {
    return state.emit_char('!');
}

/// ( -- ): always fails, with a message `vm.hpp`'s own
/// `machine_fault_throw_code` maps to a standard Forth-2012 `THROW` code.
constexpr auto always_underflows(test_state &state) -> status {
    (void)state;
    return smd::forth::foundation::parse_error{
        smd::forth::foundation::source_pos{}, "stack underflow"};
}

/// A two-instruction program: call foreign word @c index, then halt.
constexpr auto foreign_then_halt(cell index) -> compiled_program<8, 8> {
    compiled_program<8, 8> program{};
    program.code.push_back(instr{.code = op::foreign, .operand = index});
    program.code.push_back(instr{.code = op::halt, .operand = cell{0}});
    return program;
}

} // namespace

TEST_CASE("ForeignTest - HeaderIsIdempotent") { REQUIRE(true); }

// -- Merge criteria (static_assert, immediately-invoked-lambda pattern) -----

// Step F34 (docs/forth-plan-2.md), D18: `op::foreign` dispatches through the
// vocabulary and mutates the very same forth_state every primitive does --
// at compile time, which is the whole point of the interface.
static_assert([] {
    test_vocabulary vocab{};
    auto index = vocab.add(&mul_add_one);
    if (!index.has_value()) {
        return false;
    }
    auto const program = foreign_then_halt(static_cast<cell>(index.value()));
    test_state st{};
    if (!st.data().push(6).has_value() || !st.data().push(7).has_value()) {
        return false;
    }
    auto dict = default_dictionary<>();
    auto r = run_from(program, st, 0, 1000, &dict, &vocab);
    return r.has_value() && st.data().depth() == 1 &&
           st.data().peek().value() == 43;
}());

// A foreign word reaching the output buffer, at compile time.
static_assert([] {
    test_vocabulary vocab{};
    auto index = vocab.add(&shout);
    if (!index.has_value()) {
        return false;
    }
    auto const program = foreign_then_halt(static_cast<cell>(index.value()));
    test_state st{};
    auto dict = default_dictionary<>();
    auto r = run_from(program, st, 0, 1000, &dict, &vocab);
    return r.has_value() && st.output().size() == 1 && st.output()[0] == '!';
}());

// D7: a program reaching `op::foreign` with no vocabulary is diagnosed, not
// undefined -- the exact shape `op::create_word` already has for a missing
// dictionary.
static_assert([] {
    auto const program = foreign_then_halt(cell{0});
    test_state st{};
    auto r = run_from(program, st, 0, 1000);
    return !r.has_value();
}());

// -- Runtime behavior ------------------------------------------------------

TEST_CASE("ForeignTest - VocabularyDiagnosesAFullRegistry") {
    foreign_vocabulary<2, 64, 64, 1024, 256> vocab{};
    REQUIRE(vocab.add(&mul_add_one).has_value());
    REQUIRE(vocab.add(&shout).has_value());
    auto overflow = vocab.add(&mul_add_one);
    REQUIRE_FALSE(overflow.has_value());
    CHECK(vocab.size() == 2);
}

TEST_CASE("ForeignTest - VocabularyDiagnosesAnOutOfRangeIndex") {
    test_vocabulary vocab{};
    test_state st{};
    CHECK_FALSE(vocab.call(0, st).has_value());
    REQUIRE(vocab.add(&shout).has_value());
    CHECK_FALSE(vocab.call(1, st).has_value());
    CHECK_FALSE(vocab.call(-1, st).has_value());
    CHECK(vocab.call(0, st).has_value());
}

TEST_CASE("ForeignTest - VocabularyDiagnosesANullImplementation") {
    // `add` cannot reject a null implementation (the comparison would be
    // evaluated during the constant evaluation that builds a `constexpr`
    // vocabulary, and a bare function-pointer comparison is not a constant
    // condition under UBSan -- DIV-0029); `call` diagnoses it instead,
    // through the same `foundation::result` channel an out-of-range index
    // uses. Runtime only, deliberately: the compile-time counterpart of this
    // case is a hard compile error (calling through a null function pointer
    // is not a constant expression), not a returned diagnosis, so there is
    // nothing here a `static_assert` could assert.
    test_vocabulary vocab{};
    auto index = vocab.add(nullptr);
    REQUIRE(index.has_value());
    CHECK(vocab.size() == 1);

    test_state st{};
    auto r = vocab.call(index.value(), st);
    REQUIRE_FALSE(r.has_value());
    CHECK(std::string_view{r.error().message} ==
          "foreign word has no implementation");
}

TEST_CASE("ForeignTest - WithForeignRegistersHeaderAndFunctionTogether") {
    auto bundle =
        default_foreign_dictionary<256, 32, 4, 64, 64, 1024, 256>()
            .with_foreign("MUL1", &mul_add_one, declared_effect(2, 1));
    REQUIRE(bundle.has_value());
    auto extended = bundle.value().with_foreign("SHOUT", &shout);
    REQUIRE(extended.has_value());

    CHECK(extended.value().foreigns.size() == 2);

    auto const *first = extended.value().words.lookup("MUL1");
    REQUIRE(first != nullptr);
    auto const *first_binding = std::get_if<foreign_word>(&first->binding);
    REQUIRE(first_binding != nullptr);
    CHECK(first_binding->index == 0);
    CHECK(first_binding->effect_known);
    CHECK(first_binding->effect_inputs == 2);
    CHECK(first_binding->effect_outputs == 1);

    auto const *second = extended.value().words.lookup("shout");
    REQUIRE(second != nullptr);
    auto const *second_binding = std::get_if<foreign_word>(&second->binding);
    REQUIRE(second_binding != nullptr);
    CHECK(second_binding->index == 1);
    CHECK_FALSE(second_binding->effect_known);
}

TEST_CASE("ForeignTest - AForeignFailureIsMappedLikeAPrimitiveFailure") {
    // With no handler active, the diagnosis comes back verbatim (DIV-0018's
    // own rule, which vm.hpp's `op::foreign` case reuses unchanged).
    test_vocabulary vocab{};
    auto index = vocab.add(&always_underflows);
    REQUIRE(index.has_value());
    auto const program = foreign_then_halt(static_cast<cell>(index.value()));
    test_state st{};
    auto dict = default_dictionary<>();
    auto r = run_from(program, st, 0, 1000, &dict, &vocab);
    REQUIRE_FALSE(r.has_value());
    CHECK(std::string_view{r.error().message} == "stack underflow");
}
