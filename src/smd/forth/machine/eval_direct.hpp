// src/smd/forth/machine/eval_direct.hpp                              -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#ifndef SRC_SMD_FORTH_MACHINE_EVAL_DIRECT_HPP
#define SRC_SMD_FORTH_MACHINE_EVAL_DIRECT_HPP

#include <smd/forth/elaborator/elaborated_core.hpp>
#include <smd/forth/foundation/parse_error.hpp>
#include <smd/forth/foundation/result.hpp>
#include <smd/forth/foundation/source_pos.hpp>
#include <smd/forth/machine/cell.hpp>
#include <smd/forth/machine/dictionary.hpp>
#include <smd/forth/machine/forth_state.hpp>

#include <type_traits>
#include <variant>

namespace smd::forth::machine {

// Step F13 (docs/forth-plan.md): a structural-recursive reference
// interpreter over the elaborated core (F11/F12's `elaborator::
// compiled_unit`). This is the first stage that actually *runs* a program
// rather than only resolving or analyzing it -- by the time a compiled_unit
// reaches here, D9 guarantees every definition in it has already passed
// stack-effect analysis, but the evaluator does not trust that as a
// substitute for its own diagnosed bounds checks: every stack/return-stack
// misuse still goes through machine::apply_primitive's and cell_stack's own
// foundation::result-based diagnosis (D7), exactly as it would running
// directly against a forth_state.
//
// Lives in smd::forth::machine (not smd::forth::elaborator), per the plan's
// own placement (`src/smd/forth/machine/eval_direct.hpp`) and per
// handoff-next.md's F13 briefing: it depends on elaborator::compiled_unit
// the same way elaborator/*.hpp already depends on machine/*.hpp for
// dictionary/forth_state -- there is one shared build target
// (compile-time-forth.forth), so this is a documented layering exception,
// not a new library boundary.

// 0faeac96-c46d-48bb-a1db-c21191d35022
/// What happened after evaluating one core node or body.
///
/// `exited` is the runtime counterpart of a `core_exit` node: it is *not* a
/// C++ exception (per D11, all nonlocal control here is one-shot and
/// dynamic-extent, threaded through the evaluator's own result channel, not
/// unwound via the language's own exception mechanism). An `exited` signal
/// propagates upward through every nested control-structure body it is
/// returned from (`core_if`'s arms, a loop's own body) without evaluating
/// anything after it in that body, all the way up to the nearest enclosing
/// `core_call` (or the top-level program, which has nothing left to skip
/// past) -- exactly the "unwind to the enclosing definition's own boundary,
/// not just the current nested body" shape handoff.md's architectural
/// invariants call for. A `core_call` is where the signal is absorbed: the
/// callee's own `EXIT` never escapes past the call that invoked it, since a
/// call is itself a fresh definition boundary.
enum class eval_signal {
    normal, ///< Control reached the end of the body/node normally.
    exited, ///< A `core_exit` occurred; the caller must stop evaluating
            ///< anything else in the body currently unwinding through.
};

/// Decrements @p fuel, the evaluator's step budget, diagnosing exhaustion at
/// @p pos rather than letting a nonterminating program (e.g.
/// `BEGIN FALSE UNTIL`) hang the evaluator forever -- the compiler will
/// happily loop forever evaluating a `constexpr` function that never
/// terminates, so this is the only way to bound evaluation at compile time
/// (handoff-next.md's F13 briefing). Called once per core node visited by
/// @ref eval_node, so a budget of @p fuel bounds the total number of nodes
/// (including repeated visits to the same node across loop iterations) any
/// one evaluation may visit.
constexpr auto consume_fuel(int &fuel, foundation::source_pos pos)
    -> foundation::result<std::monostate> {
    if (fuel <= 0) {
        return foundation::parse_error{pos, "evaluation budget exhausted"};
    }
    --fuel;
    return std::monostate{};
}

// Forward declarations: eval_body and eval_node are mutually recursive (a
// body may contain IF/BEGIN, each of which evaluates its own nested
// body/bodies) -- the same shape elaborate_body/elaborate_body_item (F11)
// and analyze_body (F12) already use for the same underlying tree.

template <int MaxNodes, int MaxBody, int MaxName, int MaxWords, int MaxData,
          int MaxWarnings, int StackDepth, int RStackDepth, int MaxOut>
constexpr auto
eval_body(elaborator::compiled_unit<MaxNodes, MaxBody, MaxName, MaxWords,
                                    MaxData, MaxWarnings> const &unit,
          elaborator::core_body<MaxNodes, MaxBody> const &items,
          forth_state<StackDepth, RStackDepth, MaxData, MaxOut> &state,
          int &fuel) -> foundation::result<eval_signal>;

template <int MaxNodes, int MaxBody, int MaxName, int MaxWords, int MaxData,
          int MaxWarnings, int StackDepth, int RStackDepth, int MaxOut>
constexpr auto
eval_node(elaborator::compiled_unit<MaxNodes, MaxBody, MaxName, MaxWords,
                                    MaxData, MaxWarnings> const &unit,
          elaborator::core_node<MaxNodes, MaxBody> const &node,
          forth_state<StackDepth, RStackDepth, MaxData, MaxOut> &state,
          int &fuel) -> foundation::result<eval_signal>;

/// Evaluates one @ref elaborator::core_node against @p state: primitives run
/// through @ref apply_primitive (F8's imperative implementation -- *not*
/// F12's declarative `elaborator::primitive_data_effect` table, which exists
/// only for abstract interpretation and knows nothing about actually running
/// a primitive); a call recurses structurally into the callee's own core,
/// found the same way F12's `analyze_body` finds a `core_call` target's
/// effect (`unit.dictionary.entry_at(word_index)`, then
/// `unit.arena.get(colon_word.core_id)`); `IF`/`BEGIN...UNTIL`/
/// `BEGIN...WHILE...REPEAT` become real control flow, conditionally or
/// repeatedly evaluating their bodies against the live @p state rather than
/// only predicting a depth.
template <int MaxNodes, int MaxBody, int MaxName, int MaxWords, int MaxData,
          int MaxWarnings, int StackDepth, int RStackDepth, int MaxOut>
constexpr auto
eval_node(elaborator::compiled_unit<MaxNodes, MaxBody, MaxName, MaxWords,
                                    MaxData, MaxWarnings> const &unit,
          elaborator::core_node<MaxNodes, MaxBody> const &node,
          forth_state<StackDepth, RStackDepth, MaxData, MaxOut> &state,
          int &fuel) -> foundation::result<eval_signal> {
    return std::visit(
        [&](auto const &alt) -> foundation::result<eval_signal> {
            using T = std::decay_t<decltype(alt)>;

            auto budget = consume_fuel(fuel, alt.pos);
            if (!budget.has_value()) {
                return budget.error();
            }

            if constexpr (std::is_same_v<T, elaborator::core_push>) {
                auto r = state.data().push(alt.value);
                if (!r.has_value()) {
                    return r.error();
                }
                return eval_signal::normal;
            } else if constexpr (std::is_same_v<T, elaborator::core_prim>) {
                auto r = apply_primitive(alt.op, state);
                if (!r.has_value()) {
                    return r.error();
                }
                return eval_signal::normal;
            } else if constexpr (std::is_same_v<T, elaborator::core_call>) {
                auto const &entry = unit.dictionary.entry_at(alt.word_index);
                auto const *cw = std::get_if<colon_word>(&entry.binding);
                if (cw == nullptr) {
                    return foundation::parse_error{
                        alt.pos, "call target is not a colon word"};
                }
                auto const &callee_node = unit.arena.get(cw->core_id);
                auto const *seq =
                    std::get_if<elaborator::core_seq<MaxNodes, MaxBody>>(
                        &callee_node.value);
                if (seq == nullptr) {
                    return foundation::parse_error{
                        alt.pos, "call target's core is not a sequence"};
                }
                auto r = eval_body(unit, seq->items, state, fuel);
                if (!r.has_value()) {
                    return r.error();
                }
                // A call is a fresh definition boundary (D11): whatever the
                // callee's own body returned -- normal fall-through, or an
                // EXIT that unwound all the way back up to here -- is
                // equally "the call is done" from this caller's own point of
                // view. The callee's EXIT never propagates past its own
                // call.
                return eval_signal::normal;
            } else if constexpr (std::is_same_v<T, elaborator::core_var>) {
                auto r = state.data().push(static_cast<cell>(alt.address));
                if (!r.has_value()) {
                    return r.error();
                }
                return eval_signal::normal;
            } else if constexpr (std::is_same_v<T, elaborator::core_const>) {
                auto r = state.data().push(alt.value);
                if (!r.has_value()) {
                    return r.error();
                }
                return eval_signal::normal;
            } else if constexpr (std::is_same_v<T, elaborator::core_push_xt>) {
                // An execution token is simply its dictionary index, cast to
                // a cell (D11); EXECUTE (F18a) is what later decides what to
                // do with it.
                auto r = state.data().push(static_cast<cell>(alt.word_index));
                if (!r.has_value()) {
                    return r.error();
                }
                return eval_signal::normal;
            } else if constexpr (std::is_same_v<T, elaborator::core_exit>) {
                return eval_signal::exited;
            } else if constexpr (std::is_same_v<T, elaborator::core_if<
                                                       MaxNodes, MaxBody>>) {
                auto flag = state.data().pop();
                if (!flag.has_value()) {
                    return flag.error();
                }
                return flag.value() != 0
                           ? eval_body(unit, alt.then_body, state, fuel)
                           : eval_body(unit, alt.else_body, state, fuel);
            } else if constexpr (std::is_same_v<T, elaborator::core_begin_until<
                                                       MaxNodes, MaxBody>>) {
                for (;;) {
                    auto body_r = eval_body(unit, alt.body, state, fuel);
                    if (!body_r.has_value()) {
                        return body_r.error();
                    }
                    if (body_r.value() == eval_signal::exited) {
                        return eval_signal::exited;
                    }
                    auto flag = state.data().pop();
                    if (!flag.has_value()) {
                        return flag.error();
                    }
                    if (flag.value() != 0) {
                        break;
                    }
                }
                return eval_signal::normal;
            } else if constexpr (std::is_same_v<T, elaborator::core_begin_while<
                                                       MaxNodes, MaxBody>>) {
                for (;;) {
                    auto cond_r = eval_body(unit, alt.condition, state, fuel);
                    if (!cond_r.has_value()) {
                        return cond_r.error();
                    }
                    if (cond_r.value() == eval_signal::exited) {
                        return eval_signal::exited;
                    }
                    auto flag = state.data().pop();
                    if (!flag.has_value()) {
                        return flag.error();
                    }
                    if (flag.value() == 0) {
                        break;
                    }
                    auto body_r = eval_body(unit, alt.body, state, fuel);
                    if (!body_r.has_value()) {
                        return body_r.error();
                    }
                    if (body_r.value() == eval_signal::exited) {
                        return eval_signal::exited;
                    }
                }
                return eval_signal::normal;
            } else if constexpr (std::is_same_v<T, elaborator::core_do_loop<
                                                       MaxNodes, MaxBody>>) {
                // DO...LOOP has no loop-index machinery at the core level
                // yet (no node pops the loop's (limit start) pair or exposes
                // I/J) -- that is F17's own deliverable
                // (do_setup/I/J/LEAVE/UNLOOP), per handoff-next.md's F13
                // briefing. F13's own merge criteria never exercise
                // DO...LOOP, so this is diagnosed rather than silently
                // running the body some arbitrary number of times.
                return foundation::parse_error{
                    alt.pos,
                    "DO...LOOP evaluation is not implemented until F17"};
            } else {
                // core_seq: never visited here in practice -- a core_call's
                // target is unwrapped directly (see above) rather than
                // dispatched through this visitor, and core_seq is
                // otherwise only ever the top-level wrapper a
                // colon_word::core_id points at -- but std::visit must stay
                // exhaustive, so this is a diagnosed error, not UB. Mirrors
                // analyze_body's identical defensive-only arm
                // (stack_effect.hpp).
                return foundation::parse_error{
                    alt.pos, "unexpected nested sequence node"};
            }
        },
        node.value);
}

/// Evaluates @p items in order against @p state, stopping early (without
/// visiting the rest of @p items) the moment any item reports @ref
/// eval_signal::exited -- code after an `EXIT` never runs, matching F12's
/// `analyze_body` treatment of the same shape (stack_effect.hpp), except
/// here it is a real runtime early return rather than an abstract-
/// interpretation shortcut.
template <int MaxNodes, int MaxBody, int MaxName, int MaxWords, int MaxData,
          int MaxWarnings, int StackDepth, int RStackDepth, int MaxOut>
constexpr auto
eval_body(elaborator::compiled_unit<MaxNodes, MaxBody, MaxName, MaxWords,
                                    MaxData, MaxWarnings> const &unit,
          elaborator::core_body<MaxNodes, MaxBody> const &items,
          forth_state<StackDepth, RStackDepth, MaxData, MaxOut> &state,
          int &fuel) -> foundation::result<eval_signal> {
    for (int i = 0; i < items.size(); ++i) {
        auto const &node = unit.arena.get(items[i]);
        auto r = eval_node(unit, node, state, fuel);
        if (!r.has_value()) {
            return r.error();
        }
        if (r.value() == eval_signal::exited) {
            return eval_signal::exited;
        }
    }
    return eval_signal::normal;
}
// 0faeac96-c46d-48bb-a1db-c21191d35022 end

// 4caeb07c-70eb-4256-9a53-f48d9fd89fe1
/// Evaluates @p unit's whole top-level program body (F13's entry point):
/// builds a fresh @ref forth_state, runs every top-level item through @ref
/// eval_body under @p fuel, and returns the resulting state (its data stack,
/// return stack, and output buffer all reflect the program's full run) or
/// the first diagnosed error -- a stack/return-stack misuse, a division by
/// zero, an output-buffer overflow, or budget exhaustion (@ref
/// consume_fuel), any of which halts evaluation immediately rather than
/// continuing in an inconsistent state.
///
/// A bare top-level `EXIT` cannot actually reach here: elaboration itself
/// already diagnoses `"EXIT outside a definition"` (F11's
/// `elaborate_word_ref`), so no `core_exit` node can exist anywhere in
/// `unit.program`. @ref eval_signal::exited is still a possible return from
/// @ref eval_body in principle (an `EXIT` nested inside a top-level `IF`,
/// say -- elaboration only forbids a *bare* top-level `EXIT`, not one nested
/// inside a top-level control structure), and is simply treated the same as
/// @ref eval_signal::normal here: the top-level program has no further body
/// of its own to skip past either way.
///
/// @tparam StackDepth  Data stack capacity (@ref forth_state).
/// @tparam RStackDepth Return stack capacity (@ref forth_state).
/// @tparam MaxOut      Output buffer capacity (@ref forth_state).
/// @param  unit A successfully elaborated program (F11/F12): every
///              definition in it has already passed stack-effect analysis,
///              per D9 -- @ref eval_program never sees a `compiled_unit`
///              that failed elaboration.
/// @param  fuel The evaluation step budget: decremented once per core node
///              visited (@ref consume_fuel); exhaustion is a diagnosed
///              error, never a hang, even for a nonterminating
///              `BEGIN ... UNTIL`.
template <int MaxNodes, int MaxBody, int MaxName, int MaxWords, int MaxData,
          int MaxWarnings, int StackDepth = 64, int RStackDepth = 64,
          int MaxOut = 256>
constexpr auto
eval_program(elaborator::compiled_unit<MaxNodes, MaxBody, MaxName, MaxWords,
                                       MaxData, MaxWarnings> const &unit,
             int fuel = 100000)
    -> foundation::result<
        forth_state<StackDepth, RStackDepth, MaxData, MaxOut>> {
    forth_state<StackDepth, RStackDepth, MaxData, MaxOut> state{};
    auto outcome = eval_body(unit, unit.program, state, fuel);
    if (!outcome.has_value()) {
        return outcome.error();
    }
    return state;
}
// 4caeb07c-70eb-4256-9a53-f48d9fd89fe1 end

} // namespace smd::forth::machine

#endif
