// src/smd/forth/machine/foreign.hpp                                 -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#ifndef SRC_SMD_FORTH_MACHINE_FOREIGN_HPP
#define SRC_SMD_FORTH_MACHINE_FOREIGN_HPP

#include <smd/forth/foundation/parse_error.hpp>
#include <smd/forth/foundation/result.hpp>
#include <smd/forth/foundation/source_pos.hpp>
#include <smd/forth/foundation/static_vector.hpp>
#include <smd/forth/machine/cell.hpp>
#include <smd/forth/machine/dictionary.hpp>
#include <smd/forth/machine/forth_state.hpp>

#include <string_view>
#include <type_traits>
#include <variant>

namespace smd::forth::machine {

// Step F34 (docs/forth-plan-2.md), D18/D20: the foreign function interface.
//
// A foreign word is a plain C++ function pointer over @ref forth_state --
// "direct access to the underlying stacks, data space, and output buffer,"
// exactly as the plan asks. That signature is the whole reason this header
// exists rather than the function pointer simply living on @ref foreign_word
// (the dictionary binding) the way @ref constant_word's own value does:
// @ref forth_state is capacity-parameterized (four template parameters) and
// @ref dictionary is not, so a dictionary entry has no way to *name* the
// function pointer's own type. The registry below carries the pointers, the
// dictionary entry carries an index into it, and the two travel together
// through @ref foreign_dictionary. See DIV-0029 for the full design record,
// including the two rejected alternatives (folding the registry into
// @ref forth_state, and type-erasing the state behind a virtual interface).

// bf3c6a19-4e27-4d5a-9c81-7a2f6b0d4e93
/// A foreign word's own implementation: a plain function pointer taking the
/// live @ref forth_state by reference and reporting success or a diagnosed
/// failure through @ref status, exactly like @ref apply_primitive does for a
/// built-in primitive (D7: every misuse is diagnosed, never UB).
///
/// A `constexpr` function pointer is a perfectly ordinary constant
/// expression, so a foreign word written `constexpr` participates in a
/// compile-time session with no special handling anywhere; one that is not
/// `constexpr` (or one that is, but takes a runtime-only branch through
/// `std::is_constant_evaluated`) is simply unusable at compile time and
/// diagnosed as such by the compiler at the point the session is built.
template <int MaxDepth, int MaxRDepth, int MaxData, int MaxOut>
using foreign_fn =
    status (*)(forth_state<MaxDepth, MaxRDepth, MaxData, MaxOut> &);

/// A foreign word's optional declared data-stack effect (D20).
///
/// Undeclared (the default, @ref known false) is the `unknown` lattice value
/// F30's own effect checker already uses for `EXECUTE`, `CATCH`, and every
/// other construct whose shape it cannot see through
/// (`interpreter::effect_lint.hpp`); a declared effect participates in that
/// lint like any other word's, and is carried on the dictionary entry (@ref
/// foreign_word) rather than here, since the checker only ever has the
/// dictionary to consult.
struct foreign_effect {
    bool known = false; ///< True iff @ref inputs/@ref outputs are meaningful.
    int inputs = 0;     ///< Cells consumed. Meaningful only when @ref known.
    int outputs = 0;    ///< Cells produced. Meaningful only when @ref known.

    friend constexpr auto operator==(foreign_effect const &,
                                     foreign_effect const &) -> bool = default;
};

/// A declared `( inputs -- outputs )` effect for @ref foreign_dictionary::
/// with_foreign; the default-constructed @ref foreign_effect is the
/// undeclared (`unknown`) one.
[[nodiscard]] constexpr auto declared_effect(int inputs, int outputs)
    -> foreign_effect {
    return foreign_effect{.known = true, .inputs = inputs, .outputs = outputs};
}

/// The foreign-function registry: a flat, fixed-capacity, trivially
/// destructible array of @ref foreign_fn "function pointers" (D3), indexed by
/// exactly the handle @ref foreign_word::index carries and @ref op::foreign
/// names as its own operand.
///
/// @tparam MaxForeign Registry capacity, in functions.
/// @tparam MaxDepth   The registered functions' own @ref forth_state data
///                    stack capacity; likewise @p MaxRDepth, @p MaxData, and
///                    @p MaxOut. A session and every later re-run of it must
///                    use a @ref forth_state with exactly these capacities:
///                    the function pointers are typed on them.
template <int MaxForeign, int MaxDepth, int MaxRDepth, int MaxData, int MaxOut>
class foreign_vocabulary {
  public:
    /// The registered function-pointer type.
    using function_type = foreign_fn<MaxDepth, MaxRDepth, MaxData, MaxOut>;
    /// The state type those functions run against.
    using state_type = forth_state<MaxDepth, MaxRDepth, MaxData, MaxOut>;

    constexpr foreign_vocabulary() = default;

    /// Registers @p fn and returns its own index (its @ref foreign_word::
    /// index, and the operand @ref op::foreign will carry). Diagnoses a full
    /// registry rather than overflowing.
    ///
    /// A null @p fn is **not** rejected here, and cannot be: under GCC's own
    /// UndefinedBehaviorSanitizer (`-fsanitize=undefined`, this project's
    /// default `Asan` config) the address of a function is not usable in a
    /// constant expression as an *operand of a comparison* -- `fn == nullptr`
    /// alone makes any enclosing `static_assert` fail to evaluate, even
    /// though calling through the same pointer is perfectly
    /// constant-evaluable. A registration is exactly where that comparison
    /// *would* be evaluated during constant evaluation (a `constexpr`
    /// vocabulary is built by calling this function), so a check here would
    /// make every compile-time FFI session uncompilable in the configuration
    /// this project builds in.
    ///
    /// It is @ref call that diagnoses a null implementation instead, where
    /// the check can be arranged never to be evaluated at compile time -- so
    /// nothing is left undiagnosed, only moved to the one place it can be
    /// checked. See that function's own doc comment and DIV-0029.
    constexpr auto add(function_type fn) -> foundation::result<int>;

    /// The number of functions registered so far.
    [[nodiscard]] constexpr auto size() const -> int;

    /// The function registered at @p index.
    /// @pre 0 <= index < size()
    [[nodiscard]] constexpr auto function_at(int index) const -> function_type;

    /// Calls the function registered at @p index against @p state, returning
    /// whatever it reports. Diagnoses an out-of-range @p index rather than
    /// indexing past @ref size, and a null registered implementation rather
    /// than calling through it (D7: all misuse is a diagnosed error via
    /// @ref foundation::result, never UB).
    ///
    /// The null check is deliberately spelled
    /// `!std::is_constant_evaluated() && fn == nullptr`, and the order
    /// matters. A bare `fn == nullptr` would be a *non-constant condition*
    /// under GCC's own UndefinedBehaviorSanitizer (see @ref add) and would
    /// break every compile-time FFI session; short-circuiting means the
    /// comparison is simply never evaluated during constant evaluation, so
    /// the obstruction never fires, while at ordinary runtime it is an
    /// ordinary null check. Nothing is lost at compile time: a genuinely
    /// null pointer reached during constant evaluation falls through to the
    /// call below, and calling through a null function pointer is not a
    /// constant expression -- a hard compile error, which is a strictly
    /// better diagnosis than a returned @ref foundation::result would be.
    [[nodiscard]] constexpr auto call(int index, state_type &state) const
        -> status;

  private:
    foundation::static_vector<function_type, MaxForeign> functions_{};
};
// bf3c6a19-4e27-4d5a-9c81-7a2f6b0d4e93 end

template <int MaxForeign, int MaxDepth, int MaxRDepth, int MaxData, int MaxOut>
constexpr auto
foreign_vocabulary<MaxForeign, MaxDepth, MaxRDepth, MaxData, MaxOut>::add(
    function_type fn) -> foundation::result<int> {
    if (functions_.size() >= MaxForeign) {
        return foundation::parse_error{foundation::source_pos{},
                                       "foreign vocabulary full"};
    }
    int const index = functions_.size();
    functions_.push_back(fn);
    return index;
}

template <int MaxForeign, int MaxDepth, int MaxRDepth, int MaxData, int MaxOut>
constexpr auto
foreign_vocabulary<MaxForeign, MaxDepth, MaxRDepth, MaxData, MaxOut>::size()
    const -> int {
    return functions_.size();
}

template <int MaxForeign, int MaxDepth, int MaxRDepth, int MaxData, int MaxOut>
constexpr auto foreign_vocabulary<MaxForeign, MaxDepth, MaxRDepth, MaxData,
                                  MaxOut>::function_at(int index) const
    -> function_type {
    return functions_[index];
}

template <int MaxForeign, int MaxDepth, int MaxRDepth, int MaxData, int MaxOut>
constexpr auto
foreign_vocabulary<MaxForeign, MaxDepth, MaxRDepth, MaxData, MaxOut>::call(
    int index, state_type &state) const -> status {
    if (index < 0 || index >= functions_.size()) {
        return foundation::parse_error{foundation::source_pos{},
                                       "foreign word index out of range"};
    }
    // See this function's own doc comment for why the guard is spelled this
    // way: the comparison is fine as long as constant evaluation never
    // reaches it, and short-circuiting is what guarantees that.
    if (!std::is_constant_evaluated() && functions_[index] == nullptr) {
        return foundation::parse_error{foundation::source_pos{},
                                       "foreign word has no implementation"};
    }
    return functions_[index](state);
}

// e5a92c74-3b18-4f6d-8e02-1c4b7d9a3f65
/// A dictionary and its own foreign-function registry, registered together
/// so their indices can never disagree -- the "registered pre-session"
/// vocabulary the plan's own `default_dictionary().with_foreign("GCD",
/// gcd_word)` shape names, adapted to the fact that the registry cannot live
/// inside @ref dictionary itself (this header's own top comment; DIV-0029).
///
/// @ref with_foreign is the whole builder surface: it appends the function to
/// @ref foreigns and, in the same step, installs a @ref foreign_word header
/// naming that function's own index into @ref words -- a D18 header with an
/// XT like any other word (`interpreter::resolve_execution_token` builds the
/// stub; `interpreter::compile_entry` emits @ref op::foreign directly).
///
/// It returns a @ref foundation::result rather than the bundle itself, so a
/// full dictionary or a full registry is a *diagnosed* failure rather than a
/// precondition violation (D7); at the only site this type is really meant
/// for -- a namespace-scope `constexpr` initializer -- `.value()` on a failed
/// result is not a core constant expression, so the failure is a hard compile
/// error, exactly the contract `forth.hpp`'s own `compiled_forth<Source>`
/// already has for a malformed program.
template <int MaxWords = 256, int MaxName = 32, int MaxForeign = 16,
          int MaxDepth = 64, int MaxRDepth = 64, int MaxData = 1024,
          int MaxOut = 4096>
struct foreign_dictionary {
    /// The word list, exactly as any other caller would build it.
    dictionary<MaxWords, MaxName> words{};

    /// The foreign-function registry @ref words's own @ref foreign_word
    /// entries index into.
    foreign_vocabulary<MaxForeign, MaxDepth, MaxRDepth, MaxData, MaxOut>
        foreigns{};

    /// Registers @p fn under @p name, with an optional declared @p effect
    /// (undeclared -- the default -- is D20's `unknown` lattice value), and
    /// returns the extended bundle. See this struct's own doc comment for why
    /// this returns a @ref foundation::result.
    [[nodiscard]] constexpr auto
    with_foreign(std::string_view name,
                 foreign_fn<MaxDepth, MaxRDepth, MaxData, MaxOut> fn,
                 foreign_effect effect = {}) const
        -> foundation::result<foreign_dictionary>;
};
// e5a92c74-3b18-4f6d-8e02-1c4b7d9a3f65 end

template <int MaxWords, int MaxName, int MaxForeign, int MaxDepth,
          int MaxRDepth, int MaxData, int MaxOut>
constexpr auto foreign_dictionary<
    MaxWords, MaxName, MaxForeign, MaxDepth, MaxRDepth, MaxData,
    MaxOut>::with_foreign(std::string_view name,
                          foreign_fn<MaxDepth, MaxRDepth, MaxData, MaxOut> fn,
                          foreign_effect effect) const
    -> foundation::result<foreign_dictionary> {
    foreign_dictionary extended = *this;
    auto index = extended.foreigns.add(fn);
    if (!index.has_value()) {
        return index.error();
    }
    auto defined = extended.words.define_foreign(
        name, foreign_word{.index = index.value(),
                           .effect_known = effect.known,
                           .effect_inputs = effect.inputs,
                           .effect_outputs = effect.outputs});
    if (!defined.has_value()) {
        return defined.error();
    }
    return extended;
}

/// A @ref foreign_dictionary whose word list is @ref default_dictionary and
/// whose registry is empty -- the starting point every FFI session builds
/// from (`default_foreign_dictionary<>().with_foreign("GCD", gcd_word)`).
template <int MaxWords = 256, int MaxName = 32, int MaxForeign = 16,
          int MaxDepth = 64, int MaxRDepth = 64, int MaxData = 1024,
          int MaxOut = 4096>
[[nodiscard]] constexpr auto default_foreign_dictionary()
    -> foreign_dictionary<MaxWords, MaxName, MaxForeign, MaxDepth, MaxRDepth,
                          MaxData, MaxOut> {
    return foreign_dictionary<MaxWords, MaxName, MaxForeign, MaxDepth,
                              MaxRDepth, MaxData, MaxOut>{
        .words = default_dictionary<MaxWords, MaxName>()};
}

namespace detail {

// D3, exactly like dictionary.hpp's and instruction.hpp's own checks: the
// registry and the bundle are flat, trivially destructible values a session
// can carry from a constant-expression evaluation into ordinary runtime code.
// Checked against one concrete instantiation as a representative sample.
static_assert(
    std::is_trivially_destructible_v<foreign_vocabulary<8, 64, 64, 1024, 256>>);
static_assert(
    std::is_trivially_copyable_v<foreign_vocabulary<8, 64, 64, 1024, 256>>);
static_assert(std::is_trivially_destructible_v<
              foreign_dictionary<256, 32, 8, 64, 64, 1024, 256>>);

} // namespace detail

// Merge criteria (static_assert, immediately-invoked-lambda pattern), step
// F34: a registered constexpr foreign word is reachable by index and runs
// against a live forth_state at compile time; a bundle registers the function
// and its dictionary header together, under one index.

namespace detail {

/// `GCD` as a foreign word: pops two cells, pushes their greatest common
/// divisor. `constexpr`, so it runs during a compile-time session exactly
/// like a built-in primitive would. Exists for this header's own merge
/// criteria; `src/examples/ffi_gcd.cpp` is the real, user-facing one.
constexpr auto foreign_gcd_example(forth_state<64, 64, 1024, 256> &state)
    -> status {
    auto b = state.data().pop();
    if (!b.has_value()) {
        return b.error();
    }
    auto a = state.data().pop();
    if (!a.has_value()) {
        return a.error();
    }
    cell x = a.value() < 0 ? -a.value() : a.value();
    cell y = b.value() < 0 ? -b.value() : b.value();
    while (y != 0) {
        cell const t = x % y;
        x = y;
        y = t;
    }
    return state.data().push(x);
}

} // namespace detail

static_assert([] {
    foreign_vocabulary<4, 64, 64, 1024, 256> vocab{};
    auto index = vocab.add(&detail::foreign_gcd_example);
    if (!index.has_value() || index.value() != 0 || vocab.size() != 1) {
        return false;
    }
    forth_state<64, 64, 1024, 256> st{};
    if (!st.data().push(12).has_value() || !st.data().push(18).has_value()) {
        return false;
    }
    auto r = vocab.call(index.value(), st);
    return r.has_value() && st.data().depth() == 1 &&
           st.data().peek().value() == 6;
}());

static_assert([] {
    foreign_vocabulary<4, 64, 64, 1024, 256> vocab{};
    forth_state<64, 64, 1024, 256> st{};
    // An out-of-range index is diagnosed, never UB (D7).
    return !vocab.call(0, st).has_value();
}());

static_assert([] {
    auto bundle = default_foreign_dictionary<256, 32, 4, 64, 64, 1024, 256>()
                      .with_foreign("GCD", &detail::foreign_gcd_example,
                                    declared_effect(2, 1));
    if (!bundle.has_value()) {
        return false;
    }
    auto const *entry = bundle.value().words.lookup("gcd");
    if (entry == nullptr) {
        return false;
    }
    auto const *fw = std::get_if<foreign_word>(&entry->binding);
    return fw != nullptr && fw->index == 0 && fw->effect_known &&
           fw->effect_inputs == 2 && fw->effect_outputs == 1 &&
           bundle.value().foreigns.size() == 1;
}());

} // namespace smd::forth::machine

#endif
