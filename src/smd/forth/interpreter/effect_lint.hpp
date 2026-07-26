// src/smd/forth/interpreter/effect_lint.hpp                          -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#ifndef SRC_SMD_FORTH_INTERPRETER_EFFECT_LINT_HPP
#define SRC_SMD_FORTH_INTERPRETER_EFFECT_LINT_HPP

#include <smd/forth/foundation/source_span.hpp>
#include <smd/forth/machine/forth_state.hpp>
#include <smd/forth/parser/forth_chars.hpp>

#include <string_view>

namespace smd::forth::interpreter {

// Step F26 (docs/forth-plan-2.md, "the cut"): the stack-effect abstract-
// interpretation *lattice* and its pure, tree-independent supporting
// functions, extracted verbatim (behavior unchanged) from the R1 elaborator's
// elaborator/stack_effect.hpp, which this same step deletes along with the
// rest of elaborator/. D20 defers real effect *checking* to F30
// ("interpreter/effect_lint.hpp ... consuming the lattice you preserve" --
// docs/forth-plan-2.md's own words for this file); this step only relocates
// the lattice itself, so it survives the elaborator's deletion with something
// concrete to land in.
//
// What did NOT move here: elaborator/stack_effect.hpp's own analyze_body,
// check_exit_in_do_loops, and analyze_colon_effect are a tree-walk over the
// elaborated core's core_if/core_do_loop/... node kinds -- the R1-only half
// of that file, tied to a tree that no longer exists once elaborator/ is
// gone. F30 is what gives this lattice a new consumer, retargeted to instr
// ranges over the interpreter's own compile_buffer instead of the deleted
// core tree; this step deliberately does not attempt that retargeting.

// 3fd1b4b2-6cf1-4b6e-9b6b-3f2c0a8f7d21
/// The stack-effect abstract-interpretation lattice.
///
/// A @ref known value is a concrete `(inputs, outputs)` pair: @ref inputs is
/// the minimum stack depth required at entry (the "minimum entry depth" the
/// plan asks for -- there is no separate field for it, because it *is* this
/// number), and @ref outputs is the depth left behind, measured from that
/// same entry point. An @c unknown value is the lattice's top element:
/// produced by input-dependent primitives (`?DUP`) or anything reached only
/// through `EXECUTE`. Once a computation touches `unknown`, every downstream
/// combination stays `unknown` (it poisons forward through @ref
/// combine_sequential and @ref combine_branch) and depth-based checking is
/// *suppressed* for it, not diagnosed as an error -- this is what "suppresses
/// checking downstream rather than erroring" means concretely.
// b238e219-4650-4e62-96ff-95b16e1d08e3
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
// b238e219-4650-4e62-96ff-95b16e1d08e3 end

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
/// and have the same @ref effect::net -- two arms can have different shapes
/// and still be safe to run interchangeably. `NEGATE` (1,1) and an implicit
/// empty `ELSE` (0,0) both have net effect zero despite different shapes, and
/// the canonical Forth-2012 idiom `: ABS DUP 0< IF NEGATE THEN ;` depends on
/// exactly this reading to type-check at all.
constexpr auto combine_branch(effect then_eff, effect else_eff) -> effect {
    int const entry =
        then_eff.inputs > else_eff.inputs ? then_eff.inputs : else_eff.inputs;
    return known(entry + 1, entry + then_eff.net());
}
// 3fd1b4b2-6cf1-4b6e-9b6b-3f2c0a8f7d21 end

/// The per-primitive net data-stack effect table.
///
/// 46 of the 47 primitives have a fixed effect; `?DUP` is genuinely
/// input-dependent (`( a -- 0 | a a )`) and maps to @ref unknown_effect.
/// `>R`/`R>`/`R@` move cells between the data and return stacks -- this
/// table reports only their *data*-stack view; @ref primitive_return_delta
/// reports the matching return-stack contribution.
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
    case P::one_minus:
    case P::one_plus:
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
    case P::dot:
        return known(1, 0);
    case P::dot_s:
        return identity_effect; // Nondestructive: no minimum entry depth.
    case P::emit:
        return known(1, 0);
    case P::cr:
        return identity_effect;
    case P::fetch:
        return known(1, 1);
    case P::store:
        return known(2, 0);
    case P::plus_store:
        return known(2, 0);
    case P::allot:
        return known(1, 0);
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
/// than the default "none written" sentinel (`first == last`).
constexpr auto has_declared_effect(foundation::source_span span) -> bool {
    return !(span.first == span.last);
}

/// Parses a captured declared-effect span back out of @p source into an
/// @ref effect, per D9's wording: slice the span's text, then count
/// whitespace-delimited tokens left of `--` as inputs and right of it as
/// outputs. @p span covers the comment inclusive of its `(`/`)` delimiters
/// (@ref parser::scan_paren_comment's convention) -- those two tokens are
/// stripped before counting.
///
/// Returns @ref unknown_effect if, contrary to the capture precondition (a
/// comment is only ever captured when it contains `--`), no `--` token is
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
        while (i < text.size() && !parser::is_word_char(text[i])) {
            ++i;
        }
        std::size_t const start = i;
        while (i < text.size() && parser::is_word_char(text[i])) {
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

} // namespace smd::forth::interpreter

#endif
