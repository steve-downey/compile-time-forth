// src/smd/forth/machine/codegen.hpp                                  -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#ifndef SRC_SMD_FORTH_MACHINE_CODEGEN_HPP
#define SRC_SMD_FORTH_MACHINE_CODEGEN_HPP

#include <smd/forth/elaborator/elaborated_core.hpp>
#include <smd/forth/foundation/parse_error.hpp>
#include <smd/forth/foundation/result.hpp>
#include <smd/forth/foundation/source_pos.hpp>
#include <smd/forth/machine/cell.hpp>
#include <smd/forth/machine/dictionary.hpp>
#include <smd/forth/machine/instruction.hpp>

#include <type_traits>
#include <variant>

namespace smd::forth::machine {

// Step F14 (docs/forth-plan.md): flattens an elaborator::compiled_unit (the
// same input F13's eval_direct.hpp consumes) into a compiled_program: a flat
// instr array plus a word table. codegen_emit_body/codegen_emit_node below
// are the direct codegen analogue of eval_direct.hpp's eval_body/eval_node
// pair -- same recursive shape over the same closed core_node variant,
// producing instructions into a flat array instead of executing side
// effects immediately.
//
// One-pass call resolution: codegen walks unit.dictionary in index order,
// emitting each colon word's own body before moving to the next entry. F11's
// static-binding discipline guarantees a core_call's word_index is always
// either strictly less than the definition currently being emitted (an
// earlier, already-elaborated word) or exactly equal to it (RECURSE, a
// self-call) -- a core_call can never reference a word defined later, since
// elaboration only ever resolves a name against the dictionary as it stood
// at that reference's own position in the source. Recording entry_points[i]
// *before* emitting word i's own body (not after) is what makes RECURSE
// resolve too: by the time the body's own self-call is reached, its entry
// point is already in the table. This means, unlike control-structure
// branches, `call` operands never need back-patching -- only `branch`/
// `branch0` targets do, since a forward jump's target instruction has not
// been emitted yet when the jump itself is emitted.

/// Appends @p code/@p operand to @p out's instruction array, diagnosing
/// overflow rather than exceeding @p out's own @c MaxCode capacity.
/// Returns the newly appended instruction's own index -- callers that will
/// need to back-patch this instruction's operand later (an `IF`/`BEGIN`'s
/// own forward branch) keep this index.
template <int MaxCode, int MaxWords>
constexpr auto emit(compiled_program<MaxCode, MaxWords> &out, op code,
                    cell operand, foundation::source_pos pos)
    -> foundation::result<int> {
    if (out.code.size() >= MaxCode) {
        return foundation::parse_error{pos, "compiled program exceeds MaxCode "
                                            "capacity"};
    }
    int const index = out.code.size();
    out.code.push_back(instr{.code = code, .operand = operand});
    return index;
}

// Forward declarations: codegen_emit_body and codegen_emit_node are mutually
// recursive, mirroring eval_direct.hpp's eval_body/eval_node.

template <int MaxCode, int MaxNodes, int MaxBody, int MaxName, int MaxWords,
          int MaxData, int MaxWarnings>
constexpr auto codegen_emit_body(
    elaborator::compiled_unit<MaxNodes, MaxBody, MaxName, MaxWords, MaxData,
                              MaxWarnings> const &unit,
    elaborator::core_body<MaxNodes, MaxBody> const &items,
    compiled_program<MaxCode, MaxWords> &out) -> status;

template <int MaxCode, int MaxNodes, int MaxBody, int MaxName, int MaxWords,
          int MaxData, int MaxWarnings>
constexpr auto codegen_emit_node(
    elaborator::compiled_unit<MaxNodes, MaxBody, MaxName, MaxWords, MaxData,
                              MaxWarnings> const &unit,
    elaborator::core_node<MaxNodes, MaxBody> const &node,
    compiled_program<MaxCode, MaxWords> &out) -> status;

// 926e480d-dcc6-4168-b263-af0c73f1a45f
/// Emits @p node's own instructions into @p out, per @ref op's documented
/// shape.
///
/// `core_if` and `core_begin_until`/`core_begin_while` back-patch their own
/// forward branch operands: a placeholder `branch`/`branch0` is emitted with
/// a temporary operand of 0, its own instruction index is remembered, and
/// once the jump's real target instruction has actually been emitted (the
/// arm/loop-exit that follows), @p out's own instruction array is indexed
/// directly to overwrite that placeholder's operand -- the classic
/// single-pass assembler technique the plan's own wording ("computed by
/// back-patching inside codegen") names directly. Backward branches (a
/// `BEGIN ... UNTIL`/`BEGIN ... WHILE`'s own jump back to the loop's start)
/// never need this: the loop's start instruction has already been emitted by
/// the time the backward jump is, so its index is already known.
template <int MaxCode, int MaxNodes, int MaxBody, int MaxName, int MaxWords,
          int MaxData, int MaxWarnings>
constexpr auto codegen_emit_node(
    elaborator::compiled_unit<MaxNodes, MaxBody, MaxName, MaxWords, MaxData,
                              MaxWarnings> const &unit,
    elaborator::core_node<MaxNodes, MaxBody> const &node,
    compiled_program<MaxCode, MaxWords> &out) -> status {
    return std::visit(
        [&](auto const &alt) -> status {
            using T = std::decay_t<decltype(alt)>;

            if constexpr (std::is_same_v<T, elaborator::core_push>) {
                auto r = emit(out, op::push, alt.value, alt.pos);
                if (!r.has_value()) {
                    return r.error();
                }
                return std::monostate{};
            } else if constexpr (std::is_same_v<T, elaborator::core_prim>) {
                auto r =
                    emit(out, op::prim, static_cast<cell>(alt.op), alt.pos);
                if (!r.has_value()) {
                    return r.error();
                }
                return std::monostate{};
            } else if constexpr (std::is_same_v<T, elaborator::core_call>) {
                auto const &entry = unit.dictionary.entry_at(alt.word_index);
                auto const *cw = std::get_if<colon_word>(&entry.binding);
                if (cw == nullptr) {
                    return foundation::parse_error{
                        alt.pos, "call target is not a colon word"};
                }
                int const target = out.entry_points[alt.word_index];
                if (target < 0) {
                    // Defensive only: the one-pass dictionary-order walk in
                    // codegen() guarantees every colon-word call target has
                    // already been emitted (see this header's own top
                    // comment) -- unreachable in practice.
                    return foundation::parse_error{
                        alt.pos, "call target has no resolved entry point"};
                }
                auto r =
                    emit(out, op::call, static_cast<cell>(target), alt.pos);
                if (!r.has_value()) {
                    return r.error();
                }
                return std::monostate{};
            } else if constexpr (std::is_same_v<T, elaborator::core_var>) {
                auto r = emit(out, op::push, static_cast<cell>(alt.address),
                              alt.pos);
                if (!r.has_value()) {
                    return r.error();
                }
                return std::monostate{};
            } else if constexpr (std::is_same_v<T, elaborator::core_const>) {
                auto r = emit(out, op::push, alt.value, alt.pos);
                if (!r.has_value()) {
                    return r.error();
                }
                return std::monostate{};
            } else if constexpr (std::is_same_v<T, elaborator::core_push_xt>) {
                auto r = emit(out, op::push_xt,
                              static_cast<cell>(alt.word_index), alt.pos);
                if (!r.has_value()) {
                    return r.error();
                }
                return std::monostate{};
            } else if constexpr (std::is_same_v<T, elaborator::core_exit>) {
                auto r = emit(out, op::ret, cell{0}, alt.pos);
                if (!r.has_value()) {
                    return r.error();
                }
                return std::monostate{};
            } else if constexpr (std::is_same_v<T,
                                                elaborator::core_loop_index>) {
                // `I`/`J`: the level (0/1) is the operand; the VM reads the
                // return-stack loop frame at offset 2*level.
                auto r = emit(out, op::push_index, static_cast<cell>(alt.level),
                              alt.pos);
                if (!r.has_value()) {
                    return r.error();
                }
                return std::monostate{};
            } else if constexpr (std::is_same_v<T, elaborator::core_leave>) {
                // A forward branch to the enclosing loop's exit. Its target is
                // not known until that loop's own `loop_step` has been emitted,
                // so it is emitted with a -1 sentinel operand and back-patched
                // by the enclosing core_do_loop below. (-1 can never be a real
                // instruction index, so the sentinel is unambiguous, and an
                // inner nested loop patches its own leaves first, leaving only
                // this loop's own leaves still at -1 for it to find.)
                auto r = emit(out, op::leave, cell{-1}, alt.pos);
                if (!r.has_value()) {
                    return r.error();
                }
                return std::monostate{};
            } else if constexpr (std::is_same_v<T, elaborator::core_unloop>) {
                auto r = emit(out, op::unloop, cell{0}, alt.pos);
                if (!r.has_value()) {
                    return r.error();
                }
                return std::monostate{};
            } else if constexpr (std::is_same_v<T, elaborator::core_if<
                                                       MaxNodes, MaxBody>>) {
                auto branch0_r = emit(out, op::branch0, cell{0}, alt.pos);
                if (!branch0_r.has_value()) {
                    return branch0_r.error();
                }
                int const branch0_index = branch0_r.value();

                auto then_r = codegen_emit_body(unit, alt.then_body, out);
                if (!then_r.has_value()) {
                    return then_r;
                }

                if (alt.else_body.empty()) {
                    out.code[branch0_index].operand =
                        static_cast<cell>(out.code.size());
                    return std::monostate{};
                }

                auto branch_r = emit(out, op::branch, cell{0}, alt.pos);
                if (!branch_r.has_value()) {
                    return branch_r.error();
                }
                int const branch_index = branch_r.value();
                out.code[branch0_index].operand =
                    static_cast<cell>(out.code.size());

                auto else_r = codegen_emit_body(unit, alt.else_body, out);
                if (!else_r.has_value()) {
                    return else_r;
                }
                out.code[branch_index].operand =
                    static_cast<cell>(out.code.size());
                return std::monostate{};
            } else if constexpr (std::is_same_v<T, elaborator::core_begin_until<
                                                       MaxNodes, MaxBody>>) {
                int const loop_start = out.code.size();
                auto body_r = codegen_emit_body(unit, alt.body, out);
                if (!body_r.has_value()) {
                    return body_r;
                }
                // UNTIL: pop the flag; loop back while it is still false
                // (branch0 branches when the popped flag is zero).
                auto r = emit(out, op::branch0, static_cast<cell>(loop_start),
                              alt.pos);
                if (!r.has_value()) {
                    return r.error();
                }
                return std::monostate{};
            } else if constexpr (std::is_same_v<T, elaborator::core_begin_while<
                                                       MaxNodes, MaxBody>>) {
                int const loop_start = out.code.size();
                auto cond_r = codegen_emit_body(unit, alt.condition, out);
                if (!cond_r.has_value()) {
                    return cond_r;
                }
                auto branch0_r = emit(out, op::branch0, cell{0}, alt.pos);
                if (!branch0_r.has_value()) {
                    return branch0_r.error();
                }
                int const branch0_index = branch0_r.value();

                auto body_r = codegen_emit_body(unit, alt.body, out);
                if (!body_r.has_value()) {
                    return body_r;
                }
                auto branch_r = emit(out, op::branch,
                                     static_cast<cell>(loop_start), alt.pos);
                if (!branch_r.has_value()) {
                    return branch_r.error();
                }
                out.code[branch0_index].operand =
                    static_cast<cell>(out.code.size());
                return std::monostate{};
                // 5f3a8d1c-9b6e-4a20-8d4f-1c7b2e9a6f38
            } else if constexpr (std::is_same_v<T, elaborator::core_do_loop<
                                                       MaxNodes, MaxBody>>) {
                // `limit start DO <body> LOOP/+LOOP` (F17):
                //
                //     do_setup                  ; frame <- (limit start)
                //   start: <body>
                //     loop_step | plus_loop_step  operand=start
                //   exit: ...                   ; LEAVE branches here
                //
                // do_setup pops (limit start) into a return-stack frame;
                // loop_step/plus_loop_step advance the index and either branch
                // back to `start` or fall through (tearing the frame down) to
                // `exit`. Every LEAVE emitted inside <body> is back-patched to
                // `exit` here.
                auto setup_r = emit(out, op::do_setup, cell{0}, alt.pos);
                if (!setup_r.has_value()) {
                    return setup_r.error();
                }
                int const loop_start = out.code.size();
                auto body_r = codegen_emit_body(unit, alt.body, out);
                if (!body_r.has_value()) {
                    return body_r;
                }
                auto step_r = emit(
                    out, alt.is_plus_loop ? op::plus_loop_step : op::loop_step,
                    static_cast<cell>(loop_start), alt.pos);
                if (!step_r.has_value()) {
                    return step_r.error();
                }
                int const loop_exit = out.code.size();
                // Back-patch this loop's own LEAVE branches (still at the -1
                // sentinel; any inner loop's leaves were already patched).
                for (int k = loop_start; k < loop_exit; ++k) {
                    if (out.code[k].code == op::leave &&
                        out.code[k].operand == cell{-1}) {
                        out.code[k].operand = static_cast<cell>(loop_exit);
                    }
                }
                return std::monostate{};
                // 5f3a8d1c-9b6e-4a20-8d4f-1c7b2e9a6f38 end
            } else {
                // core_seq: never visited here in practice -- a core_call's
                // target is unwrapped directly (see above), and core_seq is
                // otherwise only ever the top-level wrapper a
                // colon_word::core_id points at -- but std::visit must stay
                // exhaustive. Mirrors eval_direct.hpp's identical
                // defensive-only arm.
                return foundation::parse_error{
                    alt.pos, "unexpected nested sequence node"};
            }
        },
        node.value);
}
// 926e480d-dcc6-4168-b263-af0c73f1a45f end

/// Emits @p items in order into @p out -- the straight-line-sequence
/// counterpart to @ref codegen_emit_node, mirroring @c eval_direct.hpp's
/// @c eval_body.
template <int MaxCode, int MaxNodes, int MaxBody, int MaxName, int MaxWords,
          int MaxData, int MaxWarnings>
constexpr auto codegen_emit_body(
    elaborator::compiled_unit<MaxNodes, MaxBody, MaxName, MaxWords, MaxData,
                              MaxWarnings> const &unit,
    elaborator::core_body<MaxNodes, MaxBody> const &items,
    compiled_program<MaxCode, MaxWords> &out) -> status {
    for (int i = 0; i < items.size(); ++i) {
        auto const &node = unit.arena.get(items[i]);
        auto r = codegen_emit_node(unit, node, out);
        if (!r.has_value()) {
            return r;
        }
    }
    return std::monostate{};
}

// ba7eecbe-a745-4d25-bf90-ffa3615efc19
/// Flattens @p unit (an elaborator::compiled_unit that has already passed
/// F11 resolution and F12 stack-effect analysis) into a @ref
/// compiled_program: every colon word in @p unit's dictionary becomes an
/// entry point (walked in dictionary-index order, per this header's own top
/// comment on why that ordering resolves every call without back-patching),
/// followed by the top-level program body (@ref
/// compiled_program::program_entry marks where it starts), followed by a
/// trailing @ref op::halt.
///
/// @tparam MaxCode Instruction-array capacity of the produced @ref
///                 compiled_program (see @ref compiled_program).
template <int MaxCode = 4096, int MaxNodes = 1024, int MaxBody = 64,
          int MaxName = 32, int MaxWords = 256, int MaxData = 1024,
          int MaxWarnings = 64>
constexpr auto
codegen(elaborator::compiled_unit<MaxNodes, MaxBody, MaxName, MaxWords, MaxData,
                                  MaxWarnings> const &unit)
    -> foundation::result<compiled_program<MaxCode, MaxWords>> {
    compiled_program<MaxCode, MaxWords> out{};

    for (int i = 0; i < unit.dictionary.size(); ++i) {
        out.entry_points.push_back(-1);
    }

    for (int i = 0; i < unit.dictionary.size(); ++i) {
        auto const &entry = unit.dictionary.entry_at(i);
        auto const *cw = std::get_if<colon_word>(&entry.binding);
        if (cw == nullptr) {
            continue;
        }
        // Recorded before emitting the body itself, so a RECURSE-produced
        // self-call inside this very body already resolves (this header's
        // own top comment).
        out.entry_points[i] = out.code.size();

        auto const &core = unit.arena.get(cw->core_id);
        auto const *seq =
            std::get_if<elaborator::core_seq<MaxNodes, MaxBody>>(&core.value);
        if (seq == nullptr) {
            return foundation::parse_error{
                foundation::source_pos{},
                "colon word's core is not a sequence"};
        }
        auto body_r = codegen_emit_body(unit, seq->items, out);
        if (!body_r.has_value()) {
            return body_r.error();
        }
        auto ret_r = emit(out, op::ret, cell{0}, seq->pos);
        if (!ret_r.has_value()) {
            return ret_r.error();
        }
    }

    out.program_entry = out.code.size();
    auto program_r = codegen_emit_body(unit, unit.program, out);
    if (!program_r.has_value()) {
        return program_r.error();
    }
    auto halt_r = emit(out, op::halt, cell{0}, foundation::source_pos{});
    if (!halt_r.has_value()) {
        return halt_r.error();
    }

    out.data_space_size = unit.data_space.size();
    return out;
}
// ba7eecbe-a745-4d25-bf90-ffa3615efc19 end

} // namespace smd::forth::machine

#endif
