// src/smd/forth/interpreter/interp.test.cpp                         -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <smd/forth/interpreter/interp.hpp>
#include <smd/forth/interpreter/interp.hpp> // test 2nd include OK

#include <smd/forth/interpreter/compilebuf.hpp>
#include <smd/forth/interpreter/control_flow_corpus.hpp>
#include <smd/forth/machine/dictionary.hpp>
#include <smd/forth/machine/forth_state.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string_view>

using smd::forth::interpreter::compile_buffer;
using smd::forth::interpreter::interpret;
using smd::forth::machine::default_dictionary;
using smd::forth::machine::forth_state;
namespace corpus = smd::forth::interpreter::corpus;

namespace {

/// Views a state's accumulated output as a @c std::string_view, the same
/// convenience @ref smd::forth::parser::forth_chars.test.cpp's own
/// @c view_of provides for scanned tokens.
template <int MaxDepth, int MaxRDepth, int MaxData, int MaxOut>
constexpr auto
output_of(forth_state<MaxDepth, MaxRDepth, MaxData, MaxOut> const &st)
    -> std::string_view {
    auto const &out = st.output();
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
    return r.has_value() && output_of(st) == "3 " && st.data().depth() == 0;
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
    return r.has_value() && st.data().depth() == 1 &&
           st.data().peek().value() == -1;
}());

static_assert([] {
    forth_state<64, 64, 1024, 256> st{"5 1-"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    return r.has_value() && st.data().depth() == 1 &&
           st.data().peek().value() == 4;
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
    return r.has_value() && st.data().depth() == 0 && output_of(st).empty();
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
    return r.has_value() && st.data().depth() == 1 &&
           st.data().peek().value() == 2; // "+" resolved to minus
}());

// -- Step F25 merge criteria: the colon compiler ----------------------------

// `: SQUARED DUP * ;  4 SQUARED` leaves `[16]` via the interpreter.
static_assert([] {
    forth_state<64, 64, 1024, 256> st{": SQUARED DUP * ;  4 SQUARED"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    return r.has_value() && st.state() == 0 && st.data().depth() == 1 &&
           st.data().peek().value() == 16;
}());

// `: QUAD SQUARED SQUARED ; 3 QUAD` leaves `[81]` -- a colon word calling
// another colon word, both compiled and both interpreted afterward.
static_assert([] {
    forth_state<64, 64, 1024, 256> st{
        ": SQUARED DUP * ; : QUAD SQUARED SQUARED ; 3 QUAD"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    return r.has_value() && st.data().depth() == 1 &&
           st.data().peek().value() == 81;
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
    return r.has_value() && st.data().depth() == 2 &&
           st.data().peek(1).value() == 8 && st.data().peek(0).value() == 14;
}());

TEST_CASE("InterpTest - MemoryWordsMergeCriterion") {
    forth_state<64, 64, 1024, 256> st{"VARIABLE X  5 X !  X @ 3 + X !  X @ "
                                      "7 CONSTANT LUCKY  LUCKY LUCKY +"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    REQUIRE(r.has_value());
    REQUIRE(st.data().depth() == 2);
    CHECK(st.data().peek(1).value() == 8);
    CHECK(st.data().peek(0).value() == 14);
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
    return r.has_value() && st.data().depth() == 1 &&
           st.data().peek().value() == 30;
}());

TEST_CASE("InterpTest - CreateAllotMergeCriterion") {
    forth_state<64, 64, 1024, 256> st{"CREATE BUF 4 ALLOT "
                                      "10 BUF ! 20 BUF 3 + ! "
                                      "BUF @ BUF 3 + @ +"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    REQUIRE(r.has_value());
    REQUIRE(st.data().depth() == 1);
    CHECK(st.data().peek().value() == 30);
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
    return r.has_value() && st.data().depth() == 1 &&
           st.data().peek().value() == 5;
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
    if (!r.has_value() || st.data().depth() != 1 ||
        st.data().peek().value() != 25) {
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

// -- Step F27 merge criteria: immediacy and control flow ---------------------
//
// The complete F13/F16/F17 program battery (interpreter/control_flow_corpus.
// hpp, preserved verbatim by F26 specifically so this step has something to
// prove itself against), through interpret() at compile time.

static_assert([] {
    forth_state<64, 64, 1024, 256> st{corpus::abs_program};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    return r.has_value() && st.data().depth() == 1 &&
           st.data().peek().value() == 7;
}());

static_assert([] {
    forth_state<64, 64, 1024, 256> st{corpus::countdown_program};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    return r.has_value() && st.data().depth() == 0 && output_of(st) == "3 2 1 ";
}());

// SPIN never terminates: a small VM fuel diagnoses budget exhaustion rather
// than hanging the constant evaluator (D22).
static_assert([] {
    forth_state<64, 64, 1024, 256> st{corpus::spin_program};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf, /*fuel=*/100000, /*vm_fuel=*/25);
    return !r.has_value();
}());

static_assert([] {
    forth_state<64, 64, 1024, 256> st{corpus::upto3_program};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    return r.has_value() && st.data().depth() == 1 &&
           st.data().peek().value() == 3;
}());

static_assert([] {
    forth_state<64, 64, 1024, 256> st{corpus::exit_boundary_program};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    return r.has_value() && st.data().depth() == 2 &&
           st.data().peek(1).value() == -3 && st.data().peek(0).value() == 99;
}());

static_assert([] {
    forth_state<64, 64, 1024, 256> st{corpus::sumto_program};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    return r.has_value() && st.data().depth() == 1 &&
           st.data().peek().value() == 15;
}());

static_assert([] {
    forth_state<64, 64, 1024, 256> st{corpus::find5_program};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    return r.has_value() && st.data().depth() == 1 &&
           st.data().peek().value() == 5;
}());

static_assert([] {
    forth_state<64, 64, 1024, 256> st{corpus::tens_program};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    return r.has_value() && st.data().depth() == 1 &&
           st.data().peek().value() == 9;
}());

static_assert([] {
    forth_state<64, 64, 1024, 256> st{corpus::sumeven_program};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    return r.has_value() && st.data().depth() == 1 &&
           st.data().peek().value() == 20;
}());

static_assert([] {
    forth_state<64, 64, 1024, 256> st{corpus::first_program};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    return r.has_value() && st.data().depth() == 1 &&
           st.data().peek().value() == 3;
}());

// IMMEDIATE and POSTPONE, D17's own stated example: a user-defined immediate
// word whose entire body is POSTPONE of a C++-native control word becomes a
// plain alias for it -- ENDIF behaves exactly like THEN.
static_assert([] {
    forth_state<64, 64, 1024, 256> st{
        ": ENDIF POSTPONE THEN ; IMMEDIATE"
        " : ABS2 DUP 0< IF NEGATE ENDIF ;  -7 ABS2"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    return r.has_value() && st.data().depth() == 1 &&
           st.data().peek().value() == 7;
}());

// POSTPONE's other half: postponing an ordinary (non-immediate) word appends
// its own compiled form, exactly as compiling it directly would -- DOUBLER
// need not itself be IMMEDIATE for this to work.
static_assert([] {
    forth_state<64, 64, 1024, 256> st{
        ": DOUBLER POSTPONE DUP POSTPONE * ;  5 DOUBLER"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    return r.has_value() && st.data().depth() == 1 &&
           st.data().peek().value() == 25;
}());

// `[ ... ] LITERAL`: an interpreted computation inside the brackets is baked
// into the surrounding definition as a single literal.
static_assert([] {
    forth_state<64, 64, 1024, 256> st{": SEVEN [ 3 4 + ] LITERAL ;  SEVEN"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    return r.has_value() && st.data().depth() == 1 &&
           st.data().peek().value() == 7;
}());

// Mismatched THEN without IF: diagnosed via the orig/dest discipline itself
// (nothing to pop off the control-flow stack), not UB (D7).
static_assert([] {
    forth_state<64, 64, 1024, 256> st{": BAD THEN ;"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    return !r.has_value();
}());

// An unresolved DO (no matching LOOP/+LOOP) is diagnosed at `;` via
// compiling_context::loop_depth (see that struct's own doc comment for why
// an unresolved IF/BEGIN is not caught the same way -- that would require
// comparing data-stack depth, which is unsound once an immediate word's own
// body can legitimately leave real data behind, exactly as `[ ... ]
// LITERAL`'s own bracketed computation does).
static_assert([] {
    forth_state<64, 64, 1024, 256> st{": BAD 3 0 DO I . ;"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    return !r.has_value();
}());

// LEAVE outside any DO ... LOOP is diagnosed rather than compiling a branch
// to nowhere.
static_assert([] {
    forth_state<64, 64, 1024, 256> st{": BAD LEAVE ;"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    return !r.has_value();
}());

// -- Runtime-visible mirrors of the merge criteria, plus a few extras -------

TEST_CASE("InterpTest - SimpleArithmeticAndOutput") {
    forth_state<64, 64, 1024, 256> st{"1 2 + ."};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    REQUIRE(r.has_value());
    CHECK(output_of(st) == "3 ");
    CHECK(st.data().depth() == 0);
}

TEST_CASE("InterpTest - MultipleWordsAndDotS") {
    forth_state<64, 64, 1024, 256> st{"1 2 3 .s"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    REQUIRE(r.has_value());
    CHECK(output_of(st) == "1 2 3 ");
    CHECK(st.data().depth() == 3);
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
        REQUIRE(st.data().depth() == 1);
        CHECK(st.data().peek().value() == -1);
    }
    {
        forth_state<64, 64, 1024, 256> st{"5 1-"};
        auto dict = default_dictionary<>();
        compile_buffer<> buf;
        auto r = interpret(st, dict, buf);
        REQUIRE(r.has_value());
        REQUIRE(st.data().depth() == 1);
        CHECK(st.data().peek().value() == 4);
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
    REQUIRE(st.data().depth() == 1);
    CHECK(st.data().peek().value() == 16);
    CHECK(st.state() == 0);
}

TEST_CASE("InterpTest - QuadMergeCriterion") {
    forth_state<64, 64, 1024, 256> st{
        ": SQUARED DUP * ; : QUAD SQUARED SQUARED ; 3 QUAD"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    REQUIRE(r.has_value());
    REQUIRE(st.data().depth() == 1);
    CHECK(st.data().peek().value() == 81);
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
    CHECK(st.data().peek().value() == 25);

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

// -- Step F27: runtime mirrors of the control-flow corpus, plus a few extras

TEST_CASE("InterpTest - AbsMergeCriterion") {
    forth_state<64, 64, 1024, 256> st{corpus::abs_program};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    REQUIRE(r.has_value());
    REQUIRE(st.data().depth() == 1);
    CHECK(st.data().peek().value() == 7);
}

TEST_CASE("InterpTest - CountdownMergeCriterion") {
    forth_state<64, 64, 1024, 256> st{corpus::countdown_program};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    REQUIRE(r.has_value());
    CHECK(st.data().depth() == 0);
    CHECK(output_of(st) == "3 2 1 ");
}

TEST_CASE("InterpTest - SpinBudgetExhaustionIsDiagnosed") {
    forth_state<64, 64, 1024, 256> st{corpus::spin_program};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf, /*fuel=*/100000, /*vm_fuel=*/25);
    REQUIRE_FALSE(r.has_value());
    CHECK(std::string_view{r.error().message} ==
          "vm execution budget exhausted");
}

TEST_CASE("InterpTest - Upto3MergeCriterion") {
    forth_state<64, 64, 1024, 256> st{corpus::upto3_program};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    REQUIRE(r.has_value());
    REQUIRE(st.data().depth() == 1);
    CHECK(st.data().peek().value() == 3);
}

TEST_CASE("InterpTest - ExitBoundaryMergeCriterion") {
    forth_state<64, 64, 1024, 256> st{corpus::exit_boundary_program};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    REQUIRE(r.has_value());
    REQUIRE(st.data().depth() == 2);
    CHECK(st.data().peek(1).value() == -3);
    CHECK(st.data().peek(0).value() == 99);
}

TEST_CASE("InterpTest - SumtoMergeCriterion") {
    forth_state<64, 64, 1024, 256> st{corpus::sumto_program};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    REQUIRE(r.has_value());
    REQUIRE(st.data().depth() == 1);
    CHECK(st.data().peek().value() == 15);
}

TEST_CASE("InterpTest - Find5MergeCriterion") {
    forth_state<64, 64, 1024, 256> st{corpus::find5_program};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    REQUIRE(r.has_value());
    REQUIRE(st.data().depth() == 1);
    CHECK(st.data().peek().value() == 5);
}

TEST_CASE("InterpTest - TensMergeCriterion") {
    forth_state<64, 64, 1024, 256> st{corpus::tens_program};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    REQUIRE(r.has_value());
    REQUIRE(st.data().depth() == 1);
    CHECK(st.data().peek().value() == 9);
}

TEST_CASE("InterpTest - SumevenMergeCriterion") {
    forth_state<64, 64, 1024, 256> st{corpus::sumeven_program};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    REQUIRE(r.has_value());
    REQUIRE(st.data().depth() == 1);
    CHECK(st.data().peek().value() == 20);
}

TEST_CASE("InterpTest - FirstMergeCriterion") {
    forth_state<64, 64, 1024, 256> st{corpus::first_program};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    REQUIRE(r.has_value());
    REQUIRE(st.data().depth() == 1);
    CHECK(st.data().peek().value() == 3);
}

TEST_CASE("InterpTest - PostponeAliasesAControlWord") {
    // D17's own stated example: ENDIF becomes a plain alias for THEN.
    forth_state<64, 64, 1024, 256> st{
        ": ENDIF POSTPONE THEN ; IMMEDIATE"
        " : ABS2 DUP 0< IF NEGATE ENDIF ;  -7 ABS2"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    REQUIRE(r.has_value());
    REQUIRE(st.data().depth() == 1);
    CHECK(st.data().peek().value() == 7);

    auto const *entry = dict.lookup("ENDIF");
    REQUIRE(entry != nullptr);
    REQUIRE(std::holds_alternative<smd::forth::machine::control_word>(
        entry->binding));
    CHECK(std::get<smd::forth::machine::control_word>(entry->binding).which ==
          smd::forth::machine::control_builtin::then_);
    CHECK(entry->immediate);
}

TEST_CASE("InterpTest - PostponeOfOrdinaryWordCompilesItsForm") {
    forth_state<64, 64, 1024, 256> st{
        ": DOUBLER POSTPONE DUP POSTPONE * ;  5 DOUBLER"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    REQUIRE(r.has_value());
    REQUIRE(st.data().depth() == 1);
    CHECK(st.data().peek().value() == 25);
}

TEST_CASE("InterpTest - PostponeUnknownWordIsDiagnosed") {
    forth_state<64, 64, 1024, 256> st{": BAD POSTPONE NOSUCHWORD ;"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    REQUIRE_FALSE(r.has_value());
}

TEST_CASE("InterpTest - PostponeWhileInterpretingIsDiagnosed") {
    forth_state<64, 64, 1024, 256> st{"POSTPONE DUP"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    REQUIRE_FALSE(r.has_value());
}

TEST_CASE("InterpTest - PostponeControlWordMixedWithOtherCodeIsDiagnosed") {
    // Only a definition whose entire body is one POSTPONE of a control word
    // is supported (apply_control_word's own postpone_ case has the full
    // rationale); code after it is diagnosed rather than silently dropped.
    forth_state<64, 64, 1024, 256> st{": BAD POSTPONE THEN DROP ;"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    REQUIRE_FALSE(r.has_value());
}

TEST_CASE("InterpTest - BracketLiteralBakesInAnInterpretedComputation") {
    forth_state<64, 64, 1024, 256> st{": SEVEN [ 3 4 + ] LITERAL ;  SEVEN"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    REQUIRE(r.has_value());
    REQUIRE(st.data().depth() == 1);
    CHECK(st.data().peek().value() == 7);
}

TEST_CASE("InterpTest - MismatchedThenWithoutIfIsDiagnosed") {
    forth_state<64, 64, 1024, 256> st{": BAD THEN ;"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    REQUIRE_FALSE(r.has_value());
}

TEST_CASE("InterpTest - UnresolvedDoIsDiagnosedAtSemicolon") {
    forth_state<64, 64, 1024, 256> st{": BAD 3 0 DO I . ;"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    REQUIRE_FALSE(r.has_value());
}

TEST_CASE("InterpTest - LeaveOutsideLoopIsDiagnosed") {
    forth_state<64, 64, 1024, 256> st{": BAD LEAVE ;"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    REQUIRE_FALSE(r.has_value());
}

TEST_CASE("InterpTest - UnloopIWithoutLoopAreDiagnosed") {
    {
        forth_state<64, 64, 1024, 256> st{": BAD UNLOOP ;"};
        auto dict = default_dictionary<>();
        compile_buffer<> buf;
        auto r = interpret(st, dict, buf);
        REQUIRE_FALSE(r.has_value());
    }
    {
        forth_state<64, 64, 1024, 256> st{": BAD I ;"};
        auto dict = default_dictionary<>();
        compile_buffer<> buf;
        auto r = interpret(st, dict, buf);
        REQUIRE_FALSE(r.has_value());
    }
}

TEST_CASE("InterpTest - JRequiresTwoLevelsOfNesting") {
    forth_state<64, 64, 1024, 256> st{": BAD 3 0 DO J LOOP ;"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    REQUIRE_FALSE(r.has_value());
}

TEST_CASE("InterpTest - ImmediateWordExecutesAtCompileTimeNotRuntime") {
    // DOUBLE-IT is IMMEDIATE, so meeting it while compiling USES-IT runs it
    // right then (D13's "execute ... when immediate"): its own "21" is
    // pushed once, during USES-IT's own compilation, leaving USES-IT's body
    // empty. Calling USES-IT afterward -- even twice -- adds nothing further,
    // which is exactly what distinguishes immediate dispatch from compiling
    // an ordinary call (which would push 21 on every call).
    forth_state<64, 64, 1024, 256> st{": DOUBLE-IT 21 ; IMMEDIATE"
                                      " : USES-IT DOUBLE-IT ;"
                                      " USES-IT USES-IT"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    REQUIRE(r.has_value());
    REQUIRE(st.data().depth() == 1);
    CHECK(st.data().peek().value() == 21);
}

TEST_CASE("InterpTest - CompileCommaAppendsAnEntrysCompiledForm") {
    // COMPILE, ( xt -- ): pops a dictionary index (D11's original
    // convention -- distinct from `'`/`[']`'s own F28 execution tokens,
    // which are code-space instruction indices, not dictionary indices; see
    // apply_control_word's own compile_comma_ doc) and appends that entry's
    // own compiled form -- driven directly here, in C++, rather than through
    // interpret() itself, since nothing in this project's own Forth source
    // vocabulary produces a bare dictionary index on the stack.
    forth_state<64, 64, 1024, 256> st{": X ;"}; // gives cctx somewhere to land
    auto dict = default_dictionary<>();
    compile_buffer<> buf;

    int const dup_index = dict.lookup_index("DUP");
    REQUIRE(dup_index >= 0);

    REQUIRE(st.data()
                .push(static_cast<smd::forth::machine::cell>(dup_index))
                .has_value());
    smd::forth::interpreter::compiling_context<32> cctx{};
    cctx.entry = buf.here();
    st.set_state(1);
    auto r = smd::forth::interpreter::apply_control_word(
        smd::forth::machine::control_builtin::compile_comma_, st, dict, buf,
        cctx, smd::forth::foundation::source_pos{});
    REQUIRE(r.has_value());

    auto const &code = buf.program().code;
    REQUIRE(code.size() == cctx.entry + 1);
    auto const &appended = code[cctx.entry];
    CHECK(appended.code == smd::forth::machine::op::prim);
    CHECK(appended.operand == static_cast<smd::forth::machine::cell>(
                                  smd::forth::machine::primitive::dup));
}

// -- Step F28 merge criteria: execution tokens and defining words (D18) -----

// `: CONSTANT2 CREATE , DOES> @ ;  42 CONSTANT2 LIFE  LIFE` leaves `[42]`
// (the R1 F20 classic, now required): CREATE/DOES> reach the dictionary from
// *inside* CONSTANT2's own compiled body -- once per invocation, not once at
// CONSTANT2's own definition time -- via the shared machine::create_here
// action and dictionary::attach_does. This is the merge criterion that made
// DIV-0012's SOURCE/>IN fold into machine::forth_state load-bearing: CREATE
// needs to scan "LIFE" off the same input stream interpret()'s own outer
// loop is iterating, from inside a pure VM run_from call.
static_assert([] {
    forth_state<64, 64, 1024, 256> st{
        ": CONSTANT2 CREATE , DOES> @ ;  42 CONSTANT2 LIFE  LIFE"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    return r.has_value() && st.data().depth() == 1 &&
           st.data().peek().value() == 42;
}());

TEST_CASE("InterpTest - ConstantTwoDoesMergeCriterion") {
    forth_state<64, 64, 1024, 256> st{
        ": CONSTANT2 CREATE , DOES> @ ;  42 CONSTANT2 LIFE  LIFE"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    REQUIRE(r.has_value());
    REQUIRE(st.data().depth() == 1);
    CHECK(st.data().peek().value() == 42);

    // LIFE itself is a variable_word carrying a does-field, not a plain
    // CREATE/VARIABLE (does_entry == -1 would mean DOES> never ran).
    auto const *entry = dict.lookup("LIFE");
    REQUIRE(entry != nullptr);
    auto const *vw =
        std::get_if<smd::forth::machine::variable_word>(&entry->binding);
    REQUIRE(vw != nullptr);
    CHECK(vw->does_entry >= 0);
}

// `' SQUARED EXECUTE` is equivalent to calling SQUARED directly (D14: the
// same value agrees with itself here, not merely across compile time and
// runtime) -- both leave 25 for the same input, 5.
static_assert([] {
    forth_state<64, 64, 1024, 256> st{
        ": SQUARED DUP * ;  5 SQUARED  5 ' SQUARED EXECUTE"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    return r.has_value() && st.data().depth() == 2 &&
           st.data().peek(1).value() == 25 && st.data().peek(0).value() == 25;
}());

TEST_CASE("InterpTest - TickExecuteMergeCriterion") {
    forth_state<64, 64, 1024, 256> st{
        ": SQUARED DUP * ;  5 SQUARED  5 ' SQUARED EXECUTE"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    REQUIRE(r.has_value());
    REQUIRE(st.data().depth() == 2);
    CHECK(st.data().peek(1).value() == 25);
    CHECK(st.data().peek(0).value() == st.data().peek(1).value());
}

// A DEFERred word invoked before IS is diagnosed on execution -- neither a
// silent no-op nor UB (D7).
static_assert([] {
    forth_state<64, 64, 1024, 256> st{"DEFER FOO  FOO"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    return !r.has_value();
}());

TEST_CASE("InterpTest - DeferredWordBeforeIsIsDiagnosed") {
    forth_state<64, 64, 1024, 256> st{"DEFER FOO  FOO"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    REQUIRE_FALSE(r.has_value());
    CHECK(std::string_view{r.error().message}.find("no action") !=
          std::string_view::npos);
}

TEST_CASE("InterpTest - DeferredWordAfterIsRunsTheAssignedTarget") {
    forth_state<64, 64, 1024, 256> st{
        ": SQUARED DUP * ;  DEFER FOO  ' SQUARED IS FOO  6 FOO"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    REQUIRE(r.has_value());
    REQUIRE(st.data().depth() == 1);
    CHECK(st.data().peek().value() == 36);
}

TEST_CASE("InterpTest - ValueAndToRoundTrip") {
    forth_state<64, 64, 1024, 256> st{
        "5 VALUE SPEED  SPEED  10 TO SPEED  SPEED"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    REQUIRE(r.has_value());
    REQUIRE(st.data().depth() == 2);
    CHECK(st.data().peek(1).value() == 5);
    CHECK(st.data().peek(0).value() == 10);
}

TEST_CASE("InterpTest - ToInsideAColonDefinitionCompilesAStoreSequence") {
    // TO resolves its target's address once, at the moment it is met
    // (immediate), and compiles a push-address/store sequence rather than
    // needing dictionary access when the enclosing word later runs.
    forth_state<64, 64, 1024, 256> st{
        "0 VALUE COUNTER  : BUMP 1 TO COUNTER ;  BUMP  COUNTER"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    REQUIRE(r.has_value());
    REQUIRE(st.data().depth() == 1);
    CHECK(st.data().peek().value() == 1);
}

TEST_CASE("InterpTest - BracketTickAndExecuteInsideAColonDefinition") {
    // `[']` resolves at compile time and compiles a literal push of the
    // execution token (@ref smd::forth::interpreter::resolve_execution_token
    // 's own guard-branch discipline is what makes emitting a primitive's
    // stub safe here, in the middle of DOUBLE-IT's own body); the compiled
    // (non-immediate) form of EXECUTE then runs it.
    forth_state<64, 64, 1024, 256> st{
        ": DOUBLE-IT ['] DUP EXECUTE * ;  6 DOUBLE-IT"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    REQUIRE(r.has_value());
    REQUIRE(st.data().depth() == 1);
    CHECK(st.data().peek().value() == 36);
}

TEST_CASE("InterpTest - PostponeExecuteComposesWithOtherCode") {
    // Unlike POSTPONE of a structural control word (THEN/IF/...,
    // PostponeControlWordMixedWithOtherCodeIsDiagnosed above), POSTPONE
    // EXECUTE has a real compiled form (compile_entry's own case for it), so
    // it composes freely with other code in the same definition -- DIV-0015
    // closes for this case (see its own F28 addendum). The resulting word
    // being an ordinary compiled_colon_word (not a control_word alias) is
    // the point of this test; the numeric result RUN-XT leaves behind is not
    // meaningful (DUP duplicates whatever is on top at that point, which is
    // the execution token itself, not the value beneath it -- a real caller
    // would arrange its own stack discipline, exactly as `' SQUARED EXECUTE`
    // does above).
    forth_state<64, 64, 1024, 256> st{
        ": SQUARED DUP * ;  : RUN-XT DUP POSTPONE EXECUTE ;"
        " 6 ' SQUARED RUN-XT"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    REQUIRE(r.has_value());
    auto const *entry = dict.lookup("RUN-XT");
    REQUIRE(entry != nullptr);
    CHECK(std::holds_alternative<smd::forth::machine::compiled_colon_word>(
        entry->binding));
}
