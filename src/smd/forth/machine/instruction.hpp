// src/smd/forth/machine/instruction.hpp                              -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#ifndef SRC_SMD_FORTH_MACHINE_INSTRUCTION_HPP
#define SRC_SMD_FORTH_MACHINE_INSTRUCTION_HPP

#include <smd/forth/foundation/parse_error.hpp>
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
/// Every enumerator now has real semantics, given by `vm.hpp`'s own
/// `run_from`: `push`, `prim`, `call`, `ret`, `branch`, `branch0`, `push_xt`,
/// `halt` (step F14); `do_setup`, `loop_step`, `plus_loop_step`, `push_index`
/// (`I`/`J`), `leave`, `unloop` (step F17's own `DO ... LOOP` machinery);
/// `execute`, `create_word`, `does_enter` (step F28, D18/D10); and
/// `catch_mark`/`throw_op` (step F31, D11 -- `CATCH`/`THROW`; see `vm.hpp`'s
/// own `perform_throw` for the design). `run_from`'s own fetch-execute loop
/// still diagnoses any opcode it cannot dispatch on, rather than treating
/// one as undefined behavior (D7), but that path is defensive only:
/// `interp.hpp`'s own `compile_entry`/`apply_control_word` are the sole
/// producers of `instr` values in this project, and both only ever emit
/// opcodes from this same enum.
///
/// `execute`, `create_word`, and `does_enter` are step F28's own additions
/// (D18, D10): see each enumerator's own doc comment; `interp.hpp`'s
/// `compile_entry` is what actually emits them (`EXECUTE`/`CREATE`/`DOES>`
/// respectively).
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
    push_xt,        ///< Push @ref instr::operand onto the data stack as an
                    ///< execution token (step F28, D18): a code-space
                    ///< instruction index that @ref execute can later jump
                    ///< to exactly like @ref call would -- the compiled form
                    ///< of `[']` (`interp.hpp`'s own `resolve_execution_
                    ///< token`). Distinct from @ref push only for
                    ///< documentation; runtime behavior is identical.
    execute,        ///< Pop an execution token (an instruction index, @ref
                    ///< push_xt's own convention) and jump to it exactly as
                    ///< @ref call would (push the return address, then jump)
                    ///< -- step F28's own `EXECUTE` (D18). No dictionary
                    ///< lookup: the popped value already *is* a code-space
                    ///< address, resolved once by whatever produced it
                    ///< (`'`/`[']`/`IS`).
    create_word,    ///< `CREATE`/step F28's own `does_entry`-aware
                    ///< `variable_word` install: scan the next name from
                    ///< @ref forth_state::source, allot @ref instr::operand
                    ///< cells past @ref data_space::here, and define that
                    ///< name in the dictionary passed to @ref run_from
                    ///< (`CREATE` uses operand 0; nothing yet reuses this
                    ///< for `VARIABLE`, which keeps step F26's own
                    ///< direct-name interpret()-level install). Diagnoses if
                    ///< @ref run_from was not given a dictionary.
    does_enter,     ///< `DOES>`: attaches the very next instruction index
                    ///< (this instruction's own index + 1) as the most
                    ///< recently defined dictionary entry's own does-field
                    ///< (@ref variable_word::does_entry), then acts exactly
                    ///< like @ref ret (pops the return stack and jumps
                    ///< there) -- ending the *defining* word's own
                    ///< execution right here, per Forth-2012. Diagnoses if
                    ///< @ref run_from was not given a dictionary, or if the
                    ///< most recent entry was not `CREATE`d.
    catch_mark,     ///< `CATCH` (step F31, D11): pops an execution token,
                    ///< pushes a 3-cell handler frame onto the return stack,
                    ///< then jumps to the token exactly like @ref execute
                    ///< would. @ref instr::operand is the resume instruction
                    ///< index a caught @ref throw_op restores to. See
                    ///< `vm.hpp`'s own `perform_throw` for the full design.
    throw_op,       ///< `THROW` (step F31, D11): pops @c n; a nonzero @c n
                    ///< unwinds to the innermost active @ref catch_mark
                    ///< frame (restoring both stacks to that frame's own
                    ///< recorded depth and pushing @c n), or diagnoses an
                    ///< uncaught `THROW` carrying @c n if none is active;
                    ///< @c n `== 0` is a no-op (Forth-2012).
    halt,           ///< Stop the VM's fetch-execute loop successfully.
};

/// One flat instruction: an opcode plus a single immediate @ref cell
/// operand. Every operand's meaning is opcode-dependent: an instruction
/// index for `call`/`branch`/`branch0`/`push_xt`, a literal value for
/// `push`, a `primitive` enumerator (cast to @ref cell) for `prim`, a cell
/// count for `create_word`, and unused (left zero) for `ret`/`halt`/
/// `execute`/`does_enter`.
///
/// A plain aggregate, trivially copyable and a literal type by construction
/// -- there is nothing here for the compiler to have to prove trivial.
// f42b25c1-ce56-43b1-9d4b-adecc6e3ae0b
struct instr {
    op code{};      ///< Which operation to perform.
    cell operand{}; ///< The operation's single immediate operand.
};
// f42b25c1-ce56-43b1-9d4b-adecc6e3ae0b end
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
///
/// @tparam MaxDiagnostics Capacity for @ref diagnostics (step F30, D20).
// 7bb215b8-9ae5-42f0-90f1-dff9d46b08ab
template <int MaxCode = 4096, int MaxWords = 256, int MaxDiagnostics = 32>
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

    /// Step F30 (D20): a running maximum, across every colon definition
    /// closed so far in this session whose own peak was computable
    /// (`interpreter::compiled_colon_word::peak_depth`, `-1` unless
    /// known), of that peak -- not a real whole-program bound (see this
    /// struct's own doc comment on why one no longer has a natural
    /// referent under D13's shared, ever-growing code space, DIV-0019).
    /// `-1` ("not computed") until the first definition whose own peak is
    /// computable closes.
    int required_stack_depth = -1;
    /// The same running-maximum idea as @ref required_stack_depth, for the
    /// return stack's own `>R`-driven peak.
    int required_return_depth = -1;

    /// Step F30 (D20), extended by this project's own post-F32-merge
    /// amendment (DIV-0019): every *advisory* effect-lint diagnostic
    /// collected so far in this session -- a data-stack join disagreement
    /// in an undeclared definition (`interpreter::check_definition_effect`'s
    /// own doc comment explains why this is advisory rather than fatal).
    /// A caller retrieves these directly (`compiled_program::diagnostics`,
    /// or `interpreter::compile_buffer::program().diagnostics`); nothing
    /// about them is otherwise surfaced (no output, no separate return
    /// value from `interpreter::interpret`) -- an advisory diagnostic
    /// nobody reads is exactly as good as one that was never collected, so
    /// this field is the one place a caller who cares can always find the
    /// whole list. Diagnostics past @ref MaxDiagnostics capacity are
    /// silently not recorded, never diagnosed as an error (D7 does not
    /// apply to an advisory-only list: failing compilation because an
    /// *informational* list ran out of room would defeat its own purpose).
    foundation::static_vector<foundation::parse_error, MaxDiagnostics>
        diagnostics{};
};
// 7bb215b8-9ae5-42f0-90f1-dff9d46b08ab end
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
