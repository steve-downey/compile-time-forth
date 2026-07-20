// src/smd/forth/elaborator/stack_effect.hpp                          -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#ifndef SRC_SMD_FORTH_ELABORATOR_STACK_EFFECT_HPP
#define SRC_SMD_FORTH_ELABORATOR_STACK_EFFECT_HPP

#include <smd/forth/elaborator/elaborated_core.hpp>
#include <smd/forth/foundation/parse_error.hpp>
#include <smd/forth/foundation/result.hpp>
#include <smd/forth/foundation/source_pos.hpp>
#include <smd/forth/foundation/source_span.hpp>
#include <smd/forth/machine/dictionary.hpp>
#include <smd/forth/machine/forth_state.hpp>
#include <smd/forth/reader/forth_chars.hpp>
#include <smd/forth/reader/syntax_tree.hpp>

#include <string_view>
#include <type_traits>
#include <variant>

namespace smd::forth::elaborator {

// Step F12 (D9): abstract interpretation over the elaborated core, computing
// each colon definition's net data-stack effect (and, inseparably, its
// minimum entry depth -- see @ref effect::inputs below) while tracking the
// return stack separately as a plain running delta. Runs as part of
// @ref elaborate, inside elaborate_colon_def, between "the body has been
// elaborated into a core_seq" and "define_colon installs it" -- exactly the
// seam handoff-next.md (F11 -> F12) identified.

// 3fd1b4b2-6cf1-4b6e-9b6b-3f2c0a8f7d21
/// The stack-effect abstract-interpretation lattice.
///
/// A @ref known value is a concrete `(inputs, outputs)` pair: @ref inputs is
/// the minimum stack depth required at entry (the "minimum entry depth" the
/// plan asks for -- there is no separate field for it, because it *is* this
/// number), and @ref outputs is the depth left behind, measured from that
/// same entry point. An @c unknown value is the lattice's top element:
/// produced by input-dependent primitives (`?DUP`) or anything reached only
/// through `EXECUTE` (not yet a primitive as of F12 -- F18a is what adds
/// one; @ref primitive_data_effect documents where its table entry belongs
/// once it exists). Once a computation touches `unknown`, every downstream
/// combination stays `unknown` (it poisons forward through @ref
/// combine_sequential and @ref combine_branch) and depth-based checking is
/// *suppressed* for it, not diagnosed as an error -- this is what "suppresses
/// checking downstream rather than erroring" means concretely.
struct effect {
    bool known = true; ///< False means "unknown" (the lattice's top value).
    int inputs = 0;    ///< Cells required present at entry. Only meaningful
                       ///< when @ref known is true.
    int outputs = 0;   ///< Cells left behind, from the same entry point.
                       ///< Only meaningful when @ref known is true.

    /// The net change in stack depth (`outputs - inputs`). Only meaningful
    /// when @ref known is true -- callers must check that first.
    [[nodiscard]] constexpr auto net() const -> int { return outputs - inputs; }

    friend constexpr auto operator==(effect const &, effect const &)
        -> bool = default;
};

/// The lattice's top value: an input-dependent or otherwise not-statically-
/// determinable effect.
inline constexpr effect unknown_effect{.known = false};

/// The empty/identity effect: consumes nothing, produces nothing. This is
/// the identity element for @ref combine_sequential (composing with it on
/// either side returns the other operand unchanged) -- it is also what a
/// missing `ELSE` clause and an empty loop body mean.
inline constexpr effect identity_effect{
    .known = true, .inputs = 0, .outputs = 0};

/// Builds a known effect.
constexpr auto known(int inputs, int outputs) -> effect {
    return effect{.known = true, .inputs = inputs, .outputs = outputs};
}

/// Sequential composition: @p first runs, then @p second runs immediately
/// after it, both starting from the same conceptual entry point.
///
/// Standard "stack level" derivation: to satisfy both @p first's own input
/// requirement and whatever @p second needs once @p first has run, the
/// combined entry requirement is @p first's own input requirement, plus
/// however much more @p second needs beyond what @p first's own output left
/// behind. The combined output follows from the combined input plus the sum
/// of both operands' net changes.
///
/// Returns @ref unknown_effect if either operand is unknown (the lattice's
/// top value poisons forward through composition).
constexpr auto combine_sequential(effect first, effect second) -> effect {
    if (!first.known || !second.known) {
        return unknown_effect;
    }
    int const shortfall = second.inputs - first.outputs;
    int const extra = shortfall > 0 ? shortfall : 0;
    int const combined_inputs = first.inputs + extra;
    int const combined_outputs = combined_inputs + first.net() + second.net();
    return known(combined_inputs, combined_outputs);
}

/// Combines two already-verified-compatible `IF`/`ELSE` arms (@p then_eff,
/// @p else_eff) into the `IF` node's own contributed effect.
///
/// Precondition (checked by the caller, not here): both arms are @ref known
/// and have the same @ref effect::net -- see the "IF/ELSE arms" diagnosis
/// below for why equal *net effect* (the depth delta), not the full
/// `(inputs, outputs)` pair, is what must match. `NEGATE` (1,1) and an empty
/// `ELSE`-less arm (0,0) have the same net effect (0) despite different
/// shapes, and Forth-2012 idioms like `: ABS DUP 0< IF NEGATE THEN ;`
/// require exactly this to type-check.
constexpr auto combine_branch(effect then_eff, effect else_eff) -> effect {
    int const entry =
        then_eff.inputs > else_eff.inputs ? then_eff.inputs : else_eff.inputs;
    return known(entry + 1, entry + then_eff.net());
}
// 3fd1b4b2-6cf1-4b6e-9b6b-3f2c0a8f7d21 end

/// The per-primitive net data-stack effect table (F12's deliverable -- no
/// such table existed before this step; F8's `apply_primitive` is imperative
/// runtime code, not a declarative mapping).
///
/// 35 of the 37 primitives have a fixed effect; `?DUP` is genuinely
/// input-dependent (`( a -- 0 | a a )`) and maps to @ref unknown_effect.
/// `>R`/`R>`/`R@` move cells between the data and return stacks -- this
/// table reports only their *data*-stack view; @ref primitive_return_delta
/// reports the matching return-stack contribution.
///
/// @note `EXECUTE` is not a `machine::primitive` enumerator as of F12 (F18a
/// adds one): once it exists, its table entry belongs here as
/// @ref unknown_effect too ("anything reached via EXECUTE" per the plan).
constexpr auto primitive_data_effect(machine::primitive op) -> effect {
    using P = machine::primitive;
    switch (op) {
    case P::plus:
    case P::minus:
    case P::star:
    case P::slash:
    case P::mod_:
    case P::min_:
    case P::max_:
    case P::and_:
    case P::or_:
    case P::xor_:
    case P::lshift:
    case P::rshift:
    case P::equal:
    case P::not_equal:
    case P::less:
    case P::greater:
    case P::less_equal:
    case P::greater_equal:
    case P::nip:
        return known(2, 1);
    case P::negate:
    case P::abs_:
    case P::invert:
    case P::zero_equal:
    case P::zero_less:
        return known(1, 1);
    case P::true_:
    case P::false_:
    case P::depth:
        return known(0, 1);
    case P::dup:
        return known(1, 2);
    case P::drop:
        return known(1, 0);
    case P::swap:
        return known(2, 2);
    case P::over:
        return known(2, 3);
    case P::rot:
        return known(3, 3);
    case P::tuck:
        return known(2, 3);
    case P::question_dup:
        return unknown_effect;
    case P::to_r:
        return known(1, 0); // Data-stack view only; see primitive_return_delta.
    case P::r_from:
        return known(0, 1); // Data-stack view only; see primitive_return_delta.
    case P::r_fetch:
        return known(0, 1);
    }
    // Defensive-only: every enumerator is listed above; reachable only if a
    // future step adds a primitive without updating this table.
    return unknown_effect;
}

/// The per-primitive *return*-stack contribution: `+1` for `>R` (moves one
/// data-stack cell to the return stack), `-1` for `R>` (moves one back), `0`
/// for every other primitive including `R@` (reads without moving).
constexpr auto primitive_return_delta(machine::primitive op) -> int {
    switch (op) {
    case machine::primitive::to_r:
        return 1;
    case machine::primitive::r_from:
        return -1;
    default:
        return 0;
    }
}

/// True if @p span is a real captured `( ... )` stack-effect comment rather
/// than the default "none written" sentinel (`first == last`, per F6/F7's
/// documented convention on @ref reader::syn_colon_def::declared_effect).
constexpr auto has_declared_effect(foundation::source_span span) -> bool {
    return !(span.first == span.last);
}

/// Parses a captured declared-effect span back out of @p source into an
/// @ref effect, per the plan's D9 wording: slice the span's text, then count
/// whitespace-delimited tokens left of `--` as inputs and right of it as
/// outputs. @p span covers the comment inclusive of its `(`/`)` delimiters
/// (F5's @ref reader::scan_paren_comment convention) -- those two tokens are
/// stripped before counting.
///
/// Returns @ref unknown_effect if, contrary to F7's own capture precondition
/// (a comment is only ever captured when it contains `--`), no `--` token is
/// found -- a defensive fallback, not an expected path.
constexpr auto parse_declared_effect(std::string_view source,
                                     foundation::source_span span) -> effect {
    auto const begin = static_cast<std::size_t>(span.first.offset);
    auto const end = static_cast<std::size_t>(span.last.offset);
    std::string_view text = source.substr(begin, end - begin);
    if (!text.empty() && text.front() == '(') {
        text.remove_prefix(1);
    }
    if (!text.empty() && text.back() == ')') {
        text.remove_suffix(1);
    }

    int inputs = 0;
    int outputs = 0;
    bool seen_dashes = false;
    std::size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && !reader::is_word_char(text[i])) {
            ++i;
        }
        std::size_t const start = i;
        while (i < text.size() && reader::is_word_char(text[i])) {
            ++i;
        }
        if (start == i) {
            break;
        }
        std::string_view const token = text.substr(start, i - start);
        if (token == "--") {
            seen_dashes = true;
        } else if (seen_dashes) {
            ++outputs;
        } else {
            ++inputs;
        }
    }
    if (!seen_dashes) {
        return unknown_effect;
    }
    return known(inputs, outputs);
}

/// The result of analyzing one body (a colon definition's own top-level
/// body, or any control structure's nested body/arm): the body's own net
/// data-stack @ref effect, plus the return-stack depth change (@ref r_delta)
/// accumulated purely within that body -- tracked as a plain @c int rather
/// than a lattice value, because `>R`/`R>`/`R@` are never input-dependent
/// (only `?DUP` is), so the return-stack contribution of every node kind is
/// always exactly known regardless of whether the data-stack side has gone
/// `unknown`.
struct body_analysis {
    effect eff{};    ///< Defaults to @ref identity_effect (0 in, 0 out).
    int r_delta = 0; ///< Return-stack depth change within this body alone.
};

// 7c2a9e5d-3b8f-4a1e-9d6c-8e4f1b2a6c9d
/// Walks @p items (one `core_body`), computing its @ref body_analysis and
/// diagnosing diagnoses 1-3 (`IF`/`ELSE` arm mismatch, nonzero-net-effect
/// loop bodies, return-stack imbalance across a control boundary) wherever
/// they occur, at any nesting depth.
///
/// @p self_index is the dictionary index of the colon definition currently
/// being analyzed (so a `RECURSE`-produced `core_call` back to it can be
/// recognized); @p self_effect is what that self-call should be assumed to
/// have -- the definition's own declared effect if it wrote one, else @ref
/// unknown_effect (the plan's own open "RECURSE fixed-point" question,
/// resolved this way: F12 does not attempt to fix-point-solve a
/// self-referential effect, it either trusts a human-supplied declaration or
/// falls back to the same `unknown`-suppresses-checking treatment as
/// `?DUP`/`EXECUTE`).
///
/// A `core_exit` node stops folding for the *remainder of its own immediate
/// body list only* -- code after an unconditional `EXIT` never runs, so it
/// is not visited. This is a deliberately conservative simplification, not a
/// fully flow-sensitive treatment of multiple return paths (an `EXIT` nested
/// inside one `IF` arm does not attempt to reconcile that arm's
/// early-return depth against the definition's eventual fall-through depth);
/// see stack_effect.hpp's own doc block above and DIV-0006 for the scope
/// this leaves for a future step.
template <int MaxNodes, int MaxBody, int MaxName, int MaxWords, int MaxData,
          int MaxWarnings>
constexpr auto analyze_body(compiled_unit<MaxNodes, MaxBody, MaxName, MaxWords,
                                          MaxData, MaxWarnings> const &unit,
                            core_body<MaxNodes, MaxBody> const &items,
                            int self_index, effect self_effect)
    -> foundation::result<body_analysis> {
    body_analysis result{};
    for (int i = 0; i < items.size(); ++i) {
        auto const &node = unit.arena.get(items[i]);
        effect item_eff{};
        int item_r = 0;
        bool stop = false;

        auto outcome = std::visit(
            [&](auto const &alt) -> foundation::result<std::monostate> {
                using T = std::decay_t<decltype(alt)>;
                if constexpr (std::is_same_v<T, core_push> ||
                              std::is_same_v<T, core_var> ||
                              std::is_same_v<T, core_const> ||
                              std::is_same_v<T, core_push_xt>) {
                    item_eff = known(0, 1);
                } else if constexpr (std::is_same_v<T, core_prim>) {
                    item_eff = primitive_data_effect(alt.op);
                    item_r = primitive_return_delta(alt.op);
                } else if constexpr (std::is_same_v<T, core_call>) {
                    if (alt.word_index == self_index) {
                        item_eff = self_effect;
                    } else {
                        auto const &entry =
                            unit.dictionary.entry_at(alt.word_index);
                        auto const *cw =
                            std::get_if<machine::colon_word>(&entry.binding);
                        if (cw == nullptr) {
                            return foundation::parse_error{
                                alt.pos, "call target is not a colon word"};
                        }
                        item_eff = cw->effect.known ? known(cw->effect.inputs,
                                                            cw->effect.outputs)
                                                    : unknown_effect;
                    }
                } else if constexpr (std::is_same_v<T, core_exit>) {
                    item_eff = identity_effect;
                    stop = true;
                } else if constexpr (std::is_same_v<
                                         T, core_if<MaxNodes, MaxBody>>) {
                    auto then_r = analyze_body(unit, alt.then_body, self_index,
                                               self_effect);
                    if (!then_r.has_value()) {
                        return then_r.error();
                    }
                    auto else_r = analyze_body(unit, alt.else_body, self_index,
                                               self_effect);
                    if (!else_r.has_value()) {
                        return else_r.error();
                    }
                    if (then_r.value().r_delta != 0 ||
                        else_r.value().r_delta != 0) {
                        return foundation::parse_error{
                            alt.pos, "return stack is unbalanced across a "
                                     "control-structure body"};
                    }
                    auto const &then_eff = then_r.value().eff;
                    auto const &else_eff = else_r.value().eff;
                    if (then_eff.known && else_eff.known) {
                        if (then_eff.net() != else_eff.net()) {
                            return foundation::parse_error{
                                alt.pos,
                                "IF/ELSE arms have unequal net stack effect"};
                        }
                        item_eff = combine_branch(then_eff, else_eff);
                    } else {
                        item_eff = unknown_effect;
                    }
                } else if constexpr (std::is_same_v<
                                         T,
                                         core_begin_until<MaxNodes, MaxBody>>) {
                    auto body_r =
                        analyze_body(unit, alt.body, self_index, self_effect);
                    if (!body_r.has_value()) {
                        return body_r.error();
                    }
                    if (body_r.value().r_delta != 0) {
                        return foundation::parse_error{
                            alt.pos, "return stack is unbalanced across a "
                                     "control-structure body"};
                    }
                    auto const &body_eff = body_r.value().eff;
                    if (body_eff.known) {
                        if (body_eff.net() != 1) {
                            return foundation::parse_error{
                                alt.pos,
                                "BEGIN...UNTIL body must leave exactly one "
                                "flag for UNTIL"};
                        }
                        item_eff = known(body_eff.inputs, body_eff.inputs);
                    } else {
                        item_eff = unknown_effect;
                    }
                } else if constexpr (std::is_same_v<
                                         T,
                                         core_begin_while<MaxNodes, MaxBody>>) {
                    auto cond_r = analyze_body(unit, alt.condition, self_index,
                                               self_effect);
                    if (!cond_r.has_value()) {
                        return cond_r.error();
                    }
                    auto body_r =
                        analyze_body(unit, alt.body, self_index, self_effect);
                    if (!body_r.has_value()) {
                        return body_r.error();
                    }
                    if (cond_r.value().r_delta != 0 ||
                        body_r.value().r_delta != 0) {
                        return foundation::parse_error{
                            alt.pos, "return stack is unbalanced across a "
                                     "control-structure body"};
                    }
                    auto const &cond_eff = cond_r.value().eff;
                    auto const &body_eff = body_r.value().eff;
                    if (cond_eff.known && cond_eff.net() != 1) {
                        return foundation::parse_error{
                            alt.pos,
                            "BEGIN...WHILE condition must leave exactly one "
                            "flag for WHILE"};
                    }
                    if (body_eff.known && body_eff.net() != 0) {
                        return foundation::parse_error{
                            alt.pos, "BEGIN...WHILE...REPEAT body must have a "
                                     "net-zero stack effect"};
                    }
                    if (cond_eff.known && body_eff.known) {
                        int const entry = cond_eff.inputs > body_eff.inputs
                                              ? cond_eff.inputs
                                              : body_eff.inputs;
                        item_eff = known(entry, entry);
                    } else {
                        item_eff = unknown_effect;
                    }
                } else if constexpr (std::is_same_v<
                                         T, core_do_loop<MaxNodes, MaxBody>>) {
                    auto body_r =
                        analyze_body(unit, alt.body, self_index, self_effect);
                    if (!body_r.has_value()) {
                        return body_r.error();
                    }
                    if (body_r.value().r_delta != 0) {
                        return foundation::parse_error{
                            alt.pos, "return stack is unbalanced across a "
                                     "control-structure body"};
                    }
                    auto const &body_eff = body_r.value().eff;
                    if (body_eff.known) {
                        if (body_eff.net() != 0) {
                            return foundation::parse_error{
                                alt.pos,
                                "DO loop body must have a net-zero stack "
                                "effect"};
                        }
                        item_eff = known(body_eff.inputs, body_eff.inputs);
                    } else {
                        item_eff = unknown_effect;
                    }
                } else {
                    // core_seq: never nested inside a body in practice --
                    // only ever the top-level wrapper a colon_word::core_id
                    // points at -- but std::visit must stay exhaustive.
                    return foundation::parse_error{
                        alt.pos, "unexpected nested sequence node"};
                }
                return std::monostate{};
            },
            node.value);

        if (!outcome.has_value()) {
            return outcome.error();
        }
        result.eff = combine_sequential(result.eff, item_eff);
        result.r_delta += item_r;
        if (stop) {
            break;
        }
    }
    return result;
}
// 7c2a9e5d-3b8f-4a1e-9d6c-8e4f1b2a6c9d end

/// Diagnosis 4: an `EXIT` lexically nested inside a `DO ... LOOP`/`+LOOP`
/// body, at any depth, without an `UNLOOP` to appease first.
///
/// `UNLOOP` is not a primitive yet (F17 adds real counted-loop machinery);
/// until it exists there is no way to leave a `DO` loop's parameters in a
/// consistent state before an early `EXIT`, so this diagnoses the shape
/// unconditionally rather than trying to verify an `UNLOOP` that cannot
/// exist. **F17 should relax this** to "an `EXIT` inside `DO` needs a
/// preceding `UNLOOP` on that path" once `UNLOOP` is real.
///
/// Independent of @ref analyze_body's depth/effect tracking -- this is a
/// plain lexical scan, so it is unaffected by @ref analyze_body's
/// EXIT-stops-folding simplification.
template <int MaxNodes, int MaxBody>
constexpr auto check_exit_in_do_loops(
    foundation::tree_arena<core_node<MaxNodes, MaxBody>, MaxNodes> const &arena,
    core_body<MaxNodes, MaxBody> const &items, bool in_do_loop)
    -> foundation::result<std::monostate> {
    for (int i = 0; i < items.size(); ++i) {
        auto const &node = arena.get(items[i]);
        auto outcome = std::visit(
            [&](auto const &alt) -> foundation::result<std::monostate> {
                using T = std::decay_t<decltype(alt)>;
                if constexpr (std::is_same_v<T, core_exit>) {
                    if (in_do_loop) {
                        return foundation::parse_error{
                            alt.pos, "EXIT inside a DO loop requires UNLOOP"};
                    }
                    return std::monostate{};
                } else if constexpr (std::is_same_v<
                                         T, core_if<MaxNodes, MaxBody>>) {
                    auto r1 = check_exit_in_do_loops(arena, alt.then_body,
                                                     in_do_loop);
                    if (!r1.has_value()) {
                        return r1;
                    }
                    return check_exit_in_do_loops(arena, alt.else_body,
                                                  in_do_loop);
                } else if constexpr (std::is_same_v<
                                         T,
                                         core_begin_until<MaxNodes, MaxBody>>) {
                    return check_exit_in_do_loops(arena, alt.body, in_do_loop);
                } else if constexpr (std::is_same_v<
                                         T,
                                         core_begin_while<MaxNodes, MaxBody>>) {
                    auto r1 = check_exit_in_do_loops(arena, alt.condition,
                                                     in_do_loop);
                    if (!r1.has_value()) {
                        return r1;
                    }
                    return check_exit_in_do_loops(arena, alt.body, in_do_loop);
                } else if constexpr (std::is_same_v<
                                         T, core_do_loop<MaxNodes, MaxBody>>) {
                    return check_exit_in_do_loops(arena, alt.body, true);
                } else {
                    return std::monostate{};
                }
            },
            node.value);
        if (!outcome.has_value()) {
            return outcome;
        }
    }
    return std::monostate{};
}

// f1a6c3e8-2d9b-4f5a-8c1e-6b3d9a2f7e4c
/// Analyzes one colon definition's whole body (F12's top-level entry point,
/// called from `elaborate_colon_def` between building the body's `core_seq`
/// and installing the `colon_word`): runs diagnosis 4 (@ref
/// check_exit_in_do_loops), then @ref analyze_body for diagnoses 1-3, then
/// checks diagnosis 3's whole-definition half ("a definition must end with
/// return stack balanced") and diagnosis 5 (declared-vs-computed mismatch).
///
/// @p source is the original program text @p cd's @ref
/// reader::syn_colon_def::declared_effect span indexes into -- @ref elaborate
/// did not used to take this parameter (F11 never needed the declared
/// effect's *text*, only its span); F12 adds it, threaded down from
/// @ref elaborate's own new `source` parameter.
///
/// Returns the @ref machine::stack_effect to store on the new
/// @ref machine::colon_word: the declared effect verbatim if one was
/// written (trusted ground truth, per the RECURSE design note on @ref
/// analyze_body), else the computed effect if it came out @ref known, else
/// `stack_effect{.known = false}`.
template <int MaxNodes, int MaxBody, int MaxName, int MaxWords, int MaxData,
          int MaxWarnings>
constexpr auto analyze_colon_effect(
    compiled_unit<MaxNodes, MaxBody, MaxName, MaxWords, MaxData,
                  MaxWarnings> const &unit,
    core_body<MaxNodes, MaxBody> const &body_items, int self_index,
    reader::syn_colon_def<MaxNodes, MaxBody, MaxName> const &cd,
    std::string_view source) -> foundation::result<machine::stack_effect> {
    auto exit_check = check_exit_in_do_loops(unit.arena, body_items, false);
    if (!exit_check.has_value()) {
        return exit_check.error();
    }

    bool const declared_present = has_declared_effect(cd.declared_effect);
    effect const declared =
        declared_present ? parse_declared_effect(source, cd.declared_effect)
                         : unknown_effect;
    effect const self_effect =
        (declared_present && declared.known) ? declared : unknown_effect;

    auto analysis = analyze_body(unit, body_items, self_index, self_effect);
    if (!analysis.has_value()) {
        return analysis.error();
    }
    auto const &summary = analysis.value();
    if (summary.r_delta != 0) {
        return foundation::parse_error{
            cd.pos, "return stack is unbalanced at the end of a definition"};
    }

    if (declared_present && declared.known && summary.eff.known) {
        if (declared.inputs != summary.eff.inputs ||
            declared.outputs != summary.eff.outputs) {
            return foundation::parse_error{
                cd.declared_effect.first,
                "declared stack effect does not match computed stack "
                "effect"};
        }
    }

    if (declared_present && declared.known) {
        return machine::stack_effect{.inputs = declared.inputs,
                                     .outputs = declared.outputs,
                                     .known = true};
    }
    if (summary.eff.known) {
        return machine::stack_effect{.inputs = summary.eff.inputs,
                                     .outputs = summary.eff.outputs,
                                     .known = true};
    }
    return machine::stack_effect{};
}
// f1a6c3e8-2d9b-4f5a-8c1e-6b3d9a2f7e4c end

} // namespace smd::forth::elaborator

#endif
