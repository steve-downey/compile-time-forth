// src/smd/forth/interpreter/interp.test.cpp                         -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <smd/forth/interpreter/interp.hpp>
#include <smd/forth/interpreter/interp.hpp> // test 2nd include OK

#include <smd/forth/interpreter/compilebuf.hpp>
#include <smd/forth/machine/dictionary.hpp>
#include <smd/forth/machine/forth_state.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string_view>

using smd::forth::interpreter::compile_buffer;
using smd::forth::interpreter::forth_state;
using smd::forth::interpreter::interpret;
using smd::forth::machine::default_dictionary;

namespace {

/// Views a state's accumulated output as a @c std::string_view, the same
/// convenience @ref smd::forth::parser::forth_chars.test.cpp's own
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
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
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
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    return r.has_value() && output_of(st) == "255 ";
}());

// STATE defaults to 0 (interpreting).
static_assert([] {
    forth_state<64, 64, 1024, 256> st{};
    return st.state() == 0;
}());

// Unknown word: a positioned diagnosis, at the unknown word's own start, not
// at the source's start or the previous token's end.
static_assert([] {
    forth_state<64, 64, 1024, 256> st{"1 2 FOO"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    return !r.has_value() && r.error().where.offset == 4;
}());

// Stack underflow reached through the interpreter is the exact same
// diagnosed error machine::apply_primitive itself already produces for the
// same misuse -- not a re-diagnosed or repositioned one.
static_assert([] {
    forth_state<64, 64, 1024, 256> st{"+"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);

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
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    return r.has_value() && st.machine().data().depth() == 1 &&
           st.machine().data().peek().value() == -1;
}());

static_assert([] {
    forth_state<64, 64, 1024, 256> st{"5 1-"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    return r.has_value() && st.machine().data().depth() == 1 &&
           st.machine().data().peek().value() == 4;
}());

// `\` and `( ... )` comments are consumed like any other intertoken space.
static_assert([] {
    forth_state<64, 64, 1024, 256> st{
        "1 2 + \\ a line comment\n( a paren comment ) ."};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    return r.has_value() && output_of(st) == "3 ";
}());

// An interpretation that is only whitespace/comments is a clean, empty run.
static_assert([] {
    forth_state<64, 64, 1024, 256> st{"  \\ nothing here\n( just a comment ) "};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
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
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    return r.has_value() && st.machine().data().depth() == 1 &&
           st.machine().data().peek().value() == 2; // "+" resolved to minus
}());

// -- Step F25 merge criteria: the colon compiler ----------------------------

// `: SQUARED DUP * ;  4 SQUARED` leaves `[16]` via the interpreter.
static_assert([] {
    forth_state<64, 64, 1024, 256> st{": SQUARED DUP * ;  4 SQUARED"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    return r.has_value() && st.state() == 0 &&
           st.machine().data().depth() == 1 &&
           st.machine().data().peek().value() == 16;
}());

// `: QUAD SQUARED SQUARED ; 3 QUAD` leaves `[81]` -- a colon word calling
// another colon word, both compiled and both interpreted afterward.
static_assert([] {
    forth_state<64, 64, 1024, 256> st{
        ": SQUARED DUP * ; : QUAD SQUARED SQUARED ; 3 QUAD"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    return r.has_value() && st.machine().data().depth() == 1 &&
           st.machine().data().peek().value() == 81;
}());

// -- Step F26 merge criteria: VARIABLE/CONSTANT/CREATE ----------------------
//
// F16's own combined merge criterion (docs/compiler_architecture.org's
// Phase 4 section, now history), run through the interpreter instead of the
// R1 pipeline: `VARIABLE X  5 X !  X @ 3 + X !  X @` leaves `[8]`,
// `7 CONSTANT LUCKY  LUCKY LUCKY +` leaves `[14]`, both continuing on the
// same data stack (matching forth.test.cpp's own ForthTest -
// MemoryWordsMergeCriterion, through the public API).

static_assert([] {
    forth_state<64, 64, 1024, 256> st{"VARIABLE X  5 X !  X @ 3 + X !  X @ "
                                      "7 CONSTANT LUCKY  LUCKY LUCKY +"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    return r.has_value() && st.machine().data().depth() == 2 &&
           st.machine().data().peek(1).value() == 8 &&
           st.machine().data().peek(0).value() == 14;
}());

TEST_CASE("InterpTest - MemoryWordsMergeCriterion") {
    forth_state<64, 64, 1024, 256> st{"VARIABLE X  5 X !  X @ 3 + X !  X @ "
                                      "7 CONSTANT LUCKY  LUCKY LUCKY +"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    REQUIRE(r.has_value());
    REQUIRE(st.machine().data().depth() == 2);
    CHECK(st.machine().data().peek(1).value() == 8);
    CHECK(st.machine().data().peek(0).value() == 14);
}

// `CREATE BUF 4 ALLOT` -- BUF is usable as a base address across all four
// cells ALLOT reserves past it, matching the R1 pipeline's own
// CreateAllotMergeCriterion.
static_assert([] {
    forth_state<64, 64, 1024, 256> st{"CREATE BUF 4 ALLOT "
                                      "10 BUF ! 20 BUF 3 + ! "
                                      "BUF @ BUF 3 + @ +"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    return r.has_value() && st.machine().data().depth() == 1 &&
           st.machine().data().peek().value() == 30;
}());

TEST_CASE("InterpTest - CreateAllotMergeCriterion") {
    forth_state<64, 64, 1024, 256> st{"CREATE BUF 4 ALLOT "
                                      "10 BUF ! 20 BUF 3 + ! "
                                      "BUF @ BUF 3 + @ +"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    REQUIRE(r.has_value());
    REQUIRE(st.machine().data().depth() == 1);
    CHECK(st.machine().data().peek().value() == 30);
}

// A variable's address is also usable from inside a colon definition,
// baked in as a literal at compile time (the same op::push a number
// literal gets).
static_assert([] {
    forth_state<64, 64, 1024, 256> st{
        "VARIABLE X  3 X !  : BUMP X @ 1+ X ! ;  BUMP BUMP X @"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    return r.has_value() && st.machine().data().depth() == 1 &&
           st.machine().data().peek().value() == 5;
}());

// `;` while interpreting is compile-only misuse: diagnosed, not silently
// ignored or treated as an unknown word.
static_assert([] {
    forth_state<64, 64, 1024, 256> st{";"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    return !r.has_value();
}());

// `EXIT` while interpreting is likewise compile-only misuse.
static_assert([] {
    forth_state<64, 64, 1024, 256> st{"EXIT"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    return !r.has_value();
}());

// A declared stack-effect comment right after the name is captured
// (unverified, D20), not treated as ordinary intertoken space that vanishes.
static_assert([] {
    forth_state<64, 64, 1024, 256> st{
        ": SQUARED ( n -- n*n ) DUP * ; 5 SQUARED"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    if (!r.has_value() || st.machine().data().depth() != 1 ||
        st.machine().data().peek().value() != 25) {
        return false;
    }
    auto const *entry = dict.lookup("SQUARED");
    if (entry == nullptr) {
        return false;
    }
    auto const *cw =
        std::get_if<smd::forth::machine::compiled_colon_word>(&entry->binding);
    return cw != nullptr && cw->has_effect;
}());

// RECURSE compiles a self-call to the definition's own entry point (F14's
// discipline, carried forward): a word whose body is exactly `RECURSE`
// compiles to `call <its own entry point>`. F25 has no control flow (F27's
// job), so this checks the compiled instruction directly rather than
// running a real recursive word to completion -- see
// InterpTest - RecurseCompilesASelfCall below for the runtime mirror.
static_assert([] {
    forth_state<64, 64, 1024, 256> st{": LOOPY RECURSE ;"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    if (!r.has_value()) {
        return false;
    }
    auto const *entry = dict.lookup("LOOPY");
    if (entry == nullptr) {
        return false;
    }
    auto const *cw =
        std::get_if<smd::forth::machine::compiled_colon_word>(&entry->binding);
    if (cw == nullptr) {
        return false;
    }
    auto const &call_instr =
        buf.program().code[static_cast<std::size_t>(cw->entry_point)];
    return call_instr.code == smd::forth::machine::op::call &&
           call_instr.operand == cw->entry_point;
}());

// -- Runtime-visible mirrors of the merge criteria, plus a few extras -------

TEST_CASE("InterpTest - SimpleArithmeticAndOutput") {
    forth_state<64, 64, 1024, 256> st{"1 2 + ."};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    REQUIRE(r.has_value());
    CHECK(output_of(st) == "3 ");
    CHECK(st.machine().data().depth() == 0);
}

TEST_CASE("InterpTest - MultipleWordsAndDotS") {
    forth_state<64, 64, 1024, 256> st{"1 2 3 .s"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    REQUIRE(r.has_value());
    CHECK(output_of(st) == "1 2 3 ");
    CHECK(st.machine().data().depth() == 3);
}

TEST_CASE("InterpTest - CaseInsensitiveWordLookup") {
    forth_state<64, 64, 1024, 256> st{"1 2 dup drop + ."};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    REQUIRE(r.has_value());
    CHECK(output_of(st) == "3 ");
}

TEST_CASE("InterpTest - HexBaseHandling") {
    forth_state<64, 64, 1024, 256> st{"FF ."};
    st.set_base(16);
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    REQUIRE(r.has_value());
    CHECK(output_of(st) == "255 ");
}

TEST_CASE("InterpTest - NegativeHexNumber") {
    forth_state<64, 64, 1024, 256> st{"-A ."};
    st.set_base(16);
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    REQUIRE(r.has_value());
    CHECK(output_of(st) == "-10 ");
}

TEST_CASE("InterpTest - UnknownWordCarriesPosition") {
    forth_state<64, 64, 1024, 256> st{"1 2 FOO"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().where.offset == 4);
}

TEST_CASE("InterpTest - UnderflowMatchesApplyPrimitiveDirectly") {
    forth_state<64, 64, 1024, 256> st{"+"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
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
        compile_buffer<> buf;
        auto r = interpret(st, dict, buf);
        REQUIRE(r.has_value());
        REQUIRE(st.machine().data().depth() == 1);
        CHECK(st.machine().data().peek().value() == -1);
    }
    {
        forth_state<64, 64, 1024, 256> st{"5 1-"};
        auto dict = default_dictionary<>();
        compile_buffer<> buf;
        auto r = interpret(st, dict, buf);
        REQUIRE(r.has_value());
        REQUIRE(st.machine().data().depth() == 1);
        CHECK(st.machine().data().peek().value() == 4);
    }
}

TEST_CASE("InterpTest - CommentsAreConsumedLikeIntertokenSpace") {
    forth_state<64, 64, 1024, 256> st{
        "1 2 + \\ a line comment\n( a paren comment ) ."};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    REQUIRE(r.has_value());
    CHECK(output_of(st) == "3 ");
}

TEST_CASE("InterpTest - FuelExhaustionIsDiagnosed") {
    forth_state<64, 64, 1024, 256> st{"1 1 1 1 1 1 1 1 1 1"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf, /*fuel=*/3);
    REQUIRE_FALSE(r.has_value());
    CHECK(std::string_view{r.error().message} ==
          "interpreter execution budget exhausted");
}

TEST_CASE("InterpTest - SquaredMergeCriterion") {
    forth_state<64, 64, 1024, 256> st{": SQUARED DUP * ;  4 SQUARED"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    REQUIRE(r.has_value());
    REQUIRE(st.machine().data().depth() == 1);
    CHECK(st.machine().data().peek().value() == 16);
    CHECK(st.state() == 0);
}

TEST_CASE("InterpTest - QuadMergeCriterion") {
    forth_state<64, 64, 1024, 256> st{
        ": SQUARED DUP * ; : QUAD SQUARED SQUARED ; 3 QUAD"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    REQUIRE(r.has_value());
    REQUIRE(st.machine().data().depth() == 1);
    CHECK(st.machine().data().peek().value() == 81);
}

TEST_CASE("InterpTest - SemicolonWhileInterpretingIsDiagnosed") {
    forth_state<64, 64, 1024, 256> st{";"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    REQUIRE_FALSE(r.has_value());
    CHECK(std::string_view{r.error().message}.find("compile-only") !=
          std::string_view::npos);
}

TEST_CASE("InterpTest - ExitWhileInterpretingIsDiagnosed") {
    forth_state<64, 64, 1024, 256> st{"EXIT"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    REQUIRE_FALSE(r.has_value());
    CHECK(std::string_view{r.error().message}.find("compile-only") !=
          std::string_view::npos);
}

TEST_CASE("InterpTest - RecurseWhileInterpretingIsDiagnosed") {
    forth_state<64, 64, 1024, 256> st{"RECURSE"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    REQUIRE_FALSE(r.has_value());
    CHECK(std::string_view{r.error().message}.find("compile-only") !=
          std::string_view::npos);
}

TEST_CASE("InterpTest - RecurseCompilesASelfCall") {
    // A minimal RECURSE exercise with no control flow at all: a word that
    // calls itself unconditionally would never terminate if actually run,
    // so this only checks that RECURSE compiles (emits a call to the
    // definition's own entry point) rather than diagnosing -- the defining
    // half of RECURSE's own merge criterion, without needing IF (F27).
    forth_state<64, 64, 1024, 256> st{": LOOPY RECURSE ;"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    REQUIRE(r.has_value());
    auto const *entry = dict.lookup("LOOPY");
    REQUIRE(entry != nullptr);
    auto const *cw =
        std::get_if<smd::forth::machine::compiled_colon_word>(&entry->binding);
    REQUIRE(cw != nullptr);
    auto const &code = buf.program().code;
    REQUIRE(code.size() >= cw->entry_point + 2);
    auto const &call_instr = code[static_cast<std::size_t>(cw->entry_point)];
    CHECK(call_instr.code == smd::forth::machine::op::call);
    CHECK(call_instr.operand == cw->entry_point);
}

TEST_CASE("InterpTest - NestedColonIsDiagnosed") {
    forth_state<64, 64, 1024, 256> st{": A : B ;"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    REQUIRE_FALSE(r.has_value());
}

TEST_CASE("InterpTest - UnterminatedColonDefinitionIsDiagnosed") {
    forth_state<64, 64, 1024, 256> st{": A DUP"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    REQUIRE_FALSE(r.has_value());
}

TEST_CASE("InterpTest - DeclaredEffectCommentIsCapturedUnverified") {
    forth_state<64, 64, 1024, 256> st{
        ": SQUARED ( n -- n*n ) DUP * ; 5 SQUARED"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    REQUIRE(r.has_value());
    CHECK(st.machine().data().peek().value() == 25);

    auto const *entry = dict.lookup("SQUARED");
    REQUIRE(entry != nullptr);
    auto const *cw =
        std::get_if<smd::forth::machine::compiled_colon_word>(&entry->binding);
    REQUIRE(cw != nullptr);
    CHECK(cw->has_effect);
}

TEST_CASE("InterpTest - NoEffectCommentLeavesHasEffectFalse") {
    forth_state<64, 64, 1024, 256> st{": SQUARED DUP * ; 5 SQUARED"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    REQUIRE(r.has_value());

    auto const *entry = dict.lookup("SQUARED");
    REQUIRE(entry != nullptr);
    auto const *cw =
        std::get_if<smd::forth::machine::compiled_colon_word>(&entry->binding);
    REQUIRE(cw != nullptr);
    CHECK_FALSE(cw->has_effect);
}
