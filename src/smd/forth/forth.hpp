// src/smd/forth/forth.hpp                                            -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#ifndef SRC_SMD_FORTH_FORTH_HPP
#define SRC_SMD_FORTH_FORTH_HPP

#include <smd/forth/foundation/static_vector.hpp>
#include <smd/forth/interpreter/session.hpp>
#include <smd/forth/machine/cell.hpp>

#include <algorithm>
#include <cstddef>
#include <string_view>

// Step F26 (docs/forth-plan-2.md, "the cut"): the public one-shot API,
// retargeted from the R1 pipeline (reader::read_program ->
// elaborator::elaborate -> machine::codegen -> machine::run, step F15) onto
// D15's session image (interpreter::session / interpreter::build_session,
// step F25). The R1 pipeline this header used to compose is deleted by this
// same step (docs/divergences/DIV-0011's true-Forth pivot); see DIV-0014 for
// the full record of what changed here and why.
//
// The architecture shift this retarget reflects: under D13/D14 there is no
// separate "compile" phase distinct from "run the top level" -- the text
// interpreter both compiles `:` ... `;` definitions and directly executes
// every top-level word as it meets it, in the same pass. R1's forth_program
// therefore compiled once (read/elaborate/codegen) and *ran* the result
// fresh, from scratch, once per accessor call (`.run()`/`.stack()`/
// `.output()` each constructed a new machine::forth_state and called
// machine::run against it, independently). compiled_forth<Source>'s own
// single namespace-scope constexpr initialization now *is* that one run: it
// builds the whole session -- code space, final dictionary, data-space
// high-water mark, captured output, and (F26's own addition to
// interpreter::session) the final data stack -- exactly once, and
// `.run()`/`.stack()`/`.output()` are now cheap accessors over that already-
// built image rather than three independent re-executions. A malformed
// program is still a hard compile error (`.value()` on a failed
// foundation::result inside a constexpr initializer is not a core constant
// expression): what used to be a separate, later, per-call *runtime* failure
// (a well-formed program that ran out of fuel, for instance) is now folded
// into that same single build-time evaluation too, since running the top
// level is no longer separable from building the session at all. See
// DIV-0014 for the consequence this has for a program like the old `SPIN`
// budget-exhaustion example (moved to interpreter/control_flow_corpus.hpp,
// not reachable through this header until control flow exists at F27
// regardless).
//
// source_literal<N> is unchanged from step F15 -- reused, not copied, from
// ~/src/compile-time-scheme/main's own source_literal<N>/compiled_closure
// pattern (docs/forth-plan.md section 5); nothing about this retarget
// touches it.

namespace smd::forth {

// 18245977-b4d2-4011-bac7-a36f7680aeb4
/// A compile-time string literal usable as a non-type template parameter
/// (an NTTP).
///
/// Storing a null-terminated copy of the source text in a fixed-size @c
/// char array lets the *content* of a Forth program participate in
/// template argument deduction: `compiled_forth<"...">` is a variable
/// template keyed on the literal source text itself, not merely on its
/// address or length.
///
/// @tparam N The literal's length including its trailing null terminator
///           (deduced from the array-reference constructor argument).
// 0f3a01ca-389f-4ca6-b09c-6b6423482246
template <std::size_t N>
struct source_literal {
    char text[N]{}; ///< Null-terminated copy of the source text.

    /// Copies all @p N characters (including the trailing null) out of
    /// @p input.
    constexpr source_literal(char const (&input)[N]) {
        std::copy_n(input, N, text);
    }

    /// The stored text as a @c std::string_view, excluding the trailing
    /// null terminator.
    [[nodiscard]] constexpr auto view() const -> std::string_view {
        return {text, N - 1};
    }
};
// 0f3a01ca-389f-4ca6-b09c-6b6423482246 end
// 18245977-b4d2-4011-bac7-a36f7680aeb4 end

// bb43d007-745e-4e1b-8057-b97766788d6b
/// A built D15 session image (@ref interpreter::session), bundled with the
/// same three-accessor convenience surface step F15 established
/// (`run`/`stack`/`output`).
///
/// Unlike the R1-era @c forth_program this class replaces, @ref
/// forth_program carries no separate "compiled program" distinct from the
/// session it wraps, and none of its three accessors re-executes anything:
/// the whole program -- every top-level word and every `:` ... `;`
/// definition -- already ran once, when the @ref session_type this class
/// wraps was built (@ref compiled_forth below is the one place that
/// happens). `run`/`stack`/`output` are cheap reads over that
/// already-finished result.
///
/// @tparam MaxCode  Code-space instruction-array capacity (@ref
///                  interpreter::session).
/// @tparam MaxWords Dictionary/code-space word-table capacity, likewise.
/// @tparam MaxData  Data-space capacity, likewise.
/// @tparam MaxOut   Output-buffer capacity, likewise.
/// @tparam MaxName  Maximum word-name length, likewise.
/// @tparam MaxStack Final-data-stack snapshot capacity, likewise (F26's own
///                  addition to @ref interpreter::session).
template <int MaxCode, int MaxWords, int MaxData, int MaxOut, int MaxName,
          int MaxStack>
class forth_program {
  public:
    /// The session type this class wraps.
    using session_type = interpreter::session<MaxCode, MaxWords, MaxData,
                                              MaxOut, MaxName, MaxStack>;

    /// Wraps an already-built @p session; performs no interpretation of its
    /// own.
    constexpr explicit forth_program(session_type session);

    /// The whole built session image: code space, final dictionary,
    /// data-space high-water mark, captured output, and final data stack.
    /// Unlike the R1-era @c forth_program::run, this never fails at the
    /// call site -- any failure already happened, at compile time, when
    /// @ref compiled_forth built this object's own session (see this
    /// header's own top-of-file comment).
    [[nodiscard]] constexpr auto run() const -> session_type const &;

    /// The final data stack @ref run's own session left behind, bottom cell
    /// first and top cell last -- the same bottom-to-top order @ref
    /// machine::primitive::dot_s prints in. A thin accessor over @ref
    /// session_type::stack; see that field's own doc comment
    /// (`interpreter/session.hpp`) for how it is populated.
    [[nodiscard]] constexpr auto stack() const
        -> foundation::static_vector<machine::cell, MaxStack>;

    /// The output @ref run's own session accumulated while it was built. A
    /// thin accessor over @ref session_type::output.
    [[nodiscard]] constexpr auto output() const
        -> foundation::static_vector<char, MaxOut>;

  private:
    session_type session_;
};

template <int MaxCode, int MaxWords, int MaxData, int MaxOut, int MaxName,
          int MaxStack>
constexpr forth_program<MaxCode, MaxWords, MaxData, MaxOut, MaxName,
                        MaxStack>::forth_program(session_type session)
    : session_(session) {}

template <int MaxCode, int MaxWords, int MaxData, int MaxOut, int MaxName,
          int MaxStack>
constexpr auto
forth_program<MaxCode, MaxWords, MaxData, MaxOut, MaxName, MaxStack>::run()
    const -> session_type const & {
    return session_;
}

template <int MaxCode, int MaxWords, int MaxData, int MaxOut, int MaxName,
          int MaxStack>
constexpr auto
forth_program<MaxCode, MaxWords, MaxData, MaxOut, MaxName, MaxStack>::stack()
    const -> foundation::static_vector<machine::cell, MaxStack> {
    return session_.stack;
}

template <int MaxCode, int MaxWords, int MaxData, int MaxOut, int MaxName,
          int MaxStack>
constexpr auto
forth_program<MaxCode, MaxWords, MaxData, MaxOut, MaxName, MaxStack>::output()
    const -> foundation::static_vector<char, MaxOut> {
    return session_.output;
}
// bb43d007-745e-4e1b-8057-b97766788d6b end

// 61611656-475a-405f-a35b-179c4d88f71e
/// The public one-shot entry point (docs/forth-plan.md Step F15; retargeted
/// onto D15's session image at step F26): builds @p Source's whole session
/// (@ref interpreter::build_session) exactly once, at namespace-scope
/// @c constexpr initialization, and stores the result as a ready-to-inspect
/// @ref forth_program.
///
/// A @p Source that fails to interpret -- an unknown word, a stack
/// underflow, a budget-exhausted top-level loop, or anything else @ref
/// interpreter::interpret diagnoses -- is a **hard compile error**:
/// `.value()` on a @ref foundation::result holding an error is not a core
/// constant expression, so a namespace-scope @c constexpr initializer that
/// calls it fails the whole translation unit rather than merely returning a
/// runtime-checkable failure (see `test_neg_syntax_error.cpp` for the
/// failure side, deliberately exercised). This is a strictly wider net than
/// F15's own R1-era contract caught: under D13/D14, building a session *is*
/// running the top level, so what R1 would have surfaced as a *runtime*
/// @c forth_program::run failure (budget exhaustion, for instance) is now
/// caught here too, at the same single evaluation. See DIV-0014.
///
/// Every capacity is a template parameter with a project-standard default
/// (D2): @p MaxCode/@p MaxWords/@p MaxData/@p MaxOut/@p MaxName/@p MaxStack
/// match @ref interpreter::session's own defaults; @p BuildDepth/
/// @p BuildRDepth size the transient build-time @ref machine::forth_state
/// @ref interpreter::build_session constructs internally (not part of the
/// returned session); @p Fuel is the interpreter loop's own step budget for
/// that one build.
///
/// @tparam Source The Forth source text, as an NTTP (@ref source_literal).
// 9affe4fc-3d72-4f41-8fd0-d2338f9ab603
template <source_literal Source, int MaxCode = 4096, int MaxWords = 256,
          int MaxData = 1024, int MaxOut = 4096, int MaxName = 32,
          int MaxStack = 64, int BuildDepth = 64, int BuildRDepth = 64,
          int Fuel = 100000>
inline constexpr auto compiled_forth =
    forth_program<MaxCode, MaxWords, MaxData, MaxOut, MaxName, MaxStack>{
        interpreter::build_session<MaxCode, MaxWords, MaxData, MaxOut, MaxName,
                                   BuildDepth, BuildRDepth, MaxStack>(
            Source.view(), Fuel)
            .value()};
// 9affe4fc-3d72-4f41-8fd0-d2338f9ab603 end
// 61611656-475a-405f-a35b-179c4d88f71e end

} // namespace smd::forth

#endif // SRC_SMD_FORTH_FORTH_HPP
