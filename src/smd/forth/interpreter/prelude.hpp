// src/smd/forth/interpreter/prelude.hpp                              -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#ifndef SRC_SMD_FORTH_INTERPRETER_PRELUDE_HPP
#define SRC_SMD_FORTH_INTERPRETER_PRELUDE_HPP

#include <smd/forth/foundation/parse_error.hpp>
#include <smd/forth/foundation/result.hpp>
#include <smd/forth/foundation/source_pos.hpp>
#include <smd/forth/interpreter/session.hpp>

#include <array>
#include <cstddef>
#include <string_view>

namespace smd::forth::interpreter {

// Step F35 (docs/forth-plan-2.md): the bootstrap prelude, the demonstration
// that Forth metaprogramming is just programming. Everything it needs
// already existed by F29: the text interpreter, `STATE`, immediate words,
// `POSTPONE`, `IMMEDIATE`, and (for the derived arithmetic/stack words below)
// nothing beyond ordinary `:` ... `;` colon compilation. This header is
// itself Forth source text `interpreter::interpret` compiles, not C++ that
// imitates what these words do -- exactly the discipline F32's own
// `conformance/ttester_corpus.hpp` already established for the Hayes
// ttester. DIV-0027 records the design in full (which words moved, which
// stayed in `machine::default_dictionary`, and why, and the DIV-0015
// boundary this step's own control-flow words land on).
//
// `?DUP DUP IF DUP THEN` reuses `IF`/`THEN` (still C++-installed control
// words) as ordinary immediate dictionary entries -- no special-casing was
// needed, D13's own "execute ... when immediate" rule already covers a
// user-defined word's own body doing this. `WHEN`/`OTHERWISE`/`ENDIF` are
// each a definition whose *entire* body is one `POSTPONE` of a structural
// control word (`IF`/`ELSE`/`THEN` respectively) -- DIV-0015's own F28
// addendum's one still-open door: such a definition installs as a plain
// `machine::control_word` alias for its target rather than a
// `machine::compiled_colon_word` (`interpreter::interpret`'s own `;`
// handling, `apply_control_word`'s own `postpone_` case), genuinely
// indistinguishable from the word it aliases. Because each of `IF`/`ELSE`/
// `THEN` individually is just as self-contained an action as `THEN` alone
// (D17's own stated `ENDIF` example), the same whole-body-alias shape closes
// all three -- a full `IF`/`ELSE`/`THEN` replacement, under new names, not
// only a `THEN`-only synonym. It is not a *from-scratch* reimplementation:
// each alias still ultimately dispatches through the same
// `machine::control_builtin` tag and the same C++ `apply_control_word` case
// its target does (there is no other way to reach `op::branch0`/the orig-
// dest discipline at all, per DIV-0015's own "no VM-representable form"),
// so this is the boundary's positive side, not a crossing of it: mixing
// `POSTPONE IF` with other code in the same definition remains diagnosed,
// unchanged (`interpreter::apply_control_word`'s own `postpone_` case).
//
// Every fuel unit `interpreter::consume_interp_fuel` charges is charged once
// per token `interpret`'s own outer loop scans (`interp.hpp`), so this
// text's own token count is exactly the constant per-session step-count
// D22 asks this step to measure: 35 tokens, so 35 interpreter-loop steps,
// added to every `interpreter::build_session_with_prelude` session below,
// independent of whatever @p text a caller goes on to interpret afterward.
// c46f1cb1-63a2-4d0e-9e2c-6a45f2c9d5b1
inline constexpr std::string_view prelude_source =
    ": NIP SWAP DROP ; "
    ": TUCK SWAP OVER ; "
    ": ?DUP DUP IF DUP THEN ; "
    ": WHEN POSTPONE IF ; IMMEDIATE "
    ": OTHERWISE POSTPONE ELSE ; IMMEDIATE "
    ": ENDIF POSTPONE THEN ; IMMEDIATE ";
// c46f1cb1-63a2-4d0e-9e2c-6a45f2c9d5b1 end

// 7d3e9a5c-2b4f-4e1a-8c6d-9f1b3a7e5c2d
/// Builds a session exactly like @ref build_session, except @ref
/// prelude_source is compiled first, into the same dictionary and code
/// space @p text goes on to share -- so every word @ref prelude_source
/// defines is available to @p text exactly as if @p text's own author had
/// typed @ref prelude_source at the top of their own program. This is the
/// one function `forth.hpp`'s own `compiled_forth<Source>` (the public
/// one-shot API) builds a session through as of step F35; @ref build_session
/// itself is untouched -- every existing direct caller of it (most of this
/// project's own lower-level tests, deliberately exercising the session
/// mechanism in isolation at capacities far below what a prelude needs,
/// e.g. `session.test.cpp`'s own `MaxCode=64` calls) keeps building a
/// prelude-free session exactly as before. See DIV-0027 for why the
/// injection point is here, at `compiled_forth`'s own boundary, rather than
/// inside @ref build_session itself.
///
/// One @ref machine::forth_state is built over the *concatenation* of @ref
/// prelude_source and @p text (a single `SOURCE`, per D13 -- @ref
/// machine::input_source has no way to resume over a second, independent
/// text once built), copied into a fixed @p MaxSourceLen buffer local to
/// this call; @ref session itself stores no reference back into that
/// buffer (its own fields are flat values -- code, dictionary, the data-
/// space high-water mark, captured output, and the final data stack -- not
/// source-text views), so the buffer's own lifetime need not outlive this
/// call. Diagnoses (rather than overflows) a combined length past @p
/// MaxSourceLen.
///
/// @tparam MaxSourceLen Capacity, in bytes, for @ref prelude_source and
///                      @p text combined (plus one separating newline).
template <int MaxCode = 4096, int MaxWords = 256, int MaxData = 1024,
          int MaxOut = 4096, int MaxName = 32, int MaxDepth = 64,
          int MaxRDepth = 64, int MaxStack = 64, int MaxSourceLen = 8192>
[[nodiscard]] constexpr auto
build_session_with_prelude(std::string_view text, int fuel = 100000)
    -> foundation::result<
        session<MaxCode, MaxWords, MaxData, MaxOut, MaxName, MaxStack>> {
    std::size_t const combined_len = prelude_source.size() + 1 + text.size();
    if (combined_len > static_cast<std::size_t>(MaxSourceLen)) {
        return foundation::parse_error{
            foundation::source_pos{},
            "prelude plus source text exceeds MaxSourceLen"};
    }
    std::array<char, MaxSourceLen> combined{};
    std::size_t pos = 0;
    for (char c : prelude_source) {
        combined[pos++] = c;
    }
    combined[pos++] = '\n';
    for (char c : text) {
        combined[pos++] = c;
    }
    std::string_view combined_view{combined.data(), pos};
    return build_session<MaxCode, MaxWords, MaxData, MaxOut, MaxName, MaxDepth,
                         MaxRDepth, MaxStack>(combined_view, fuel);
}
// 7d3e9a5c-2b4f-4e1a-8c6d-9f1b3a7e5c2d end

} // namespace smd::forth::interpreter

#endif
