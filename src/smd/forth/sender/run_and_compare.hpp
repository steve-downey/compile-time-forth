// src/smd/forth/sender/run_and_compare.hpp                          -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#ifndef INCLUDED_SMD_FORTH_SENDER_RUN_AND_COMPARE
#define INCLUDED_SMD_FORTH_SENDER_RUN_AND_COMPARE

#include <smd/forth/interpreter/compilebuf.hpp>
#include <smd/forth/interpreter/interp.hpp>
#include <smd/forth/machine/cell.hpp>
#include <smd/forth/machine/dictionary.hpp>
#include <smd/forth/machine/forth_state.hpp>
#include <smd/forth/sender/lower.hpp>

#include <initializer_list>
#include <string_view>
#include <variant>

/// Step F33 (docs/forth-plan-2.md), D14: shared test support for this
/// component's own sharded `.test.cpp` files -- one small place to compile a
/// word once and run it through *both* executors (`machine::run_from`, via
/// `interpreter::call_word`'s own halt-pad convention, and this component's
/// own `run_from_via_senders`) against otherwise-identical fresh
/// `forth_state`s, so every shard can assert D14's own merge criterion
/// directly ("the same instruction range, run a different way, must reach
/// the same final state") rather than re-deriving the comparison by hand
/// per test. Mirrors `interpreter::corpus`'s own role (shared corpus text,
/// not itself a test) -- this header holds shared comparison *machinery*
/// instead, since the corpus text this component's own tests exercise is
/// mostly `interpreter::corpus`'s own existing F27/F28/F31 battery.
namespace smd::forth::sender::testing {

/// The result of running one word through both executors, each against its
/// own fresh, otherwise-identical `forth_state` (@ref compile_and_run_both).
template <int MaxDepth, int MaxRDepth, int MaxData, int MaxOut>
struct dual_run_result {
    machine::status vm_status{std::monostate{}};
    machine::status sender_status{std::monostate{}};
    machine::forth_state<MaxDepth, MaxRDepth, MaxData, MaxOut> vm_state{};
    machine::forth_state<MaxDepth, MaxRDepth, MaxData, MaxOut> sender_state{};
};

/// True iff @p a and @p b agree on everything D14's own "identical final
/// states" merge criterion can mean for two `forth_state`s: data-stack
/// depth and contents (bottom to top), and captured output. Deliberately
/// does *not* compare return-stack contents (this lowering's own native
/// `CATCH`/`call` never materialize the same return-stack shape the VM
/// does -- DIV-0026/DIV-0027's own record -- so equal return-stack *shape*
/// was never part of what the two executors promise to agree on; only the
/// externally observable state a Forth program can itself read is).
template <int MaxDepth, int MaxRDepth, int MaxData, int MaxOut>
[[nodiscard]] constexpr auto
states_agree(machine::forth_state<MaxDepth, MaxRDepth, MaxData, MaxOut> const &a,
            machine::forth_state<MaxDepth, MaxRDepth, MaxData, MaxOut> const &b)
    -> bool {
    if (a.data().depth() != b.data().depth()) {
        return false;
    }
    for (int i = 0; i < a.data().depth(); ++i) {
        if (a.data().peek(i).value() != b.data().peek(i).value()) {
            return false;
        }
    }
    if (a.output().size() != b.output().size()) {
        return false;
    }
    for (int i = 0; i < a.output().size(); ++i) {
        if (a.output()[i] != b.output()[i]) {
            return false;
        }
    }
    return true;
}

/// Compiles @p definitions (one or more `: ... ;` forms, no trailing
/// invocation -- callers push their own arguments via @p args), looks up
/// @p word_name, and runs it from a fresh `forth_state` seeded with
/// @p args (bottom to top) through both executors: `machine::run_from` via
/// `interpreter::call_word` (the VM, entered exactly the way any ordinary
/// top-level word invocation is -- see `call_word`'s own doc comment for
/// why a bare `run_from` at a word's own entry point is not this), and
/// this component's own `run_from_via_senders`, seeded and driven
/// identically. A compile failure aborts via `assert`-shaped failure
/// (rare in practice: every call site here compiles a fixed, hand-written
/// source string) rather than participating in the returned comparison,
/// since a compile-time failure is not itself part of what D14's own
/// merge criterion is about.
template <int MaxDepth = 64, int MaxRDepth = 64, int MaxData = 1024,
          int MaxOut = 512, int MaxCode = 4096, int MaxWords = 256,
          int MaxName = 32>
[[nodiscard]] auto
compile_and_run_both(std::string_view definitions, std::string_view word_name,
                     std::initializer_list<machine::cell> args = {},
                     int fuel = 100000)
    -> dual_run_result<MaxDepth, MaxRDepth, MaxData, MaxOut> {
    machine::forth_state<MaxDepth, MaxRDepth, MaxData, MaxOut> defst{
        definitions};
    auto dict = machine::default_dictionary<MaxWords, MaxName>();
    interpreter::compile_buffer<MaxCode, MaxWords> buf;
    auto compiled = interpreter::interpret(defst, dict, buf);
    if (!compiled.has_value()) {
        return dual_run_result<MaxDepth, MaxRDepth, MaxData, MaxOut>{
            .vm_status = compiled.error(), .sender_status = compiled.error()};
    }
    auto const *entry = dict.lookup(word_name);
    if (entry == nullptr) {
        auto const missing = foundation::parse_error{
            foundation::source_pos{}, "run_and_compare: word not found"};
        return dual_run_result<MaxDepth, MaxRDepth, MaxData, MaxOut>{
            .vm_status = missing, .sender_status = missing};
    }
    auto const *cw =
        std::get_if<machine::compiled_colon_word>(&entry->binding);
    if (cw == nullptr) {
        auto const not_colon = foundation::parse_error{
            foundation::source_pos{}, "run_and_compare: not a colon word"};
        return dual_run_result<MaxDepth, MaxRDepth, MaxData, MaxOut>{
            .vm_status = not_colon, .sender_status = not_colon};
    }
    int const entry_point = cw->entry_point;

    dual_run_result<MaxDepth, MaxRDepth, MaxData, MaxOut> result{};
    for (machine::cell a : args) {
        auto pushed = result.vm_state.data().push(a);
        (void)pushed;
        pushed = result.sender_state.data().push(a);
        (void)pushed;
    }
    int vm_fuel = fuel;
    result.vm_status = interpreter::call_word(buf, result.vm_state,
                                              entry_point, vm_fuel, &dict);
    result.sender_status = run_from_via_senders(
        buf.program(), result.sender_state, entry_point, fuel, &dict);
    return result;
}

} // namespace smd::forth::sender::testing

#endif // INCLUDED_SMD_FORTH_SENDER_RUN_AND_COMPARE
