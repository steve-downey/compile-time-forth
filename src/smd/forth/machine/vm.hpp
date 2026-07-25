// src/smd/forth/machine/vm.hpp                                       -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#ifndef SRC_SMD_FORTH_MACHINE_VM_HPP
#define SRC_SMD_FORTH_MACHINE_VM_HPP

#include <smd/forth/foundation/parse_error.hpp>
#include <smd/forth/foundation/result.hpp>
#include <smd/forth/foundation/source_pos.hpp>
#include <smd/forth/machine/cell.hpp>
#include <smd/forth/machine/forth_state.hpp>
#include <smd/forth/machine/instruction.hpp>

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

// 3b356d6c-c4c1-4676-b16a-48e975b5d46b
/// Runs @p program against @p state, starting at @p program.program_entry
/// and executing until @ref op::halt or a diagnosed error.
///
/// Every opcode @ref op currently gives real semantics to (@ref op::push,
/// @ref op::push_xt, @ref op::prim, @ref op::call, @ref op::ret, @ref
/// op::branch, @ref op::branch0, @ref op::halt) is handled below; the nine
/// opcodes reserved for steps F17/F18a (@ref instruction.hpp's own doc
/// comment lists them) are diagnosed if ever encountered rather than
/// invoking undefined behavior (D7) -- unreachable in practice, since this
/// step's own @ref codegen never emits any of them.
///
/// Before the fetch-execute loop starts, @p state's own data space is seeded
/// from @p program.data_space_size (F16, D10) -- see this function's own
/// leading comment -- so `@`/`!`/`+!` against a `VARIABLE`/`CREATE` address
/// codegen already inlined as a push literal, and any address a later
/// runtime `ALLOT` extends past it, are both valid from the first
/// instruction onward.
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
/// @param  program A successfully compiled program (@ref codegen).
/// @param  state   The machine state to run against; mutated in place.
/// @param  fuel    The execution step budget: decremented once per
///                 instruction fetched (@ref consume_vm_fuel); exhaustion is
///                 a diagnosed error, never a hang, even for a
///                 nonterminating loop.
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

    int ip = program.program_entry;

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
        case op::execute:
        case op::catch_mark:
        case op::throw_op:
            return foundation::parse_error{
                foundation::source_pos{},
                "execution-token/exception opcode not implemented until "
                "F18a"};
        }
    }
}
// 3b356d6c-c4c1-4676-b16a-48e975b5d46b end

} // namespace smd::forth::machine

#endif
