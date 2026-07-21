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
#include <utility>

namespace smd::forth {

// Step F15 (docs/forth-plan.md): the public one-shot API. This header
// replaces the pre-pipeline placeholder that used to live here (a bare
// forth::forth() free function). It reuses the *pattern*, not the code, of
// ~/src/compile-time-scheme/main's src/smd/smdscheme/smdscheme.hpp:
// source_literal<N> (an NTTP char-array wrapper) plus a single-shot
// `inline constexpr` variable template whose initializer runs the whole
// pipeline and calls .value() at every stage. A program that fails to parse
// or elaborate is therefore a HARD COMPILE ERROR: foundation::result<T>::
// value() does `std::get<T>` on its backing variant, which throws when the
// variant instead holds a foundation::parse_error; a throw during the
// evaluation of a constexpr initializer is not a core constant expression,
// so the whole translation unit fails to compile -- exactly the discipline
// eval_direct.test.cpp's and vm.test.cpp's own namespace-scope constexpr
// programs already rely on for known-good inputs (F13/F14), made the
// *public* contract here. Forth's own pipeline is three stages
// (read_program -> elaborate -> codegen), not Scheme's single
// compile_to_closure, so compiled_forth composes all three explicitly. See
// src/smd/forth/neg_compile_syntax_error.cpp and this component's own
// CMakeLists.txt for the negative half of this contract: a program with a
// genuine syntax error is proven, by a configure-time try_compile() check,
// to fail to compile.

// a5ec0c45-2cc3-4cbc-9761-05447805199d
/// A compile-time string literal usable as a non-type template parameter
/// (NTTP).
///
/// Storing a null-terminated copy of the source text in a fixed-size @c char
/// array (rather than merely a @c std::string_view over the original
/// literal) is what lets the text participate in template argument
/// deduction and become part of a type's own identity, so @ref
/// compiled_forth can be a variable template keyed on the literal source
/// text itself (`compiled_forth<"...">`).
///
/// @tparam N Length of the string literal, including its trailing null
///           terminator; deduced automatically from the array bound of the
///           string literal passed to the converting constructor.
template <std::size_t N>
struct source_literal {
    char text[N]{}; ///< Null-terminated copy of the source text.

    /// Converting constructor from a string literal; copies all @p N
    /// characters. This is what makes `compiled_forth<"...">` deduce @ref N
    /// automatically from the literal's own length.
    constexpr source_literal(char const (&input)[N]) {
        std::copy_n(input, N, text);
    }

    /// A @c std::string_view over the stored text, excluding the trailing
    /// null terminator.
    [[nodiscard]] constexpr auto view() const -> std::string_view {
        return {text, N - 1};
    }
};
// a5ec0c45-2cc3-4cbc-9761-05447805199d end

namespace detail {

/// Runs the whole read -> elaborate -> codegen pipeline over @p source.
///
/// @pre @p source parses, elaborates, and compiles without error. A
/// violation is a hard compile error when this function is called from a
/// namespace-scope `constexpr` initializer (see this header's own top
/// comment) -- the entire point of @ref compiled_forth.
template <int MaxCode, int MaxNodes, int MaxBody, int MaxName, int MaxDepth,
          int MaxWords, int MaxData, int MaxWarnings>
constexpr auto compile_program(std::string_view source)
    -> machine::compiled_program<MaxCode, MaxWords> {
    auto tree =
        reader::read_program<MaxNodes, MaxBody, MaxName, MaxDepth>(source);
    auto unit =
        elaborator::elaborate<MaxNodes, MaxBody, MaxName, MaxWords, MaxData,
                              MaxWarnings>(tree.value(), source);
    auto program =
        machine::codegen<MaxCode, MaxNodes, MaxBody, MaxName, MaxWords, MaxData,
                         MaxWarnings>(unit.value());
    return program.value();
}

} // namespace detail

// f4cd7386-6d93-4b6b-8d91-7edf72dfcde7
/// The value @ref compiled_forth<Source> holds: a compiled program (F14)
/// bundled with the @c forth_state capacities @ref run, @ref stack, and
/// @ref output construct their own fresh machine state with.
///
/// @ref run constructs a fresh @c machine::forth_state and calls @c
/// machine::run against it; @ref stack and @ref output are convenience
/// wrappers around @ref run for the common case of "just show me the
/// result." Each of the three does its own independent run from scratch: a
/// namespace-scope `compiled_forth<Source>` is one immutable @c
/// machine::compiled_program value (produced once by codegen), not a
/// mutable machine, so there is no state to share between calls -- running
/// the same @c compiled_program object against two different @c
/// forth_state objects is exactly F14's own "survives-to-runtime" pattern
/// (`vm.test.cpp`).
///
/// @tparam MaxCode      Compiled instruction-array capacity.
/// @tparam MaxWords     Dictionary word-table capacity.
/// @tparam StackDepth   Data stack capacity of any @c forth_state this type
///                      constructs.
/// @tparam RStackDepth  Return stack capacity of any @c forth_state this
///                      type constructs.
/// @tparam MaxData      Data-space capacity of any @c forth_state this type
///                      constructs -- shared with the same-named capacity
///                      the source program was elaborated/compiled against
///                      (@ref compiled_forth), so every data-space address
///                      the program computes is in range.
/// @tparam MaxOut       Output-buffer capacity of any @c forth_state this
///                      type constructs.
template <int MaxCode, int MaxWords, int StackDepth, int RStackDepth,
          int MaxData, int MaxOut>
class compiled_forth_program {
  public:
    /// The concrete @c forth_state type @ref run constructs and runs
    /// against.
    using state_type =
        machine::forth_state<StackDepth, RStackDepth, MaxData, MaxOut>;

    constexpr explicit compiled_forth_program(
        machine::compiled_program<MaxCode, MaxWords> program);

    /// The underlying compiled program (F14's own artifact).
    [[nodiscard]] constexpr auto program() const
        -> machine::compiled_program<MaxCode, MaxWords> const &;

    /// Constructs a fresh @ref state_type and runs the compiled program
    /// against it (@c machine::run, F14), returning either the resulting
    /// machine state or the first diagnosed runtime error (stack
    /// underflow, stack overflow, division by zero, budget exhaustion, an
    /// unresolved instruction pointer, ...).
    ///
    /// @param fuel The VM's own step budget (see @c machine::run); defaults
    ///             to the same `100000` @c machine::run itself defaults to.
    [[nodiscard]] constexpr auto run(int fuel = 100000) const
        -> foundation::result<state_type>;

    /// Convenience wrapper around @ref run: the resulting data stack,
    /// snapshotted bottom to top (the same order @c primitive::dot_s prints
    /// in, `forth_state.hpp`'s own `.S`). Returns an empty stack if @ref run
    /// itself diagnoses an error -- call @ref run directly to see the
    /// error.
    [[nodiscard]] constexpr auto stack(int fuel = 100000) const
        -> foundation::static_vector<machine::cell, StackDepth>;

    /// Convenience wrapper around @ref run: the accumulated output buffer.
    /// Returns an empty buffer if @ref run itself diagnoses an error -- call
    /// @ref run directly to see the error.
    [[nodiscard]] constexpr auto output(int fuel = 100000) const
        -> foundation::static_vector<char, MaxOut>;

  private:
    machine::compiled_program<MaxCode, MaxWords> program_;
};

template <int MaxCode, int MaxWords, int StackDepth, int RStackDepth,
          int MaxData, int MaxOut>
constexpr compiled_forth_program<MaxCode, MaxWords, StackDepth, RStackDepth,
                                 MaxData, MaxOut>::
    compiled_forth_program(machine::compiled_program<MaxCode, MaxWords> program)
    : program_(std::move(program)) {}

template <int MaxCode, int MaxWords, int StackDepth, int RStackDepth,
          int MaxData, int MaxOut>
constexpr auto
compiled_forth_program<MaxCode, MaxWords, StackDepth, RStackDepth, MaxData,
                       MaxOut>::program() const
    -> machine::compiled_program<MaxCode, MaxWords> const & {
    return program_;
}

template <int MaxCode, int MaxWords, int StackDepth, int RStackDepth,
          int MaxData, int MaxOut>
constexpr auto
compiled_forth_program<MaxCode, MaxWords, StackDepth, RStackDepth, MaxData,
                       MaxOut>::run(int fuel) const
    -> foundation::result<state_type> {
    state_type state{};
    auto status = machine::run(program_, state, fuel);
    if (!status.has_value()) {
        return status.error();
    }
    return state;
}

template <int MaxCode, int MaxWords, int StackDepth, int RStackDepth,
          int MaxData, int MaxOut>
constexpr auto
compiled_forth_program<MaxCode, MaxWords, StackDepth, RStackDepth, MaxData,
                       MaxOut>::stack(int fuel) const
    -> foundation::static_vector<machine::cell, StackDepth> {
    foundation::static_vector<machine::cell, StackDepth> out{};
    auto r = run(fuel);
    if (!r.has_value()) {
        return out;
    }
    auto const &data = r.value().data();
    for (int offset = data.depth() - 1; offset >= 0; --offset) {
        out.push_back(data.peek(offset).value());
    }
    return out;
}

template <int MaxCode, int MaxWords, int StackDepth, int RStackDepth,
          int MaxData, int MaxOut>
constexpr auto
compiled_forth_program<MaxCode, MaxWords, StackDepth, RStackDepth, MaxData,
                       MaxOut>::output(int fuel) const
    -> foundation::static_vector<char, MaxOut> {
    auto r = run(fuel);
    if (!r.has_value()) {
        return foundation::static_vector<char, MaxOut>{};
    }
    return r.value().output();
}
// f4cd7386-6d93-4b6b-8d91-7edf72dfcde7 end

// ea5eea2a-1508-4eca-a4eb-9ded5d706801
/// Compiles @p Source's text through the entire front-to-back pipeline
/// (@c reader::read_program -> @c elaborator::elaborate -> @c
/// machine::codegen) at compile time and stores the result -- the public
/// one-shot entry point (docs/forth-plan.md Step F15).
///
/// A parse or elaboration failure is a **hard compile error** (see this
/// header's own top comment).
///
/// Usage:
/// @code
///   constexpr auto program = compiled_forth<": SQUARED DUP * ; 4 SQUARED">;
///   static_assert(program.stack().size() == 1);
///   auto result = program.run();          // result<forth_state<...>>
/// @endcode
///
/// Every capacity below defaults to the same production default the
/// underlying pipeline stage already uses on its own (`read_program.hpp`,
/// `elaborate.hpp`, `codegen.hpp`); @ref StackDepth, @ref RStackDepth, and
/// @ref MaxOut have no such existing production default to inherit (F13/F14
/// only ever chose small test-scoped values) and are sized generously here
/// instead (see `docs/divergences/DIV-0009-*.md`).
///
/// @tparam Source       The Forth source literal (deduced from the NTTP).
/// @tparam MaxCode      Compiled instruction-array capacity (@c codegen).
/// @tparam MaxNodes     Parse/elaboration arena capacity, shared by the
///                      syntax tree and the elaborated core.
/// @tparam MaxBody      Maximum forms in any one body, or at the top level.
/// @tparam MaxName      Maximum name/word length.
/// @tparam MaxDepth     Maximum nesting depth of control structures
///                      (`read_program`'s own recursive-descent bound).
/// @tparam MaxWords     Maximum dictionary entries.
/// @tparam MaxData      Maximum data-space cells -- shared between
///                      elaboration/codegen and the @c forth_state @ref
///                      compiled_forth_program::run constructs.
/// @tparam MaxWarnings  Maximum collected redefinition warnings.
/// @tparam StackDepth   Data stack capacity of any @c forth_state @ref
///                      compiled_forth_program::run constructs.
/// @tparam RStackDepth  Return stack capacity of any @c forth_state @ref
///                      compiled_forth_program::run constructs.
/// @tparam MaxOut       Output-buffer capacity of any @c forth_state @ref
///                      compiled_forth_program::run constructs.
template <source_literal Source, int MaxCode = 4096, int MaxNodes = 1024,
          int MaxBody = 64, int MaxName = 32, int MaxDepth = 32,
          int MaxWords = 256, int MaxData = 1024, int MaxWarnings = 64,
          int StackDepth = 1024, int RStackDepth = 1024, int MaxOut = 4096>
inline constexpr auto compiled_forth =
    compiled_forth_program<MaxCode, MaxWords, StackDepth, RStackDepth, MaxData,
                           MaxOut>(
        detail::compile_program<MaxCode, MaxNodes, MaxBody, MaxName, MaxDepth,
                                MaxWords, MaxData, MaxWarnings>(Source.view()));
// ea5eea2a-1508-4eca-a4eb-9ded5d706801 end

} // namespace smd::forth

#endif
