// src/smd/forth/machine/instruction.hpp                              -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#ifndef SRC_SMD_FORTH_MACHINE_INSTRUCTION_HPP
#define SRC_SMD_FORTH_MACHINE_INSTRUCTION_HPP

#include <smd/forth/foundation/static_vector.hpp>
#include <smd/forth/machine/cell.hpp>

#include <cstdint>
#include <type_traits>

namespace smd::forth::machine {

// Step F14 (docs/forth-plan.md): the classic stack machine. Where F13's
// eval_direct.hpp walks the elaborated core directly by structural
// recursion, F14 flattens that same core into a linear instruction array
// first (codegen.hpp), then executes the array with an explicit
// instruction-pointer loop (vm.hpp) -- the same two-stage shape a real
// bytecode interpreter uses, still fully constexpr.

// 5b9e9c1a-6b0e-4b7a-9f3a-7a5b7c9d2e1f
/// One stack-machine opcode.
///
/// Only the opcodes this step's own codegen (`codegen.hpp`) actually emits
/// and this step's own VM (`vm.hpp`) actually executes have real semantics
/// yet: `push`, `prim`, `call`, `ret`, `branch`, `branch0`, `push_xt`,
/// `halt`. The remaining nine enumerators reserve opcode space for later
/// steps per the plan's own instruction-set shape, so the enum itself never
/// has to change once those steps land: `do_setup`, `loop_step`,
/// `plus_loop_step`, `push_index` (`I`/`J`), `leave`, and `unloop` are all
/// `DO ... LOOP` machinery (step F17's own deliverable -- F14's codegen
/// diagnoses `core_do_loop` as "not implemented" rather than emitting any of
/// these, mirroring F13's identical choice for the direct evaluator); and
/// `execute` (`EXECUTE`), `catch_mark`, and `throw_op` are step F18a's
/// (execution tokens and exceptions). The VM diagnoses any of these nine if
/// it ever encounters one in a compiled program, rather than treating an
/// unimplemented opcode as undefined behavior (D7) -- codegen simply never
/// emits them yet, so this path is defensive, not currently reachable from
/// `codegen(compiled_unit)`.
///
/// `branch0` pops a flag and branches only when that flag is zero (Forth's
/// "false" -- see `cell.hpp`'s `flag_false`/`flag_true`); this is the
/// opposite polarity from an "if true, branch" instruction, chosen because
/// it is the natural encoding for both `IF`'s "skip the `THEN` arm when the
/// condition is false" and `BEGIN ... UNTIL`'s "loop back while the flag is
/// still false."
enum class op : std::uint8_t {
    push,           ///< Push @ref instr::operand onto the data stack.
    prim,           ///< Invoke the primitive named by @ref instr::operand
                    ///< (cast from @ref instr::operand) via @ref
                    ///< apply_primitive.
    call,           ///< Push the return address (next instruction index)
                    ///< onto the return stack, then jump to @ref
                    ///< instr::operand.
    ret,            ///< Pop a return address off the return stack and jump
                    ///< to it.
    branch,         ///< Unconditionally jump to @ref instr::operand.
    branch0,        ///< Pop a flag; jump to @ref instr::operand if it is
                    ///< zero (Forth false), else fall through.
    do_setup,       ///< Reserved for step F17 (`DO`): not emitted yet.
    loop_step,      ///< Reserved for step F17 (`LOOP`): not emitted yet.
    plus_loop_step, ///< Reserved for step F17 (`+LOOP`): not emitted yet.
    push_index,     ///< Reserved for step F17 (`I`/`J`): not emitted yet.
    leave,          ///< Reserved for step F17 (`LEAVE`): not emitted yet.
    unloop,         ///< Reserved for step F17 (`UNLOOP`): not emitted yet.
    push_xt,        ///< Push @ref instr::operand (a dictionary word index)
                    ///< onto the data stack as an execution token (D11) --
                    ///< the elaborated form of `' NAME`. Distinct from
                    ///< @ref push only for documentation/extensibility;
                    ///< runtime behavior is identical.
    execute,        ///< Reserved for step F18a (`EXECUTE`): not emitted yet.
    catch_mark,     ///< Reserved for step F18a (`CATCH`): not emitted yet.
    throw_op,       ///< Reserved for step F18a (`THROW`): not emitted yet.
    halt,           ///< Stop the VM's fetch-execute loop successfully.
};

/// One flat instruction: an opcode plus a single immediate @ref cell
/// operand. Every operand's meaning is opcode-dependent: an instruction
/// index for `call`/`branch`/`branch0`, a literal value for `push`, a
/// `primitive` enumerator (cast to @ref cell) for `prim`, a dictionary word
/// index for `push_xt`, and unused (left zero) for `ret`/`halt`.
///
/// A plain aggregate, trivially copyable and a literal type by construction
/// -- there is nothing here for the compiler to have to prove trivial.
struct instr {
    op code{};      ///< Which operation to perform.
    cell operand{}; ///< The operation's single immediate operand.
};
// 5b9e9c1a-6b0e-4b7a-9f3a-7a5b7c9d2e1f end

// c520a0cc-7a47-4db4-a218-617a1cfebe8d
/// The compiled artifact codegen produces and the VM runs: a flat
/// instruction array plus everything a caller needs to run it, bundled so it
/// survives as one self-contained value from compile time to runtime (the
/// plan's own words: "it is THE artifact that survives to runtime").
///
/// @tparam MaxCode  Instruction-array capacity (@ref code).
/// @tparam MaxWords Word-table capacity (@ref entry_points); shares its
///                  value with whatever `elaborator::compiled_unit`'s own
///                  `MaxWords` was when @ref codegen built this program, so
///                  every dictionary index @ref entry_points might be
///                  indexed by is always in range.
///
/// Word-table shape (a design choice this step had to make -- the plan only
/// says "entry point per word"): @ref entry_points is a plain
/// `foundation::static_vector<int, MaxWords>`, one slot per dictionary
/// entry in dictionary-index order, exactly mirroring how
/// `machine::dictionary` itself is a flat, index-addressed structure. A
/// colon word's slot holds the instruction index its body starts at; every
/// other binding kind (primitive, variable, constant, foreign) has no
/// instruction-level entry point of its own -- primitives are inlined as
/// `prim` at each reference site, variables/constants as `push`, so their
/// slot holds `-1`.
///
/// "Required stack capacities" (the plan's own phrase): step F12 computes
/// each colon definition's *net* data-stack effect and minimum entry depth,
/// not a whole-program running peak, so this step does not attempt real
/// peak-depth analysis (see `docs/divergences/DIV-0008-*.md`).
/// @ref required_stack_depth and @ref required_return_depth are carried as
/// the struct shape the plan calls for, but default to `-1` ("not
/// computed") rather than a real bound; a caller must still size its own
/// `forth_state` generously, exactly as F13's own tests already do.
template <int MaxCode = 4096, int MaxWords = 256>
struct compiled_program {
    foundation::static_vector<instr, MaxCode> code{}; ///< The flat program.

    /// Entry-point instruction index for dictionary word `i`, or `-1` if
    /// word `i` is not a colon word.
    foundation::static_vector<int, MaxWords> entry_points{};

    /// Instruction index where the top-level program body begins -- the
    /// VM's own "main," run after every colon definition's own code (which
    /// only ever runs when called).
    int program_entry = 0;

    /// Cells the source `compiled_unit`'s data space had actually allotted
    /// (`VARIABLE`/`CREATE`) by the time codegen ran. F16's own @ref run
    /// consumes this at the start of every run to seed a fresh
    /// @ref forth_state's own data space before executing any instruction.
    int data_space_size = 0;

    /// See the class doc comment: not computed by this step.
    int required_stack_depth = -1;
    /// See the class doc comment: not computed by this step.
    int required_return_depth = -1;
};
// c520a0cc-7a47-4db4-a218-617a1cfebe8d end

namespace detail {

// compiled_program must be a literal type and trivially copyable (the
// plan's own words) -- it is what survives from a constexpr codegen call to
// an ordinary-runtime VM call. Checked against one concrete instantiation
// (the header's own defaults) as a representative sample, exactly like
// elaborated_core.hpp's and dictionary.hpp's own trivially-destructible
// checks.
static_assert(std::is_trivially_destructible_v<instr>);
static_assert(std::is_trivially_copyable_v<instr>);
static_assert(std::is_trivially_destructible_v<compiled_program<>>);
static_assert(std::is_trivially_copyable_v<compiled_program<>>);

} // namespace detail

} // namespace smd::forth::machine

#endif
