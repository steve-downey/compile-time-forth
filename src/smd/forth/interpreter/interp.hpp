// src/smd/forth/interpreter/interp.hpp                               -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#ifndef SRC_SMD_FORTH_INTERPRETER_INTERP_HPP
#define SRC_SMD_FORTH_INTERPRETER_INTERP_HPP

#include <smd/forth/foundation/parse_error.hpp>
#include <smd/forth/foundation/result.hpp>
#include <smd/forth/foundation/source_pos.hpp>
#include <smd/forth/foundation/source_span.hpp>
#include <smd/forth/interpreter/compilebuf.hpp>
#include <smd/forth/interpreter/effect_lint.hpp>
#include <smd/forth/machine/cell.hpp>
#include <smd/forth/machine/dictionary.hpp>
#include <smd/forth/machine/forth_state.hpp>
#include <smd/forth/machine/instruction.hpp>
#include <smd/forth/machine/vm.hpp>
#include <smd/forth/parser/cursor.hpp>
#include <smd/forth/parser/forth_chars.hpp>

#include <string_view>
#include <type_traits>
#include <variant>

namespace smd::forth::interpreter {

// Step F24 (docs/forth-plan-2.md): the Forth-2012 section 3.4 outer text
// interpreter, interpret state only (D13). F24 itself never left STATE 0 --
// there was no `:` yet -- but the field already existed, tested to exist and
// default correctly, because forth_state growing STATE/BASE/SOURCE/>IN
// together is the point of D13, not something to phase in word by word.
//
// D19 places "combinators below the word, owning scanning and
// classification" under parser<F>; F24's own number-per-BASE classification
// (is_number_token_in_base/token_to_cell_in_base below) follows that
// placement, generalizing parser::is_number_token/token_to_cell
// (parser/forth_chars.hpp, fixed to decimal per D8's original scope) rather
// than editing them.
//
// Step F25 is what actually writes STATE: `interpret` below now compiles as
// well as interprets. `:`, `;`, `EXIT`, and `RECURSE` are recognized by
// direct name comparison before any dictionary lookup -- the same technique
// the (now-deleted) R1 elaborator used for `I`/`J`/`LEAVE`/`UNLOOP`; F26 adds
// `VARIABLE`/`CONSTANT` to this same direct-name set (see below). F27
// generalizes immediate-word dispatch through the dictionary itself
// (execute_entry/compile_entry, D13's "execute when interpreting or
// immediate, else compile" as one rule); F28 moves `CREATE` off the
// direct-name list onto that same generalized dispatch (see this header's
// own `create_`/`does_` control-word cases below), because `CREATE` must now
// also be reachable from *inside* another word's own compiled body
// (`: CONSTANT2 CREATE , DOES> @ ;`), which a direct-name special case in
// this loop's own token scan could never reach. `VARIABLE`/`CONSTANT` stay
// on the direct-name list: nothing requires either to work from inside
// another word's own body yet, and leaving working code alone needed no
// justification beyond that. Compiling a colon definition appends directly
// into a compile_buffer (interpreter/compilebuf.hpp, D16's retained
// compiled_program as the toolkit's own artifact shape) as each token is
// met, one instruction at a time; there is no elaborated tree anywhere in
// this path. Interpreting an already-compiled word (D14) calls into that
// same code space via compile_buffer::call_word, against the identical live
// @p st this loop itself is mutating -- one semantics, not a second
// evaluator.
//
// Step F28 also closes DIV-0012's own deferred fold: `interpreter::
// forth_state`, the composed wrapper F24 introduced around machine::
// forth_state (SOURCE/>IN, BASE, STATE held alongside it rather than in it),
// is deleted. Every function below that used to take the wrapper now takes
// machine::forth_state directly, and every `st.machine().foo()` becomes
// plain `st.foo()`. See DIV-0012's own F28 addendum and machine/
// forth_state.hpp's own top comment for why this could not be deferred any
// further: `'`/`EXECUTE` (D18) and `CREATE`/`DOES>` (D10) all need the input
// stream and the data space reachable from whatever `machine::run_from` runs
// against, and `run_from` only ever sees a machine::forth_state.

/// Returns the value of digit @p c in @p base, or -1 if @p c is not a valid
/// digit in that base. Letters above `9` are `A`..`Z` -- already uppercase,
/// since @ref parser::scan_word folds every token before this ever sees it
/// (D8) -- so base 16's `FF` and base 36's `Z` both work without a separate
/// lowercase branch.
[[nodiscard]] constexpr auto digit_value(char c, int base) -> int {
    int value = -1;
    if (c >= '0' && c <= '9') {
        value = c - '0';
    } else if (c >= 'A' && c <= 'Z') {
        value = c - 'A' + 10;
    }
    if (value < 0 || value >= base) {
        return -1;
    }
    return value;
}

/// True iff @p text is an optional leading `-` followed by one or more
/// valid digits in @p base, and nothing else -- @ref parser::is_number_token
/// generalized from decimal to `BASE` (D19): at `base == 10` the two agree
/// on every input (this header's own tests check that directly), since
/// letters are never valid decimal digits either way.
[[nodiscard]] constexpr auto is_number_token_in_base(std::string_view text,
                                                     int base) -> bool {
    std::size_t i = 0;
    if (!text.empty() && text[0] == '-') {
        i = 1;
    }
    if (i == text.size()) {
        return false;
    }
    for (; i < text.size(); ++i) {
        if (digit_value(text[i], base) < 0) {
            return false;
        }
    }
    return true;
}

/// Converts @p text into its signed value in @p base.
/// @pre is_number_token_in_base(text, base)
[[nodiscard]] constexpr auto token_to_cell_in_base(std::string_view text,
                                                   int base) -> machine::cell {
    bool const negative = !text.empty() && text[0] == '-';
    std::size_t i = negative ? 1 : 0;
    machine::cell value = 0;
    for (; i < text.size(); ++i) {
        value = value * base + digit_value(text[i], base);
    }
    return negative ? -value : value;
}

// Merge criteria (static_assert, immediately-invoked-lambda pattern):
// base-10 agreement with parser::is_number_token/token_to_cell, and a
// representative base-16 case.

static_assert(is_number_token_in_base("-1", 10) ==
              parser::is_number_token("-1"));
static_assert(is_number_token_in_base("1-", 10) ==
              parser::is_number_token("1-"));
static_assert(is_number_token_in_base("-", 10) == parser::is_number_token("-"));
static_assert(is_number_token_in_base("42", 10) ==
              parser::is_number_token("42"));
static_assert(is_number_token_in_base("DUP", 10) ==
              parser::is_number_token("DUP"));
static_assert(token_to_cell_in_base("42", 10) == parser::token_to_cell("42"));
static_assert(token_to_cell_in_base("-1", 10) == parser::token_to_cell("-1"));

static_assert(is_number_token_in_base("FF", 16));
static_assert(token_to_cell_in_base("FF", 16) == 255);
static_assert(!is_number_token_in_base("FG", 16));
static_assert(is_number_token_in_base("-FF", 16));
static_assert(token_to_cell_in_base("-FF", 16) == -255);

/// Decrements @p fuel, the text interpreter loop's own step budget (D22):
/// distinct from @ref machine::consume_vm_fuel (which only bounds a
/// *compiled* program's VM execution) and from @ref machine::consume_fuel
/// (which only bounds the direct evaluator) -- this one bounds @ref
/// interpret's own outer loop, so a pathological interpret-time source
/// (arbitrarily many tokens, none of which ever exhausts the data stack or
/// any other already-fueled subsystem) cannot run the constant evaluator
/// past its budget undiagnosed. Diagnoses exhaustion at @p pos rather than
/// looping forever. Called once per token @ref interpret actually
/// processes, so a budget of @p fuel bounds the total number of tokens any
/// one interpretation may consume.
[[nodiscard]] constexpr auto consume_interp_fuel(int &fuel,
                                                 foundation::source_pos pos)
    -> machine::status {
    if (fuel <= 0) {
        return foundation::parse_error{
            pos, "interpreter execution budget exhausted"};
    }
    --fuel;
    return std::monostate{};
}

/// The result of @ref scan_colon_header: a `:` definition's folded name,
/// plus its declared `( ... -- ... )` stack-effect comment span if one was
/// present (D20: captured, not verified until F30).
template <int MaxName>
struct colon_header {
    machine::word_name<MaxName> name{};
    foundation::source_span effect{};
    bool has_effect = false;
};

/// Scans the name following `:` and, if one is immediately present, a
/// declared `( ... -- ... )` stack-effect comment -- the header-opening half
/// of `:`, split out of @ref interpret only to keep that function's own loop
/// body readable.
///
/// @p name_cur must already be positioned at the name itself, or at most
/// preceded by plain whitespace -- @ref interpret passes its own
/// post-`:`-token cursor, which @ref parser::scan_word's trailing skip has
/// already advanced past any such whitespace. Since step F29 (D19), a
/// comment cannot appear between `:` and its name: `\`/`(` are ordinary
/// dictionary words now (@ref apply_control_word's own `paren_`/
/// `backslash_` cases), found and dispatched by @ref interpret's own token
/// loop like any other word, so `: ( oops ) NAME` names the new word `(`,
/// exactly as parsing the next raw name would in any Forth-2012 system --
/// not a comment silently skipped first.
///
/// Unlike scan_colon_header's own pre-F29 shape, this can now use @ref
/// parser::scan_word's own rest cursor directly to find the name's own end:
/// @ref parser::scan_word's trailing skip is plain whitespace only (D19), so
/// a `( ... )` stack-effect comment immediately after the name is never at
/// risk of being silently consumed as ordinary intertoken space the way it
/// would have been when that trailing skip still ate comments too.
template <int MaxName>
[[nodiscard]] constexpr auto scan_colon_header(parser::cursor name_cur)
    -> parser::parse_result<colon_header<MaxName>> {
    auto name_scanned = parser::scan_word<MaxName>(name_cur);
    if (!name_scanned.has_value()) {
        // Unreachable in practice, mirroring interpret's own identical
        // defensive comment: scan_word's some<> only ever fails on empty
        // input, and an empty name is diagnosed explicitly just below
        // instead.
        return name_scanned.error();
    }
    auto const &folded_name = name_scanned.value().value;
    if (folded_name.empty()) {
        return foundation::parse_error{name_cur.position(),
                                       "expected a name after :"};
    }
    auto const after_name = name_scanned.value().rest;

    if (!after_name.empty() && after_name.peek() == '(') {
        auto comment = parser::scan_paren_comment(after_name);
        if (comment.has_value()) {
            return parser::parse_state<colon_header<MaxName>>{
                colon_header<MaxName>{.name = folded_name,
                                      .effect = comment.value().value,
                                      .has_effect = true},
                comment.value().rest};
        }
        // An unterminated '(' right after the name: fall through and leave
        // the cursor at the name's own end. The very next token this
        // function's own caller (@ref interpret) scans is then that same
        // dangling `(`, dispatched as the ordinary `paren_` control word
        // (step F29, D19) -- which scans forward for a closing `)` itself
        // and diagnoses "unterminated ( comment" when it finds none, so the
        // failure still surfaces, just through the same path any other
        // unterminated `(` anywhere else in the source does now.
    }
    return parser::parse_state<colon_header<MaxName>>{
        colon_header<MaxName>{.name = folded_name}, after_name};
}

/// Bookkeeping for the definition currently being compiled, if any --
/// replaces the four loose locals @ref interpret used before step F27
/// (`compiling_name`/`compiling_entry`/`compiling_effect`/
/// `compiling_has_effect`) with one value, now that control flow needs four
/// more fields alongside them. Meaningful only while the owning @ref
/// interpret call's own `st.state() != 0`; reset to a fresh value at every
/// `:` (D13: one colon definition's own bookkeeping never leaks into the
/// next, since nested `:` is diagnosed rather than allowed).
///
/// @ref loop_depth exists for step F27's own control-flow discipline (D17):
/// it lets `LEAVE`/`UNLOOP`/`I`/`J` diagnose use outside a `DO ... LOOP`,
/// and lets `;` diagnose an unresolved `DO` (one whose matching `LOOP`/
/// `+LOOP` was never reached). It is *not* the data stack itself -- unlike
/// `IF`/`BEGIN`'s own orig/dest markers, which genuinely live on @p st's
/// data stack per D17 ("the data stack serving as the control-flow stack as
/// the standard permits"), @ref loop_depth is private bookkeeping @ref
/// apply_control_word's own `do_`/`loop_`/`plus_loop_` cases maintain
/// directly. That split is deliberate: an early draft of this step also
/// tried to diagnose an unresolved `IF`/`BEGIN` at `;` by checking that @p
/// st's data stack depth returned to what it was at `:`, and that check is
/// unsound -- an immediate word's own body can legitimately leave real data
/// on that same stack when it runs at compile time (`: DOUBLE-IT 21 ;
/// IMMEDIATE` genuinely leaves a `21` behind wherever it is met while
/// compiling, exactly as `[ ... ] LITERAL`'s own bracketed computation
/// does), so a plain depth comparison cannot tell that apart from a
/// genuinely unresolved `IF`. `LOOP`/`+LOOP` without a matching `DO` is
/// still diagnosed (via @ref loop_depth, see above); `THEN`/`REPEAT`
/// without a matching `IF`/`BEGIN` is still diagnosed, just later, when the
/// data stack (a genuine data stack at that point, not a control-flow
/// stack) does not hold what they expect to pop (D7: a diagnosed
/// underflow, not UB) -- the "mismatched THEN without IF" merge criterion
/// this step names explicitly is exactly that case.
///
/// @ref has_postponed_alias and @ref postponed_target exist for `POSTPONE`
/// of a C++-native control word (`: ENDIF POSTPONE THEN ; IMMEDIATE`): see
/// @ref apply_control_word's own `postpone_` case for the full rationale.
// b4e7b8e3-3275-47e2-81da-1c7eb25ae1db
template <int MaxName>
struct compiling_context {
    machine::word_name<MaxName> name{};
    int entry = -1;
    foundation::source_span effect{};
    bool has_effect = false;
    int loop_depth = 0;
    bool has_postponed_alias = false;
    machine::control_builtin postponed_target{};
};
// b4e7b8e3-3275-47e2-81da-1c7eb25ae1db end

/// Appends @p entry's own *compiled* form to @p buf -- the action every
/// non-immediate dictionary entry gets when the text interpreter meets it
/// while compiling (D13), and also what `POSTPONE`/`COMPILE,` (step F27)
/// reuse for any target that has one: a primitive emits @ref
/// machine::op::prim, a @ref machine::compiled_colon_word emits @ref
/// machine::op::call to its own entry point, a @ref machine::variable_word
/// emits @ref machine::op::push (its own address) followed by @ref
/// machine::op::call to its own @ref machine::variable_word::does_entry if
/// one is set (step F28's own `DOES>`, D18 -- unset, the default, emits
/// nothing further, exactly as `interpret`'s own pre-F28 compiling dispatch
/// already did for a plain `VARIABLE`/`CREATE`), a @ref
/// machine::constant_word emits @ref machine::op::push (its own value), and
/// a @ref machine::value_word emits @ref machine::op::push (its own address)
/// followed by @ref machine::op::prim @ref machine::primitive::fetch (so a
/// compiled reference always reads the *current* value, respecting a later
/// `TO`). A @ref machine::defer_word (step F28) emits the same
/// push-address/fetch pair followed by @ref machine::op::execute: no
/// dictionary access needed at runtime at all, since the address already
/// holds either a valid execution token or the `-1` "not yet `IS`sed"
/// sentinel, and `EXECUTE`ing `-1` diagnoses cleanly as an out-of-range
/// instruction pointer (D7) rather than needing a bespoke check here.
///
/// A bare @ref machine::control_word has no compiled form in general -- its
/// whole point (see @ref machine::control_builtin's own doc comment) is that
/// it can *only* run as C++ code with direct access to @p buf, never as VM
/// bytecode with no such access -- except the three step F28 control words
/// that *do* have one: `EXECUTE` emits a bare @ref machine::op::execute
/// (D18: pop an execution token, jump to it, exactly like a call), `CREATE`
/// emits @ref machine::op::create_word with operand 0 (D10: scan a name,
/// install it, at *runtime*, once per invocation -- this is what lets a
/// defining word like `: CONSTANT2 CREATE , DOES> @ ;` reach the dictionary
/// from inside its own compiled body), and `DOES>` emits @ref machine::op::
/// does_enter. Every other control word, and @ref machine::foreign_word (not
/// compilable before step F19 either), both diagnose rather than emit
/// anything.
template <int MaxCode, int MaxBufWords, int MaxName>
[[nodiscard]] constexpr auto
compile_entry(machine::dictionary_entry<MaxName> const &entry,
              compile_buffer<MaxCode, MaxBufWords> &buf,
              foundation::source_pos pos) -> machine::status {
    if (auto const *op = std::get_if<machine::primitive>(&entry.binding)) {
        auto r =
            buf.emit(machine::op::prim, static_cast<machine::cell>(*op), pos);
        if (!r.has_value()) {
            return r.error();
        }
        return std::monostate{};
    }
    if (auto const *cw =
            std::get_if<machine::compiled_colon_word>(&entry.binding)) {
        auto r = buf.emit(machine::op::call,
                          static_cast<machine::cell>(cw->entry_point), pos);
        if (!r.has_value()) {
            return r.error();
        }
        return std::monostate{};
    }
    if (auto const *vw = std::get_if<machine::variable_word>(&entry.binding)) {
        auto r = buf.emit(machine::op::push,
                          static_cast<machine::cell>(vw->address), pos);
        if (!r.has_value()) {
            return r.error();
        }
        if (vw->does_entry >= 0) {
            auto r2 = buf.emit(machine::op::call,
                               static_cast<machine::cell>(vw->does_entry), pos);
            if (!r2.has_value()) {
                return r2.error();
            }
        }
        return std::monostate{};
    }
    if (auto const *cnw = std::get_if<machine::constant_word>(&entry.binding)) {
        auto r = buf.emit(machine::op::push, cnw->value, pos);
        if (!r.has_value()) {
            return r.error();
        }
        return std::monostate{};
    }
    if (auto const *vlw = std::get_if<machine::value_word>(&entry.binding)) {
        auto r = buf.emit(machine::op::push,
                          static_cast<machine::cell>(vlw->address), pos);
        if (!r.has_value()) {
            return r.error();
        }
        auto r2 = buf.emit(
            machine::op::prim,
            static_cast<machine::cell>(machine::primitive::fetch), pos);
        if (!r2.has_value()) {
            return r2.error();
        }
        return std::monostate{};
    }
    if (auto const *dw = std::get_if<machine::defer_word>(&entry.binding)) {
        auto r = buf.emit(machine::op::push,
                          static_cast<machine::cell>(dw->address), pos);
        if (!r.has_value()) {
            return r.error();
        }
        auto r2 = buf.emit(
            machine::op::prim,
            static_cast<machine::cell>(machine::primitive::fetch), pos);
        if (!r2.has_value()) {
            return r2.error();
        }
        auto r3 = buf.emit(machine::op::execute, machine::cell{0}, pos);
        if (!r3.has_value()) {
            return r3.error();
        }
        return std::monostate{};
    }
    if (auto const *ctl = std::get_if<machine::control_word>(&entry.binding)) {
        using machine::control_builtin;
        if (ctl->which == control_builtin::execute_) {
            auto r = buf.emit(machine::op::execute, machine::cell{0}, pos);
            if (!r.has_value()) {
                return r.error();
            }
            return std::monostate{};
        }
        if (ctl->which == control_builtin::create_) {
            auto r = buf.emit(machine::op::create_word, machine::cell{0}, pos);
            if (!r.has_value()) {
                return r.error();
            }
            return std::monostate{};
        }
        if (ctl->which == control_builtin::does_) {
            auto r = buf.emit(machine::op::does_enter, machine::cell{0}, pos);
            if (!r.has_value()) {
                return r.error();
            }
            return std::monostate{};
        }
        if (ctl->which == control_builtin::catch_) {
            // `CATCH` (step F31, D11/D18): two instructions, both fixed
            // length, so the resume ip (this frame's own `RESUME`, one past
            // both) is known immediately -- no back-patching needed. `op::
            // catch_mark` pops the execution token already on the data
            // stack, pushes the 3-cell handler frame, and jumps to it;
            // `prim catch_ok` is the normal-completion epilogue it returns
            // to (see machine/vm.hpp's own op::catch_mark and machine/
            // forth_state.hpp's own catch_ok primitive).
            int const mark_idx = buf.here();
            auto r1 = buf.emit(machine::op::catch_mark,
                               static_cast<machine::cell>(mark_idx + 2), pos);
            if (!r1.has_value()) {
                return r1.error();
            }
            auto r2 = buf.emit(
                machine::op::prim,
                static_cast<machine::cell>(machine::primitive::catch_ok), pos);
            if (!r2.has_value()) {
                return r2.error();
            }
            return std::monostate{};
        }
        if (ctl->which == control_builtin::throw_) {
            // `THROW` (step F31, D11): a bare op::throw_op; n is already on
            // the data stack.
            auto r = buf.emit(machine::op::throw_op, machine::cell{0}, pos);
            if (!r.has_value()) {
                return r.error();
            }
            return std::monostate{};
        }
        if (ctl->which == control_builtin::abort_) {
            // `ABORT` (step F31, D11): `-1 THROW`, per Forth-2012.
            auto r1 = buf.emit(machine::op::push, machine::cell{-1}, pos);
            if (!r1.has_value()) {
                return r1.error();
            }
            auto r2 = buf.emit(machine::op::throw_op, machine::cell{0}, pos);
            if (!r2.has_value()) {
                return r2.error();
            }
            return std::monostate{};
        }
    }
    return foundation::parse_error{pos, "word has no compiled form"};
}

// 1c9e6a4f-7b3d-4e8a-9c2f-6a1d8b4e3f7c
/// Resolves @p entry to an execution token (D18): a code-space instruction
/// index that @ref machine::op::execute can later jump to exactly like a
/// call, given only @p entry's own binding -- the shared machinery behind
/// both `'` (@ref apply_control_word's own `tick_` case, interpreting) and
/// `[']` (its `bracket_tick_` case, compiling).
///
/// A @ref machine::compiled_colon_word's own entry point already is one (it
/// is `ret`-terminated by construction, F14's discipline): returned directly,
/// no emission. Every other resolvable kind -- @ref machine::primitive, @ref
/// machine::variable_word, @ref machine::constant_word, @ref
/// machine::value_word -- has no standing code-space location of its own, so
/// this function builds a small `ret`-terminated stub for it, unconditionally
/// guarded by a leading @ref machine::op::branch that jumps past the stub
/// (patched to land just after it): @p buf may be positioned *inside* a
/// still-open colon definition's own body when this runs (`'`/`[']` used
/// inside a `[ ... ]` bracket while compiling something else, D13's own
/// bracket-interpreting state) -- appending a stub's own instructions inline,
/// unguarded, would be silently executed as part of that enclosing
/// definition's own body the next time it runs (its own `ret` would return
/// early, corrupting control flow), the same hazard @ref apply_control_word's
/// own `IF`/`WHILE` sentinel-and-patch discipline exists to avoid. The guard
/// costs one extra instruction in the (common) case where @p buf actually was
/// at a safe append point; it is never wrong to pay it.
///
/// Diagnoses if @p entry is a @ref machine::control_word, @ref
/// machine::foreign_word, or @ref machine::defer_word: none has a stable
/// code-space location an XT can usefully name yet (a control word's whole
/// point is that it has no VM-representable form at all; a not-yet-`IS`sed
/// deferred word's *target* does not exist yet either) -- a documented,
/// narrower scope than full Forth-2012 `'`/`[']` (DIV-0016 records this and
/// its own revisit condition).
template <int MaxCode, int MaxBufWords, int MaxName>
[[nodiscard]] constexpr auto
resolve_execution_token(machine::dictionary_entry<MaxName> const &entry,
                        compile_buffer<MaxCode, MaxBufWords> &buf,
                        foundation::source_pos pos) -> foundation::result<int> {
    using machine::cell;
    using machine::op;

    if (auto const *cw =
            std::get_if<machine::compiled_colon_word>(&entry.binding)) {
        return cw->entry_point;
    }

    auto skip = buf.emit(op::branch, cell{-1}, pos);
    if (!skip.has_value()) {
        return skip.error();
    }
    int const stub = buf.here();

    if (auto const *p = std::get_if<machine::primitive>(&entry.binding)) {
        auto r = buf.emit(op::prim, static_cast<cell>(*p), pos);
        if (!r.has_value()) {
            return r.error();
        }
    } else if (auto const *vw =
                   std::get_if<machine::variable_word>(&entry.binding)) {
        auto r = buf.emit(op::push, static_cast<cell>(vw->address), pos);
        if (!r.has_value()) {
            return r.error();
        }
        if (vw->does_entry >= 0) {
            auto r2 =
                buf.emit(op::call, static_cast<cell>(vw->does_entry), pos);
            if (!r2.has_value()) {
                return r2.error();
            }
        }
    } else if (auto const *cn =
                   std::get_if<machine::constant_word>(&entry.binding)) {
        auto r = buf.emit(op::push, cn->value, pos);
        if (!r.has_value()) {
            return r.error();
        }
    } else if (auto const *vl =
                   std::get_if<machine::value_word>(&entry.binding)) {
        auto r = buf.emit(op::push, static_cast<cell>(vl->address), pos);
        if (!r.has_value()) {
            return r.error();
        }
        auto r2 = buf.emit(op::prim,
                           static_cast<cell>(machine::primitive::fetch), pos);
        if (!r2.has_value()) {
            return r2.error();
        }
    } else {
        return foundation::parse_error{pos, "word has no execution token"};
    }

    auto ret_r = buf.emit(op::ret, cell{0}, pos);
    if (!ret_r.has_value()) {
        return ret_r.error();
    }

    buf.program().code[skip.value()].operand = static_cast<cell>(buf.here());
    return stub;
}
// 1c9e6a4f-7b3d-4e8a-9c2f-6a1d8b4e3f7c end

// d1a4f7b2-6c8e-4a3d-9b5f-2e7c4a9d1b6e
/// Performs one C++-native control word's own action: `IF ELSE THEN`,
/// `BEGIN UNTIL`, `BEGIN WHILE REPEAT`, `DO LOOP +LOOP LEAVE UNLOOP I J`,
/// `LITERAL`, `POSTPONE`, `IMMEDIATE`, `[`, `]`, `COMPILE,` (step F27, D17),
/// and `'`, `[']`, `EXECUTE`, `CREATE`, `DOES>`, `VALUE`, `TO`, `DEFER`, `IS`
/// (step F28, D18 -- see each `case` below for its own rationale).
///
/// Every control word but `[`, `]`, `IMMEDIATE`, and every step F28 word
/// whose own execution semantics only ever needs to fire while interpreting
/// anyway (`'`, `EXECUTE`, `CREATE`, `VALUE`, `DEFER`, `IS` -- each is
/// non-immediate, so this function is only ever reached for them via @ref
/// execute_entry's own "if found while interpreting, execute" dispatch, D13,
/// never via a compiling dispatch, which calls @ref compile_entry for a
/// non-immediate entry instead) is diagnosed as compile-only if @p st is
/// interpreting (`state() == 0`) -- the same "compile-only, used while
/// interpreting" shape @ref interpret already gives `;`/`EXIT`/`RECURSE`
/// directly.
///
/// **`IF`/`WHILE`**: emit a sentinel @ref machine::op::branch0 (operand -1,
/// which can never be a real instruction index) and push the emitted
/// instruction's own index (its "orig", Forth-2012's term) onto @p st's data
/// stack, which doubles as the control-flow stack while compiling (D17: "the
/// data stack serving as the control-flow stack as the standard permits").
/// **`ELSE`**: pops `IF`'s orig, emits a sentinel @ref machine::op::branch
/// (the jump past the else-arm), patches the popped orig to land right after
/// it, and pushes the new branch's own index as its own orig for `THEN` to
/// resolve. **`THEN`**: pops an orig and patches it to @p buf's own current
/// position -- the diagnosis an orig-less `THEN` (`THEN` with no matching
/// `IF`) gets is exactly a data-stack-underflow @ref foundation::result, the
/// same discipline that makes every other misuse here a diagnosed error
/// rather than UB (D7).
///
/// **`BEGIN`**: pushes @p buf's own current position as a "dest" (no
/// instruction emitted -- a backward branch target needs no sentinel).
/// **`UNTIL`**: pops a dest and emits @ref machine::op::branch0 back to it.
/// **`WHILE`**: emits its own sentinel @ref machine::op::branch0 (an orig,
/// exactly like `IF`'s), leaving `BEGIN`'s own dest underneath it on the
/// stack. **`REPEAT`**: pops `WHILE`'s orig and `BEGIN`'s dest (in that
/// order), emits an unconditional @ref machine::op::branch back to dest, and
/// patches orig to land just past it.
///
/// **`DO`**: emits @ref machine::op::do_setup and pushes @p buf's own
/// resulting position as a dest, and increments @p cctx's own @ref
/// compiling_context::loop_depth. **`LOOP`/`+LOOP`**: pops that dest, emits
/// @ref machine::op::loop_step or @ref machine::op::plus_loop_step with it
/// as the backward-branch operand, then re-plays F17's own sentinel scan
/// (`docs/compiler_architecture.org`, Phase 9's own DO...LOOP codegen,
/// before this step relocated it here token-by-token instead of
/// tree-structured): every @ref machine::op::leave instruction between dest
/// and @p buf's own new position that still carries `LEAVE`'s own -1
/// sentinel operand is patched to that new position, bounded exactly to this
/// `DO`'s own body since any inner nested loop's own `LOOP`/`+LOOP` already
/// resolved its own `LEAVE`s first. **`LEAVE`/`UNLOOP`/`I`/`J`**: diagnosed
/// if @p cctx's own @ref compiling_context::loop_depth does not cover them
/// (`J` needs two levels of nesting), else each emits its own single
/// instruction (@ref machine::op::leave with the same -1 sentinel, @ref
/// machine::op::unloop, or @ref machine::op::push_index at level 0 or 1)
/// with no control-flow-stack push or pop of its own.
///
/// **`LITERAL`**: pops @p st's own data stack (a value some earlier
/// interpreted code -- typically inside a `[ ... ]` bracket, see below --
/// left there) and emits it as an @ref machine::op::push literal.
/// **`[`**: sets `STATE` to 0, dropping @p st back to interpreting for the
/// tokens up to the matching `]` (an ordinary, non-immediate control word:
/// found and executed like any other word once @p st is interpreting again,
/// needing no special dispatch of its own). **`]`**: sets `STATE` back to 1.
/// **`IMMEDIATE`**: flags @p dict's own most recently defined entry via
/// @ref machine::dictionary::mark_last_immediate, regardless of that entry's
/// own binding kind.
///
/// **`COMPILE,`**: pops an execution token or dictionary index (see
/// `interp.test.cpp`'s own `CompileCommaAppendsAnEntrysCompiledForm`, which
/// still drives it as a dictionary index directly, D11's original
/// convention) and appends that entry's own compiled form via @ref
/// compile_entry. **`POSTPONE`**: scans the next source token as a name
/// (bypassing ordinary dispatch entirely, the same way `:` scans its own
/// following name) and looks it up in @p dict. If the target is anything
/// @ref compile_entry can compile, `POSTPONE` does exactly that -- appending
/// a call/push to the *current* definition so that definition's own later
/// execution runs the target, which is POSTPONE's Forth-2012 contract
/// regardless of whether the target happens to be immediate (an immediate
/// @ref machine::compiled_colon_word still has a real entry point; appending
/// a call to it is correct either way). Since step F28's own @ref
/// compile_entry gives `EXECUTE`/`CREATE`/`DOES>` real compiled forms
/// (@ref machine::op::execute/@ref machine::op::create_word/@ref
/// machine::op::does_enter), `POSTPONE EXECUTE`, `POSTPONE CREATE`, and
/// `POSTPONE DOES>` now compose freely with other code in the same
/// definition, exactly like postponing any primitive already did -- DIV-0015
/// closes for these three specifically (see that record's own F28 addendum).
/// If the target is a bare @ref machine::control_word with no compiled form
/// (`IF`/`THEN`/`DO`/`LOOP`/... -- the orig/dest-patching structural control
/// words D17 introduced), no VM code can represent "run this word's own C++
/// action later" (@ref machine::control_builtin's own doc comment: its whole
/// action *is* mutating @p buf directly, not a runtime effect an XT could
/// ever name), so `POSTPONE` instead records the target on @p cctx itself
/// (@ref compiling_context::has_postponed_alias /
/// @ref compiling_context::postponed_target): @ref interpret's own `;`
/// handling, seeing that flag, defines the word being closed as a plain
/// alias for the target control word (a @ref machine::control_word entry
/// with the same tag) instead of a @ref machine::compiled_colon_word. This
/// remains a deliberate, narrower scope than full Forth-2012 `POSTPONE` for
/// exactly these structural control words (which permits postponing one
/// anywhere, freely mixed with ordinary compiled code, in the same
/// definition): only a definition whose *entire* body is that one
/// `POSTPONE` is supported, diagnosed otherwise. DIV-0015's own revisit
/// condition asked whether D18's header unification would close this; it
/// does not, and cannot: a structural control word has no runtime action to
/// name in the first place (giving it a header/XT slot would not create
/// one), so this is a structural fact about what these words *are*, not a
/// scope cut header unification merely failed to close. See DIV-0015's own
/// F28 addendum for the full resolution.
template <int MaxDepth, int MaxRDepth, int MaxData, int MaxOut, int MaxWords,
          int MaxCode, int MaxBufWords, int MaxName>
[[nodiscard]] constexpr auto apply_control_word(
    machine::control_builtin which,
    machine::forth_state<MaxDepth, MaxRDepth, MaxData, MaxOut> &st,
    machine::dictionary<MaxWords, MaxName> &dict,
    compile_buffer<MaxCode, MaxBufWords> &buf, compiling_context<MaxName> &cctx,
    foundation::source_pos pos, int vm_fuel = 100000) -> machine::status {
    using machine::cell;
    using machine::control_builtin;
    using machine::op;

    auto compile_only = [&]() -> machine::status {
        return foundation::parse_error{
            pos, "control word is compile-only, used while interpreting"};
    };
    // Patches the branch instruction at code index @c idx to jump to
    // @c target, diagnosing an out-of-range @c idx (an orig/dest popped off
    // a data stack that was not actually holding one, e.g. `THEN` with no
    // matching `IF`) rather than letting static_vector::operator[]'s own
    // precondition fire (D7: diagnosed, never UB).
    auto patch = [&](int idx, cell target) -> machine::status {
        if (idx < 0 || idx >= buf.program().code.size()) {
            return foundation::parse_error{
                pos, "control-flow discipline violated: no matching IF/WHILE"};
        }
        buf.program().code[idx].operand = target;
        return std::monostate{};
    };

    switch (which) {
    // e1452910-f0ef-482d-a77b-c8dc081445db
    case control_builtin::if_:
    case control_builtin::while_: {
        if (st.state() == 0) {
            return compile_only();
        }
        auto r = buf.emit(op::branch0, cell{-1}, pos);
        if (!r.has_value()) {
            return r.error();
        }
        return st.data().push(static_cast<cell>(r.value()));
    }
    case control_builtin::else_: {
        if (st.state() == 0) {
            return compile_only();
        }
        auto orig = st.data().pop();
        if (!orig.has_value()) {
            return orig.error();
        }
        auto r = buf.emit(op::branch, cell{-1}, pos);
        if (!r.has_value()) {
            return r.error();
        }
        auto p = patch(static_cast<int>(orig.value()),
                       static_cast<cell>(buf.here()));
        if (!p.has_value()) {
            return p;
        }
        return st.data().push(static_cast<cell>(r.value()));
    }
    case control_builtin::then_: {
        if (st.state() == 0) {
            return compile_only();
        }
        auto orig = st.data().pop();
        if (!orig.has_value()) {
            return orig.error();
        }
        return patch(static_cast<int>(orig.value()),
                     static_cast<cell>(buf.here()));
    }
    // e1452910-f0ef-482d-a77b-c8dc081445db end
    case control_builtin::begin_: {
        if (st.state() == 0) {
            return compile_only();
        }
        return st.data().push(static_cast<cell>(buf.here()));
    }
    case control_builtin::until_: {
        if (st.state() == 0) {
            return compile_only();
        }
        auto dest = st.data().pop();
        if (!dest.has_value()) {
            return dest.error();
        }
        auto r = buf.emit(op::branch0, static_cast<cell>(dest.value()), pos);
        if (!r.has_value()) {
            return r.error();
        }
        return std::monostate{};
    }
    case control_builtin::repeat_: {
        if (st.state() == 0) {
            return compile_only();
        }
        auto orig = st.data().pop(); // WHILE's own orig
        if (!orig.has_value()) {
            return orig.error();
        }
        auto dest = st.data().pop(); // BEGIN's own dest
        if (!dest.has_value()) {
            return dest.error();
        }
        auto r = buf.emit(op::branch, static_cast<cell>(dest.value()), pos);
        if (!r.has_value()) {
            return r.error();
        }
        return patch(static_cast<int>(orig.value()),
                     static_cast<cell>(buf.here()));
    }
    // ea0ed94a-2b2c-4cfa-9a0d-d27eea165a14
    case control_builtin::do_: {
        if (st.state() == 0) {
            return compile_only();
        }
        auto setup_r = buf.emit(op::do_setup, cell{0}, pos);
        if (!setup_r.has_value()) {
            return setup_r.error();
        }
        auto pr = st.data().push(static_cast<cell>(buf.here()));
        if (!pr.has_value()) {
            return pr;
        }
        ++cctx.loop_depth;
        return std::monostate{};
    }
    case control_builtin::loop_:
    case control_builtin::plus_loop_: {
        if (st.state() == 0) {
            return compile_only();
        }
        if (cctx.loop_depth <= 0) {
            return foundation::parse_error{pos,
                                           which == control_builtin::loop_
                                               ? "LOOP without a matching DO"
                                               : "+LOOP without a matching DO"};
        }
        auto dest = st.data().pop();
        if (!dest.has_value()) {
            return dest.error();
        }
        int const dest_idx = static_cast<int>(dest.value());
        auto r = buf.emit(which == control_builtin::loop_ ? op::loop_step
                                                          : op::plus_loop_step,
                          static_cast<cell>(dest_idx), pos);
        if (!r.has_value()) {
            return r.error();
        }
        int const loop_exit = buf.here();
        for (int k = dest_idx; k < loop_exit; ++k) {
            auto &code_k = buf.program().code[k];
            if (code_k.code == op::leave && code_k.operand == cell{-1}) {
                code_k.operand = static_cast<cell>(loop_exit);
            }
        }
        --cctx.loop_depth;
        return std::monostate{};
    }
    case control_builtin::leave_: {
        if (st.state() == 0) {
            return compile_only();
        }
        if (cctx.loop_depth <= 0) {
            return foundation::parse_error{pos, "LEAVE outside a DO ... LOOP"};
        }
        auto r = buf.emit(op::leave, cell{-1}, pos);
        if (!r.has_value()) {
            return r.error();
        }
        return std::monostate{};
    }
    // ea0ed94a-2b2c-4cfa-9a0d-d27eea165a14 end
    case control_builtin::unloop_: {
        if (st.state() == 0) {
            return compile_only();
        }
        if (cctx.loop_depth <= 0) {
            return foundation::parse_error{pos, "UNLOOP outside a DO ... LOOP"};
        }
        auto r = buf.emit(op::unloop, cell{0}, pos);
        if (!r.has_value()) {
            return r.error();
        }
        return std::monostate{};
    }
    case control_builtin::i_: {
        if (st.state() == 0) {
            return compile_only();
        }
        if (cctx.loop_depth < 1) {
            return foundation::parse_error{pos, "I outside a DO ... LOOP"};
        }
        auto r = buf.emit(op::push_index, cell{0}, pos);
        if (!r.has_value()) {
            return r.error();
        }
        return std::monostate{};
    }
    case control_builtin::j_: {
        if (st.state() == 0) {
            return compile_only();
        }
        if (cctx.loop_depth < 2) {
            return foundation::parse_error{pos,
                                           "J requires a nested DO ... LOOP"};
        }
        auto r = buf.emit(op::push_index, cell{1}, pos);
        if (!r.has_value()) {
            return r.error();
        }
        return std::monostate{};
    }
    case control_builtin::literal_: {
        if (st.state() == 0) {
            return compile_only();
        }
        auto v = st.data().pop();
        if (!v.has_value()) {
            return v.error();
        }
        auto r = buf.emit(op::push, v.value(), pos);
        if (!r.has_value()) {
            return r.error();
        }
        return std::monostate{};
    }
    case control_builtin::bracket_open_: {
        st.set_state(0);
        return std::monostate{};
    }
    case control_builtin::bracket_close_: {
        st.set_state(1);
        return std::monostate{};
    }
    case control_builtin::immediate_: {
        return dict.mark_last_immediate();
    }
    case control_builtin::compile_comma_: {
        if (st.state() == 0) {
            return compile_only();
        }
        auto idx_v = st.data().pop();
        if (!idx_v.has_value()) {
            return idx_v.error();
        }
        int const idx = static_cast<int>(idx_v.value());
        if (idx < 0 || idx >= dict.size()) {
            return foundation::parse_error{pos,
                                           "COMPILE,: invalid execution token"};
        }
        return compile_entry(dict.entry_at(idx), buf, pos);
    }
    case control_builtin::postpone_: {
        if (st.state() == 0) {
            return compile_only();
        }
        auto name_scanned =
            parser::scan_word<MaxName>(st.source().cursor_at_in());
        if (!name_scanned.has_value()) {
            return name_scanned.error();
        }
        auto const &folded_name = name_scanned.value().value;
        if (folded_name.empty()) {
            return foundation::parse_error{pos,
                                           "expected a name after POSTPONE"};
        }
        st.source().set_in(name_scanned.value().rest.position().offset);
        std::string_view name_text{
            folded_name.begin(), static_cast<std::size_t>(folded_name.size())};
        auto const *target = dict.lookup(name_text);
        if (target == nullptr) {
            return foundation::parse_error{pos, "POSTPONE: unknown word"};
        }
        if (auto const *ctl =
                std::get_if<machine::control_word>(&target->binding)) {
            // Step F28: EXECUTE/CREATE/DOES> now have real compiled forms
            // (@ref compile_entry's own cases for them), so only the
            // structural, orig/dest-patching control words (IF/THEN/DO/
            // LOOP/...) still fall back to the whole-body-only alias --
            // see this function's own doc comment and DIV-0015's F28
            // addendum.
            bool const has_compiled_form =
                ctl->which == control_builtin::execute_ ||
                ctl->which == control_builtin::create_ ||
                ctl->which == control_builtin::does_;
            if (!has_compiled_form) {
                if (cctx.has_postponed_alias || buf.here() != cctx.entry) {
                    return foundation::parse_error{
                        pos, "POSTPONE of a control word must be the only "
                             "content of its definition"};
                }
                cctx.has_postponed_alias = true;
                cctx.postponed_target = ctl->which;
                return std::monostate{};
            }
        }
        return compile_entry(*target, buf, pos);
    }
    case control_builtin::tick_: {
        // `'` (step F28, D18): interpretation-only (Forth-2012) -- scan the
        // next name, look it up, resolve it to an execution token (@ref
        // resolve_execution_token), and push that token. Only ever reached
        // while interpreting (`'` is not immediate, so a compiling dispatch
        // calls @ref compile_entry instead, which has no case for it and
        // diagnoses -- `'`'s own compilation semantics are undefined in
        // Forth-2012, and this project chooses to diagnose rather than
        // guess).
        auto name_scanned =
            parser::scan_word<MaxName>(st.source().cursor_at_in());
        if (!name_scanned.has_value()) {
            return name_scanned.error();
        }
        auto const &folded_name = name_scanned.value().value;
        if (folded_name.empty()) {
            return foundation::parse_error{pos, "expected a name after '"};
        }
        st.source().set_in(name_scanned.value().rest.position().offset);
        std::string_view name_text{
            folded_name.begin(), static_cast<std::size_t>(folded_name.size())};
        auto const *target = dict.lookup(name_text);
        if (target == nullptr) {
            return foundation::parse_error{pos, "': unknown word"};
        }
        auto xt = resolve_execution_token(*target, buf, pos);
        if (!xt.has_value()) {
            return xt.error();
        }
        return st.data().push(static_cast<cell>(xt.value()));
    }
    case control_builtin::bracket_tick_: {
        // `[']` (step F28, D18): compile-only and immediate -- scans the
        // next name at the moment it is met (like `'`, but only valid while
        // compiling) and compiles a literal push of its execution token
        // (@ref machine::op::push_xt) into the definition being built,
        // rather than pushing it immediately.
        if (st.state() == 0) {
            return compile_only();
        }
        auto name_scanned =
            parser::scan_word<MaxName>(st.source().cursor_at_in());
        if (!name_scanned.has_value()) {
            return name_scanned.error();
        }
        auto const &folded_name = name_scanned.value().value;
        if (folded_name.empty()) {
            return foundation::parse_error{pos, "expected a name after [']"};
        }
        st.source().set_in(name_scanned.value().rest.position().offset);
        std::string_view name_text{
            folded_name.begin(), static_cast<std::size_t>(folded_name.size())};
        auto const *target = dict.lookup(name_text);
        if (target == nullptr) {
            return foundation::parse_error{pos, "[']: unknown word"};
        }
        auto xt = resolve_execution_token(*target, buf, pos);
        if (!xt.has_value()) {
            return xt.error();
        }
        auto r = buf.emit(op::push_xt, static_cast<cell>(xt.value()), pos);
        if (!r.has_value()) {
            return r.error();
        }
        return std::monostate{};
    }
    case control_builtin::execute_: {
        // `EXECUTE` (step F28, D18): interpreting-time behavior is call_word
        // at the popped execution token's own instruction index -- exactly
        // what @ref execute_entry's own compiled_colon_word case would do
        // for the word `'` originally resolved the token from, so
        // `' SQUARED EXECUTE` and `SQUARED` agree by construction (D14).
        // While compiling, this control word is not immediate, so
        // @ref compile_entry's own case (a bare @ref machine::op::execute)
        // handles it instead; this case is only ever reached interpreting.
        auto xt = st.data().pop();
        if (!xt.has_value()) {
            return xt.error();
        }
        return call_word(buf, st, static_cast<int>(xt.value()), vm_fuel, &dict);
    }
    case control_builtin::create_: {
        // `CREATE` (step F28, D10/D18): interpreting-time behavior is
        // @ref machine::create_here directly (scan a name, install a
        // variable_word at the current data-space HERE, no cells allotted).
        // While compiling, @ref compile_entry's own case emits @ref
        // machine::op::create_word instead, so the identical action happens
        // again, once per invocation, when the enclosing definition runs
        // (the `: CONSTANT2 CREATE , DOES> @ ;` pattern) -- @ref
        // machine::create_here is the one place that action is written
        // down, shared by both paths.
        return machine::create_here(dict, st, 0);
    }
    case control_builtin::does_: {
        // `DOES>` (step F28, D10/D18) only ever makes sense while defining a
        // new word (compiling); meeting it while interpreting is compile-
        // only misuse, same shape as every other structural control word.
        return compile_only();
    }
    case control_builtin::value_: {
        // `VALUE` (step F28, D18): pops the initial value, allots one cell,
        // stores it, and installs a @ref machine::value_word -- unlike
        // @ref machine::variable_word, executing the resulting name later
        // pushes the *contents* of that cell (@ref execute_entry's own
        // case), not its address; `TO` (below) is what later changes it.
        auto value = st.data().pop();
        if (!value.has_value()) {
            return value.error();
        }
        auto name_scanned =
            parser::scan_word<MaxName>(st.source().cursor_at_in());
        if (!name_scanned.has_value()) {
            return name_scanned.error();
        }
        auto const &folded_name = name_scanned.value().value;
        if (folded_name.empty()) {
            return foundation::parse_error{pos, "expected a name after VALUE"};
        }
        st.source().set_in(name_scanned.value().rest.position().offset);
        auto a = st.data_space().allot(1);
        if (!a.has_value()) {
            return a.error();
        }
        auto store_r = st.data_space().store(a.value(), value.value());
        if (!store_r.has_value()) {
            return store_r;
        }
        std::string_view name_text{
            folded_name.begin(), static_cast<std::size_t>(folded_name.size())};
        return dict.define_value(name_text,
                                 machine::value_word{.address = a.value()});
    }
    case control_builtin::to_: {
        // `TO` (step F28, D18): immediate, since its target's address must
        // be resolved once, at the moment `TO NAME` is met, regardless of
        // `STATE` -- interpreting stores the popped value directly;
        // compiling emits the equivalent push-address/store sequence into
        // the definition being built (the value `TO` will store is already
        // on the runtime stack by the time that sequence runs, pushed by
        // whatever source came before `TO NAME`, e.g. the `1` in
        // `1 TO COUNTER`).
        auto name_scanned =
            parser::scan_word<MaxName>(st.source().cursor_at_in());
        if (!name_scanned.has_value()) {
            return name_scanned.error();
        }
        auto const &folded_name = name_scanned.value().value;
        if (folded_name.empty()) {
            return foundation::parse_error{pos, "expected a name after TO"};
        }
        st.source().set_in(name_scanned.value().rest.position().offset);
        std::string_view name_text{
            folded_name.begin(), static_cast<std::size_t>(folded_name.size())};
        auto const *target = dict.lookup(name_text);
        if (target == nullptr) {
            return foundation::parse_error{pos, "TO: unknown word"};
        }
        auto const *vlw = std::get_if<machine::value_word>(&target->binding);
        if (vlw == nullptr) {
            return foundation::parse_error{pos, "TO: target is not a VALUE"};
        }
        if (st.state() == 0) {
            auto value = st.data().pop();
            if (!value.has_value()) {
                return value.error();
            }
            return st.data_space().store(vlw->address, value.value());
        }
        auto r1 = buf.emit(op::push, static_cast<cell>(vlw->address), pos);
        if (!r1.has_value()) {
            return r1.error();
        }
        auto r2 = buf.emit(op::prim,
                           static_cast<cell>(machine::primitive::store), pos);
        if (!r2.has_value()) {
            return r2.error();
        }
        return std::monostate{};
    }
    case control_builtin::defer_: {
        // `DEFER` (step F28, D18): allots one cell initialized to `-1`
        // ("never IS'd", @ref machine::defer_word's own doc comment) and
        // installs a @ref machine::defer_word.
        auto name_scanned =
            parser::scan_word<MaxName>(st.source().cursor_at_in());
        if (!name_scanned.has_value()) {
            return name_scanned.error();
        }
        auto const &folded_name = name_scanned.value().value;
        if (folded_name.empty()) {
            return foundation::parse_error{pos, "expected a name after DEFER"};
        }
        st.source().set_in(name_scanned.value().rest.position().offset);
        auto a = st.data_space().allot(1);
        if (!a.has_value()) {
            return a.error();
        }
        auto init_r = st.data_space().store(a.value(), cell{-1});
        if (!init_r.has_value()) {
            return init_r;
        }
        std::string_view name_text{
            folded_name.begin(), static_cast<std::size_t>(folded_name.size())};
        return dict.define_defer(name_text,
                                 machine::defer_word{.address = a.value()});
    }
    case control_builtin::is_: {
        // `IS` (step F28, D18): pops an execution token (produced by `'`)
        // and stores it into the named DEFERred word's own data-space cell
        // -- no dictionary mutation needed (contrast `DOES>`'s own @ref
        // dictionary::attach_does), since the target is just a value.
        auto xt = st.data().pop();
        if (!xt.has_value()) {
            return xt.error();
        }
        auto name_scanned =
            parser::scan_word<MaxName>(st.source().cursor_at_in());
        if (!name_scanned.has_value()) {
            return name_scanned.error();
        }
        auto const &folded_name = name_scanned.value().value;
        if (folded_name.empty()) {
            return foundation::parse_error{pos, "expected a name after IS"};
        }
        st.source().set_in(name_scanned.value().rest.position().offset);
        std::string_view name_text{
            folded_name.begin(), static_cast<std::size_t>(folded_name.size())};
        auto const *target = dict.lookup(name_text);
        if (target == nullptr) {
            return foundation::parse_error{pos, "IS: unknown word"};
        }
        auto const *dw = std::get_if<machine::defer_word>(&target->binding);
        if (dw == nullptr) {
            return foundation::parse_error{pos,
                                           "IS: target is not a DEFERred word"};
        }
        return st.data_space().store(dw->address, xt.value());
    }
    // f73e653b-0bfe-4315-b0e9-d7ff2b4c044c
    case control_builtin::paren_: {
        // `(` (step F29, D19): reexpressed from scanner-level comment
        // skipping (@ref parser::skip_forth_space, deleted this step) into
        // an ordinary immediate word over the same input stream -- exactly
        // D19's own "combinators below the word" move, generalized one more
        // level: the scanner no longer knows `(` exists at all (@ref
        // parser::scan_word treats it as an ordinary one-character word,
        // like any other name that happens to be followed by whitespace);
        // this dictionary word is what actually consumes the comment body,
        // via the same @ref parser::scan_delimited every other F29 parsing
        // word uses. Immediate and not compile-only: it must run the moment
        // it is met, in either state, exactly like the scanner-level
        // skipping it replaces did.
        auto scan =
            parser::scan_delimited(st.source().cursor_at_in(), ')', false);
        st.source().set_in(scan.rest.position().offset);
        if (!scan.found_delim) {
            return foundation::parse_error{pos, "unterminated ( comment"};
        }
        return std::monostate{};
    }
    case control_builtin::backslash_: {
        // `\` (step F29, D19): the line-comment counterpart to `paren_`
        // above, same reexpression. Running off the end of input with no
        // trailing newline is not an error (Forth-2012: `\` discards "the
        // remainder of the parse area"; a source that simply ends there is
        // an ordinary end of input, not an unterminated form the way a
        // missing `)` is for `(`).
        auto scan =
            parser::scan_delimited(st.source().cursor_at_in(), '\n', false);
        st.source().set_in(scan.rest.position().offset);
        return std::monostate{};
    }
    case control_builtin::s_quote_: {
        // `S"` (step F29, D19/D21): parses a `"`-delimited string, copies it
        // into freshly allotted data-space cells (one cell per character,
        // D21 -- the same convention `WORD` uses), and leaves its address
        // and length. Works both interpreting (push immediately) and
        // compiling (compile the equivalent two literal pushes, since the
        // string's own data-space address is already fixed by the time this
        // runs, exactly like `LITERAL`'s own push, just two of them) --
        // Forth-2012 defines both, unlike `ABORT"`/`[CHAR]` below.
        auto scan =
            parser::scan_delimited(st.source().cursor_at_in(), '"', false);
        st.source().set_in(scan.rest.position().offset);
        if (!scan.found_delim) {
            return foundation::parse_error{pos, "unterminated S\" string"};
        }
        auto a = st.data_space().allot(static_cast<int>(scan.text.size()));
        if (!a.has_value()) {
            return a.error();
        }
        for (std::size_t i = 0; i < scan.text.size(); ++i) {
            auto sr = st.data_space().store(
                machine::addr{static_cast<cell>(a.value()) +
                              static_cast<cell>(i)},
                static_cast<cell>(scan.text[i]));
            if (!sr.has_value()) {
                return sr;
            }
        }
        if (st.state() == 0) {
            auto r1 = st.data().push(static_cast<cell>(a.value()));
            if (!r1.has_value()) {
                return r1;
            }
            return st.data().push(static_cast<cell>(scan.text.size()));
        }
        auto r1 = buf.emit(op::push, static_cast<cell>(a.value()), pos);
        if (!r1.has_value()) {
            return r1.error();
        }
        auto r2 = buf.emit(op::push, static_cast<cell>(scan.text.size()), pos);
        if (!r2.has_value()) {
            return r2.error();
        }
        return std::monostate{};
    }
    case control_builtin::dot_quote_: {
        // `."` (step F29, D19/D21): like `s_quote_` above, but the string is
        // printed rather than left on the stack -- interpreting prints it
        // immediately (no data-space storage needed at all, since nothing
        // has to survive past this one call); compiling stores it (it must
        // survive until this definition's own later runtime call) and
        // compiles a push-address/push-length/`TYPE` sequence instead of
        // `s_quote_`'s own bare pushes.
        auto scan =
            parser::scan_delimited(st.source().cursor_at_in(), '"', false);
        st.source().set_in(scan.rest.position().offset);
        if (!scan.found_delim) {
            return foundation::parse_error{pos, "unterminated .\" string"};
        }
        if (st.state() == 0) {
            for (char c : scan.text) {
                if (auto r = st.emit_char(c); !r.has_value()) {
                    return r;
                }
            }
            return std::monostate{};
        }
        auto a = st.data_space().allot(static_cast<int>(scan.text.size()));
        if (!a.has_value()) {
            return a.error();
        }
        for (std::size_t i = 0; i < scan.text.size(); ++i) {
            auto sr = st.data_space().store(
                machine::addr{static_cast<cell>(a.value()) +
                              static_cast<cell>(i)},
                static_cast<cell>(scan.text[i]));
            if (!sr.has_value()) {
                return sr;
            }
        }
        auto r1 = buf.emit(op::push, static_cast<cell>(a.value()), pos);
        if (!r1.has_value()) {
            return r1.error();
        }
        auto r2 = buf.emit(op::push, static_cast<cell>(scan.text.size()), pos);
        if (!r2.has_value()) {
            return r2.error();
        }
        auto r3 = buf.emit(op::prim,
                           static_cast<cell>(machine::primitive::type_), pos);
        if (!r3.has_value()) {
            return r3.error();
        }
        return std::monostate{};
    }
    case control_builtin::char_bracket_: {
        // `[CHAR]` (step F29, D19): compile-only `CHAR` -- scans the next
        // blank-delimited name at the moment it is met (like `CHAR`'s own
        // primitive, @ref parser::scan_bare_name) and compiles a literal
        // push of its first character's own code, rather than leaving that
        // work for runtime the way the ordinary, non-immediate `CHAR`
        // primitive does.
        if (st.state() == 0) {
            return compile_only();
        }
        auto scanned = parser::scan_bare_name(st.source().cursor_at_in());
        if (!scanned.has_value()) {
            return scanned.error();
        }
        st.source().set_in(scanned.value().rest.position().offset);
        auto r = buf.emit(op::push,
                          static_cast<cell>(static_cast<unsigned char>(
                              scanned.value().value.front())),
                          pos);
        if (!r.has_value()) {
            return r.error();
        }
        return std::monostate{};
    }
    case control_builtin::abort_quote_: {
        // `ABORT"` (step F29, D19/D21): compile-only, per Forth-2012
        // (interpretation semantics are undefined). Parses the message text
        // exactly like `s_quote_` above, stores it, and compiles a
        // push-address/push-length/`abort_quote` sequence -- the runtime
        // primitive (`machine/forth_state.hpp`) that prints the message and
        // fails with a distinguished condition. Step F31's own VM dispatch
        // (`vm.hpp`'s `run_from`, `op::prim` case) always routes that
        // condition through `THROW -2` (DIV-0017's revisit, DIV-0018): a
        // `CATCH` around a caller of this word catches it; with none active,
        // it is an uncaught diagnosed `THROW` carrying `-2`.
        if (st.state() == 0) {
            return compile_only();
        }
        auto scan =
            parser::scan_delimited(st.source().cursor_at_in(), '"', false);
        st.source().set_in(scan.rest.position().offset);
        if (!scan.found_delim) {
            return foundation::parse_error{pos, "unterminated ABORT\" string"};
        }
        auto a = st.data_space().allot(static_cast<int>(scan.text.size()));
        if (!a.has_value()) {
            return a.error();
        }
        for (std::size_t i = 0; i < scan.text.size(); ++i) {
            auto sr = st.data_space().store(
                machine::addr{static_cast<cell>(a.value()) +
                              static_cast<cell>(i)},
                static_cast<cell>(scan.text[i]));
            if (!sr.has_value()) {
                return sr;
            }
        }
        auto r1 = buf.emit(op::push, static_cast<cell>(a.value()), pos);
        if (!r1.has_value()) {
            return r1.error();
        }
        auto r2 = buf.emit(op::push, static_cast<cell>(scan.text.size()), pos);
        if (!r2.has_value()) {
            return r2.error();
        }
        auto r3 = buf.emit(
            op::prim, static_cast<cell>(machine::primitive::abort_quote), pos);
        if (!r3.has_value()) {
            return r3.error();
        }
        return std::monostate{};
    }
        // f73e653b-0bfe-4315-b0e9-d7ff2b4c044c end
    // 8f2d7a4c-6b1e-4c9a-8d3f-2b7e5c9a1f6d
    case control_builtin::catch_: {
        // `CATCH` (step F31, D11/D18): interpreting-time behavior appends
        // the identical two-instruction shape @ref compile_entry's own
        // `catch_` case compiles (`op::catch_mark` + `prim catch_ok`) to
        // @p buf at its own current position, guarded by a leading
        // unconditional branch around it -- the same discipline @ref
        // resolve_execution_token uses, since @p buf may be positioned
        // *inside* a still-open colon definition's own body (`CATCH` used
        // inside a `[ ... ]` bracket while compiling something else) -- then
        // runs it via @ref machine::run_from, starting at the `catch_mark`
        // instruction itself so the *same* iterative VM loop that runs a
        // compiled `CATCH` runs this one too (D14: one semantics either
        // way). The execution token `CATCH` pops is left on @p st's own data
        // stack by whatever ran just before this word (`'`/`[']` et al.);
        // this case never touches it directly. Resume ip is @p buf's own
        // `halt_pad` (0), reached two different ways: a caught `THROW`
        // jumps there directly (perform_throw); normal completion instead
        // *falls into* the third, explicit `branch halt_pad` below (`prim
        // catch_ok`'s own success path is an ordinary `++ip`, landing on
        // whatever instruction physically follows it -- unlike the compiled
        // case, where that is real code belonging to the rest of the
        // definition, here nothing else was ever emitted, so this branch is
        // what makes falling through land somewhere valid instead of
        // walking off the end of @p buf's own code array). Either way
        // @ref machine::run_from then fetches `halt_pad`'s own @ref
        // machine::op::halt and returns cleanly.
        auto skip = buf.emit(op::branch, cell{-1}, pos);
        if (!skip.has_value()) {
            return skip.error();
        }
        int const mark_idx = buf.here();
        auto r1 =
            buf.emit(op::catch_mark, static_cast<cell>(buf.halt_pad()), pos);
        if (!r1.has_value()) {
            return r1.error();
        }
        auto r2 = buf.emit(
            op::prim, static_cast<cell>(machine::primitive::catch_ok), pos);
        if (!r2.has_value()) {
            return r2.error();
        }
        auto r3 = buf.emit(op::branch, static_cast<cell>(buf.halt_pad()), pos);
        if (!r3.has_value()) {
            return r3.error();
        }
        auto p = patch(skip.value(), static_cast<cell>(buf.here()));
        if (!p.has_value()) {
            return p;
        }
        return machine::run_from(buf.program(), st, mark_idx, vm_fuel, &dict);
    }
    case control_builtin::throw_: {
        // `THROW` (step F31, D11): n is already on @p st's own data stack.
        // Same guarded-append-and-run shape as `catch_` above, and the same
        // reason for the trailing `branch halt_pad`: THROW's own `n == 0`
        // no-op case (Forth-2012) falls through via an ordinary `++ip`
        // exactly like `catch_ok`'s own success path does, so it needs the
        // identical landing instruction to avoid walking off the end of
        // @p buf's own code array. Shared with `abort_` below via
        // @c run_throw.
        auto run_throw = [&]() -> machine::status {
            auto skip = buf.emit(op::branch, cell{-1}, pos);
            if (!skip.has_value()) {
                return skip.error();
            }
            int const throw_idx = buf.here();
            auto r = buf.emit(op::throw_op, cell{0}, pos);
            if (!r.has_value()) {
                return r.error();
            }
            auto r2 =
                buf.emit(op::branch, static_cast<cell>(buf.halt_pad()), pos);
            if (!r2.has_value()) {
                return r2.error();
            }
            auto p = patch(skip.value(), static_cast<cell>(buf.here()));
            if (!p.has_value()) {
                return p;
            }
            return machine::run_from(buf.program(), st, throw_idx, vm_fuel,
                                     &dict);
        };
        return run_throw();
    }
    case control_builtin::abort_: {
        // `ABORT` (step F31, D11): `-1 THROW`, per Forth-2012 -- push -1,
        // then the identical guarded append-and-run `op::throw_op` shape
        // `throw_` above uses (duplicated rather than factored out to a
        // shared lambda across cases: each is a short, self-contained body,
        // and the two cases differ only in whether a value must be pushed
        // first).
        auto push_r = st.data().push(cell{-1});
        if (!push_r.has_value()) {
            return push_r;
        }
        auto skip = buf.emit(op::branch, cell{-1}, pos);
        if (!skip.has_value()) {
            return skip.error();
        }
        int const throw_idx = buf.here();
        auto r = buf.emit(op::throw_op, cell{0}, pos);
        if (!r.has_value()) {
            return r.error();
        }
        // -1 is always nonzero, so op::throw_op always calls perform_throw
        // here (never THROW's own n == 0 fallthrough) -- this landing
        // instruction is precautionary symmetry with throw_'s own case, not
        // a path this specific call can actually reach.
        auto r2 = buf.emit(op::branch, static_cast<cell>(buf.halt_pad()), pos);
        if (!r2.has_value()) {
            return r2.error();
        }
        auto p = patch(skip.value(), static_cast<cell>(buf.here()));
        if (!p.has_value()) {
            return p;
        }
        return machine::run_from(buf.program(), st, throw_idx, vm_fuel, &dict);
    }
        // 8f2d7a4c-6b1e-4c9a-8d3f-2b7e5c9a1f6d end
    }
    return foundation::parse_error{pos, "unknown control word"};
}
// d1a4f7b2-6c8e-4a3d-9b5f-2e7c4a9d1b6e end

/// Runs @p entry's own execution semantics against @p st -- the action
/// every dictionary entry gets when the text interpreter meets it while
/// interpreting (D13), and also what a compiling loop gives an *immediate*
/// entry instead of compiling it (D13's "execute ... when immediate"): a
/// primitive runs via @ref machine::apply_primitive, a @ref
/// machine::compiled_colon_word runs via @ref call_word (D14: interpreting a
/// defined word runs the same code a compiled call would; @p dict is passed
/// through so a body that reaches `CREATE`/`DOES>`, step F28, can still reach
/// the dictionary), a @ref machine::variable_word pushes its own address and
/// then, if @ref machine::variable_word::does_entry is set (`DOES>`), calls
/// into that code too, a @ref machine::constant_word pushes its own value, a
/// @ref machine::value_word pushes the *current* value at its own address
/// (step F28's own `VALUE`), a @ref machine::defer_word (step F28's own
/// `DEFER`) fetches its own current target and calls it, diagnosing the `-1`
/// "never `IS`sed" sentinel with a specific message rather than the generic
/// out-of-range instruction pointer a compiled reference to the same word
/// would get (@ref compile_entry's own case), and a @ref machine::
/// control_word dispatches to @ref apply_control_word.
// c2d6b26d-5166-4b73-aadd-bf66be9d933c
template <int MaxDepth, int MaxRDepth, int MaxData, int MaxOut, int MaxWords,
          int MaxCode, int MaxBufWords, int MaxName>
[[nodiscard]] constexpr auto
execute_entry(machine::dictionary_entry<MaxName> const &entry,
              machine::forth_state<MaxDepth, MaxRDepth, MaxData, MaxOut> &st,
              machine::dictionary<MaxWords, MaxName> &dict,
              compile_buffer<MaxCode, MaxBufWords> &buf,
              compiling_context<MaxName> &cctx, foundation::source_pos pos,
              int vm_fuel) -> machine::status {
    if (auto const *op = std::get_if<machine::primitive>(&entry.binding)) {
        return machine::apply_primitive(*op, st);
    }
    if (auto const *cw =
            std::get_if<machine::compiled_colon_word>(&entry.binding)) {
        return call_word(buf, st, cw->entry_point, vm_fuel, &dict);
    }
    if (auto const *vw = std::get_if<machine::variable_word>(&entry.binding)) {
        auto r = st.data().push(static_cast<machine::cell>(vw->address));
        if (!r.has_value()) {
            return r;
        }
        if (vw->does_entry >= 0) {
            return call_word(buf, st, vw->does_entry, vm_fuel, &dict);
        }
        return std::monostate{};
    }
    if (auto const *cnw = std::get_if<machine::constant_word>(&entry.binding)) {
        return st.data().push(cnw->value);
    }
    if (auto const *vlw = std::get_if<machine::value_word>(&entry.binding)) {
        auto v = st.data_space().fetch(vlw->address);
        if (!v.has_value()) {
            return v.error();
        }
        return st.data().push(v.value());
    }
    if (auto const *dw = std::get_if<machine::defer_word>(&entry.binding)) {
        auto target = st.data_space().fetch(dw->address);
        if (!target.has_value()) {
            return target.error();
        }
        if (target.value() < 0) {
            return foundation::parse_error{
                pos, "deferred word has no action (use IS)"};
        }
        return call_word(buf, st, static_cast<int>(target.value()), vm_fuel,
                         &dict);
    }
    if (auto const *ctl = std::get_if<machine::control_word>(&entry.binding)) {
        return apply_control_word(ctl->which, st, dict, buf, cctx, pos,
                                  vm_fuel);
    }
    return foundation::parse_error{
        pos, "word is not executable yet (F25: primitives and colon "
             "words only)"};
}
// c2d6b26d-5166-4b73-aadd-bf66be9d933c end

// aa1d6f83-9b3c-4e2a-8d5f-3c7b1e9a4f62
/// The Forth-2012 section 3.4 outer text interpreter (D13): scans one word
/// at a time from @p st's own @ref input_source and, per @p st's own
/// `STATE`, either interprets it or compiles it into @p buf, repeating
/// until @p st's source is exhausted or the first diagnosed error, whichever
/// comes first.
///
/// **Interpreting** (`STATE == 0`): a dictionary hit runs via @ref
/// execute_entry (D13's "execute ... when interpreting"); a miss is
/// classified as a number per @p st's own `BASE` and pushed, or diagnosed as
/// an unknown word. `;`, `EXIT`, and `RECURSE` are compile-only (D13,
/// Forth-2012): diagnosed here rather than acted on -- as are the step F27/
/// F28 control words found via @ref execute_entry's own dispatch to @ref
/// apply_control_word, all but `[`/`]`/`IMMEDIATE`/`'`/`EXECUTE`/`CREATE`/
/// `VALUE`/`DEFER`/`IS` (every one of those six is non-immediate, but has
/// ordinary execution semantics that only ever needs to fire while
/// interpreting, so none is compile-only either). `VARIABLE` and `CONSTANT`
/// (F26) are ordinary defining words, still recognized here by the same
/// direct-name technique as `:`: they scan a following name and install a
/// @ref machine::variable_word (allotting one cell) or a @ref
/// machine::constant_word (popping the value `CONSTANT` binds) into @p dict.
/// `CREATE` moved off this direct-name list at step F28 (D18): it is now an
/// ordinary dictionary entry (@ref machine::control_builtin::create_),
/// found and dispatched exactly like any other word, because it must also
/// be reachable from *inside* another word's own compiled body (see @ref
/// apply_control_word's own `create_`/`does_` cases).
///
/// **Compiling** (`STATE == 1`, entered by `:`): a dictionary hit that is
/// *not* `IMMEDIATE` (step F27, D13's "... or when immediate, else compile")
/// is appended to @p buf via @ref compile_entry rather than run; one that
/// *is* immediate runs right away via @ref execute_entry instead, exactly as
/// interpreting would -- this is how every control word (`IF`/`THEN`/
/// `DO`/`LOOP`/... , D17) does its own compiling work, and how a
/// user-defined immediate word (`: ENDIF POSTPONE THEN ; IMMEDIATE`) does
/// too. A miss is classified as a number and emitted as @ref
/// machine::op::push. `;` emits @ref machine::op::ret, installs the
/// finished @ref machine::compiled_colon_word into @p dict, and clears
/// `STATE` -- unless the definition being closed is a `POSTPONE`-of-a-
/// control-word alias (@ref compiling_context::has_postponed_alias), in
/// which case it installs a @ref machine::control_word instead (@ref
/// apply_control_word's own `postpone_` case has the full rationale); either
/// way, `;` first diagnoses an unresolved `DO` (@ref compiling_context::
/// loop_depth still nonzero -- @ref compiling_context's own doc comment
/// explains why an unresolved `IF`/`BEGIN` is not caught the same way).
/// `EXIT` emits @ref
/// machine::op::ret without closing the definition. `RECURSE` emits a
/// self-@ref machine::op::call to the entry point @ref scan_colon_header's
/// own caller (`:` itself, below) recorded *before* the body was compiled --
/// F14's discipline, carried forward unchanged, is what makes this resolve
/// without any back-patching. `VARIABLE`/`CONSTANT` are still not
/// recognized while compiling (nothing requires defining-inside-a-colon-
/// definition semantics for either): a dictionary miss on one of those names
/// inside `:` ... `;` falls through to the same "unknown word" diagnosis any
/// other undefined name would get. `CREATE` *is* now reachable while
/// compiling (step F28): a compiling dispatch hit on it is not immediate, so
/// @ref compile_entry appends @ref machine::op::create_word, and `DOES>`
/// likewise appends @ref machine::op::does_enter -- both are how a defining
/// word's own body (`: CONSTANT2 CREATE , DOES> @ ;`) reaches the dictionary
/// again each time it runs, not only once, at its own definition time.
///
/// `\` line comments and `( ... )` comments are ordinary immediate
/// dictionary words to this loop since step F29 (D19: `paren_`/
/// `backslash_`, @ref apply_control_word) -- found, dispatched, and
/// executed exactly like any other token, rather than skipped by the
/// scanner before a token is even identified -- in every position except
/// immediately after a `:` definition's own name, where @ref
/// scan_colon_header instead captures a `( ... )` comment as the declared
/// effect (D20: stored, unverified until F30) before this loop's own token
/// scan ever runs.
///
/// Every stack/data-space/code-space misuse a primitive's own @ref
/// machine::apply_primitive, or @ref compile_buffer::emit, can diagnose is
/// returned here unchanged -- the same @ref foundation::parse_error value
/// either would have produced directly, not a re-diagnosed or repositioned
/// one.
///
/// @tparam MaxDepth    @p st's data stack capacity, in cells.
/// @tparam MaxRDepth   @p st's return stack capacity, in cells.
/// @tparam MaxData     @p st's data space capacity, in cells.
/// @tparam MaxOut      @p st's output buffer capacity, in characters.
/// @tparam MaxWords    @p dict's capacity, in entries -- independent of
///                     @p buf's own @c MaxBufWords (@ref compile_buffer's
///                     word-table capacity is unused by this step: every
///                     @ref machine::compiled_colon_word carries its own
///                     entry point directly, see compilebuf.hpp), so a small
///                     custom @p dict may pair with a generously sized
///                     @p buf, or vice versa, without their capacities
///                     having to match.
/// @tparam MaxCode     @p buf's instruction-array capacity.
/// @tparam MaxBufWords @p buf's own (unused by this step) word-table
///                     capacity.
/// @tparam MaxName     Maximum word-name/token length, in characters; shared
///                     with @p dict, since a token longer than a dictionary
///                     name could never match one anyway.
/// @param  st      The interpreter state to run against; mutated in place --
///                 its stacks, output buffer, `STATE`, and `>IN` all advance
///                 as the loop runs, even on the run that ends in a
///                 diagnosed error.
/// @param  dict    The dictionary to resolve words against, and to install
///                 every finished colon definition into.
/// @param  buf     The code space every `:` ... `;` pair compiles into.
/// @param  fuel    The interpreter loop's own step budget (@ref
///                 consume_interp_fuel): decremented once per token
///                 processed; exhaustion is a diagnosed error, never a hang.
/// @param  vm_fuel The VM's own step budget (D22, a distinct budget from
///                 @p fuel), passed to @ref call_word each time interpreting
///                 a defined word actually runs one.
template <int MaxDepth, int MaxRDepth, int MaxData, int MaxOut, int MaxWords,
          int MaxCode, int MaxBufWords, int MaxName = 32>
constexpr auto
interpret(machine::forth_state<MaxDepth, MaxRDepth, MaxData, MaxOut> &st,
          machine::dictionary<MaxWords, MaxName> &dict,
          compile_buffer<MaxCode, MaxBufWords> &buf, int fuel = 100000,
          int vm_fuel = 100000) -> machine::status {
    // Bookkeeping for the definition currently being compiled, if any --
    // local to this call frame, deliberately not part of forth_state itself
    // even after DIV-0012's own fold (this step) folds SOURCE/BASE/STATE
    // into it: compiling_context is per-*call*, per-definition state, not
    // per-machine state D13 ever calls for forth_state to carry. Meaningful
    // only while st.state() != 0; see compiling_context's own doc comment
    // for why step F27 grew this from four loose locals into one value.
    compiling_context<MaxName> cctx{};

    for (;;) {
        auto pre = st.source().cursor_at_in();
        // Plain ASCII whitespace only (step F29, D19): `(` and `\` are
        // ordinary dictionary words now, found and dispatched below like
        // any other token, not skipped here -- a source that ends in a
        // trailing comment still terminates cleanly, just one loop
        // iteration later, once that comment word's own action has run.
        auto token_start = parser::skip_intertoken_space(pre);
        if (token_start.empty()) {
            if (st.state() != 0) {
                return foundation::parse_error{token_start.position(),
                                               "unterminated colon definition"};
            }
            // Nothing left but whitespace: a clean end of source, not an
            // error.
            return std::monostate{};
        }

        auto budget = consume_interp_fuel(fuel, token_start.position());
        if (!budget.has_value()) {
            return budget;
        }

        auto scanned = parser::scan_word<MaxName>(pre);
        if (!scanned.has_value()) {
            // Unreachable in practice: token_start non-empty means at least
            // one is_word_char run exists at that position, so scan_word's
            // own some<> cannot fail here. Diagnosed defensively, per D7,
            // rather than assumed away.
            return scanned.error();
        }

        auto const &token = scanned.value().value;
        st.source().set_in(scanned.value().rest.position().offset);

        std::string_view text{token.begin(),
                              static_cast<std::size_t>(token.size())};

        if (st.state() == 0) {
            // -- Interpreting -----------------------------------------

            // 5d9a3f7c-2e6b-4a81-9c4d-7f1b8e3a6d52
            if (text == ":") {
                auto header =
                    scan_colon_header<MaxName>(st.source().cursor_at_in());
                if (!header.has_value()) {
                    return header.error();
                }
                cctx = compiling_context<MaxName>{};
                cctx.name = header.value().value.name;
                cctx.effect = header.value().value.effect;
                cctx.has_effect = header.value().value.has_effect;
                st.source().set_in(header.value().rest.position().offset);
                cctx.entry = buf.here();
                st.set_state(1);
                continue;
            }
            // 5d9a3f7c-2e6b-4a81-9c4d-7f1b8e3a6d52 end
            if (text == ";") {
                return foundation::parse_error{
                    token_start.position(),
                    "\";\" is compile-only, used while interpreting"};
            }
            if (text == "EXIT") {
                return foundation::parse_error{
                    token_start.position(),
                    "EXIT is compile-only, used while interpreting"};
            }
            if (text == "RECURSE") {
                return foundation::parse_error{
                    token_start.position(),
                    "RECURSE is compile-only, used while interpreting"};
            }
            if (text == "VARIABLE") {
                // F26 ("the cut"): VARIABLE was an R1-elaborator syntax
                // production (reader::syn_variable); D13 has no reader
                // phase, so it is an ordinary defining word the interpreter
                // recognizes by direct name comparison, exactly like `:`
                // above. `CREATE` used to share this branch (it allotted 0
                // cells where VARIABLE allots 1) but moved onto the ordinary
                // dictionary dispatch at step F28 (D18) -- see this
                // function's own top comment -- via @ref machine::
                // create_here, the same shared action VARIABLE reuses here
                // with @c cells_to_allot 1 (its error message still says
                // "after CREATE", not "after VARIABLE"/"CREATE": sharing one
                // action outweighs a per-caller-customized message for a
                // case this narrow).
                auto def_r = machine::create_here(dict, st, 1);
                if (!def_r.has_value()) {
                    return def_r;
                }
                continue;
            }
            // eabc2d13-9f50-4eb9-98e8-7d69cca95046
            if (text == "CONSTANT") {
                // Forth-2012 CONSTANT semantics directly, not R1's
                // elaborate_constant (which required a syntactically
                // preceding literal token and constant-folded it): pop
                // whatever the data stack holds at this point, whatever
                // pushed it there -- `7 CONSTANT LUCKY` and
                // `3 4 + CONSTANT SEVEN` are equally valid under an
                // interpreter that has already executed the "7" (or "3 4 +")
                // before meeting CONSTANT.
                auto value = st.data().pop();
                if (!value.has_value()) {
                    return value.error();
                }
                auto name_scanned =
                    parser::scan_word<MaxName>(st.source().cursor_at_in());
                if (!name_scanned.has_value()) {
                    return name_scanned.error();
                }
                auto const &folded_name = name_scanned.value().value;
                if (folded_name.empty()) {
                    return foundation::parse_error{
                        token_start.position(),
                        "expected a name after CONSTANT"};
                }
                st.source().set_in(name_scanned.value().rest.position().offset);
                std::string_view name_text{
                    folded_name.begin(),
                    static_cast<std::size_t>(folded_name.size())};
                auto def_r = dict.define_constant(
                    name_text, machine::constant_word{value.value()});
                if (!def_r.has_value()) {
                    return def_r;
                }
                continue;
            }
            // eabc2d13-9f50-4eb9-98e8-7d69cca95046 end

            // D13: "if found, execute" -- every binding kind, including a
            // step F27 control word, runs via execute_entry while
            // interpreting (@ref execute_entry's own doc comment).
            auto const *entry = dict.lookup(text);
            if (entry != nullptr) {
                auto r = execute_entry(*entry, st, dict, buf, cctx,
                                       token_start.position(), vm_fuel);
                if (!r.has_value()) {
                    return r;
                }
                continue;
            }

            if (is_number_token_in_base(text, st.base())) {
                auto r = st.data().push(token_to_cell_in_base(text, st.base()));
                if (!r.has_value()) {
                    return r;
                }
                continue;
            }

            return foundation::parse_error{token_start.position(),
                                           "unknown word"};
        }

        // -- Compiling -------------------------------------------------

        if (text == ":") {
            return foundation::parse_error{token_start.position(),
                                           "nested : is not allowed"};
        }
        // 8b4e1c7a-3f9d-4a62-8e5b-1d7c4a9f6e38
        if (text == ";") {
            if (cctx.has_postponed_alias) {
                // `: ENDIF POSTPONE THEN ; IMMEDIATE`-style alias (@ref
                // apply_control_word's own `postpone_` case): nothing was
                // ever emitted for this definition, so it becomes a plain
                // machine::control_word entry -- the same tag POSTPONE's own
                // target carried -- rather than a compiled_colon_word.
                // buf.here() having moved past cctx.entry means ordinary
                // code was compiled too (either before the POSTPONE, which
                // apply_control_word already rejects, or after it): diagnose
                // rather than silently drop that code.
                if (buf.here() != cctx.entry) {
                    return foundation::parse_error{
                        token_start.position(),
                        "code after a postponed control word is not "
                        "supported"};
                }
                std::string_view name_text{
                    cctx.name.begin(),
                    static_cast<std::size_t>(cctx.name.size())};
                auto def_r = dict.define_control(
                    name_text, machine::control_word{cctx.postponed_target});
                if (!def_r.has_value()) {
                    return def_r;
                }
                cctx = compiling_context<MaxName>{};
                st.set_state(0);
                continue;
            }
            if (cctx.loop_depth != 0) {
                // A DO was left without its matching LOOP/+LOOP -- see
                // compiling_context's own doc comment for why this checks
                // loop_depth specifically rather than comparing data-stack
                // depth the way an early draft of this step did.
                return foundation::parse_error{
                    token_start.position(),
                    "unbalanced control-flow structure (unresolved DO)"};
            }
            auto ret_r = buf.emit(machine::op::ret, machine::cell{0},
                                  token_start.position());
            if (!ret_r.has_value()) {
                return ret_r.error();
            }
            // Step F30 (D20): the effect lint runs here, over the
            // definition's own just-finished instruction range -- ending
            // DIV-0014's own suspension window (a declared effect was
            // captured at scan_colon_header time but never verified between
            // F26's own cut and this step). See effect_lint.hpp's own
            // check_definition_effect for the full design.
            auto lint = check_definition_effect(
                buf.program(), dict, cctx.entry, buf.here(), st.source().text(),
                cctx.effect, cctx.has_effect, token_start.position());
            if (!lint.has_value()) {
                return lint.error();
            }
            // DIV-0008's own second cut left these two fields at `-1`
            // ("not computed"); this step is the first to have anything to
            // fill them with. Given D13's own shared, ever-growing code
            // space (many colon words compiled into one buf over a whole
            // session, not F14's original "one compiled_program is one
            // whole executable program"), a single peak-depth number for
            // the *whole* compiled_program cannot mean "this exact program
            // requires this much" the way it did before F26 -- so this
            // step tracks a running maximum across every definition closed
            // so far whose own peak was computable at all, a documented,
            // coarser interpretation (DIV-0019) that still answers the
            // field's own question ("how big must a caller size its own
            // forth_state to safely run anything defined in this session
            // so far") without pretending to a precision the architecture
            // no longer supports.
            if (lint.value().peak_depth >= 0 &&
                lint.value().peak_depth > buf.program().required_stack_depth) {
                buf.program().required_stack_depth = lint.value().peak_depth;
            }
            if (lint.value().peak_return_depth >= 0 &&
                lint.value().peak_return_depth >
                    buf.program().required_return_depth) {
                buf.program().required_return_depth =
                    lint.value().peak_return_depth;
            }
            std::string_view name_text{
                cctx.name.begin(), static_cast<std::size_t>(cctx.name.size())};
            auto def_r = dict.define_compiled_colon(
                name_text, machine::compiled_colon_word{
                               .entry_point = cctx.entry,
                               .effect_span = cctx.effect,
                               .has_effect = cctx.has_effect,
                               .effect_known = lint.value().net.known,
                               .effect_inputs = lint.value().net.inputs,
                               .effect_outputs = lint.value().net.outputs,
                               .peak_depth = lint.value().peak_depth});
            if (!def_r.has_value()) {
                return def_r;
            }
            cctx = compiling_context<MaxName>{};
            st.set_state(0);
            continue;
        }
        if (text == "EXIT") {
            auto r = buf.emit(machine::op::ret, machine::cell{0},
                              token_start.position());
            if (!r.has_value()) {
                return r.error();
            }
            continue;
        }
        if (text == "RECURSE") {
            auto r = buf.emit(machine::op::call,
                              static_cast<machine::cell>(cctx.entry),
                              token_start.position());
            if (!r.has_value()) {
                return r.error();
            }
            continue;
        }
        // 8b4e1c7a-3f9d-4a62-8e5b-1d7c4a9f6e38 end

        // D13: "execute ... when immediate, else compile" -- an immediate
        // entry (every step F27 control word but `]`/`IMMEDIATE`, or a
        // user-defined immediate colon word) runs now via execute_entry;
        // anything else is appended to the definition being compiled via
        // compile_entry (the same form POSTPONE/COMPILE, reuse).
        auto const *entry = dict.lookup(text);
        if (entry != nullptr) {
            if (entry->immediate) {
                auto r = execute_entry(*entry, st, dict, buf, cctx,
                                       token_start.position(), vm_fuel);
                if (!r.has_value()) {
                    return r;
                }
                continue;
            }
            auto r = compile_entry(*entry, buf, token_start.position());
            if (!r.has_value()) {
                return r;
            }
            continue;
        }

        if (is_number_token_in_base(text, st.base())) {
            auto r = buf.emit(machine::op::push,
                              token_to_cell_in_base(text, st.base()),
                              token_start.position());
            if (!r.has_value()) {
                return r.error();
            }
            continue;
        }

        return foundation::parse_error{token_start.position(), "unknown word"};
    }
}
// aa1d6f83-9b3c-4e2a-8d5f-3c7b1e9a4f62 end

} // namespace smd::forth::interpreter

#endif
