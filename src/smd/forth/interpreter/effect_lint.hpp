// src/smd/forth/interpreter/effect_lint.hpp                          -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#ifndef SRC_SMD_FORTH_INTERPRETER_EFFECT_LINT_HPP
#define SRC_SMD_FORTH_INTERPRETER_EFFECT_LINT_HPP

#include <smd/forth/foundation/parse_error.hpp>
#include <smd/forth/foundation/result.hpp>
#include <smd/forth/foundation/source_span.hpp>
#include <smd/forth/foundation/static_vector.hpp>
#include <smd/forth/machine/dictionary.hpp>
#include <smd/forth/machine/forth_state.hpp>
#include <smd/forth/machine/instruction.hpp>
#include <smd/forth/parser/forth_chars.hpp>

#include <array>
#include <string_view>
#include <variant>

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
/// 54 of the 55 @ref machine::primitive enumerators (step F29 added `PARSE
/// WORD CHAR COUNT TYPE` and the runtime half of `ABORT"` to F28's own 48;
/// step F31 added `catch_ok`; step F32 added the D21 address-unit words'
/// one new enumerator, `identity` -- `CELL+`/`CHAR+`/`C@`/`C!` alias
/// existing enumerators rather than adding new ones, so the six new D21
/// dictionary names add only this one entry here) have a fixed effect;
/// `?DUP` is genuinely input-dependent (`( a -- 0 | a a )`) and maps to @ref
/// unknown_effect. `>R`/`R>`/`R@` move cells between the data and return
/// stacks -- this table reports only their *data*-stack view; @ref
/// primitive_return_delta reports the matching return-stack contribution.
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
    case P::comma:
        return known(1, 0); // Step F28: `,` ( x -- ), like `dot`/`allot`.
    case P::parse:
        return known(1, 2); // Step F29: `PARSE` ( char -- c-addr u ).
    case P::word:
        return known(1, 1); // Step F29: `WORD` ( char -- c-addr ).
    case P::char_:
        return known(0, 1); // Step F29: `CHAR` ( -- char ).
    case P::count:
        return known(1, 2); // Step F29: `COUNT` ( c-addr1 -- c-addr2 u ).
    case P::type_:
        return known(2, 0); // Step F29: `TYPE` ( c-addr u -- ).
    case P::abort_quote:
        // Step F29: `ABORT"`'s own runtime primitive ( flag c-addr u -- ).
        // The stack shape is statically known even though whether it
        // returns at all is not (DIV-0017) -- that "may not return" fact is
        // exactly the kind of thing D20 defers to F30, not something this
        // per-primitive *data*-stack-shape table decides.
        return known(3, 0);
    case P::catch_ok:
        // Step F31: CATCH's own "normal completion" epilogue ( -- 0 ).
        // Consumes nothing from the *data* stack (its own 3 popped cells are
        // all on the return stack); pushes exactly one flag.
        return known(0, 1);
    case P::identity:
        // Step F32, D21: `CELLS`/`CHARS` ( n -- n ). Same shape as
        // `one_plus` above -- one cell required, one left behind, net zero.
        return known(1, 1);
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

// ---------------------------------------------------------------------------
// Step F30 (D20): the checker itself, retargeted to instr ranges.
//
// The lattice above this comment is F26's own relocation of the R1
// elaborator's pure, tree-independent half (unchanged). Everything below is
// new: a real control-flow graph recovered over one colon definition's own
// instruction range (`interpreter::compile_buffer`'s own code, between a
// `compiled_colon_word::entry_point` and the `ret` `;` emits to close it),
// and an abstract interpreter over that graph that reproduces every F12/F17
// diagnosis structurally instead of by folding the deleted elaborated-core
// tree. See DIV-0019 for the full design record, including the two
// discoveries the plan's own literal wording did not anticipate:
//
//  - Multiple reachable exit points (`EXIT`s, or the closing `;` itself) are
//    *not* required to agree with each other. `docs/divergences/DIV-0006-
//    exit-not-flow-sensitive.md` names a genuinely different-shaped
//    definition -- `: F DUP 0< IF EXIT THEN DROP ;`, the R1 elaborator's own
//    example, and this project's own `interpreter::corpus::
//    exit_boundary_program` (`INNER`) is exactly this shape -- as valid,
//    intentional Forth-2012, not a defect: `EXIT` unwinds to its own call
//    boundary with whatever the stack looks like *there*, and nothing in
//    the standard requires every return path to leave the same depth unless
//    a declared effect commits to one. Recovering a real CFG makes it
//    possible to see this distinction precisely (DIV-0006's own request),
//    rather than silently assuming consistency the way the old tree-fold
//    did (stopping at `EXIT` without ever comparing it to the fall-through
//    depth): multiple *known* exits that disagree simply make this
//    definition's own net effect `unknown` (never storing a single,
//    possibly-wrong number) -- not an error. A *declared* effect changes
//    this: D20's own "hard gate exactly when declared" means every
//    reachable exit is checked against the declaration individually, which
//    is what actually closes DIV-0006 for the case that matters (a written
//    contract now cannot silently disagree with an early `EXIT`, whichever
//    arm the old checker happened to fold last).
//  - `LEAVE`'s own local effect must stay `unknown` (ported verbatim from
//    the deleted `elaborator/stack_effect.hpp`'s own `core_leave` case, not
//    an independent rediscovery): unlike `EXIT`, `LEAVE`'s own target *is* a
//    genuine control-flow join (the loop's normal-exhaustion path and a
//    `LEAVE`-taken path both continue at the same following instruction),
//    so treating it as a known, no-op effect would make
//    `interpreter::corpus::find5_program` (`10 0 DO I 5 = IF I LEAVE THEN
//    LOOP`, where the `LEAVE` arm leaves one extra cell the exhaustion path
//    never pushes) a *hard, always-on* diagnosed conflict at that join --
//    exactly the corpus regression this step's own instructions warn
//    against ("an advisory lint must not start rejecting corpus programs
//    that legitimately have unknown or unbalanced-looking effects").
//    `unknown` at the join suppresses the numeric comparison there instead
//    of erroring, the same lattice rule every other unknown-producing
//    construct already gets.

/// One instruction's own local contribution to the two channels this
/// checker tracks: @ref data (the @ref effect lattice, generalizing
/// @ref primitive_data_effect from `op::prim` to every opcode
/// `interpreter::compile_entry`/`interpreter::apply_control_word` can
/// emit) and @ref ret_delta (the plain, always-known return-stack delta
/// @ref primitive_return_delta already reports for `>R`/`R>`/`R@` --
/// `do_setup`/`loop_step`/`plus_loop_step`/`leave`/`unloop`'s own loop-frame
/// push/pops are deliberately *not* counted here, exactly as the deleted
/// tree-walker's own `r_delta` excluded them: this channel is only ever
/// about a definition's own `>R`/`R>` balance, not the loop machinery's
/// private bookkeeping).
struct instr_effect {
    effect data{};
    int ret_delta = 0;
};

/// Computes @p index's own @ref instr_effect within @p program.
///
/// `op::call`'s own contribution depends on its *target*: a self-call
/// (`RECURSE`, whose target equals @p self_entry -- the definition currently
/// being closed, which has no dictionary entry yet to look up) uses
/// @p self_effect, exactly the R1 elaborator's own design note for this case
/// (ported verbatim in spirit, not code): this checker never attempts to
/// fix-point-solve a self-referential effect, it either trusts a declared
/// effect (@p self_effect, resolved by the caller from @ref
/// parse_declared_effect) or falls back to @ref unknown_effect. Every other
/// call target is resolved by scanning @p dict for whichever @ref
/// machine::compiled_colon_word already carries that instruction index as
/// its own @ref machine::compiled_colon_word::entry_point, reading back the
/// effect *that* word's own `;` already computed and stored -- always
/// available, since Forth-2012 requires a word to be defined before it is
/// called, and this checker runs at every `;` in program order.
///
/// `?DUP`-style input-dependent primitives, `EXECUTE`, `CATCH` (`op::
/// catch_mark`), and `LEAVE` all map to @ref unknown_effect (this header's
/// own top comment explains why `LEAVE` specifically must); every other
/// opcode has a fixed, statically known contribution.
/// @tparam MaxCode      Must match @p program's own instruction-array
///                      capacity.
/// @tparam MaxProgWords @p program's own word-table capacity -- independent
///                      of @p dict's own @p MaxDictWords, exactly as
///                      `interpreter::compile_buffer`'s own `MaxBufWords`
///                      is independent of `interpreter::interpret`'s own
///                      `MaxWords` (a custom-sized dictionary may pair with
///                      a differently-sized code buffer).
/// @tparam MaxDictWords @p dict's own entry capacity.
template <int MaxCode, int MaxProgWords, int MaxDictWords, int MaxName>
[[nodiscard]] constexpr auto instruction_effect(
    machine::compiled_program<MaxCode, MaxProgWords> const &program,
    machine::dictionary<MaxDictWords, MaxName> const &dict, int index,
    int self_entry, effect self_effect) -> instr_effect {
    using machine::op;
    machine::instr const &in = program.code[index];
    switch (in.code) {
    case op::push:
    case op::push_xt:
        return {known(0, 1), 0};
    case op::prim: {
        auto const p = static_cast<machine::primitive>(in.operand);
        return {primitive_data_effect(p), primitive_return_delta(p)};
    }
    case op::call: {
        int const target = static_cast<int>(in.operand);
        if (target == self_entry) {
            return {self_effect, 0};
        }
        for (int i = 0; i < dict.size(); ++i) {
            auto const &e = dict.entry_at(i);
            if (auto const *cw =
                    std::get_if<machine::compiled_colon_word>(&e.binding)) {
                if (cw->entry_point == target) {
                    return {cw->effect_known
                                ? known(cw->effect_inputs, cw->effect_outputs)
                                : unknown_effect,
                            0};
                }
            }
        }
        // Defensive-only (D7): every call target this checker ever sees was
        // emitted by compile_entry against a real, already-installed
        // dictionary entry.
        return {unknown_effect, 0};
    }
    case op::ret:
    case op::branch:
    case op::loop_step:
    case op::unloop:
    case op::create_word:
    case op::does_enter:
    case op::halt:
        return {identity_effect, 0};
    case op::branch0:
        return {known(1, 0), 0};
    case op::do_setup:
        return {known(2, 0), 0};
    case op::plus_loop_step:
        return {known(1, 0), 0};
    case op::push_index:
        return {known(0, 1), 0};
    case op::leave:
        // See this header's own top comment: LEAVE's target is a genuine
        // join with the loop's normal-exhaustion path, so this must stay
        // unknown rather than identity_effect.
        return {unknown_effect, 0};
    case op::execute:
    case op::catch_mark:
        return {unknown_effect, 0};
    case op::throw_op:
        return {known(1, 0), 0};
    }
    // Defensive-only (D7): every enumerator is listed above.
    return {unknown_effect, 0};
}

/// An instruction's own successor instruction index(es) within the same
/// definition's instruction range -- the edges of the control-flow graph
/// this step recovers. `-1` means "no such edge": both `-1` marks a
/// terminal instruction (`ret`/`does_enter`/`halt` -- none of the three
/// falls through to whatever instruction happens to follow it physically,
/// per each one's own semantics in `vm.hpp`), and @ref b alone `-1` marks an
/// unconditional single-successor instruction.
// 8a1d5c3e-9f27-4b6a-8e2d-1c7f9a3b5e6d
struct instr_edges {
    int a = -1;
    int b = -1;
};

/// Computes @p in's own @ref instr_edges, given its own instruction @p index
/// (needed only for the fallthrough case, `index + 1`).
///
/// Shared verbatim by @ref check_definition_effect's own worklist and by
/// @ref recover_basic_blocks (D24: "the recovered CFG is shared with F33's
/// sender lowering -- one analysis, two clients") -- this function *is*
/// that one shared analysis' own edge relation; the two clients differ only
/// in what they do with it (a per-instruction abstract interpretation here,
/// a leader-based block partition there).
[[nodiscard]] constexpr auto instruction_successors(machine::instr const &in,
                                                    int index) -> instr_edges {
    using machine::op;
    switch (in.code) {
    case op::ret:
    case op::does_enter:
    case op::halt:
        return {-1, -1};
    case op::branch:
        return {static_cast<int>(in.operand), -1};
    case op::branch0:
    case op::loop_step:
    case op::plus_loop_step:
        return {index + 1, static_cast<int>(in.operand)};
    case op::leave:
        return {static_cast<int>(in.operand), -1};
    case op::catch_mark:
        // Step F33 (docs/forth-plan-2.md), D24: `catch_mark`'s own operand
        // is a real second control-flow edge -- the resume instruction a
        // caught `throw_op` jumps to directly (`vm.hpp`'s own
        // `perform_throw`), reached without ever falling through the paired
        // `prim catch_ok` that always sits at `index + 1` (both of
        // `interp.hpp`'s own `CATCH` cases emit the pair together, D11/D18).
        // F30's own checker never needed this edge (`catch_mark`'s own local
        // effect is `unknown_effect` regardless of which edge is taken, see
        // this header's own top comment), so it went unmodeled until F33's
        // own sender lowering needed both of `CATCH`'s real completions
        // (normal vs. caught) as two separately wired continuations -- see
        // `sender/lower.hpp`'s own top comment for how it consumes this.
        return {index + 1, static_cast<int>(in.operand)};
    default:
        return {index + 1, -1};
    }
}
// 8a1d5c3e-9f27-4b6a-8e2d-1c7f9a3b5e6d end

/// One recovered `DO ... LOOP`/`+LOOP` region: @ref start is `do_setup`'s
/// own dest (the first instruction of the loop body, `do_setup`'s own index
/// plus one -- `interpreter::apply_control_word`'s own `do_` case pushes
/// exactly this value as the dest `LOOP`/`+LOOP` later pops), @ref end is
/// the matching `loop_step`/`plus_loop_step` instruction's own index
/// (exclusive: the body is `[start, end)`, the same convention `LOOP`'s own
/// back-patch scan uses for `LEAVE` sentinels).
struct loop_region {
    int start = 0;
    int end = 0;
};

/// Recovers every `DO ... LOOP`/`+LOOP` region in @p program's own
/// instruction range `[entry, end_exclusive)`, by matching each `do_setup`
/// at index `i` to the unique `loop_step`/`plus_loop_step` in
/// `[i + 1, end_exclusive)` whose own operand equals `i + 1` -- `do_setup`'s
/// own dest, which `interpreter::apply_control_word`'s own `loop_`/
/// `plus_loop_` case always emits as that instruction's backward-branch
/// operand. Every `DO` reaching `;` has exactly one such match:
/// `interpreter::interpret`'s own `;` handling already diagnoses an
/// unresolved `DO` (`compiling_context::loop_depth != 0`) before this
/// checker ever runs, so a `do_setup` with no match here would be a defect
/// in *that* invariant, not a case this function needs to diagnose itself.
///
/// @tparam MaxRegions Capacity for the returned list -- deliberately its
///                    own, smaller-by-default template parameter rather
///                    than reusing @p MaxCode (which bounds the *whole*
///                    session's shared code space, not one definition's own
///                    handful of loops): a `std::array`/`static_vector`
///                    sized off @p MaxCode's own usual multi-thousand
///                    default would make every call here (one per `;`, via
///                    @ref check_definition_effect) needlessly expensive at
///                    constant-evaluation time.
template <int MaxCode, int MaxWords, int MaxRegions = 64>
[[nodiscard]] constexpr auto recover_loop_regions(
    machine::compiled_program<MaxCode, MaxWords> const &program, int entry,
    int end_exclusive) -> foundation::static_vector<loop_region, MaxRegions> {
    foundation::static_vector<loop_region, MaxRegions> regions{};
    for (int i = entry; i < end_exclusive; ++i) {
        if (program.code[i].code != machine::op::do_setup) {
            continue;
        }
        int const dest = i + 1;
        for (int k = dest; k < end_exclusive; ++k) {
            auto const &ck = program.code[k];
            if ((ck.code == machine::op::loop_step ||
                 ck.code == machine::op::plus_loop_step) &&
                static_cast<int>(ck.operand) == dest) {
                // A definition with more than MaxRegions loops is not a
                // program this helper has ever been asked to handle in
                // practice; silently stop recording further regions rather
                // than exceeding static_vector's own precondition (D7:
                // still no UB, just a documented, generous-default limit).
                if (regions.size() < MaxRegions) {
                    regions.push_back(loop_region{dest, k});
                }
                break;
            }
        }
    }
    return regions;
}

/// One maximal straight-line run of instructions: no jump target lands
/// inside it, and only its own last instruction can be a branch/loop/return.
/// This is the reusable CFG product D24 asks this step to leave behind for
/// F33's own sender lowering ("one sender per block, block completions
/// wired as continuations") -- @ref check_definition_effect below does
/// *not* itself consume this type (a per-instruction worklist over @ref
/// instruction_successors is simpler to get right for a checker that only
/// cares about levels, not block identity), but both walk the exact same
/// edge relation, so a block boundary recovered here is always also an
/// instruction @ref check_definition_effect treats as a join or a branch.
struct basic_block {
    int start = 0; ///< First instruction index (inclusive).
    int end = 0;   ///< One past the last instruction index (exclusive).
};

/// Recovers @p program's own basic blocks over `[entry, end_exclusive)`
/// (one colon definition's own instruction range): a leader is @p entry
/// itself, the instruction immediately following any block-ending
/// instruction (whether or not anything actually falls into it -- unreached
/// code still gets its own block rather than being folded into whatever
/// precedes it), and *both* real edges of a genuine branch instruction
/// (`branch`/`branch0`/`loop_step`/`plus_loop_step`/`leave`/`catch_mark`) --
/// including a conditional's own fallthrough edge, since that is a second,
/// real way to reach what follows, distinct from plain physical adjacency.
/// Blocks are the maximal runs between consecutive leaders, in instruction
/// order. Diagnoses @p MaxBlocks exhaustion rather than exceeding it (D7).
///
/// Step F33 (docs/forth-plan-2.md) fix: an earlier draft of this function
/// added *every* instruction's own @ref instruction_successors edges as
/// leaders, unconditionally -- including @c edges.a for an ordinary,
/// non-branching instruction (@ref instruction_successors's own "default"
/// case, `{index + 1, -1}`), which is pure bookkeeping for @ref
/// check_definition_effect's own worklist advance, not a real CFG boundary.
/// That made *every* instruction a leader of its own physically-following
/// instruction, so *every* block came out one instruction long regardless of
/// whether any branch was nearby -- silently contradicting this function's
/// own "maximal straight-line run" contract (the existing unit test below
/// never caught it, since its only case already forces single-instruction
/// blocks for an unrelated reason: a leading `branch0`). F33's own sender
/// lowering is the first real consumer of block *granularity* (`recover_
/// basic_blocks` had none before this step, per D24's own doc comment) and
/// depends on straight-line runs actually being recovered as one block, so
/// this function now only consults @ref instruction_successors's own edges
/// for instructions that are genuinely branches -- an ordinary instruction's
/// physical adjacency to whatever follows needs no separate leader entry.
/// Filed as DIV-0025.
template <int MaxBlocks, int MaxCode, int MaxWords>
[[nodiscard]] constexpr auto recover_basic_blocks(
    machine::compiled_program<MaxCode, MaxWords> const &program, int entry,
    int end_exclusive)
    -> foundation::result<foundation::static_vector<basic_block, MaxBlocks>> {
    using machine::op;
    foundation::static_vector<int, MaxBlocks> leaders{};
    auto add_leader = [&](int idx) -> foundation::result<std::monostate> {
        if (idx < entry || idx >= end_exclusive) {
            return std::monostate{};
        }
        for (int i = 0; i < leaders.size(); ++i) {
            if (leaders[i] == idx) {
                return std::monostate{};
            }
        }
        if (leaders.size() >= MaxBlocks) {
            return foundation::parse_error{
                foundation::source_pos{},
                "recover_basic_blocks: MaxBlocks capacity exceeded"};
        }
        leaders.push_back(idx);
        return std::monostate{};
    };

    if (auto r = add_leader(entry); !r.has_value()) {
        return r.error();
    }
    for (int i = entry; i < end_exclusive; ++i) {
        auto const &in = program.code[i];
        // Only a genuine branch instruction's own edges are leader-worthy
        // (this function's own top comment, DIV-0025): an ordinary
        // instruction's "default" successor (index + 1) is physical
        // adjacency, not a real alternate control-flow entry.
        bool const is_branch =
            in.code == op::branch || in.code == op::branch0 ||
            in.code == op::loop_step || in.code == op::plus_loop_step ||
            in.code == op::leave || in.code == op::catch_mark;
        if (is_branch) {
            auto const edges = instruction_successors(in, i);
            if (edges.a != -1) {
                if (auto r = add_leader(edges.a); !r.has_value()) {
                    return r.error();
                }
            }
            if (edges.b != -1) {
                if (auto r = add_leader(edges.b); !r.has_value()) {
                    return r.error();
                }
            }
        }
        bool const is_terminator =
            in.code == op::ret || in.code == op::does_enter ||
            in.code == op::halt || in.code == op::branch ||
            in.code == op::branch0 || in.code == op::loop_step ||
            in.code == op::plus_loop_step || in.code == op::leave;
        if (is_terminator && i + 1 < end_exclusive) {
            if (auto r = add_leader(i + 1); !r.has_value()) {
                return r.error();
            }
        }
    }

    // Small insertion sort: MaxBlocks is always modest (bounded by one
    // definition's own instruction count), never worth a general sort.
    for (int i = 1; i < leaders.size(); ++i) {
        int const key = leaders[i];
        int j = i - 1;
        while (j >= 0 && leaders[j] > key) {
            leaders[j + 1] = leaders[j];
            --j;
        }
        leaders[j + 1] = key;
    }

    foundation::static_vector<basic_block, MaxBlocks> blocks{};
    for (int i = 0; i < leaders.size(); ++i) {
        int const block_end =
            (i + 1 < leaders.size()) ? leaders[i + 1] : end_exclusive;
        blocks.push_back(basic_block{leaders[i], block_end});
    }
    return blocks;
}

/// The result of @ref check_definition_effect: the definition's own overall
/// net @ref effect (either the declared effect, trusted once verified
/// against every reachable *known* exit, or the computed effect when
/// undeclared and every reachable exit is both known and mutually
/// consistent -- @ref unknown_effect otherwise, never a guess), plus
/// @ref peak_depth: the greatest absolute data-stack depth reached anywhere
/// in the body, assuming entry with exactly @ref effect::inputs cells (the
/// tightest safe case) -- `-1` ("not computed") unless @ref net::known is
/// also true (DIV-0019: peak depth is reported only for a definition whose
/// overall shape is fully pinned down, never as a partial number for one
/// that touches `unknown` anywhere or whose return paths genuinely differ).
/// @ref peak_return_depth is the same idea for the return stack's own
/// `>R`-driven depth.
///
/// @ref diagnostics (DIV-0019's own post-merge amendment) collects every
/// *advisory* data-stack join disagreement this definition's own body
/// reached -- a data-stack level mismatch across a branch or a loop's back
/// edge, undeclared, poisons that join to @ref unknown_effect exactly like
/// `EXECUTE`/`CATCH`/`LEAVE` already do (this header's own top comment),
/// rather than aborting the whole check: D20's own "advisory by default,
/// promoted to a hard gate for a definition exactly when a declared effect
/// is present" applies to a data-stack shape disagreement specifically,
/// because a loop body's own net data-stack effect can genuinely depend on
/// a runtime-determined trip count (the Hayes ttester's own `EMPTY-STACK`,
/// `DEPTH START-DEPTH @ < IF DEPTH START-DEPTH @ SWAP DO 0 LOOP THEN`, pads
/// the stack by a count only known at the moment it runs -- standard,
/// intentional Forth, not a defect this checker is entitled to reject
/// merely because it cannot pin one static shape on it). The *return*
/// stack gets no such allowance (@ref check_definition_effect's own doc
/// comment): Forth-2012 requires it balanced before `LOOP`, unconditionally,
/// so a return-stack join disagreement stays a hard, always-on error
/// regardless of @ref diagnostics or any declared effect.
template <int MaxDiagnostics>
struct definition_effect {
    effect net{};
    int peak_depth = -1;
    int peak_return_depth = -1;
    foundation::static_vector<foundation::parse_error, MaxDiagnostics>
        diagnostics{};
};

// 2f6a9c1d-7b3e-4d8a-9c1f-6e2b8a4d7f3c
/// Recovers the control-flow graph over @p program's own instruction range
/// `[entry, end_exclusive)` -- one colon definition's compiled body,
/// `interpreter::interpret`'s own `;` handling, immediately after it emits
/// that definition's own closing `ret` -- and checks it against every
/// diagnosis D20 carries forward from F12/F17, now over instructions
/// instead of the deleted elaborated-core tree:
///
///  - a branch's two arms, or a loop's back edge, disagreeing on net
///    data-stack effect (generalizes the old "IF/ELSE arms unequal" and
///    "DO/BEGIN body net must be zero"/"+LOOP body net must be +1"
///    diagnoses to *every* join this checker finds, via one uniform
///    mechanism -- a level assigned to each reachable instruction, required
///    to agree across every edge that reaches it -- rather than the old
///    per-construct special cases). **Advisory unless a declared effect is
///    present** (D20's own "advisory by default, promoted to a hard gate
///    ... exactly when a declared effect is present"): undeclared, a
///    disagreement here is collected into @ref definition_effect::
///    diagnostics and the offending join poisoned to @ref unknown_effect,
///    not fatal, because a loop body's own net data-stack effect can
///    genuinely depend on a runtime-determined trip count (the Hayes
///    ttester's own `EMPTY-STACK`, DIV-0019's own post-merge amendment).
///    With a declared effect present, this is promoted to a hard,
///    unconditional compile error at `;` -- the F17 corrections
///    (`DO`'s own two-cell entry cost, `+LOOP`'s own net-+1 body) are this
///    same mechanism, so a *declared* colon word using either is checked
///    exactly the way an undeclared one merely collects a diagnostic for;
///  - a `>R`/`R>` imbalance across any such join, including a loop body
///    (the F17 residual this step is assigned to close: `10 0 DO 5 >R
///    LOOP` now fails exactly here, at `;`, because the loop's own back
///    edge disagrees with the forward path on how deep the return stack
///    is -- rather than tearing down the wrong return-stack cells silently
///    at runtime), and at the end of the definition (every reachable exit
///    must leave the return stack exactly as deep as it was at entry).
///    **Always a hard, unconditional compile error, declared or not**:
///    unlike the data stack, Forth-2012 requires the return stack balanced
///    before `LOOP`, full stop -- a runtime-determined trip count changes
///    nothing about that requirement, so there is no "cannot be sure"
///    case to be advisory about (DIV-0019's own post-merge amendment: the
///    justification is the standard, not this analysis's own confidence);
///  - a bare `EXIT` lexically inside a `DO ... LOOP`/`+LOOP` with no
///    preceding `UNLOOP` on that path (@ref recover_loop_regions plus a
///    linear scan for an intervening `unloop`) -- also always a hard,
///    unconditional error (a structural defect independent of any runtime
///    trip count: the wrong return-stack cell is popped regardless of how
///    many iterations ran).
///
/// A declared `( ... -- ... )` effect additionally gates: @ref
/// required_depth (this function's own computed minimum entry depth) is
/// checked against the declaration's own inputs, and every reachable known
/// exit's own absolute output depth is checked against the declaration's
/// own outputs individually -- not just one arbitrarily-chosen path, which
/// is what actually closes DIV-0006 for the case that matters (see this
/// header's own top comment for why multiple *undeclared* exits
/// disagreeing is not itself an error).
///
/// @tparam MaxCode      Must match @p program's own instruction-array
///                      capacity.
/// @tparam MaxProgWords @p program's own word-table capacity; see @ref
///                      instruction_effect's own doc comment for why this
///                      is independent of @p dict's own @p MaxDictWords.
/// @tparam MaxDictWords @p dict's own entry capacity.
/// @tparam MaxSpan      Capacity for this function's own *per-definition*
///                      bookkeeping (the flow graph's node table, its own
///                      worklist, and the collected exit/`ret` lists),
///                      indexed by *offset from* @p entry rather than by
///                      raw instruction index -- deliberately independent
///                      of @p MaxCode (which bounds the whole session's
///                      ever-growing shared code space, D13, not any one
///                      definition's own instruction count): sizing this
///                      function's own working set off @p MaxCode's usual
///                      multi-thousand default would make every `;` pay
///                      for the whole session's own worst case, not just
///                      the definition actually being closed. Diagnoses a
///                      definition whose own instruction count exceeds it
///                      rather than exceeding a fixed-capacity container's
///                      own precondition (D7).
/// @tparam MaxDiagnostics Capacity for @ref definition_effect::diagnostics
///                      (this definition's own collected, advisory
///                      data-stack join disagreements). Excess diagnostics
///                      past this capacity are silently not recorded
///                      (never diagnosed as an error -- an advisory list
///                      running out of room must not itself become fatal,
///                      which would defeat the entire point of it being
///                      advisory); every corpus/conformance definition this
///                      project compiles produces at most a handful.
/// @param entry          The definition's own entry point (@ref
///                       machine::compiled_colon_word::entry_point) --
///                       also, per Forth-2012, `RECURSE`'s own call target,
///                       used to recognize a self-call.
/// @param end_exclusive  One past the definition's own trailing `ret` (i.e.
///                       `interpreter::compile_buffer::here()` immediately
///                       after emitting it).
/// @param source         The interpreter's own whole source text (@ref
///                       machine::input_source::text), for @ref
///                       parse_declared_effect to re-slice @p declared_span
///                       out of.
/// @param declared_span  The definition's own captured `( ... -- ... )`
///                       span (@ref machine::compiled_colon_word::
///                       effect_span), meaningless unless @p has_declared.
/// @param diag_pos       The position every diagnosis from this function
///                       carries: `machine::instr` has no source position
///                       of its own (D16's own retained shape), so every
///                       diagnosis this checker raises is positioned at the
///                       closing `;` itself -- a coarser position than the
///                       deleted elaborator-era checker's own per-node
///                       positions, and a documented divergence (DIV-0019).
template <int MaxCode, int MaxProgWords, int MaxDictWords, int MaxName,
          int MaxSpan = 160, int MaxDiagnostics = 8>
[[nodiscard]] constexpr auto check_definition_effect(
    machine::compiled_program<MaxCode, MaxProgWords> const &program,
    machine::dictionary<MaxDictWords, MaxName> const &dict, int entry,
    int end_exclusive, std::string_view source,
    foundation::source_span declared_span, bool has_declared,
    foundation::source_pos diag_pos)
    -> foundation::result<definition_effect<MaxDiagnostics>> {
    using machine::op;

    if (end_exclusive - entry > MaxSpan) {
        return foundation::parse_error{
            diag_pos, "effect lint: definition exceeds MaxSpan capacity"};
    }

    effect self_effect = unknown_effect;
    effect declared = unknown_effect;
    if (has_declared) {
        declared = parse_declared_effect(source, declared_span);
        if (declared.known) {
            self_effect = declared;
        }
    }
    // D20: "advisory by default, promoted to a hard gate for a definition
    // exactly when a declared effect is present" -- applies here to a
    // data-stack join disagreement specifically (see definition_effect's
    // own doc comment for why the return stack does not get the same
    // allowance). True exactly when a data-stack join mismatch below must
    // still abort the whole check rather than being poisoned to unknown
    // and collected.
    bool const declared_effect_gate = has_declared && declared.known;
    foundation::static_vector<foundation::parse_error, MaxDiagnostics>
        diagnostics{};

    // ---- Worklist: propagate a (data, return) level pair from `entry`
    // over every recovered edge, requiring agreement wherever two edges
    // reach the same instruction. Each node's state moves monotonically
    // down a small lattice (unvisited -> known or unknown; known -> unknown
    // only, never known -> a *different* known value, which is instead
    // diagnosed immediately) so this always terminates, including across
    // loop back edges. ----
    struct flow_node {
        bool data_visited = false;
        bool data_known = false;
        int data_level = 0;
        bool ret_visited = false;
        int ret_level = 0;
    };
    std::array<flow_node, MaxSpan> nodes{};

    struct pending_edge {
        int index = 0;
        bool data_known = true;
        int data_level = 0;
        int ret_level = 0;
    };
    // Each node's state changes at most twice (data: unvisited -> known or
    // unknown, then optionally known -> unknown; ret: unvisited -> known
    // once, any further disagreement diagnoses immediately rather than
    // re-enqueuing), and each change propagates to at most two successors --
    // a safe, generous capacity bound, checked defensively below anyway.
    foundation::static_vector<pending_edge, MaxSpan * 4 + 8> queue{};
    queue.push_back(pending_edge{entry, true, 0, 0});

    int head = 0;
    while (head < queue.size()) {
        pending_edge const item = queue[head];
        ++head;
        int const idx = item.index;
        if (idx < entry || idx >= end_exclusive) {
            return foundation::parse_error{
                diag_pos,
                "effect lint: control flow left the definition's own range"};
        }
        flow_node &node = nodes[idx - entry];

        bool ret_changed = false;
        if (!node.ret_visited) {
            node.ret_visited = true;
            node.ret_level = item.ret_level;
            ret_changed = true;
        } else if (node.ret_level != item.ret_level) {
            return foundation::parse_error{
                diag_pos, "return stack is unbalanced across a branch or "
                          "loop boundary"};
        }

        bool data_changed = false;
        if (!node.data_visited) {
            node.data_visited = true;
            node.data_known = item.data_known;
            node.data_level = item.data_level;
            data_changed = true;
        } else if (node.data_known) {
            if (item.data_known) {
                if (node.data_level != item.data_level) {
                    if (declared_effect_gate) {
                        return foundation::parse_error{
                            diag_pos,
                            "stack effect is inconsistent across a branch "
                            "or loop boundary"};
                    }
                    // Advisory (D20, DIV-0019's own post-merge amendment):
                    // undeclared, so a runtime-determined loop trip count
                    // (the Hayes ttester's own `EMPTY-STACK`) may
                    // legitimately make this join's own data-stack depth
                    // unknowable statically -- collect the disagreement
                    // rather than aborting, and poison this node to
                    // unknown exactly like an EXECUTE/CATCH/LEAVE-caused
                    // one (this header's own top comment).
                    if (diagnostics.size() < MaxDiagnostics) {
                        diagnostics.push_back(foundation::parse_error{
                            diag_pos,
                            "stack effect is inconsistent across a branch "
                            "or loop boundary (advisory: undeclared)"});
                    }
                    node.data_known = false;
                    data_changed = true;
                }
            } else {
                node.data_known = false;
                data_changed = true;
            }
        }
        // else: node already unknown -- stays unknown regardless of item.

        if (!ret_changed && !data_changed) {
            continue;
        }

        auto const ie =
            instruction_effect(program, dict, idx, entry, self_effect);
        bool const out_known = node.data_known && ie.data.known;
        int const out_level = out_known ? node.data_level + ie.data.net() : 0;
        int const out_ret = node.ret_level + ie.ret_delta;

        auto const edges = instruction_successors(program.code[idx], idx);
        if (edges.a != -1) {
            if (queue.size() >= MaxSpan * 4 + 8) {
                return foundation::parse_error{
                    diag_pos, "effect lint: worklist capacity exceeded"};
            }
            queue.push_back(
                pending_edge{edges.a, out_known, out_level, out_ret});
        }
        if (edges.b != -1) {
            if (queue.size() >= MaxSpan * 4 + 8) {
                return foundation::parse_error{
                    diag_pos, "effect lint: worklist capacity exceeded"};
            }
            queue.push_back(
                pending_edge{edges.b, out_known, out_level, out_ret});
        }
    }

    // ---- Final pass over the settled fixpoint: minimum entry depth, peak
    // depth, and every reachable exit's own absolute level. ----
    int required_depth = 0;
    int peak_relative = 0;
    int peak_return_relative = 0;
    // True the moment any reachable instruction's own *local* effect is
    // unknown (`?DUP`/`EXECUTE`/`CATCH`/a call to an unknown-effect word) --
    // distinct from a node's own settled @c data_known (which can already be
    // false by the time we reach some *later* instruction on the same
    // path). @ref required_depth's own running max silently contributes `0`
    // for such an instruction (@ref instruction_effect's own contract: an
    // unknown effect carries no verified minimum requirement, exactly like
    // the deleted elaborator's own `?DUP` treatment) -- an *undercount*, not
    // a sound bound, the moment this happens, so a declared effect's own
    // input side must not be checked against @ref required_depth once it
    // is true (this header's own top comment's `RUNIT`-shaped case: `( xt
    // -- ) EXECUTE` genuinely needs one cell, but this checker cannot see
    // that, and must not silently claim `0` disagrees with a correct `1`).
    bool any_unknown_reached = false;
    struct exit_point {
        bool known = true;
        int level = 0;
    };
    foundation::static_vector<exit_point, MaxSpan> exits{};
    foundation::static_vector<int, MaxSpan> ret_indices{};

    for (int i = entry; i < end_exclusive; ++i) {
        flow_node const &node = nodes[i - entry];
        if (!node.data_visited) {
            continue; // Unreachable from entry; not this checker's concern.
        }
        if (node.ret_level > peak_return_relative) {
            peak_return_relative = node.ret_level;
        }
        if (node.data_known) {
            auto const ie =
                instruction_effect(program, dict, i, entry, self_effect);
            if (!ie.data.known) {
                any_unknown_reached = true;
            }
            int const needed = ie.data.known ? ie.data.inputs : 0;
            int const contribution = needed - node.data_level;
            if (contribution > required_depth) {
                required_depth = contribution;
            }
            if (node.data_level > peak_relative) {
                peak_relative = node.data_level;
            }
        }

        auto const &in = program.code[i];
        if (in.code == op::ret || in.code == op::does_enter ||
            in.code == op::halt) {
            // A definition must leave the return stack exactly as deep as
            // it found it, on *every* reachable exit -- unlike the data
            // stack's own possibly-differing exits (this header's own top
            // comment), Forth-2012 gives no path any license to leave a
            // different >R/R> balance behind.
            if (node.ret_level != 0) {
                return foundation::parse_error{
                    diag_pos, "return stack is unbalanced at the end of a "
                              "definition"};
            }
            exits.push_back(exit_point{node.data_known, node.data_level});
        }
        if (in.code == op::ret) {
            ret_indices.push_back(i);
        }
    }

    // ---- Diagnosis 4: bare EXIT inside a DO loop without UNLOOP. ----
    auto const regions = recover_loop_regions(program, entry, end_exclusive);
    for (int i = 0; i < ret_indices.size(); ++i) {
        int const r = ret_indices[i];
        loop_region const *innermost = nullptr;
        for (int k = 0; k < regions.size(); ++k) {
            auto const &reg = regions[k];
            if (reg.start <= r && r < reg.end) {
                if (innermost == nullptr || reg.start > innermost->start) {
                    innermost = &regions[k];
                }
            }
        }
        if (innermost == nullptr) {
            continue;
        }
        bool unloop_seen = false;
        for (int k = innermost->start; k < r; ++k) {
            if (program.code[k].code == op::unloop) {
                unloop_seen = true;
                break;
            }
        }
        if (!unloop_seen) {
            return foundation::parse_error{
                diag_pos, "EXIT inside a DO loop requires UNLOOP"};
        }
    }

    // ---- Declared-effect gate (D20: hard exactly when declared). ----
    //
    // Gated on `!any_unknown_reached`, not merely per reachable exit's own
    // `known` flag: `required_depth` itself is only a *sound* number when
    // nothing unknown was ever reached (this loop's own top comment on
    // `any_unknown_reached`) -- once it has been, `required_depth` may
    // silently undercount (an `EXECUTE`'s own real requirement is invisible
    // to it), so neither the input nor the output side can be trusted
    // enough to diagnose a mismatch, even for an individual exit that
    // happens to still read as "known". This matches the deleted
    // elaborator-era checker's own precedent exactly (`declared_present &&
    // declared.known && summary.eff.known`): an undeclared or a fully-
    // unknown-touching definition's declared effect is trusted wholesale,
    // never partially argued with.
    if (has_declared && declared.known && !any_unknown_reached) {
        if (required_depth != declared.inputs) {
            return foundation::parse_error{
                diag_pos, "declared stack effect does not match computed "
                          "stack effect"};
        }
        for (int i = 0; i < exits.size(); ++i) {
            if (required_depth + exits[i].level != declared.outputs) {
                return foundation::parse_error{
                    diag_pos, "declared stack effect does not match "
                              "computed stack effect"};
            }
        }
    }

    // ---- Overall computed effect, when undeclared (informational: exits
    // disagreeing here is not itself an error -- this header's own top
    // comment). ----
    bool computed_known = !exits.empty();
    int common_level = 0;
    bool first = true;
    for (int i = 0; i < exits.size() && computed_known; ++i) {
        if (!exits[i].known) {
            computed_known = false;
            break;
        }
        if (first) {
            common_level = exits[i].level;
            first = false;
        } else if (common_level != exits[i].level) {
            computed_known = false;
        }
    }

    definition_effect<MaxDiagnostics> result{};
    if (has_declared && declared.known) {
        result.net = declared;
    } else if (computed_known) {
        result.net = known(required_depth, required_depth + common_level);
    } else {
        result.net = unknown_effect;
    }
    if (result.net.known) {
        result.peak_depth = required_depth + peak_relative;
        result.peak_return_depth = peak_return_relative;
    }
    result.diagnostics = diagnostics;
    return result;
}
// 2f6a9c1d-7b3e-4d8a-9c1f-6e2b8a4d7f3c end

} // namespace smd::forth::interpreter

#endif
