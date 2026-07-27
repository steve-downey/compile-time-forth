// src/smd/forth/interpreter/effect_lint.test.cpp                    -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <smd/forth/interpreter/effect_lint.hpp>
#include <smd/forth/interpreter/effect_lint.hpp> // test 2nd include OK

#include <catch2/catch_test_macros.hpp>

#include <string_view>

using smd::forth::foundation::source_pos;
using smd::forth::foundation::source_span;
using smd::forth::interpreter::check_definition_effect;
using smd::forth::interpreter::combine_branch;
using smd::forth::interpreter::combine_sequential;
using smd::forth::interpreter::has_declared_effect;
using smd::forth::interpreter::identity_effect;
using smd::forth::interpreter::instruction_effect;
using smd::forth::interpreter::instruction_successors;
using smd::forth::interpreter::known;
using smd::forth::interpreter::parse_declared_effect;
using smd::forth::interpreter::primitive_data_effect;
using smd::forth::interpreter::primitive_return_delta;
using smd::forth::interpreter::recover_basic_blocks;
using smd::forth::interpreter::recover_loop_regions;
using smd::forth::interpreter::unknown_effect;
using smd::forth::machine::cell;
using smd::forth::machine::compiled_program;
using smd::forth::machine::default_dictionary;
using smd::forth::machine::instr;
using smd::forth::machine::op;
using smd::forth::machine::primitive;

TEST_CASE("EffectLintTest - HeaderIsIdempotent") { REQUIRE(true); }

// -- Lattice primitives, in isolation --------------------------------------
//
// Ported verbatim (values and comments unchanged) from the R1 elaborator's
// own elaborator/stack_effect.test.cpp, which step F26 deletes along with
// the rest of elaborator/ -- these static_asserts exercise only the
// tree-independent lattice this header preserves for F30, not the deleted
// tree-walking analyze_body.

static_assert(known(1, 1).net() == 0);
static_assert(known(1, 2).net() == 1);
static_assert(!unknown_effect.known);
static_assert(identity_effect == known(0, 0));

// Identity element: combining with identity_effect on either side is a no-op.
static_assert(combine_sequential(identity_effect, known(2, 3)) == known(2, 3));
static_assert(combine_sequential(known(2, 3), identity_effect) == known(2, 3));

// "DUP *" == SQUARED's body: (1,2) then (2,1) composes to (1,1).
static_assert(combine_sequential(known(1, 2), known(2, 1)) == known(1, 1));

// "1 +": needs one operand already on the stack, leaves one behind.
static_assert(combine_sequential(known(0, 1), known(2, 1)) == known(1, 1));

// Unknown poisons sequencing from either side.
static_assert(!combine_sequential(unknown_effect, known(1, 1)).known);
static_assert(!combine_sequential(known(1, 1), unknown_effect).known);

// IF/ELSE combination: NEGATE (1,1), net 0, vs. an empty ELSE-less arm (0,0),
// net 0 -- same net, different shape; combine_branch reflects "the flag plus
// whichever arm needs more, net-adjusted".
static_assert(combine_branch(known(1, 1), known(0, 0)) == known(2, 1));

// -- Primitive net-effect table ---------------------------------------------

static_assert(primitive_data_effect(primitive::plus) == known(2, 1));
static_assert(primitive_data_effect(primitive::dup) == known(1, 2));
static_assert(primitive_data_effect(primitive::drop) == known(1, 0));
static_assert(primitive_data_effect(primitive::swap) == known(2, 2));
static_assert(primitive_data_effect(primitive::over) == known(2, 3));
static_assert(primitive_data_effect(primitive::rot) == known(3, 3));
static_assert(primitive_data_effect(primitive::depth) == known(0, 1));
static_assert(primitive_data_effect(primitive::true_) == known(0, 1));
static_assert(!primitive_data_effect(primitive::question_dup).known);
static_assert(primitive_data_effect(primitive::to_r) == known(1, 0));
static_assert(primitive_data_effect(primitive::r_from) == known(0, 1));
static_assert(primitive_data_effect(primitive::r_fetch) == known(0, 1));

// Memory (Step F16): none of the four are input-dependent, unlike ?DUP.
static_assert(primitive_data_effect(primitive::fetch) == known(1, 1));
static_assert(primitive_data_effect(primitive::store) == known(2, 0));
static_assert(primitive_data_effect(primitive::plus_store) == known(2, 0));
static_assert(primitive_data_effect(primitive::allot) == known(1, 0));

// Step F28: `,` is likewise not input-dependent.
static_assert(primitive_data_effect(primitive::comma) == known(1, 0));

// Step F31: catch_ok pushes exactly one flag, consuming nothing from the
// data stack (its own bookkeeping cells all live on the return stack).
static_assert(primitive_data_effect(primitive::catch_ok) == known(0, 1));

static_assert(primitive_return_delta(primitive::to_r) == 1);
static_assert(primitive_return_delta(primitive::r_from) == -1);
static_assert(primitive_return_delta(primitive::r_fetch) == 0);
static_assert(primitive_return_delta(primitive::dup) == 0);

// -- Declared-effect parsing --------------------------------------------------

static_assert(!has_declared_effect(source_span{}));
static_assert(has_declared_effect(source_span{source_pos{0, 1, 1},
                                              source_pos{1, 1, 2}}));

static_assert([] {
    constexpr std::string_view text = "( n -- n )";
    constexpr source_span span{source_pos{0, 1, 1},
                               source_pos{static_cast<int>(text.size()), 1,
                                          static_cast<int>(text.size()) + 1}};
    return parse_declared_effect(text, span) == known(1, 1);
}());

static_assert([] {
    constexpr std::string_view text = "( -- n )";
    constexpr source_span span{source_pos{0, 1, 1},
                               source_pos{static_cast<int>(text.size()), 1,
                                          static_cast<int>(text.size()) + 1}};
    return parse_declared_effect(text, span) == known(0, 1);
}());

// -- Step F30: CFG recovery and the checker, over hand-built instructions ----
//
// The F12/F17 program battery is reproduced end-to-end through `interpret()`
// in `interp.test.cpp` (`EffectLintTest -` prefix); these exercise this
// header's own new constexpr APIs directly, over hand-built
// `machine::compiled_program`/`machine::dictionary` values, the same style
// `compilebuf.test.cpp` already uses for `machine::instr` sequences.

// -- instruction_successors ---------------------------------------------------

static_assert([] {
    instr const push_i{.code = op::push, .operand = cell{5}};
    auto const e = instruction_successors(push_i, 3);
    return e.a == 4 && e.b == -1;
}());

static_assert([] {
    instr const ret_i{.code = op::ret, .operand = cell{0}};
    auto const e = instruction_successors(ret_i, 7);
    return e.a == -1 && e.b == -1;
}());

static_assert([] {
    instr const branch_i{.code = op::branch, .operand = cell{10}};
    auto const e = instruction_successors(branch_i, 2);
    return e.a == 10 && e.b == -1;
}());

static_assert([] {
    instr const branch0_i{.code = op::branch0, .operand = cell{20}};
    auto const e = instruction_successors(branch0_i, 5);
    return e.a == 6 && e.b == 20; // fallthrough, then the jump target.
}());

static_assert([] {
    // LEAVE never falls through -- its own single successor is the loop
    // exit its own operand names, not `index + 1`.
    instr const leave_i{.code = op::leave, .operand = cell{15}};
    auto const e = instruction_successors(leave_i, 9);
    return e.a == 15 && e.b == -1;
}());

// -- instruction_effect
// --------------------------------------------------------

static_assert([] {
    compiled_program<16, 8> program{};
    program.code.push_back(instr{.code = op::push, .operand = cell{5}});
    auto const dict = default_dictionary<>();
    auto const e = instruction_effect(program, dict, 0, -1, unknown_effect);
    return e.data == known(0, 1) && e.ret_delta == 0;
}());

static_assert([] {
    compiled_program<16, 8> program{};
    program.code.push_back(
        instr{.code = op::prim, .operand = static_cast<cell>(primitive::dup)});
    auto const dict = default_dictionary<>();
    auto const e = instruction_effect(program, dict, 0, -1, unknown_effect);
    return e.data == known(1, 2) && e.ret_delta == 0;
}());

static_assert([] {
    // `>R`'s own data-stack view (pops 1, pushes 0) is distinct from its
    // return-stack contribution (+1) -- both come back from one call.
    compiled_program<16, 8> program{};
    program.code.push_back(
        instr{.code = op::prim, .operand = static_cast<cell>(primitive::to_r)});
    auto const dict = default_dictionary<>();
    auto const e = instruction_effect(program, dict, 0, -1, unknown_effect);
    return e.data == known(1, 0) && e.ret_delta == 1;
}());

static_assert([] {
    // LEAVE and EXECUTE both map to unknown_effect (this header's own top
    // comment: LEAVE's target is a genuine join with the loop's own
    // normal-exhaustion path, so a known no-op treatment would falsely
    // conflict at it).
    compiled_program<16, 8> program{};
    program.code.push_back(instr{.code = op::leave, .operand = cell{3}});
    program.code.push_back(instr{.code = op::execute, .operand = cell{0}});
    auto const dict = default_dictionary<>();
    auto const leave_e =
        instruction_effect(program, dict, 0, -1, unknown_effect);
    auto const exec_e =
        instruction_effect(program, dict, 1, -1, unknown_effect);
    return !leave_e.data.known && !exec_e.data.known;
}());

// -- recover_loop_regions
// -------------------------------------------------------

static_assert([] {
    // do_setup at 0; dest (its own index + 1) is 1; loop_step at 2 closes
    // it, matched by its own operand equaling dest.
    compiled_program<16, 8> program{};
    program.code.push_back(instr{.code = op::do_setup, .operand = cell{0}});
    program.code.push_back(instr{.code = op::push, .operand = cell{1}});
    program.code.push_back(instr{.code = op::loop_step, .operand = cell{1}});
    auto const regions = recover_loop_regions(program, 0, 3);
    return regions.size() == 1 && regions[0].start == 1 && regions[0].end == 2;
}());

// -- recover_basic_blocks
// --------------------------------------------------------

static_assert([] {
    // branch0 at 0 (fallthrough 1, jump target 2); a push at 1; a ret at 2
    // -- three leaders (0, 1, 2), so three single-instruction blocks.
    compiled_program<16, 8> program{};
    program.code.push_back(instr{.code = op::branch0, .operand = cell{2}});
    program.code.push_back(instr{.code = op::push, .operand = cell{9}});
    program.code.push_back(instr{.code = op::ret, .operand = cell{0}});
    auto const blocks = recover_basic_blocks<8>(program, 0, 3);
    if (!blocks.has_value()) {
        return false;
    }
    auto const &bs = blocks.value();
    return bs.size() == 3 && bs[0].start == 0 && bs[0].end == 1 &&
           bs[1].start == 1 && bs[1].end == 2 && bs[2].start == 2 &&
           bs[2].end == 3;
}());

// Step F33 (docs/forth-plan-2.md), DIV-0025: the leader-computation
// regression guard the case above could not be. A straight-line run with no
// branch anywhere (push, push, prim +, ret) must recover as exactly *one*
// block -- an earlier draft of recover_basic_blocks added every ordinary
// instruction's own fallthrough edge as a leader unconditionally, making
// every block one instruction long regardless of whether any branch was
// nearby. The case above never caught this: its own leading branch0 already
// forces three single-instruction blocks for an unrelated reason, so the
// defect and the fix agree on that one input. This one does not.
static_assert([] {
    compiled_program<16, 8> program{};
    program.code.push_back(instr{.code = op::push, .operand = cell{1}});
    program.code.push_back(instr{.code = op::push, .operand = cell{2}});
    program.code.push_back(
        instr{.code = op::prim, .operand = static_cast<cell>(primitive::plus)});
    program.code.push_back(instr{.code = op::ret, .operand = cell{0}});
    auto const blocks = recover_basic_blocks<8>(program, 0, 4);
    if (!blocks.has_value()) {
        return false;
    }
    auto const &bs = blocks.value();
    return bs.size() == 1 && bs[0].start == 0 && bs[0].end == 4;
}());

// -- check_definition_effect
// -----------------------------------------------------

// SQUARED's own shape (`DUP *`), hand-built rather than compiled through
// `interpret()`: net effect known(1, 1), and a hand-computed peak depth of
// 2 (DUP takes one input to two; `*` brings it back to one).
static_assert([] {
    compiled_program<16, 8> program{};
    program.code.push_back(instr{.code = op::halt, .operand = cell{0}});
    int const entry = 1;
    program.code.push_back(
        instr{.code = op::prim, .operand = static_cast<cell>(primitive::dup)});
    program.code.push_back(
        instr{.code = op::prim, .operand = static_cast<cell>(primitive::star)});
    program.code.push_back(instr{.code = op::ret, .operand = cell{0}});
    auto const dict = default_dictionary<>();
    auto const result = check_definition_effect(
        program, dict, entry, 4, "", source_span{}, false, source_pos{});
    if (!result.has_value()) {
        return false;
    }
    auto const &eff = result.value();
    return eff.net == known(1, 1) && eff.peak_depth == 2;
}());

// A DO-loop body with a nonzero net effect (the F17 correction) is still
// *recognized* even hand-built, not just through `interpret()` -- but
// undeclared (`has_declared = false` here), it is advisory (D20's own
// "advisory unless declared," DIV-0019's post-merge amendment): accepted,
// with the disagreement collected onto the returned definition_effect's
// own diagnostics rather than aborting the whole check.
static_assert([] {
    compiled_program<16, 8> program{};
    program.code.push_back(instr{.code = op::halt, .operand = cell{0}});
    int const entry = 1;
    program.code.push_back(instr{.code = op::do_setup, .operand = cell{0}});
    program.code.push_back(
        instr{.code = op::prim,
              .operand = static_cast<cell>(primitive::dup)}); // dest, net +1
    program.code.push_back(instr{.code = op::loop_step, .operand = cell{2}});
    program.code.push_back(instr{.code = op::ret, .operand = cell{0}});
    auto const dict = default_dictionary<>();
    auto const result = check_definition_effect(
        program, dict, entry, 5, "", source_span{}, false, source_pos{});
    if (!result.has_value()) {
        return false;
    }
    return !result.value().net.known && result.value().diagnostics.size() == 1;
}());

// The same shape, declared: promoted to a hard, unconditional error (D20's
// own "promoted to a hard gate ... exactly when a declared effect is
// present").
static_assert([] {
    compiled_program<16, 8> program{};
    program.code.push_back(instr{.code = op::halt, .operand = cell{0}});
    int const entry = 1;
    program.code.push_back(instr{.code = op::do_setup, .operand = cell{0}});
    program.code.push_back(
        instr{.code = op::prim, .operand = static_cast<cell>(primitive::dup)});
    program.code.push_back(instr{.code = op::loop_step, .operand = cell{2}});
    program.code.push_back(instr{.code = op::ret, .operand = cell{0}});
    auto const dict = default_dictionary<>();
    constexpr std::string_view text = "( n -- n )";
    constexpr source_span span{source_pos{0, 1, 1},
                               source_pos{static_cast<int>(text.size()), 1,
                                          static_cast<int>(text.size()) + 1}};
    auto const result = check_definition_effect(program, dict, entry, 5, text,
                                                span, true, source_pos{});
    return !result.has_value();
}());
