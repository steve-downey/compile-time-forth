// src/smd/forth/forth.hpp                                            -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#ifndef SRC_SMD_FORTH_FORTH_HPP
#define SRC_SMD_FORTH_FORTH_HPP

#include <smd/forth/elaborator/elaborate.hpp>
#include <smd/forth/foundation/result.hpp>
#include <smd/forth/foundation/static_vector.hpp>
#include <smd/forth/machine/cell.hpp>
#include <smd/forth/machine/codegen.hpp>
#include <smd/forth/machine/forth_state.hpp>
#include <smd/forth/machine/instruction.hpp>
#include <smd/forth/machine/vm.hpp>
#include <smd/forth/reader/read_program.hpp>

#include <algorithm>
#include <cstddef>
#include <string_view>

// Step F15 (docs/forth-plan.md): the public one-shot API. Steps F5-F14 are
// only reachable by naming individual pipeline stages by hand
// (reader::read_program -> elaborator::elaborate -> machine::codegen ->
// machine::run, exactly the sequence every prior step's own test file
// already builds locally as a small `compile()` helper -- see e.g.
// machine/vm.test.cpp's identically-shaped helper). This header promotes
// that same sequence to the one thing an external caller is meant to use:
// `compiled_forth<"...">`, a namespace-scope constexpr value keyed on the
// source text itself via `source_literal`, plus `forth_program`'s
// `.run()`/`.stack()`/`.output()` convenience surface.
//
// `source_literal` is a pattern reused, not copied, from
// ~/src/compile-time-scheme/main/src/smd/smdscheme/smdscheme.hpp's
// `source_literal<N>` + `compiled_closure<Source>` (docs/forth-plan.md
// section 5, "reuse as pattern only"): reimplemented here under
// `smd::forth` with this project's own file-prolog/guard/namespace
// conventions, and `compiled_forth` composes three pipeline stages
// (read -> elaborate -> codegen) rather than Scheme's single
// `compile_to_closure` call.

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

namespace detail {

/// Runs the whole read -> elaborate -> codegen pipeline (F5/F7, F11/F12,
/// F14) over @p source, threading every capacity template parameter
/// through to its owning stage.
///
/// @p source is passed to both @ref reader::read_program and
/// @ref elaborator::elaborate: F12's stack-effect checker re-slices a
/// declared effect comment out of the original source text a second time
/// (see `elaborator::elaborate`'s own doc comment in
/// `elaborator/elaborate.hpp`), so the same text is threaded through
/// twice, not shared via the already-parsed tree.
template <int MaxCode, int MaxNodes, int MaxBody, int MaxName, int MaxDepth,
          int MaxWords, int MaxData, int MaxWarnings>
constexpr auto compile_program(std::string_view source)
    -> foundation::result<machine::compiled_program<MaxCode, MaxWords>> {
    auto tree =
        reader::read_program<MaxNodes, MaxBody, MaxName, MaxDepth>(source);
    if (!tree.has_value()) {
        return tree.error();
    }
    auto unit =
        elaborator::elaborate<MaxNodes, MaxBody, MaxName, MaxWords, MaxData,
                              MaxWarnings>(tree.value(), source);
    if (!unit.has_value()) {
        return unit.error();
    }
    return machine::codegen<MaxCode, MaxNodes, MaxBody, MaxName, MaxWords,
                            MaxData, MaxWarnings>(unit.value());
}

} // namespace detail

// bb43d007-745e-4e1b-8057-b97766788d6b
/// A compiled Forth program bundled with the runtime capacities its own
/// @ref run needs, ready to run or inspect.
///
/// @ref compiled_forth is the intended way to obtain one of these: this
/// class's own constructor takes an already-successful
/// @ref machine::compiled_program by value and performs no elaboration or
/// codegen of its own.
///
/// @tparam MaxCode      Instruction-array capacity (@ref
///                      machine::compiled_program).
/// @tparam MaxWords     Word-table capacity (@ref
///                      machine::compiled_program).
/// @tparam StackDepth   Data stack capacity for every @ref run's fresh
///                      @ref machine::forth_state.
/// @tparam RStackDepth  Return stack capacity, likewise.
/// @tparam MaxData      Data-space capacity, likewise; independent of (but
///                      must be at least as large as) the source program's
///                      own @ref machine::compiled_program::data_space_size.
/// @tparam MaxOut       Output-buffer capacity, likewise.
template <int MaxCode, int MaxWords, int StackDepth, int RStackDepth,
          int MaxData, int MaxOut>
class forth_program {
  public:
    /// The state type a @ref run produces.
    using state_type =
        machine::forth_state<StackDepth, RStackDepth, MaxData, MaxOut>;

    /// Wraps an already-compiled @p program; performs no compilation
    /// itself.
    constexpr explicit forth_program(
        machine::compiled_program<MaxCode, MaxWords> program);

    /// Runs this program against a freshly constructed @ref state_type and
    /// returns it, or the first diagnosed runtime error (stack
    /// underflow/overflow, division by zero, budget exhaustion, an
    /// out-of-range instruction pointer, or an unimplemented reserved
    /// opcode -- see @ref machine::run).
    ///
    /// Each call constructs a brand-new @ref state_type and runs this
    /// program's own @ref machine::compiled_program against it from
    /// scratch: a @ref forth_program carries no mutable execution state of
    /// its own (`compiled_forth<Source>` is a namespace-scope @c constexpr
    /// *value*, so it could not carry one), so two calls to @ref run never
    /// observe each other's side effects -- but a caller that calls
    /// @ref run, @ref stack, and @ref output on the same object runs the
    /// whole program three times over, not once. This is a deliberate,
    /// documented choice (see handoff.md's Step F15 section), not an
    /// accidental quadratic surprise: it is the only option available
    /// without giving @ref forth_program mutable state of its own, and it
    /// is exactly right for the plan's own merge-criterion-sized programs.
    ///
    /// @param fuel The VM's step budget (@ref machine::run); defaults to
    ///             the same 100000 @ref machine::run itself defaults to.
    [[nodiscard]] constexpr auto run(int fuel = 100000) const
        -> foundation::result<state_type>;

    /// Runs this program (@ref run, with @p fuel) and returns a snapshot of
    /// the resulting data stack, bottom cell first and top cell last -- the
    /// same bottom-to-top order @ref machine::primitive::dot_s prints in
    /// (`machine/forth_state.hpp`).
    ///
    /// Unlike @ref run, a runtime error does not prevent a result here:
    /// @ref machine::run mutates its @ref state_type argument in place
    /// even when it returns an error partway through, so @ref stack
    /// returns whatever the data stack held at the point execution
    /// stopped, successful or not. A caller that must distinguish "ran to
    /// completion" from "stopped early with a diagnosed error" needs
    /// @ref run itself, not this convenience accessor.
    [[nodiscard]] constexpr auto stack(int fuel = 100000) const
        -> foundation::static_vector<machine::cell, StackDepth>;

    /// Runs this program (@ref run, with @p fuel) and returns a snapshot of
    /// the resulting output buffer. Same best-effort-on-error contract as
    /// @ref stack.
    [[nodiscard]] constexpr auto output(int fuel = 100000) const
        -> foundation::static_vector<char, MaxOut>;

  private:
    machine::compiled_program<MaxCode, MaxWords> program_;
};

template <int MaxCode, int MaxWords, int StackDepth, int RStackDepth,
          int MaxData, int MaxOut>
constexpr forth_program<
    MaxCode, MaxWords, StackDepth, RStackDepth, MaxData,
    MaxOut>::forth_program(machine::compiled_program<MaxCode, MaxWords> program)
    : program_(program) {}

template <int MaxCode, int MaxWords, int StackDepth, int RStackDepth,
          int MaxData, int MaxOut>
constexpr auto
forth_program<MaxCode, MaxWords, StackDepth, RStackDepth, MaxData, MaxOut>::run(
    int fuel) const -> foundation::result<state_type> {
    state_type state{};
    auto status = machine::run(program_, state, fuel);
    if (!status.has_value()) {
        return status.error();
    }
    return state;
}

template <int MaxCode, int MaxWords, int StackDepth, int RStackDepth,
          int MaxData, int MaxOut>
constexpr auto forth_program<MaxCode, MaxWords, StackDepth, RStackDepth,
                             MaxData, MaxOut>::stack(int fuel) const
    -> foundation::static_vector<machine::cell, StackDepth> {
    state_type state{};
    (void)machine::run(program_, state, fuel);
    foundation::static_vector<machine::cell, StackDepth> snapshot{};
    for (int offset = state.data().depth() - 1; offset >= 0; --offset) {
        snapshot.push_back(state.data().peek(offset).value());
    }
    return snapshot;
}

template <int MaxCode, int MaxWords, int StackDepth, int RStackDepth,
          int MaxData, int MaxOut>
constexpr auto forth_program<MaxCode, MaxWords, StackDepth, RStackDepth,
                             MaxData, MaxOut>::output(int fuel) const
    -> foundation::static_vector<char, MaxOut> {
    state_type state{};
    (void)machine::run(program_, state, fuel);
    return state.output();
}
// bb43d007-745e-4e1b-8057-b97766788d6b end

// 61611656-475a-405f-a35b-179c4d88f71e
/// The public one-shot entry point (docs/forth-plan.md Step F15): compiles
/// @p Source's text through the whole pipeline exactly once, at
/// namespace-scope @c constexpr initialization, and stores the result as a
/// ready-to-run @ref forth_program.
///
/// A @p Source that fails to parse, fails elaboration (an unresolved word,
/// an unbalanced control structure, a failed stack-effect analysis, ...),
/// or overflows any of the capacities below is a **hard compile error**:
/// `.value()` on a @ref foundation::result holding an error is not a core
/// constant expression (`std::get` on the wrong @c std::variant
/// alternative throws), so a namespace-scope @c constexpr initializer that
/// calls it fails the whole translation unit rather than merely returning
/// a runtime-checkable failure -- the same discipline
/// `machine/eval_direct.test.cpp`'s and `machine/vm.test.cpp`'s own
/// namespace-scope @c constexpr programs already rely on for known-good
/// inputs; @ref compiled_forth makes that same discipline the *public*
/// contract (see `test_neg_syntax_error.cpp` for the failure side,
/// deliberately exercised).
///
/// Every capacity is a template parameter with a project-standard default
/// (D2): the pipeline-stage capacities (@p MaxCode, @p MaxNodes,
/// @p MaxBody, @p MaxName, @p MaxDepth, @p MaxWords, @p MaxData,
/// @p MaxWarnings) default to the same values @ref reader::read_program,
/// @ref elaborator::elaborate, and @ref machine::codegen already default
/// to on their own; the runtime-state capacities (@p StackDepth,
/// @p RStackDepth, @p MaxOut) are new to this step, chosen generously for
/// a one-shot caller who has not sized a @ref machine::forth_state by
/// hand.
///
/// @tparam Source The Forth source text, as an NTTP (@ref source_literal).
// 9affe4fc-3d72-4f41-8fd0-d2338f9ab603
template <source_literal Source, int MaxCode = 4096, int MaxNodes = 1024,
          int MaxBody = 64, int MaxName = 32, int MaxDepth = 32,
          int MaxWords = 256, int MaxData = 1024, int MaxWarnings = 64,
          int StackDepth = 64, int RStackDepth = 64, int MaxOut = 4096>
inline constexpr auto compiled_forth =
    forth_program<MaxCode, MaxWords, StackDepth, RStackDepth, MaxData, MaxOut>{
        detail::compile_program<MaxCode, MaxNodes, MaxBody, MaxName, MaxDepth,
                                MaxWords, MaxData, MaxWarnings>(Source.view())
            .value()};
// 9affe4fc-3d72-4f41-8fd0-d2338f9ab603 end
// 61611656-475a-405f-a35b-179c4d88f71e end

} // namespace smd::forth

#endif // SRC_SMD_FORTH_FORTH_HPP
