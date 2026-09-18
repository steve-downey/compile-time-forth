// src/smd/forth/interpreter/diagnostics.test.cpp                    -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Step F36 (consolidation, docs/forth-plan-2.md): the positioned-diagnostic
// half of this step's error-quality pass. Every prior step already leaves a
// scattering of individual "is this diagnosed, at the right position"
// TEST_CASEs and static_asserts next to the feature that needed one
// (interp.test.cpp's UnknownWordDiagnosesPosition-shaped static_asserts,
// effect_lint.test.cpp's DeclaredEffectMismatchDiagnosed, and so on). This
// file does not replace any of those; it is the table this step's own merge
// criterion asks for -- one place that walks a representative sample of bad
// programs across every diagnosis *kind* interp.hpp raises (an unresolved
// word, a malformed compile-time construct, a control-word misuse, a stack
// fault, and a declared-effect mismatch) and checks both the message *and*
// the source_pos it is reported at, table-driven, in one pass. Every
// expected offset/line/column below was captured by actually running the
// corresponding program through interpret() (not hand-computed), so a
// change to the diagnostic's own wording or position is exactly what turns
// this table red. The "unknown_word" case has the same shape (and reason)
// as interp.test.cpp's own UnknownWordDiagnosesPosition-style static_assert
// near its top (`1 2 FOO`, offset 4): kept here too, as the table's first
// row, so the table is a complete battery on its own rather than one that
// silently depends on a sibling file for its simplest case.

#include <smd/forth/interpreter/interp.hpp>
#include <smd/forth/interpreter/interp.hpp> // test 2nd include OK

#include <smd/forth/foundation/parse_error.hpp>
#include <smd/forth/interpreter/compilebuf.hpp>
#include <smd/forth/machine/dictionary.hpp>
#include <smd/forth/machine/forth_state.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string_view>

using smd::forth::interpreter::compile_buffer;
using smd::forth::interpreter::interpret;
using smd::forth::machine::default_dictionary;
using smd::forth::machine::forth_state;

TEST_CASE("DiagnosticsTest - HeaderIsIdempotent") { REQUIRE(true); }

namespace {

struct diagnostic_case {
    std::string_view name;
    std::string_view program;
    char const *message;
    int offset;
    int line;
    int column;
};

// Table-driven positioned-diagnostic battery (this project's own idiom for
// a table-driven test: a constexpr std::array<Case, N> plus a loop, per
// conformance/gforth_diff.test.cpp's own battery). One representative bad
// program per diagnosis kind interp.hpp raises: an unresolved name, an
// unterminated compile-time construct, a control word used outside the
// structure it requires, a runtime stack fault, a declared-vs-computed
// effect mismatch (F30's effect lint, promoted to a hard gate exactly when
// a declared effect is present, D20), and POSTPONE naming an unresolved
// word.
constexpr std::array<diagnostic_case, 7> diagnostic_battery{{
    {"unknown_word", "1 2 FOO", "unknown word", 4, 1, 5},
    {"unterminated_colon_definition", ": UNCLOSED DUP *",
     "unterminated colon definition", 16, 1, 17},
    {"declared_effect_mismatch", ": BADDECL ( n -- n n ) DUP DROP ;",
     "declared stack effect does not match computed stack effect", 32, 1, 33},
    {"leave_outside_loop", ": BADLV LEAVE ;", "LEAVE outside a DO ... LOOP", 8,
     1, 9},
    {"stack_underflow", "DROP", "stack underflow", 0, 1, 1},
    {"exit_inside_do_without_unloop", ": DOEX DO EXIT LOOP ;",
     "EXIT inside a DO loop requires UNLOOP", 20, 1, 21},
    {"postpone_unknown_word", ": P1 POSTPONE FOOBAR ;",
     "POSTPONE: unknown word", 5, 1, 6},
}};

} // namespace

TEST_CASE(
    "DiagnosticsTest - PositionedDiagnosticBatteryReportsMessageAndPosition") {
    for (auto const &c : diagnostic_battery) {
        INFO("case: " << c.name << " (" << c.program << ")");

        forth_state<64, 64, 1024, 256> st{c.program};
        auto dict = default_dictionary<>();
        compile_buffer<> buf;
        auto r = interpret(st, dict, buf);

        REQUIRE_FALSE(r.has_value());
        CHECK(std::string_view{r.error().message} == c.message);
        CHECK(r.error().where.offset == c.offset);
        CHECK(r.error().where.line == c.line);
        CHECK(r.error().where.column == c.column);
    }
}

// The same battery's first three cases, constant-evaluated (the
// immediately-invoked-lambda static_assert pattern this project's own house
// style asks every constexpr contract to carry): interpret() is itself
// constexpr, so a positioned diagnostic is exactly as checkable at compile
// time as at runtime -- the Catch2 loop above is not standing in for a
// missing compile-time guarantee, it is covering the same contract with a
// table a compile-time check alone could not report all 7 members of one
// failure at a time for.
static_assert([] {
    forth_state<64, 64, 1024, 256> st{"1 2 FOO"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    return !r.has_value() &&
           std::string_view{r.error().message} == "unknown word" &&
           r.error().where.offset == 4 && r.error().where.column == 5;
}());

static_assert([] {
    forth_state<64, 64, 1024, 256> st{": BADDECL ( n -- n n ) DUP DROP ;"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    return !r.has_value() &&
           std::string_view{r.error().message} ==
               "declared stack effect does not match computed stack effect" &&
           r.error().where.offset == 32;
}());

static_assert([] {
    forth_state<64, 64, 1024, 256> st{"DROP"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    return !r.has_value() &&
           std::string_view{r.error().message} == "stack underflow" &&
           r.error().where.offset == 0;
}());

// The positive twins of the two negative-compile tests this step adds
// (test_neg_effect_mismatch.cpp, test_neg_capacity_overflow.cpp). A
// WILL_FAIL CTest proves only that its program is a hard compile error; the
// compiler's own output for a failed constexpr initializer never names the
// parse_error message, so "fails for its stated reason" is checked here
// instead: the same program, through interpret(), diagnosed with exactly
// the message the negative test's file comment claims. The declared-effect
// twin is the battery's BADDECL row and static_assert above; this is the
// capacity twin -- "1 2 3 4 5" against a data stack of four cells. The
// position is the origin, as for the battery's stack_underflow row: a
// stack fault is diagnosed by the machine substrate, which carries no
// source-position concept of its own, and the interpreter surfaces that
// error unchanged rather than repositioning it (Phase 7).
static_assert([] {
    forth_state<4, 64, 1024, 256> st{"1 2 3 4 5"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    return !r.has_value() &&
           std::string_view{r.error().message} == "stack overflow" &&
           r.error().where.offset == 0 && r.error().where.line == 1 &&
           r.error().where.column == 1;
}());
