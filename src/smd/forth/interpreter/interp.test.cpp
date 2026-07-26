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

// -- Step F29: parsing words and strings (D19/D21) --------------------------

// Merge criterion: `: GREET ." HELLO" CR ;  GREET` outputs correctly.
static_assert([] {
    forth_state<64, 64, 1024, 256> st{": GREET .\" HELLO\" CR ;  GREET"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    return r.has_value() && output_of(st) == "HELLO\n";
}());

TEST_CASE("InterpTest - DotQuoteMergeCriterion") {
    forth_state<64, 64, 1024, 256> st{": GREET .\" HELLO\" CR ;  GREET"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    REQUIRE(r.has_value());
    CHECK(output_of(st) == "HELLO\n");
}

// Merge criterion, and the reason D19 built the input stream as plain
// machine state rather than a scanner-hidden cursor: a *user-defined*
// parsing word, written in ordinary Forth and calling `WORD` (a primitive
// with no dictionary or compile-time special-casing at all, F29's own step
// text), reads its own argument out of the input stream at the moment it is
// *called* -- not the text present when it was *defined*. `ECHO-WORD` is
// defined once; `FOO` is text that exists nowhere near its own definition,
// only at its own call site.
static_assert([] {
    forth_state<64, 64, 1024, 256> st{
        ": ECHO-WORD 32 WORD COUNT TYPE ;  ECHO-WORD FOO"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    return r.has_value() && output_of(st) == "FOO";
}());

TEST_CASE("InterpTest - UserDefinedParsingWordMergeCriterion") {
    forth_state<64, 64, 1024, 256> st{
        ": ECHO-WORD 32 WORD COUNT TYPE ;  ECHO-WORD FOO"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    REQUIRE(r.has_value());
    CHECK(output_of(st) == "FOO");
}

// A second instance of the same demonstration using PARSE directly instead
// of WORD, confirming the merge criterion's own "PARSE or WORD" wording: a
// colon word that reads a comma-terminated argument via PARSE, called twice
// with two different trailing arguments, reads each one correctly at its
// own call site.
static_assert([] {
    forth_state<64, 64, 1024, 256> st{": ECHO-TO-COMMA 44 PARSE TYPE ;  "
                                      "ECHO-TO-COMMA one,ECHO-TO-COMMA two,"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    return r.has_value() && output_of(st) == "onetwo";
}());

TEST_CASE("InterpTest - UserDefinedParsingWordViaParseMergeCriterion") {
    forth_state<64, 64, 1024, 256> st{": ECHO-TO-COMMA 44 PARSE TYPE ;  "
                                      "ECHO-TO-COMMA one,ECHO-TO-COMMA two,"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    REQUIRE(r.has_value());
    CHECK(output_of(st) == "onetwo");
}

// Merge criterion: `S"` round-trips through `COUNT`/`TYPE`. `S"` itself
// already leaves ( c-addr u -- ), Forth-2012's own runtime shape, so its own
// round trip is through `TYPE` directly; `COUNT` is what turns a *counted*
// string -- `WORD`'s own output shape (D21) -- back into the same ( c-addr
// u -- ) pair. Both round trips are exercised together here, over data this
// step's own primitives produced, not hand-built test fixtures.
static_assert([] {
    forth_state<64, 64, 1024, 256> st{
        "S\" HELLO \" TYPE : ECHO 32 WORD COUNT TYPE ;  ECHO WORLD"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    return r.has_value() && output_of(st) == "HELLO WORLD";
}());

TEST_CASE("InterpTest - StringRoundTripMergeCriterion") {
    forth_state<64, 64, 1024, 256> st{
        "S\" HELLO \" TYPE : ECHO 32 WORD COUNT TYPE ;  ECHO WORLD"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    REQUIRE(r.has_value());
    CHECK(output_of(st) == "HELLO WORLD");
}

// S" also works purely interpreting, leaving an address/length pair rather
// than printing.
static_assert([] {
    forth_state<64, 64, 1024, 256> st{"S\" HI\""};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    return r.has_value() && st.data().depth() == 2 &&
           st.data().peek(0).value() == 2;
}());

// `(` and `\` are ordinary dictionary words now (D19), not scanner-level
// special cases: this is the same source `InterpTest -
// CommentsAreConsumedLikeIntertokenSpace` (above) already exercises, still
// producing the identical output through the new mechanism.
TEST_CASE("InterpTest - ParenAndBackslashAreOrdinaryImmediateWords") {
    forth_state<64, 64, 1024, 256> st{
        "1 2 + \\ a line comment\n( a paren comment ) ."};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    REQUIRE(r.has_value());
    CHECK(output_of(st) == "3 ");
    auto const *entry = dict.lookup("(");
    REQUIRE(entry != nullptr);
    CHECK(entry->immediate);
}

TEST_CASE("InterpTest - UnterminatedParenCommentIsDiagnosed") {
    forth_state<64, 64, 1024, 256> st{"1 2 + ( never closed"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    REQUIRE_FALSE(r.has_value());
    CHECK(std::string_view{r.error().message}.find("unterminated") !=
          std::string_view::npos);
}

// `\` at end of input with no trailing newline is a clean end of source, not
// an error -- unlike `(` with no closing `)`.
TEST_CASE("InterpTest - BackslashToEndOfInputIsNotAnError") {
    forth_state<64, 64, 1024, 256> st{"1 2 + . \\ trailing, no newline"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    REQUIRE(r.has_value());
    CHECK(output_of(st) == "3 ");
}

TEST_CASE("InterpTest - CharAndBracketCharMergeCriterion") {
    // CHAR is an ordinary (non-immediate) primitive: usable directly while
    // interpreting.
    forth_state<64, 64, 1024, 256> st{"CHAR ! : STAR [CHAR] * ;  STAR"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    REQUIRE(r.has_value());
    REQUIRE(st.data().depth() == 2);
    CHECK(st.data().peek(1).value() == static_cast<int>('!'));
    CHECK(st.data().peek(0).value() == static_cast<int>('*'));
}

TEST_CASE("InterpTest - BracketCharIsCompileOnly") {
    forth_state<64, 64, 1024, 256> st{"[CHAR] *"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    REQUIRE_FALSE(r.has_value());
    CHECK(std::string_view{r.error().message}.find("compile-only") !=
          std::string_view::npos);
}

// ABORT" (DIV-0017's revisit, DIV-0018): compile-only, and `THROW -2`
// (uncaught here, so a diagnosed error, carrying -2) when the runtime flag
// is nonzero; a silent no-op when it is zero.
TEST_CASE("InterpTest - AbortQuoteIsCompileOnly") {
    forth_state<64, 64, 1024, 256> st{"ABORT\" boom\""};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    REQUIRE_FALSE(r.has_value());
    CHECK(std::string_view{r.error().message}.find("compile-only") !=
          std::string_view::npos);
}

TEST_CASE("InterpTest - AbortQuoteFalseFlagIsANoOp") {
    forth_state<64, 64, 1024, 256> st{
        ": CHECK ABORT\" boom\" ;  0 CHECK 1 2 +"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    REQUIRE(r.has_value());
    CHECK(output_of(st).empty());
    REQUIRE(st.data().depth() == 1);
    CHECK(st.data().peek().value() == 3);
}

TEST_CASE("InterpTest - AbortQuoteTrueFlagPrintsMessageThenDiagnoses") {
    forth_state<64, 64, 1024, 256> st{
        ": CHECK ABORT\" boom\" ;  -1 CHECK 1 2 +"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    REQUIRE_FALSE(r.has_value());
    CHECK(output_of(st) == "boom");
    // The abort happened before "1 2 +" ran.
    CHECK(st.data().depth() == 0);
    // Step F31: uncaught here (no CATCH anywhere in this program), so this
    // is now genuinely an uncaught `THROW -2` -- the code rides along in
    // where.offset (vm.hpp's own perform_throw).
    CHECK(r.error().where.offset == -2);
}

// ===========================================================================
// Step F31 (docs/forth-plan-2.md): CATCH and THROW.
//
// R1's own F18a step (retired unexecuted) never wrote BOOM/SAFE/TRY as code;
// docs/forth-plan-2.md's own F31 section describes them as "the conventional
// shapes -- a word that throws, a word that catches and recovers, and a
// wrapper that reports which happened." BOOM always throws 42; SAFE always
// completes normally, leaving 99; TRY runs either xt under CATCH and reports
// which happened, printing the value either way -- DUP before IF so the
// flag survives being tested, since IF itself pops whatever it tests.
// ===========================================================================

inline constexpr std::string_view boom_safe_try_program =
    ": BOOM 42 THROW ; "
    ": SAFE 99 ; "
    ": TRY CATCH DUP IF .\" CAUGHT \" . ELSE DROP .\" OK \" . THEN ; "
    "' BOOM TRY ' SAFE TRY";

// `' BOOM TRY` prints "CAUGHT 42 "; `' SAFE TRY` prints "OK 99 ": both
// worlds, compile time (this static_assert) and runtime (the TEST_CASE
// below) -- D14's "the same value agrees with itself."
static_assert([] {
    forth_state<64, 64, 1024, 256> st{boom_safe_try_program};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    return r.has_value() && output_of(st) == "CAUGHT 42 OK 99 " &&
           st.data().depth() == 0;
}());

TEST_CASE("InterpTest - BoomSafeTryMergeCriterion") {
    forth_state<64, 64, 1024, 256> st{boom_safe_try_program};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    REQUIRE(r.has_value());
    CHECK(output_of(st) == "CAUGHT 42 OK 99 ");
    CHECK(st.data().depth() == 0);
}

// `xt CATCH` while interpreting, not compiled into any colon word -- CATCH
// works identically either way (D14), exactly like EXECUTE.
TEST_CASE("InterpTest - CatchWorksWhileInterpreting") {
    forth_state<64, 64, 1024, 256> st{": BOOM 42 THROW ;  ' BOOM CATCH"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    REQUIRE(r.has_value());
    REQUIRE(st.data().depth() == 1);
    CHECK(st.data().peek().value() == 42);
}

TEST_CASE("InterpTest - CatchOfSafeWordWhileInterpreting") {
    forth_state<64, 64, 1024, 256> st{": SAFE 99 ;  ' SAFE CATCH"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    REQUIRE(r.has_value());
    REQUIRE(st.data().depth() == 2);
    CHECK(st.data().peek(1).value() == 99);
    CHECK(st.data().peek(0).value() == 0);
}

// Depth-restoration test: whatever the thrown word pushed *after* CATCH
// began (1, 2, 3 here) is discarded; whatever was on the stack *before*
// CATCH ran (5, 6 here) survives untouched, with the thrown code on top.
TEST_CASE("InterpTest - CatchRestoresDataStackDepth") {
    forth_state<64, 64, 1024, 256> st{
        ": MESSY 1 2 3 999 THROW ;  5 6 ' MESSY CATCH"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    REQUIRE(r.has_value());
    REQUIRE(st.data().depth() == 3);
    CHECK(st.data().peek(2).value() == 5);
    CHECK(st.data().peek(1).value() == 6);
    CHECK(st.data().peek(0).value() == 999);
}

// Nested CATCH: INNER's own throw is caught by OUTER's own inner CATCH and
// handled there; OUTER's own later throw escapes past that (already
// completed) inner handler straight to OUTERTRY's own outer one -- proving
// handler-depth restoration chains correctly across nested scopes, not just
// within one.
TEST_CASE("InterpTest - NestedCatchEscapesToTheRightHandler") {
    forth_state<64, 64, 1024, 256> st{
        ": INNER 1 THROW ; "
        ": OUTER ['] INNER CATCH DUP IF .\" INNER-CAUGHT \" . ELSE DROP "
        ".\" INNER-OK \" THEN 2 THROW ; "
        ": OUTERTRY ['] OUTER CATCH DUP IF .\" OUTER-CAUGHT \" . ELSE DROP "
        ".\" OUTER-OK \" THEN ; "
        "OUTERTRY"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    REQUIRE(r.has_value());
    CHECK(output_of(st) == "INNER-CAUGHT 1 OUTER-CAUGHT 2 ");
    CHECK(st.data().depth() == 0);
    CHECK(st.returns().depth() == 0);
}

// Interaction battery (Part 11's own recorded, unverified return-stack
// teardown gap, F17): a call frame (GUARD calling DEEP), a `>R` value, and a
// DO-loop frame all sit above CATCH's own handler frame when THROW fires
// inside the loop body -- THROW must unwind through all three in one
// truncate, without knowing or caring what any of them are (vm.hpp's own
// perform_throw). FIND5B runs afterward, using DO/LEAVE normally, proving
// ordinary DO-loop teardown is unaffected and the return stack is genuinely
// back to depth 0, not merely "shifted but not yet crashed."
TEST_CASE("InterpTest - CatchUnwindsThroughToRDoLoopAndCallFrames") {
    forth_state<64, 64, 1024, 256> st{
        ": DEEP 42 >R 10 0 DO 5 THROW LOOP R> DROP ; "
        ": GUARD ['] DEEP CATCH DUP IF .\" CAUGHT \" . ELSE DROP .\" OK \" "
        "THEN ; "
        ": FIND5B 10 0 DO I 5 = IF I LEAVE THEN LOOP ; "
        "GUARD FIND5B"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    REQUIRE(r.has_value());
    CHECK(output_of(st) == "CAUGHT 5 ");
    REQUIRE(st.data().depth() == 1);
    CHECK(st.data().peek().value() == 5);
    // The return stack is exactly back to where it started: no leftover
    // cells from the >R, the DO-loop frame, or CATCH's own handler frame.
    CHECK(st.returns().depth() == 0);
}

// A machine fault (division by zero) mapped to its standard THROW code
// (-10) and caught, exactly like an explicit THROW would be (D7).
TEST_CASE("InterpTest - DivisionByZeroCaughtByCatch") {
    forth_state<64, 64, 1024, 256> st{
        ": DIVZERO 1 0 / ; "
        ": GUARDDIV ['] DIVZERO CATCH DUP IF .\" CAUGHT \" . ELSE DROP "
        ".\" OK \" THEN ; "
        "GUARDDIV"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    REQUIRE(r.has_value());
    CHECK(output_of(st) == "CAUGHT -10 ");
    CHECK(st.data().depth() == 0);
}

// An uncaught machine fault keeps its original, specific diagnostic message
// verbatim -- the fault-to-THROW-code mapping only ever applies when a
// handler is active (DIV-0018), so this is unchanged from every division-by
// -zero test that predates CATCH/THROW.
TEST_CASE("InterpTest - UncaughtDivisionByZeroKeepsItsOwnMessage") {
    forth_state<64, 64, 1024, 256> st{"1 0 /"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    REQUIRE_FALSE(r.has_value());
    CHECK(std::string_view{r.error().message} == "division by zero");
}

// ABORT" caught by an enclosing CATCH: the message still prints (ABORT"'s
// own runtime primitive is unchanged), and the code it now genuinely throws
// (-2) reaches the handler, exactly like an explicit `-2 THROW` would --
// RUN's own trailing `999` never runs, since THROW is a one-shot, upward,
// nonlocal exit (D11).
TEST_CASE("InterpTest - AbortQuoteCaughtByCatch") {
    forth_state<64, 64, 1024, 256> st{
        ": CHECK ABORT\" boom\" ; "
        ": RUN -1 CHECK 999 ; "
        ": GUARDABORT ['] RUN CATCH DUP IF .\" CAUGHT \" . ELSE DROP .\" OK \" "
        "THEN ; "
        "GUARDABORT"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    REQUIRE(r.has_value());
    CHECK(output_of(st) == "boomCAUGHT -2 ");
    CHECK(st.data().depth() == 0);
}

// ABORT is `-1 THROW` (Forth-2012), caught here like any other THROW.
TEST_CASE("InterpTest - AbortIsMinusOneThrow") {
    forth_state<64, 64, 1024, 256> st{": MAYBE ABORT ;  ' MAYBE CATCH"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    REQUIRE(r.has_value());
    REQUIRE(st.data().depth() == 1);
    CHECK(st.data().peek().value() == -1);
}

// ABORT while interpreting, uncaught: a diagnosed THROW carrying -1.
TEST_CASE("InterpTest - UncaughtAbortWhileInterpreting") {
    forth_state<64, 64, 1024, 256> st{"ABORT"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().where.offset == -1);
}

// `0 THROW` is a no-op (Forth-2012): execution continues normally.
TEST_CASE("InterpTest - ThrowZeroIsANoOp") {
    forth_state<64, 64, 1024, 256> st{"1 2 0 THROW +"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    REQUIRE(r.has_value());
    REQUIRE(st.data().depth() == 1);
    CHECK(st.data().peek().value() == 3);
}

// An uncaught THROW, typed directly at the top level (not compiled into any
// colon word): diagnosed, carrying n.
TEST_CASE("InterpTest - UncaughtThrowWhileInterpreting") {
    forth_state<64, 64, 1024, 256> st{"5 THROW"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().where.offset == 5);
    CHECK(st.data().depth() == 0);
}
