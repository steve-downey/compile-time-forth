// src/smd/forth/interpreter/interp.test.cpp                         -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <smd/forth/interpreter/interp.hpp>
#include <smd/forth/interpreter/interp.hpp> // test 2nd include OK

#include <smd/forth/machine/dictionary.hpp>
#include <smd/forth/machine/forth_state.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string_view>

using smd::forth::interpreter::forth_state;
using smd::forth::interpreter::interpret;
using smd::forth::machine::default_dictionary;

namespace {

/// Views a state's accumulated output as a @c std::string_view, the same
/// convenience @ref smd::forth::reader::forth_chars.test.cpp's own
/// @c view_of provides for scanned tokens.
template <int MaxDepth, int MaxRDepth, int MaxData, int MaxOut, int MaxName>
constexpr auto
output_of(forth_state<MaxDepth, MaxRDepth, MaxData, MaxOut, MaxName> const &st)
    -> std::string_view {
    auto const &out = st.machine().output();
    return std::string_view{out.begin(), static_cast<std::size_t>(out.size())};
}

} // namespace

TEST_CASE("InterpTest - HeaderIsIdempotent") { REQUIRE(true); }

// -- Merge criteria (static_assert, immediately-invoked-lambda pattern) -----

// `1 2 + .` yields output "3 " and an empty stack.
static_assert([] {
    forth_state<64, 64, 1024, 256> st{"1 2 + ."};
    auto dict = default_dictionary<>();
    auto r = interpret(st, dict);
    return r.has_value() && output_of(st) == "3 " &&
           st.machine().data().depth() == 0;
}());

// BASE plumbed and tested directly: BASE defaults to 10 ...
static_assert([] {
    forth_state<64, 64, 1024, 256> st{};
    return st.base() == 10;
}());

// ... and HEX-style handling works once BASE is set to 16: `FF .` prints the
// decimal rendering of the hex value 0xFF.
static_assert([] {
    forth_state<64, 64, 1024, 256> st{"FF ."};
    st.set_base(16);
    auto dict = default_dictionary<>();
    auto r = interpret(st, dict);
    return r.has_value() && output_of(st) == "255 ";
}());

// STATE defaults to 0 (interpreting); F24 never leaves interpret state.
static_assert([] {
    forth_state<64, 64, 1024, 256> st{};
    return st.state() == 0;
}());

// Unknown word: a positioned diagnosis, at the unknown word's own start, not
// at the source's start or the previous token's end.
static_assert([] {
    forth_state<64, 64, 1024, 256> st{"1 2 FOO"};
    auto dict = default_dictionary<>();
    auto r = interpret(st, dict);
    return !r.has_value() && r.error().where.offset == 4;
}());

// Stack underflow reached through the interpreter is the exact same
// diagnosed error machine::apply_primitive itself already produces for the
// same misuse -- not a re-diagnosed or repositioned one.
static_assert([] {
    forth_state<64, 64, 1024, 256> st{"+"};
    auto dict = default_dictionary<>();
    auto r = interpret(st, dict);

    smd::forth::machine::forth_state<64, 64, 1024, 256> raw{};
    auto direct = smd::forth::machine::apply_primitive(
        smd::forth::machine::primitive::plus, raw);

    return !r.has_value() && !direct.has_value() && r.error() == direct.error();
}());

// The F5 number/word tests, carried here intact (D8): `-1` is a number,
// `1-` is a word.
static_assert([] {
    forth_state<64, 64, 1024, 256> st{"-1"};
    auto dict = default_dictionary<>();
    auto r = interpret(st, dict);
    return r.has_value() && st.machine().data().depth() == 1 &&
           st.machine().data().peek().value() == -1;
}());

static_assert([] {
    forth_state<64, 64, 1024, 256> st{"5 1-"};
    auto dict = default_dictionary<>();
    auto r = interpret(st, dict);
    return r.has_value() && st.machine().data().depth() == 1 &&
           st.machine().data().peek().value() == 4;
}());

// `\` and `( ... )` comments are consumed like any other intertoken space.
static_assert([] {
    forth_state<64, 64, 1024, 256> st{
        "1 2 + \\ a line comment\n( a paren comment ) ."};
    auto dict = default_dictionary<>();
    auto r = interpret(st, dict);
    return r.has_value() && output_of(st) == "3 ";
}());

// An interpretation that is only whitespace/comments is a clean, empty run.
static_assert([] {
    forth_state<64, 64, 1024, 256> st{"  \\ nothing here\n( just a comment ) "};
    auto dict = default_dictionary<>();
    auto r = interpret(st, dict);
    return r.has_value() && st.machine().data().depth() == 0 &&
           output_of(st).empty();
}());

// Redefinition shadows: the dictionary's own newest-first lookup means the
// interpreter sees whatever default_dictionary installed, unaffected by a
// caller redefining a name in a *separate* dictionary instance.
static_assert([] {
    smd::forth::machine::dictionary<8> dict;
    (void)dict.define_primitive("+", smd::forth::machine::primitive::minus);
    forth_state<64, 64, 1024, 256> st{"3 1 +"};
    auto r = interpret(st, dict);
    return r.has_value() && st.machine().data().depth() == 1 &&
           st.machine().data().peek().value() == 2; // "+" resolved to minus
}());

// -- Runtime-visible mirrors of the merge criteria, plus a few extras -------

TEST_CASE("InterpTest - SimpleArithmeticAndOutput") {
    forth_state<64, 64, 1024, 256> st{"1 2 + ."};
    auto dict = default_dictionary<>();
    auto r = interpret(st, dict);
    REQUIRE(r.has_value());
    CHECK(output_of(st) == "3 ");
    CHECK(st.machine().data().depth() == 0);
}

TEST_CASE("InterpTest - MultipleWordsAndDotS") {
    forth_state<64, 64, 1024, 256> st{"1 2 3 .s"};
    auto dict = default_dictionary<>();
    auto r = interpret(st, dict);
    REQUIRE(r.has_value());
    CHECK(output_of(st) == "1 2 3 ");
    CHECK(st.machine().data().depth() == 3);
}

TEST_CASE("InterpTest - CaseInsensitiveWordLookup") {
    forth_state<64, 64, 1024, 256> st{"1 2 dup drop + ."};
    auto dict = default_dictionary<>();
    auto r = interpret(st, dict);
    REQUIRE(r.has_value());
    CHECK(output_of(st) == "3 ");
}

TEST_CASE("InterpTest - HexBaseHandling") {
    forth_state<64, 64, 1024, 256> st{"FF ."};
    st.set_base(16);
    auto dict = default_dictionary<>();
    auto r = interpret(st, dict);
    REQUIRE(r.has_value());
    CHECK(output_of(st) == "255 ");
}

TEST_CASE("InterpTest - NegativeHexNumber") {
    forth_state<64, 64, 1024, 256> st{"-A ."};
    st.set_base(16);
    auto dict = default_dictionary<>();
    auto r = interpret(st, dict);
    REQUIRE(r.has_value());
    CHECK(output_of(st) == "-10 ");
}

TEST_CASE("InterpTest - UnknownWordCarriesPosition") {
    forth_state<64, 64, 1024, 256> st{"1 2 FOO"};
    auto dict = default_dictionary<>();
    auto r = interpret(st, dict);
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().where.offset == 4);
}

TEST_CASE("InterpTest - UnderflowMatchesApplyPrimitiveDirectly") {
    forth_state<64, 64, 1024, 256> st{"+"};
    auto dict = default_dictionary<>();
    auto r = interpret(st, dict);
    REQUIRE_FALSE(r.has_value());

    smd::forth::machine::forth_state<64, 64, 1024, 256> raw{};
    auto direct = smd::forth::machine::apply_primitive(
        smd::forth::machine::primitive::plus, raw);
    REQUIRE_FALSE(direct.has_value());
    CHECK(r.error() == direct.error());
}

TEST_CASE("InterpTest - NegativeOneIsANumberOneMinusIsAWord") {
    {
        forth_state<64, 64, 1024, 256> st{"-1"};
        auto dict = default_dictionary<>();
        auto r = interpret(st, dict);
        REQUIRE(r.has_value());
        REQUIRE(st.machine().data().depth() == 1);
        CHECK(st.machine().data().peek().value() == -1);
    }
    {
        forth_state<64, 64, 1024, 256> st{"5 1-"};
        auto dict = default_dictionary<>();
        auto r = interpret(st, dict);
        REQUIRE(r.has_value());
        REQUIRE(st.machine().data().depth() == 1);
        CHECK(st.machine().data().peek().value() == 4);
    }
}

TEST_CASE("InterpTest - CommentsAreConsumedLikeIntertokenSpace") {
    forth_state<64, 64, 1024, 256> st{
        "1 2 + \\ a line comment\n( a paren comment ) ."};
    auto dict = default_dictionary<>();
    auto r = interpret(st, dict);
    REQUIRE(r.has_value());
    CHECK(output_of(st) == "3 ");
}

TEST_CASE("InterpTest - FuelExhaustionIsDiagnosed") {
    forth_state<64, 64, 1024, 256> st{"1 1 1 1 1 1 1 1 1 1"};
    auto dict = default_dictionary<>();
    auto r = interpret(st, dict, /*fuel=*/3);
    REQUIRE_FALSE(r.has_value());
    CHECK(std::string_view{r.error().message} ==
          "interpreter execution budget exhausted");
}
