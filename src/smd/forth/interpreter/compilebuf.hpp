// src/smd/forth/interpreter/compilebuf.hpp                          -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#ifndef SRC_SMD_FORTH_INTERPRETER_COMPILEBUF_HPP
#define SRC_SMD_FORTH_INTERPRETER_COMPILEBUF_HPP

#include <smd/forth/foundation/result.hpp>
#include <smd/forth/foundation/source_pos.hpp>
#include <smd/forth/machine/cell.hpp>
#include <smd/forth/machine/dictionary.hpp>
#include <smd/forth/machine/emit.hpp>
#include <smd/forth/machine/forth_state.hpp>
#include <smd/forth/machine/instruction.hpp>
#include <smd/forth/machine/vm.hpp>

#include <type_traits>

namespace smd::forth::interpreter {

// Step F25 (docs/forth-plan-2.md): the colon compiler's own code space.
// Where machine::codegen (step F14) flattens an already-complete elaborated
// tree into a compiled_program in one batch call, this type grows one
// instruction at a time as the live text interpreter meets `:` ... `;`
// (D13/D14) -- the "incrementally-appendable code space" F24's own
// step-brief flagged as missing. D16 keeps compiled_program itself as the
// retained artifact shape ("the opcode enum, instr, compiled_program, and
// the VM loop are retained"), so this class wraps one directly rather than
// inventing a second code representation; machine::emit (machine/emit.hpp)
// is exactly the "append one instruction, diagnosing MaxCode overflow"
// primitive this class needs.

/// The interpreter's own incrementally-appendable code space (D15's "code
/// space", built one `:` ... `;` pair at a time).
///
/// Instruction 0 is reserved at construction as a permanent `halt` landing
/// pad (@ref halt_pad): interpreting a defined word means calling into the
/// *middle* of this buffer from the outer text-interpreter loop, not running
/// a whole compiled_program from its own `program_entry` the way @ref
/// machine::run does -- @ref call_word arranges for the word's own trailing
/// `ret` to land here, so the same VM fetch-execute loop (@ref
/// machine::run_from, the additive entry point that runs from an arbitrary
/// instruction index against an already-live state without @ref
/// machine::run's own data-space seeding) stops cleanly instead of
/// underflowing the return stack.
///
/// @tparam MaxCode  Instruction-array capacity (@ref machine::
///                  compiled_program).
/// @tparam MaxWords Word-table capacity, likewise; unused by this step (no
///                  binding kind this step defines consults @ref
///                  machine::compiled_program::entry_points -- @ref
///                  machine::dictionary::compiled_colon_word carries its own
///                  entry point directly), kept only because it is part of
///                  @ref machine::compiled_program's own shape.
template <int MaxCode = 4096, int MaxWords = 256>
class compile_buffer {
  public:
    constexpr compile_buffer();

    /// Appends one instruction (@ref machine::emit), diagnosing @ref MaxCode
    /// overflow rather than exceeding capacity.
    constexpr auto emit(machine::op code, machine::cell operand,
                        foundation::source_pos pos) -> foundation::result<int>;

    /// The next instruction index @ref emit will append at -- a colon
    /// word's own entry point is exactly this value, captured by `:` before
    /// its body's own instructions are emitted (F14's discipline, carried
    /// forward: `RECURSE` resolves because the entry point is known before
    /// the body that might reference it).
    [[nodiscard]] constexpr auto here() const -> int;

    /// The permanent `halt` landing pad's own instruction index (always 0):
    /// see this class's own doc comment and @ref call_word.
    [[nodiscard]] constexpr auto halt_pad() const -> int { return 0; }

    /// The underlying compiled_program, for direct use by @ref call_word
    /// (which reads it through @ref machine::run_from without mutating it --
    /// this class never sets @c program_entry or @c data_space_size itself)
    /// or by a later step's own batch run (F26).
    [[nodiscard]] constexpr auto program() const
        -> machine::compiled_program<MaxCode, MaxWords> const &;
    /// The underlying compiled_program, mutable -- for a later step to
    /// extend (e.g. F26's own batch run), not used by this step's own
    /// @ref call_word.
    [[nodiscard]] constexpr auto program()
        -> machine::compiled_program<MaxCode, MaxWords> &;

  private:
    machine::compiled_program<MaxCode, MaxWords> program_{};
};

template <int MaxCode, int MaxWords>
constexpr compile_buffer<MaxCode, MaxWords>::compile_buffer() {
    // Reserve instruction 0 as the permanent halt landing pad (see this
    // class's own doc comment). machine::emit is not used here: it is not
    // fallible for the very first instruction of a buffer sized for at
    // least one (every capacity this project uses is comfortably larger),
    // and using it would require threading a foundation::result through a
    // constructor with no way to report failure anyway.
    program_.code.push_back(
        machine::instr{.code = machine::op::halt, .operand = machine::cell{0}});
}

template <int MaxCode, int MaxWords>
constexpr auto
compile_buffer<MaxCode, MaxWords>::emit(machine::op code, machine::cell operand,
                                        foundation::source_pos pos)
    -> foundation::result<int> {
    return machine::emit(program_, code, operand, pos);
}

template <int MaxCode, int MaxWords>
constexpr auto compile_buffer<MaxCode, MaxWords>::here() const -> int {
    return program_.code.size();
}

template <int MaxCode, int MaxWords>
constexpr auto compile_buffer<MaxCode, MaxWords>::program() const
    -> machine::compiled_program<MaxCode, MaxWords> const & {
    return program_;
}

template <int MaxCode, int MaxWords>
constexpr auto compile_buffer<MaxCode, MaxWords>::program()
    -> machine::compiled_program<MaxCode, MaxWords> & {
    return program_;
}

namespace detail {

static_assert(std::is_trivially_destructible_v<compile_buffer<64, 32>>);

} // namespace detail

// f7a3c9e1-6d4b-4a2f-8c1e-9b5d7a3f2e6c
/// Interprets a defined word (D14: "interpreting a defined word runs its
/// code on the VM against the live forth_state -- one semantics"): calls
/// into @p buf's own code space at @p entry_point, against the already-live
/// @p state, and returns once that word's own top-level `ret` reaches @p
/// buf's own @ref compile_buffer::halt_pad "halt landing pad".
///
/// Runs through @ref machine::run_from -- the VM entry point that executes
/// from a given instruction index against an already-live state *without*
/// F16's data-space seeding -- rather than @ref machine::run itself: @ref
/// machine::run seeds @p state's own data space from @p buf's own @ref
/// machine::compiled_program::data_space_size once, unconditionally, on
/// every call, which is correct for a whole program's own single top-level
/// run but would be wrong here, where this function is called once per
/// top-level reference to a previously defined word -- repeating the seed
/// would `allot` more cells on every single call, without bound. @p state's
/// own data space instead just grows incrementally, the way any other
/// primitive's side effect would (`VARIABLE`/`ALLOT`, when compiled) -- see
/// DIV-0013 for why this needed a genuine additive VM entry point rather
/// than transiently pointing @p buf's own `program_entry` at @p entry_point
/// and calling @ref machine::run unmodified (the shape this function used
/// before DIV-0013's revision).
///
/// @param buf         The code space to call into.
/// @param state       The live machine state to run against; mutated in
///                     place.
/// @param entry_point An instruction index inside @p buf (a @ref
///                     machine::dictionary::compiled_colon_word's own @ref
///                     machine::compiled_colon_word::entry_point).
/// @param fuel         The VM's own execution step budget (@ref
///                     machine::consume_vm_fuel).
/// @param dict         Step F28's own addition: forwarded to @ref
///                     machine::run_from unchanged (`nullptr` by default, as
///                     every caller before this step already implicitly
///                     assumed) -- only needed if @p entry_point's own body
///                     reaches `CREATE`/`DOES>` (@ref machine::op::
///                     create_word/@ref machine::op::does_enter).
template <int MaxCode, int MaxWords, int MaxDepth, int MaxRDepth, int MaxData,
          int MaxOut, int DictWords = 256, int DictName = 32>
constexpr auto
call_word(compile_buffer<MaxCode, MaxWords> &buf,
          machine::forth_state<MaxDepth, MaxRDepth, MaxData, MaxOut> &state,
          int entry_point, int fuel,
          machine::dictionary<DictWords, DictName> *dict = nullptr)
    -> machine::status {
    auto push_return =
        state.returns().push(static_cast<machine::cell>(buf.halt_pad()));
    if (!push_return.has_value()) {
        return push_return;
    }
    return machine::run_from(buf.program(), state, entry_point, fuel, dict);
}
// f7a3c9e1-6d4b-4a2f-8c1e-9b5d7a3f2e6c end

// Merge criteria (static_assert, immediately-invoked-lambda pattern): a
// fresh buffer's here() is 1 (instruction 0 is the reserved halt pad); emit
// appends starting there; call_word runs a hand-compiled "DUP *" body
// (SQUARED's own shape) against a live state and leaves the squared value.

static_assert([] {
    compile_buffer<64, 32> buf;
    return buf.here() == 1 && buf.halt_pad() == 0 &&
           buf.program().code.size() == 1 &&
           buf.program().code[0].code == machine::op::halt;
}());

static_assert([] {
    compile_buffer<64, 32> buf;
    int const entry = buf.here();
    auto r0 = buf.emit(machine::op::prim,
                       static_cast<machine::cell>(machine::primitive::dup),
                       foundation::source_pos{});
    auto r1 = buf.emit(machine::op::prim,
                       static_cast<machine::cell>(machine::primitive::star),
                       foundation::source_pos{});
    auto r2 =
        buf.emit(machine::op::ret, machine::cell{0}, foundation::source_pos{});
    if (!r0.has_value() || !r1.has_value() || !r2.has_value()) {
        return false;
    }

    machine::forth_state<64, 64, 1024, 256> state{};
    auto push = state.data().push(4);
    if (!push.has_value()) {
        return false;
    }
    auto r = call_word(buf, state, entry, 1000);
    return r.has_value() && state.data().depth() == 1 &&
           state.data().peek().value() == 16;
}());

} // namespace smd::forth::interpreter

#endif
