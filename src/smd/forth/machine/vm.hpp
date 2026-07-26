// src/smd/forth/machine/vm.hpp                                       -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#ifndef SRC_SMD_FORTH_MACHINE_VM_HPP
#define SRC_SMD_FORTH_MACHINE_VM_HPP

#include <smd/forth/foundation/parse_error.hpp>
#include <smd/forth/foundation/result.hpp>
#include <smd/forth/foundation/source_pos.hpp>
#include <smd/forth/machine/cell.hpp>
#include <smd/forth/machine/dictionary.hpp>
#include <smd/forth/machine/forth_state.hpp>
#include <smd/forth/machine/instruction.hpp>
#include <smd/forth/parser/forth_chars.hpp>

#include <string_view>
#include <variant>

namespace smd::forth::machine {

// Step F14 (docs/forth-plan.md): the VM half of the classic stack machine
// (codegen.hpp is the other half). run() below is an explicit loop over an
// instruction pointer (ip), fully constexpr, with a call/return stack --
// Forth convention, and this project's own already-existing forth_state::
// returns() (F8), used here for calls exactly the way F17 will later reuse
// it for loop parameters (see instruction.hpp's own reserved do_setup/
// loop_step/etc. opcodes).
//
// Where F13's eval_direct.hpp threads an eval_signal::exited value up
// through nested C++ call frames to model EXIT, the VM needs nothing of the
// sort: EXIT compiled straight to a literal ret instruction (codegen.hpp),
// and ret's own runtime behavior -- pop a return address off the return
// stack, jump to it -- unwinds for free, because the return stack already
// holds exactly the call chain that needs unwinding. This is the
// "significantly simpler than eval_signal propagation" case
// handoff-next.md's F14 briefing predicted.

/// Decrements @p fuel, the VM's own step budget, diagnosing exhaustion
/// rather than letting a nonterminating compiled program (e.g. `branch0`
/// back to its own start forever) loop the VM's fetch-execute cycle without
/// bound -- the same reason @c eval_direct.hpp's own @c consume_fuel exists,
/// ported to per-instruction accounting: called once per instruction
/// fetched by @ref run, so a budget of @p fuel bounds the total number of
/// instructions (including repeated visits to the same instruction across
/// loop iterations) any one run may execute.
constexpr auto consume_vm_fuel(int &fuel) -> status {
    if (fuel <= 0) {
        return foundation::parse_error{foundation::source_pos{},
                                       "vm execution budget exhausted"};
    }
    --fuel;
    return std::monostate{};
}

// Step F31 (docs/forth-plan-2.md), D11/D18: CATCH and THROW. See DIV-0018
// for the full design record; the shape in brief, because it is the answer
// to Part 11's own recorded-but-unverified return-stack teardown gap (F17):
// a `CATCH` handler frame is *found by depth*, never by scanning. `machine::
// forth_state::handler_depth` (a scalar register alongside `BASE`/`STATE`,
// not a stack slot) always names the return-stack depth *at which* the
// innermost active handler's own 3-cell frame begins. `THROW` reads that
// depth directly and calls @ref cell_stack::truncate once to discard
// *everything* above it -- whatever mix of call frames, `DO`-loop frames,
// and `>R` values the caught execution built, all at once, without ever
// having to identify any individual one of them. This is what makes `CATCH`/
// `THROW` correct regardless of F17's own still-unverified assumption that a
// `DO`-loop's own teardown (`op::loop_step`/`op::leave`/`op::unloop`) always
// finds its own frame at the very top of the return stack: `THROW`'s own
// unwind never relies on that assumption, or on any assumption about what is
// above the target depth -- it simply is not there anymore.
//
// A handler frame's own 3 cells, pushed in this order by @ref op::catch_mark
// (so they are popped in the reverse order below): the handler-depth
// register's own previous value (the next-outer handler, restored when this
// one is consumed either way), the data-stack depth `CATCH` itself recorded
// (restored on a caught `THROW`, ignored on normal completion), and the
// instruction index execution resumes at afterward (used only by a caught
// `THROW`; normal completion resumes there too, but by physically falling
// into it via `op::prim primitive::catch_ok`, emitted right after
// `op::catch_mark` by both of interp.hpp's own `CATCH` cases, not by reading
// this cell).

// 5e8b3a1c-6d2f-4c9e-8b7a-3f1d9c6e2a4b
/// Unwinds to the innermost active `CATCH` handler and pushes @p n, or
/// diagnoses an uncaught `THROW` carrying @p n if `state.handler_depth()` is
/// `-1` (no handler active) -- the one place both @ref op::throw_op and the
/// `ABORT"`/machine-fault-mapping paths in @ref run_from's own `op::prim`
/// case perform the actual unwind, so both stay in exact agreement.
///
/// @p n must be nonzero: @ref op::throw_op's own `n == 0` no-op case (per
/// Forth-2012) is handled by its caller, before this function is ever
/// called.
///
/// On success, mutates @p ip to the resumed instruction index; @p state's
/// data and return stacks, and its own handler-depth register, are restored
/// to exactly what they were when the matching `CATCH` began running its own
/// execution token.
template <int MaxDepth, int MaxRDepth, int MaxData, int MaxOut>
constexpr auto
perform_throw(forth_state<MaxDepth, MaxRDepth, MaxData, MaxOut> &state, cell n,
              int &ip) -> status {
    int const handler = state.handler_depth();
    if (handler < 0) {
        // Uncaught: foundation::parse_error::message must stay a static
        // string literal (it does not own or copy what it points to, per
        // its own doc comment -- the exact wall DIV-0017's own F29 finding
        // named), so the thrown code cannot ride along in the message text.
        // where.offset carries it instead: a deliberate, documented reuse of
        // a field that already means nothing for a condition with no real
        // source position of its own.
        return foundation::parse_error{
            foundation::source_pos{.offset = static_cast<int>(n)},
            "uncaught THROW (code in foundation::parse_error::where.offset)"};
    }
    auto tr = state.returns().truncate(handler + 3);
    if (!tr.has_value()) {
        return tr;
    }
    auto resume_ip = state.returns().pop();
    if (!resume_ip.has_value()) {
        return resume_ip.error();
    }
    auto saved_data_depth = state.returns().pop();
    if (!saved_data_depth.has_value()) {
        return saved_data_depth.error();
    }
    auto prev_handler = state.returns().pop();
    if (!prev_handler.has_value()) {
        return prev_handler.error();
    }
    auto td = state.data().truncate(static_cast<int>(saved_data_depth.value()));
    if (!td.has_value()) {
        return td;
    }
    auto push_r = state.data().push(n);
    if (!push_r.has_value()) {
        return push_r;
    }
    state.set_handler_depth(static_cast<int>(prev_handler.value()));
    ip = static_cast<int>(resume_ip.value());
    return std::monostate{};
}
// 5e8b3a1c-6d2f-4c9e-8b7a-3f1d9c6e2a4b end

/// True iff @p e is specifically `ABORT"`'s own "condition met" failure
/// (`machine::primitive::abort_quote`, `forth_state.hpp`) -- the one
/// primitive failure @ref run_from's own `op::prim` case always routes
/// through `THROW -2` (DIV-0017's revisit, DIV-0018), whether or not a
/// handler is active, rather than only when one is (see @ref
/// machine_fault_throw_code for the "only when caught" ones).
[[nodiscard]] constexpr auto
is_abort_quote_condition(foundation::parse_error const &e) -> bool {
    return e.message != nullptr &&
           std::string_view{e.message} == "ABORT\" condition met";
}

/// Maps a handful of `apply_primitive` diagnoses to their standard
/// Forth-2012 `THROW` code (D7: "machine faults ... mapped to standard
/// THROW codes where sensible"), by matching @p e's own static message text.
/// Unmapped otherwise -- @p e is returned unchanged, and @ref run_from's own
/// `op::prim` case only consults this mapping at all when a handler is
/// already active (`state.handler_depth() >= 0`), so every uncaught
/// diagnosis anywhere in this project keeps its original, more specific
/// message verbatim (DIV-0018 records exactly which faults are mapped and
/// why the rest are not).
[[nodiscard]] constexpr auto
machine_fault_throw_code(foundation::parse_error const &e)
    -> foundation::result<cell> {
    if (e.message != nullptr) {
        std::string_view const msg{e.message};
        if (msg == "stack overflow") {
            return cell{-3}; // Forth-2012: stack overflow.
        }
        if (msg == "stack underflow") {
            return cell{-4}; // Forth-2012: stack underflow.
        }
        if (msg == "division by zero") {
            return cell{-10}; // Forth-2012: division by zero.
        }
        if (msg == "data space address out of bounds") {
            return cell{-9}; // Forth-2012: invalid memory address.
        }
    }
    return e;
}

// 7f3a9c1e-4b8d-4e2a-9c6f-1d8b3a7e5f2c
/// `CREATE`'s own action (step F28, D18/D10): scan the next name off @p
/// state's own SOURCE/>IN, allot @p cells_to_allot cells past the current
/// data-space HERE, and define the scanned name in @p dict as a @ref
/// variable_word at that address, with no does-field yet (@ref
/// variable_word::does_entry defaults to -1; `DOES>` is what later attaches
/// one, via @ref dictionary::attach_does).
///
/// Shared between @ref run_from's own @ref op::create_word case (`CREATE`
/// invoked from inside another word's own compiled body -- a defining word
/// like `: CONSTANT2 CREATE , DOES> @ ;`, once per invocation) and
/// `interpreter::apply_control_word`'s own `create_` case (`CREATE` met
/// directly by the text interpreter, interpreting): both need the identical
/// action, and this is the one place it is written down.
template <int MaxWords, int MaxName, int MaxDepth, int MaxRDepth, int MaxData,
          int MaxOut>
constexpr auto
create_here(dictionary<MaxWords, MaxName> &dict,
            forth_state<MaxDepth, MaxRDepth, MaxData, MaxOut> &state,
            int cells_to_allot) -> status {
    auto scanned = parser::scan_word<MaxName>(state.source().cursor_at_in());
    if (!scanned.has_value()) {
        return scanned.error();
    }
    auto const &folded = scanned.value().value;
    if (folded.empty()) {
        return foundation::parse_error{foundation::source_pos{},
                                       "expected a name after CREATE"};
    }
    state.source().set_in(scanned.value().rest.position().offset);
    std::string_view name_text{folded.begin(),
                               static_cast<std::size_t>(folded.size())};
    auto address = state.data_space().allot(cells_to_allot);
    if (!address.has_value()) {
        return address.error();
    }
    return dict.define_variable(
        name_text, variable_word{.address = address.value(), .does_entry = -1});
}
// 7f3a9c1e-4b8d-4e2a-9c6f-1d8b3a7e5f2c end

// e2a7c9f4-5d1b-4e8a-9c3f-7b2d6a4e1f8c
/// Runs @p program against @p state, starting at @p entry (an arbitrary
/// instruction index inside @p program -- not necessarily @p
/// program.program_entry) and executing until @ref op::halt or a diagnosed
/// error, *without* seeding @p state's own data space first: see @ref run,
/// which does seed it and is the entry point ordinary callers want.
///
/// Step F25 (docs/forth-plan-2.md) adds this entry point for the colon
/// compiler's own interpreter loop (`interpreter::compile_buffer::
/// call_word`), which calls into an already-compiled word's body against a
/// *live*, already-running @p state, potentially many times in a single
/// session (once per top-level reference to a previously defined word).
/// @ref run's own data-space seeding step is correct exactly once, before a
/// whole compiled_program's own top-level execution starts (F16, D10); it
/// would be wrong to repeat on every single interpreted word call, since
/// each call would `allot` @p program.data_space_size more cells,
/// unconditionally, growing the live data space without bound. @ref run is
/// now implemented in terms of this function -- it seeds, then delegates to
/// `run_from(program, state, program.program_entry, fuel)` -- so its own
/// signature and every observable behavior are unchanged (D16: the VM loop
/// is retained, not frozen; only the existing opcodes' semantics and @ref
/// run's own existing entry-point behavior must not change, and neither
/// has).
///
/// Every @ref op enumerator now has real semantics, handled below: @ref
/// op::push/@ref op::push_xt/@ref op::prim/@ref op::call/@ref op::ret/@ref
/// op::branch/@ref op::branch0/@ref op::halt (step F14), @ref op::do_setup/
/// @ref op::loop_step/@ref op::plus_loop_step/@ref op::push_index/@ref
/// op::leave/@ref op::unloop (step F17), @ref op::execute/@ref
/// op::create_word/@ref op::does_enter (step F28), and @ref op::catch_mark/
/// @ref op::throw_op (step F31, D11 -- @ref perform_throw is the shared
/// unwind logic both this switch's own `op::throw_op` case and its
/// `op::prim` case's machine-fault mapping call into).
///
/// @p dict is step F28's own addition (D18): `nullptr` by default, exactly
/// as every caller before this step already implicitly assumed (none of
/// them ever reached an opcode that would need it). Only @ref op::
/// create_word and @ref op::does_enter consult it -- both are how `CREATE`/
/// `DOES>` (`interp.hpp`'s own `compile_entry`) reach the dictionary from
/// *inside* another word's own compiled body (the classic `: CONSTANT2
/// CREATE , DOES> @ ;` pattern, DIV-0012's own reason F28 could not defer
/// folding `SOURCE`/`>IN` into @ref forth_state any further): a colon word's
/// body is ordinary VM bytecode with no other channel back to the
/// dictionary, so @p dict is this function's own one deliberate exception to
/// "the VM only ever sees a forth_state" -- a non-owning, nullable pointer
/// (mirroring @ref dictionary::lookup's own "maybe" convention), never
/// stored past this call. Passing `nullptr` while running a program that
/// does reach one of these two opcodes is diagnosed, not UB.
///
/// @tparam MaxCode      @p program's instruction-array capacity.
/// @tparam MaxWords     @p program's word-table capacity.
/// @tparam StackDepth   @p state's data stack capacity.
/// @tparam RStackDepth  @p state's return stack capacity -- also the
///                      call/return stack @ref op::call/@ref op::ret share,
///                      per Forth convention (this header's own top
///                      comment).
/// @tparam MaxData      @p state's data-space capacity.
/// @tparam MaxOut       @p state's output-buffer capacity.
/// @tparam DictWords    @p dict's own entry capacity, if given.
/// @tparam DictName     @p dict's own maximum name length, if given.
/// @param  program A successfully compiled program (@ref codegen), or any
///                  other @ref compiled_program whose @c code array @p entry
///                  indexes into.
/// @param  state   The machine state to run against; mutated in place.
/// @param  entry   The instruction index to start execution at.
/// @param  fuel    The execution step budget: decremented once per
///                 instruction fetched (@ref consume_vm_fuel); exhaustion is
///                 a diagnosed error, never a hang, even for a
///                 nonterminating loop.
/// @param  dict    The dictionary @ref op::create_word/@ref op::does_enter
///                  mutate, or `nullptr` if @p program is known not to reach
///                  either (every caller before step F28).
template <int MaxCode, int MaxWords, int StackDepth, int RStackDepth,
          int MaxData, int MaxOut, int DictWords = 256, int DictName = 32>
constexpr auto
run_from(compiled_program<MaxCode, MaxWords> const &program,
         forth_state<StackDepth, RStackDepth, MaxData, MaxOut> &state,
         int entry, int fuel = 100000,
         dictionary<DictWords, DictName> *dict = nullptr) -> status {
    int ip = entry;

    for (;;) {
        if (ip < 0 || ip >= program.code.size()) {
            return foundation::parse_error{foundation::source_pos{},
                                           "instruction pointer out of range"};
        }
        auto budget = consume_vm_fuel(fuel);
        if (!budget.has_value()) {
            return budget;
        }

        instr const &in = program.code[ip];
        switch (in.code) {
        case op::push:
        case op::push_xt: {
            auto r = state.data().push(in.operand);
            if (!r.has_value()) {
                return r;
            }
            ++ip;
            break;
        }
        case op::prim: {
            auto r = apply_primitive(static_cast<primitive>(in.operand), state);
            if (!r.has_value()) {
                // ABORT" (step F31, DIV-0017's revisit/DIV-0018): its own
                // "condition met" failure always means THROW -2, whether or
                // not a handler is active -- perform_throw itself produces
                // the correct uncaught diagnosis when handler_depth() < 0.
                if (is_abort_quote_condition(r.error())) {
                    auto th = perform_throw(state, cell{-2}, ip);
                    if (!th.has_value()) {
                        return th;
                    }
                    break;
                }
                // Every other primitive-level machine fault (stack
                // over/underflow, division by zero, an out-of-bounds data-
                // space address) is mapped to its standard THROW code only
                // when a handler is active (D7): with no CATCH anywhere,
                // every existing diagnosis keeps its original, more
                // specific message verbatim -- see machine_fault_throw_code
                // and DIV-0018.
                if (state.handler_depth() >= 0) {
                    auto mapped = machine_fault_throw_code(r.error());
                    if (mapped.has_value()) {
                        auto th = perform_throw(state, mapped.value(), ip);
                        if (!th.has_value()) {
                            return th;
                        }
                        break;
                    }
                }
                return r;
            }
            ++ip;
            break;
        }
        case op::call: {
            auto r = state.returns().push(static_cast<cell>(ip + 1));
            if (!r.has_value()) {
                return r.error();
            }
            ip = static_cast<int>(in.operand);
            break;
        }
        case op::ret: {
            auto r = state.returns().pop();
            if (!r.has_value()) {
                return r.error();
            }
            ip = static_cast<int>(r.value());
            break;
        }
        case op::branch: {
            ip = static_cast<int>(in.operand);
            break;
        }
        case op::branch0: {
            auto flag = state.data().pop();
            if (!flag.has_value()) {
                return flag.error();
            }
            if (flag.value() == 0) {
                ip = static_cast<int>(in.operand);
            } else {
                ++ip;
            }
            break;
        }
        case op::halt:
            return std::monostate{};
        case op::do_setup: {
            // ( limit start -- ) ( R: -- limit index ); index (=start) on top
            // so I/J read it by offset and loops nest correctly. Shares the
            // return stack with call frames -- the frame sits above the
            // caller's return address and is torn down before the matching
            // ret.
            auto start = state.data().pop();
            if (!start.has_value()) {
                return start.error();
            }
            auto limit = state.data().pop();
            if (!limit.has_value()) {
                return limit.error();
            }
            if (auto r = state.returns().push(limit.value()); !r.has_value()) {
                return r;
            }
            if (auto r = state.returns().push(start.value()); !r.has_value()) {
                return r;
            }
            ++ip;
            break;
        }
        case op::push_index: {
            // `I`/`J`: read the level-L loop index at return-stack offset 2*L.
            int const level = static_cast<int>(in.operand);
            auto v = state.returns().peek(2 * level);
            if (!v.has_value()) {
                return v.error();
            }
            auto r = state.data().push(v.value());
            if (!r.has_value()) {
                return r;
            }
            ++ip;
            break;
        }
        case op::loop_step: {
            // `LOOP`: index += 1; terminate when it reaches the limit.
            auto index = state.returns().pop();
            if (!index.has_value()) {
                return index.error();
            }
            auto limit = state.returns().peek(0);
            if (!limit.has_value()) {
                return limit.error();
            }
            cell const next = index.value() + 1;
            if (next == limit.value()) {
                if (auto r = state.returns().pop(); !r.has_value()) {
                    return r.error();
                }
                ++ip;
            } else {
                if (auto r = state.returns().push(next); !r.has_value()) {
                    return r;
                }
                ip = static_cast<int>(in.operand);
            }
            break;
        }
        // 2d8b6f4a-7c1e-4b93-9a5d-6e0f3c8a1d54
        case op::plus_loop_step: {
            // `+LOOP`: pop the increment; index += n; terminate when the index
            // crosses the boundary between limit-1 and limit (Forth-2012),
            // i.e. when the sign of (index - limit) flips.
            auto incr = state.data().pop();
            if (!incr.has_value()) {
                return incr.error();
            }
            auto index = state.returns().pop();
            if (!index.has_value()) {
                return index.error();
            }
            auto limit = state.returns().peek(0);
            if (!limit.has_value()) {
                return limit.error();
            }
            cell const before = index.value() - limit.value();
            cell const next = index.value() + incr.value();
            cell const after = next - limit.value();
            if (((before ^ after) < 0)) {
                if (auto r = state.returns().pop(); !r.has_value()) {
                    return r.error();
                }
                ++ip;
            } else {
                if (auto r = state.returns().push(next); !r.has_value()) {
                    return r;
                }
                ip = static_cast<int>(in.operand);
            }
            break;
        }
        // 2d8b6f4a-7c1e-4b93-9a5d-6e0f3c8a1d54 end
        case op::leave: {
            // `LEAVE`: discard this loop's frame (index then limit) and branch
            // past the loop.
            if (auto r = state.returns().pop(); !r.has_value()) {
                return r.error();
            }
            if (auto r = state.returns().pop(); !r.has_value()) {
                return r.error();
            }
            ip = static_cast<int>(in.operand);
            break;
        }
        case op::unloop: {
            // `UNLOOP`: discard this loop's frame without branching, leaving
            // the return stack as it was before do_setup (so a following ret
            // pops the real return address).
            if (auto r = state.returns().pop(); !r.has_value()) {
                return r.error();
            }
            if (auto r = state.returns().pop(); !r.has_value()) {
                return r.error();
            }
            ++ip;
            break;
        }
        case op::execute: {
            // `EXECUTE` (step F28, D18): pop an execution token -- a
            // code-space instruction index, @ref op::push_xt's own
            // convention -- and jump to it exactly like @ref op::call does
            // (push the return address, then jump). No dictionary lookup:
            // whatever produced the token on the data stack (`'`, `[']`,
            // `IS`) already resolved it to a real, `ret`-terminated code-space
            // location.
            auto target = state.data().pop();
            if (!target.has_value()) {
                return target.error();
            }
            auto r = state.returns().push(static_cast<cell>(ip + 1));
            if (!r.has_value()) {
                return r.error();
            }
            ip = static_cast<int>(target.value());
            break;
        }
        case op::create_word: {
            // `CREATE` (step F28, D18/D10): scan the next name off @p
            // state's own SOURCE/>IN (folded into forth_state at this same
            // step, DIV-0012) and define it in @p dict as a variable_word at
            // the current data-space HERE, allotting @ref instr::operand
            // cells (0 for `CREATE` itself; nothing yet reuses this opcode
            // for `VARIABLE`, which keeps its own step F26 direct-name
            // install). Needs @p dict because this opcode is how a defining
            // word like `: CONSTANT2 CREATE , DOES> @ ;` reaches the
            // dictionary from *inside* its own compiled body, once per
            // invocation -- not once, at CONSTANT2's own definition time.
            if (dict == nullptr) {
                return foundation::parse_error{
                    foundation::source_pos{},
                    "CREATE: no dictionary available to this VM run"};
            }
            auto def_r =
                create_here(*dict, state, static_cast<int>(in.operand));
            if (!def_r.has_value()) {
                return def_r;
            }
            ++ip;
            break;
        }
        case op::does_enter: {
            // `DOES>` (step F28, D18/D10): attach the instruction right
            // after this one as the most recently defined (i.e. most
            // recently `CREATE`d) dictionary entry's own does-field, then
            // act like @ref op::ret -- ending the *defining* word's own
            // execution here, per Forth-2012, so the code after `DOES>`
            // never runs as part of defining the new word, only later, as
            // that new word's own action.
            if (dict == nullptr) {
                return foundation::parse_error{
                    foundation::source_pos{},
                    "DOES>: no dictionary available to this VM run"};
            }
            auto attach_r = dict->attach_does(ip + 1);
            if (!attach_r.has_value()) {
                return attach_r;
            }
            auto ret_addr = state.returns().pop();
            if (!ret_addr.has_value()) {
                return ret_addr.error();
            }
            ip = static_cast<int>(ret_addr.value());
            break;
        }
        // c4a7e9d2-3f6b-4e1a-9c8d-5b2f7a4e6c1d
        case op::catch_mark: {
            // `CATCH` (step F31, D11/D18): xt CATCH ( xt -- 0 | n ). Pops
            // the execution token, pushes a 3-cell handler frame [prev
            // handler-depth, saved data-stack depth, resume ip] onto the
            // return stack (`in.operand` is the resume ip -- either
            // interp.hpp's own compiled or interpreting CATCH case computes
            // it, see each one's own comment), points handler_depth() at
            // this frame's own base, then pushes this instruction's own
            // return address (`ip + 1`, where `prim primitive::catch_ok` --
            // the normal-completion epilogue -- always sits, emitted right
            // after this instruction by both of interp.hpp's own CATCH
            // cases) and jumps to the token, exactly like @ref op::execute.
            // See this header's own top comment (by perform_throw) for how
            // this composes with @ref op::throw_op to unwind correctly
            // through an arbitrary mix of call/DO-loop/`>R`/handler frames
            // sharing this same return stack.
            auto xt = state.data().pop();
            if (!xt.has_value()) {
                return xt.error();
            }
            int const frame_base = state.returns().depth();
            int const saved_data_depth = state.data().depth();
            int const prev_handler = state.handler_depth();
            if (auto r = state.returns().push(static_cast<cell>(prev_handler));
                !r.has_value()) {
                return r;
            }
            if (auto r =
                    state.returns().push(static_cast<cell>(saved_data_depth));
                !r.has_value()) {
                return r;
            }
            if (auto r = state.returns().push(in.operand); !r.has_value()) {
                return r;
            }
            state.set_handler_depth(frame_base);
            if (auto r = state.returns().push(static_cast<cell>(ip + 1));
                !r.has_value()) {
                return r;
            }
            ip = static_cast<int>(xt.value());
            break;
        }
        case op::throw_op: {
            // `THROW` (step F31, D11): n THROW ( n -- ). Zero is a no-op
            // per Forth-2012; otherwise unwind to the innermost active
            // handler (@ref perform_throw), or diagnose an uncaught THROW
            // carrying n.
            auto n = state.data().pop();
            if (!n.has_value()) {
                return n.error();
            }
            if (n.value() == 0) {
                ++ip;
                break;
            }
            auto r = perform_throw(state, n.value(), ip);
            if (!r.has_value()) {
                return r;
            }
            break;
        }
            // c4a7e9d2-3f6b-4e1a-9c8d-5b2f7a4e6c1d end
        }
    }
}
// e2a7c9f4-5d1b-4e8a-9c3f-7b2d6a4e1f8c end

// 3b356d6c-c4c1-4676-b16a-48e975b5d46b
/// Runs @p program against @p state, starting at @p program.program_entry
/// and executing until @ref op::halt or a diagnosed error -- the entry point
/// ordinary callers want (a whole compiled program's own top-level run).
///
/// Before delegating to @ref run_from, @p state's own data space is seeded
/// from @p program.data_space_size (F16, D10) -- see this function's own
/// leading comment -- so `@`/`!`/`+!` against a `VARIABLE`/`CREATE` address
/// codegen already inlined as a push literal, and any address a later
/// runtime `ALLOT` extends past it, are both valid from the first
/// instruction onward.
///
/// @tparam MaxCode      @p program's instruction-array capacity.
/// @tparam MaxWords     @p program's word-table capacity.
/// @tparam StackDepth   @p state's data stack capacity.
/// @tparam RStackDepth  @p state's return stack capacity.
/// @tparam MaxData      @p state's data-space capacity.
/// @tparam MaxOut       @p state's output-buffer capacity.
/// @param  program A successfully compiled program (@ref codegen).
/// @param  state   The machine state to run against; mutated in place.
/// @param  fuel    The execution step budget; see @ref run_from.
template <int MaxCode, int MaxWords, int StackDepth, int RStackDepth,
          int MaxData, int MaxOut>
constexpr auto run(compiled_program<MaxCode, MaxWords> const &program,
                   forth_state<StackDepth, RStackDepth, MaxData, MaxOut> &state,
                   int fuel = 100000) -> status {
    // f1514ad1-894c-4812-9ccc-2bdecd54a986
    // F16: seed state's own data space with program.data_space_size -- the
    // high-water mark the source compiled_unit's data space reached during
    // elaboration (every VARIABLE/CREATE allotment) -- so the addresses
    // codegen already inlined as push literals are valid for @/!/+! from the
    // first instruction onward. Reuses allot itself (discarding the
    // returned address) rather than adding a second way to advance the
    // high-water mark, exactly like eval_direct.hpp's eval_program does for
    // the direct evaluator.
    auto data_init = state.data_space().allot(program.data_space_size);
    if (!data_init.has_value()) {
        return data_init.error();
    }
    // f1514ad1-894c-4812-9ccc-2bdecd54a986 end

    return run_from(program, state, program.program_entry, fuel);
}
// 3b356d6c-c4c1-4676-b16a-48e975b5d46b end

} // namespace smd::forth::machine

#endif
