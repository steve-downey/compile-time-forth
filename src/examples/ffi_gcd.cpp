// src/examples/ffi_gcd.cpp                                          -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Step F34 (docs/forth-plan-2.md): the foreign function interface, as a
// single-file Godbolt extraction in the same shape as godbolt_forth.cpp --
// pasting this file into a Godbolt "tree" alongside the headers under
// src/smd/forth/ it transitively includes is enough to reproduce it, since
// every public header in this project is self-contained and header-only.
//
// Three things at once, because they are three faces of one interface:
//
//   1. **Forth calling C++, at compile time.** `GCD` is an ordinary C++
//      function over `machine::forth_state`; `default_foreign_dictionary()
//      .with_foreign(...)` gives it a dictionary header with an execution
//      token like any other word, and the whole session -- including every
//      call out to `GCD` -- runs inside one `constexpr` initialization.
//   2. **A runtime-only foreign word.** `TRACE` prints, which no constant
//      evaluation can do, so it guards its own I/O with
//      `std::is_constant_evaluated()`: silent while the session is being
//      built at compile time, chatty when the same compiled word is run
//      again at ordinary runtime.
//   3. **C++ calling Forth (the reverse direction).** The built session is
//      a trivially copyable literal value; `main` pushes arguments from
//      ordinary C++ variables, runs one of the session's own words again
//      through the same VM that ran it at compile time, and reads the
//      result back off the exposed stack -- Forth as an embedded scripting
//      engine whose script was already compiled before the program started.
#include <smd/forth/forth.hpp>

#include <smd/forth/interpreter/prelude.hpp>
#include <smd/forth/interpreter/session.hpp>
#include <smd/forth/machine/cell.hpp>
#include <smd/forth/machine/foreign.hpp>
#include <smd/forth/machine/forth_state.hpp>

#include <print>
#include <type_traits>
#include <variant>

namespace forth = smd::forth;

// 7a5b21ef-9c04-4d38-8b16-e3f70c9a52d1
/// The one `forth_state` shape this example's foreign words are typed on --
/// a foreign function pointer names its state's four capacities, so the
/// vocabulary, the session build, and every later runtime re-run must all
/// agree on them (`machine/foreign.hpp`).
using ffi_state = forth::machine::forth_state<64, 64, 1024, 4096>;

/// `GCD` ( a b -- gcd ) -- ordinary C++, with direct access to the data
/// stack. `constexpr`, so it runs during a compile-time session exactly like
/// a built-in primitive; every misuse it can hit (an empty stack here) is
/// reported through the same `machine::status` channel a primitive uses,
/// never as undefined behavior.
constexpr auto gcd_word(ffi_state &state) -> forth::machine::status {
    auto b = state.data().pop();
    if (!b.has_value()) {
        return b.error();
    }
    auto a = state.data().pop();
    if (!a.has_value()) {
        return a.error();
    }
    forth::machine::cell x = a.value() < 0 ? -a.value() : a.value();
    forth::machine::cell y = b.value() < 0 ? -b.value() : b.value();
    while (y != 0) {
        forth::machine::cell const t = x % y;
        x = y;
        y = t;
    }
    return state.data().push(x);
}

/// `TRACE` ( n -- n ) -- prints the top of the stack without consuming it,
/// but only when it is actually running at runtime. During the constant
/// evaluation that builds the session, `std::is_constant_evaluated()` is
/// true and this does nothing at all; the very same compiled instruction,
/// executed again from `main`, prints.
constexpr auto trace_word(ffi_state &state) -> forth::machine::status {
    auto top = state.data().peek(0);
    if (!top.has_value()) {
        return top.error();
    }
    if (!std::is_constant_evaluated()) {
        std::println("[trace] top of stack = {}", top.value());
    }
    return std::monostate{};
}

/// The vocabulary, registered pre-session: `machine::default_dictionary`
/// plus two foreign words, one with a declared `( a b -- c )` effect that
/// the F30 effect lint then checks like any other word's, one undeclared
/// (D20's `unknown`).
constexpr auto vocabulary =
    forth::machine::default_foreign_dictionary<256, 32, 4, 64, 64, 1024, 4096>()
        .with_foreign("GCD", &gcd_word, forth::machine::declared_effect(2, 1))
        .value()
        .with_foreign("TRACE", &trace_word)
        .value();

/// One `constexpr` initialization: compile `GCD3`, then run it. Every `GCD`
/// call below happens while this line is being evaluated, at compile time.
constexpr auto program =
    forth::compiled_forth_with(": GCD3 ( a b c -- d ) GCD TRACE GCD ;  "
                               "1071 462 210 GCD3",
                               vocabulary)
        .value();

/// The same source, kept as a session so `main` can run `GCD3` again with
/// arguments chosen at runtime (the reverse direction).
constexpr auto session =
    forth::interpreter::build_session_with_prelude<4096, 256, 1024, 4096, 32,
                                                   64, 64, 64, 8192, 4>(
        ": GCD3 ( a b c -- d ) GCD TRACE GCD ;", 100000, &vocabulary)
        .value();

auto main() -> int {
    // 1. The compile-time result, already computed. Note that no "[trace]"
    //    line was printed while building it.
    auto const built = program.stack();
    if (built.size() != 1) {
        std::println("unexpected stack depth: {}", built.size());
        return 1;
    }
    std::println("GCD3(1071, 462, 210) = {}  (computed at compile time)",
                 built[0]);

    // 3. The reverse direction: arguments from ordinary C++ variables, run
    //    the already-compiled word, read the result back. TRACE prints this
    //    time, from inside the running Forth word.
    int const a = 1998;
    int const b = 918;
    int const c = 486;

    auto image = session;
    ffi_state state{};
    if (!forth::interpreter::seed_from_session(image, state).has_value()) {
        std::println("could not seed the data space");
        return 1;
    }
    if (!state.data().push(a).has_value() ||
        !state.data().push(b).has_value() ||
        !state.data().push(c).has_value()) {
        std::println("could not push arguments");
        return 1;
    }
    auto ran = forth::interpreter::call_defined_word(
        image, state, "GCD3", 100000, &vocabulary.foreigns);
    if (!ran.has_value()) {
        std::println("GCD3 failed: {}", ran.error().message);
        return 1;
    }
    if (state.data().depth() != 1) {
        std::println("unexpected stack depth: {}", state.data().depth());
        return 1;
    }
    std::println("GCD3({}, {}, {}) = {}  (computed at runtime)", a, b, c,
                 state.data().peek().value());

    return (built[0] == 21 && state.data().peek().value() == 54) ? 0 : 1;
}
// 7a5b21ef-9c04-4d38-8b16-e3f70c9a52d1 end
