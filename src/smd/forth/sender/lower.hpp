// src/smd/forth/sender/lower.hpp                                     -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#ifndef INCLUDED_SMD_FORTH_SENDER_LOWER
#define INCLUDED_SMD_FORTH_SENDER_LOWER

#include <smd/forth/foundation/parse_error.hpp>
#include <smd/forth/foundation/result.hpp>
#include <smd/forth/foundation/source_pos.hpp>
#include <smd/forth/interpreter/effect_lint.hpp>
#include <smd/forth/machine/cell.hpp>
#include <smd/forth/machine/dictionary.hpp>
#include <smd/forth/machine/forth_state.hpp>
#include <smd/forth/machine/instruction.hpp>
#include <smd/forth/machine/vm.hpp>
#include <smd/forth/sender/vocab.hpp>

#include <beman/execution26/execution.hpp>

#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>

// Step F33 (docs/forth-plan-2.md), D24/D11: the sender backend.
//
// The thesis this step exists to test: threaded code is CPS with the
// continuation defunctionalized as the return stack, so lowering to senders
// is *refunctionalization* -- taking that defunctionalized representation
// (a stack of addresses plus a dispatch loop, `vm.hpp`'s own `run_from`)
// back to explicit continuations. This header is a *second executor* of the
// exact same `machine::instr` stream `run_from` already runs (D14: one
// semantics, two executors) -- it does not recompile or retarget anything;
// every opcode below is the one `interp.hpp`'s `compile_entry`/
// `apply_control_word` already emit.
//
// The refunctionalization, opcode by opcode:
//   - `op::call`/`op::execute` no longer push a return address onto
//     `forth_state::returns()` at all: entering a callee is an ordinary
//     recursive C++ call (@ref word_sender::run constructing and driving a
//     nested @ref word_sender), so the *C++ call stack* is the
//     continuation -- exactly the return-stack entry it replaces.
//   - `op::ret`/`op::does_enter` (EXIT compiles to a bare `ret`, `vm.hpp`'s
//     own top comment) complete @ref word_sender's own value channel
//     (`block_outcome{ret}`) instead of popping anything: the recursive
//     caller's own C++ frame resumes on its own, needing no address to jump
//     to.
//   - `op::throw_op`, and any primitive fault this project already maps to
//     a THROW code (D7), complete the *error* channel
//     (`set_error(control_error{n, &state})`) instead of calling `vm.hpp`'s
//     own `perform_throw` -- D24's "THROW -> error channel with (n,
//     state)", literally.
//   - `CATCH` (the `catch_mark`/`catch_ok` pair `interp.hpp` always emits
//     together) is lowered as one compound step: the protected xt's own
//     nested @ref word_sender is composed with `then`/`upon_error` (real
//     Execution26 combinators, not a hand-rolled if/else) into "0 on normal
//     completion, n with the Forth stack restored on a caught throw" --
//     D24's "CATCH as the error-to-value adapter with Forth stack
//     restoration". Nothing is pushed onto `returns()` for CATCH's own
//     bookkeeping (unlike `vm.hpp`'s own 3-cell handler frame): the saved
//     depths live as plain C++ locals in @ref word_sender::run's own stack
//     frame instead, which is the *same* refunctionalization move calls
//     already made -- see this header's own "boundary" section below.
//   - `IF`/`BEGIN` (both forms)/`DO`/`LOOP`/`+LOOP`/`LEAVE` all lower to the
//     same thing: a `block_outcome::jump` (or `fallthrough`) outcome that
//     @ref word_sender::run's own trampoline loop acts on directly --
//     "loops as repeat-until composition" (D24) via ordinary C++ iteration,
//     not template-recursive sender composition, because a loop's own trip
//     count is runtime data and Execution26 senders are concrete types (no
//     type-level fixed point without erasure this step does not add). This
//     loop *is* the "explicit constexpr trampoline for control transfer"
//     R1 F18's own pre-authorization names; each pass through it still
//     builds and drives exactly one fresh sender per recovered
//     `interpreter::basic_block` (see @ref word_sender::run's own body),
//     so "one composed sender per block" and "an explicit trampoline
//     between blocks" are not in tension -- they are two different
//     granularities of the same design.
//
// The fallback boundary (D24 requires this be named, not silently papered
// over): `>R`/`R>`/`R@` let a word read or overwrite the return stack as
// plain data. Under `vm.hpp`, a `call`'s own return address is a real cell
// on that stack, so a sufficiently adversarial word could `R>` it, inspect
// or replace it, and `>R` a different one back -- redirecting its own
// control flow through data. This lowering elides that cell entirely (the
// point above): nothing is ever pushed onto `returns()` for a call, so a
// word relying on reading its own return address via `R>`/`R@` would see
// wrong data (garbage, or some other frame's own bookkeeping) if lowered
// natively. Precisely characterizing *which* `>R`/`R>`/`R@` uses are
// actually safe (many are -- see `interp.test.cpp`'s own `DEEP`/`GUARD`
// CATCH corpus, replayed in this component's own test file, which never
// touches a call-frame address and would in fact produce identical results
// lowered natively) would require a real return-stack shape analysis this
// step does not attempt. @ref word_uses_return_stack_data is this header's
// own conservative, *sound* trigger instead: any of the three primitives
// anywhere in a word's own body routes that whole word through
// @ref run_word_via_vm -- D24's permitted "VM-in-a-sender fallback for
// non-refunctionalizable return-stack use" -- wrapped in exactly one
// sender, never partially lowered. `docs/compiler_architecture.org`'s own
// Phase 16 section names the one program on each side of this boundary.
namespace smd::forth::sender {

// e7f4b8a2-1c6d-4e3f-9a5b-2d7c8e1f4a6b
/// How one recovered @ref interpreter::basic_block's own execution ended, or
/// (from @ref word_sender's own value channel) how a whole word's execution
/// ended -- the value @ref word_sender completes with when it does not
/// error or stop.
enum class transfer_kind : std::uint8_t {
    fallthrough, ///< The block simply ran out with no explicit terminator;
                 ///< continue at @ref block_outcome::target (the next
                 ///< block's own start).
    jump,        ///< An unconditional or resolved conditional branch;
                 ///< continue at @ref block_outcome::target.
    ret,         ///< `op::ret`/`op::does_enter` reached: this word's own
                 ///< execution is complete (EXIT compiles to a bare `ret`,
                 ///< so this covers both).
    halt,        ///< `op::halt` reached: the whole run is complete, not just
                 ///< this word -- every recursive caller must also stop
                 ///< rather than resume (see @ref word_sender::run's own
                 ///< propagation of this case).
};

/// See @ref transfer_kind. @ref target is meaningful only for
/// @ref transfer_kind::fallthrough and @ref transfer_kind::jump.
struct block_outcome {
    transfer_kind kind = transfer_kind::ret;
    int target = -1;
};
// e7f4b8a2-1c6d-4e3f-9a5b-2d7c8e1f4a6b end

// d3e6a9c1-4f7b-4a2e-8c1d-6b9e3a5f7c2d
/// The error-channel payload D24 calls for verbatim ("THROW -> error
/// channel with (n, state)"): either a genuine Forth `THROW` code bound for
/// the nearest active `CATCH` (@ref numbered, @ref n), or a raw, terminal
/// diagnosis that no `CATCH` anywhere in the current dynamic extent will
/// ever intercept (`!numbered`, @ref diag) -- mirroring `vm.hpp`'s own
/// `run_from` `op::prim` case exactly: a primitive fault only ever becomes a
/// numbered code when @ref machine::forth_state::handler_depth is active
/// *and* `vm.hpp`'s own `machine_fault_throw_code` maps it (`ABORT"`'s own
/// condition is the one unconditional exception, always numbered `-2`); any
/// other fault, or a numbered code that reaches the true top of the run with
/// no `CATCH` ever having intercepted it, is what @ref to_status renders.
///
/// @ref state does not own or copy anything -- it names the one live
/// `forth_state` the whole run shares (never a snapshot, which would already
/// be stale by the time an adapter inspects it), exactly as
/// `foundation::parse_error::message` already documents for its own pointee
/// (this project's standing non-owning-pointer convention).
template <int MaxDepth, int MaxRDepth, int MaxData, int MaxOut>
struct control_error {
    bool numbered = false;
    machine::cell n{};
    foundation::parse_error diag{};
    machine::forth_state<MaxDepth, MaxRDepth, MaxData, MaxOut> *state = nullptr;
};
// d3e6a9c1-4f7b-4a2e-8c1d-6b9e3a5f7c2d end

/// Renders @ref control_error the way an uncaught completion is reported to
/// an ordinary, non-sender-aware caller (@ref run_from_via_senders' own
/// value): a numbered code becomes byte-identical to `vm.hpp`'s own
/// `perform_throw`'s "uncaught THROW" diagnosis (so a program that ends up
/// uncaught either way is indistinguishable between the two executors, D14);
/// a raw diagnosis is returned verbatim.
template <int MaxDepth, int MaxRDepth, int MaxData, int MaxOut>
[[nodiscard]] constexpr auto
to_status(control_error<MaxDepth, MaxRDepth, MaxData, MaxOut> const &e)
    -> machine::status {
    if (e.numbered) {
        return foundation::parse_error{
            foundation::source_pos{.offset = static_cast<int>(e.n)},
            "uncaught THROW (code in foundation::parse_error::where.offset)"};
    }
    return e.diag;
}

/// True iff @p in is one of the three primitives that can observe or
/// overwrite the return stack as plain data (`>R`/`R>`/`R@`) -- this header's
/// own top comment explains why any use anywhere in a word's own body routes
/// that whole word through @ref run_word_via_vm rather than native lowering.
// f4b1c8e6-2a5d-4b9f-8c3e-1d6a9f4b2c8e
[[nodiscard]] constexpr auto touches_return_stack_data(machine::instr const &in)
    -> bool {
    if (in.code != machine::op::prim) {
        return false;
    }
    auto const p = static_cast<machine::primitive>(in.operand);
    return p == machine::primitive::to_r || p == machine::primitive::r_from ||
           p == machine::primitive::r_fetch;
}

/// True iff any instruction in `[entry, end_exclusive)` touches the return
/// stack as data (@ref touches_return_stack_data) -- the whole-word,
/// conservative trigger for @ref run_word_via_vm.
template <int MaxCode, int MaxWords>
[[nodiscard]] constexpr auto
word_uses_return_stack_data(machine::compiled_program<MaxCode, MaxWords> const
                                &program,
                            int entry, int end_exclusive) -> bool {
    for (int i = entry; i < end_exclusive; ++i) {
        if (touches_return_stack_data(program.code[i])) {
            return true;
        }
    }
    return false;
}
// f4b1c8e6-2a5d-4b9f-8c3e-1d6a9f4b2c8e end

/// A safe (possibly loose) upper bound on one word's own instruction range,
/// given only its own @p entry: the smallest recorded
/// @ref machine::compiled_program::entry_points value strictly greater than
/// @p entry, or `program.code.size()` if none (this is the last colon word
/// compiled so far, or @p entry names top-level rather than colon-word
/// code). `compiled_program` has no *stored* per-word end -- D13's shared,
/// ever-growing code space means the only place that boundary was ever a
/// natural, cheap fact was `interp.hpp`'s own `;` handling, mid-compile
/// (`buf.here()` at closing time, threaded straight into
/// `interpreter::check_definition_effect`). A post-hoc executor reopening an
/// already-compiled `compiled_program` has no such fact available and must
/// reconstruct a bound instead; see DIV-0026. Since colon words compile
/// sequentially into one flat array (dictionary-index order matching
/// code-array order, D13), one word's own body can never extend into a
/// *later* word's own recorded entry point, making this bound always safe
/// (@ref interpreter::recover_basic_blocks over an over-wide range is still
/// correct, merely more capacity than strictly needed) even though it is
/// not always tight (interleaved top-level code between two colon words
/// widens it further, harmlessly).
template <int MaxCode, int MaxWords>
[[nodiscard]] constexpr auto
word_body_end(machine::compiled_program<MaxCode, MaxWords> const &program,
              int entry) -> int {
    int bound = program.code.size();
    for (int i = 0; i < program.entry_points.size(); ++i) {
        int const ep = program.entry_points[i];
        if (ep > entry && ep < bound) {
            bound = ep;
        }
    }
    return bound;
}

/// Captures whichever of a sender's three completion channels actually
/// fired, without ever throwing a C++ exception for a Forth-level fault
/// (D7) -- @ref drive's own reason for existing rather than reusing
/// `sender::sync_wait` at every recursive step: `sync_wait` unconditionally
/// converts an unhandled `set_error` into a rethrown `std::exception_ptr`,
/// which is exactly the C++-exception-for-a-Forth-fault D7 bars, and the
/// plan's own "never `sync_wait` inside evaluation" names precisely this
/// recursive, mid-composition position. @ref run_from_via_senders' own
/// single, true top-level call site is the one place this project does use
/// the real `sender::sync_wait` -- see its own doc comment for why that use
/// is safe.
template <typename Value, typename Error>
struct drive_result {
    std::optional<Value> value{};
    std::optional<Error> error{};
    bool stopped = false;
};

template <typename Value, typename Error>
struct drive_receiver {
    using receiver_concept = receiver_tag;

    drive_result<Value, Error> *out;

    auto set_value(Value v) && noexcept -> void { out->value.emplace(std::move(v)); }
    auto set_error(Error e) && noexcept -> void { out->error.emplace(std::move(e)); }
    auto set_stopped() && noexcept -> void { out->stopped = true; }
};

/// Connects @p snd to a fresh @ref drive_receiver and starts it immediately,
/// returning whichever channel fired. Every sender this component builds
/// completes synchronously within `start()` (no real scheduler, no genuine
/// suspension anywhere in this backend -- D24's own constexpr-trampoline
/// framing), so this is always a same-stack-frame, non-blocking call in
/// practice, not the synchronous-wait-inside-an-async-op anti-pattern
/// `sync_wait` itself would be here (see @ref drive_result's own doc
/// comment).
template <typename Value, typename Error, typename Sender>
[[nodiscard]] auto drive(Sender &&snd) -> drive_result<Value, Error> {
    drive_result<Value, Error> result{};
    auto op = connect(std::forward<Sender>(snd),
                      drive_receiver<Value, Error>{&result});
    start(op);
    return result;
}

/// A sender whose entire body is @p F, a callable invoked with the
/// (by-value) receiver it was connected to; @p F decides which of the three
/// completion channels fires by calling `set_value`/`set_error`/
/// `set_stopped` on it directly. This is exactly the shape @ref word_sender
/// itself uses (a hand-written `connect`/`operation_state`, per this
/// project's own `vendor/execution/examples/sender_demo.cpp`), generalized
/// so @ref word_sender::run can build one fresh instance per recovered
/// @ref interpreter::basic_block (D24: "one composed sender per block")
/// without a second hand-rolled sender type: @p F's own body is free to
/// recurse into nested senders via @ref drive, exactly like a block's own
/// straight-line instructions may need to (a `call`/`execute` reaching
/// another word, or `CATCH`'s own protected xt).
template <typename Value, typename Error, typename F>
class inline_sender {
  public:
    using sender_concept = sender_tag;
    using completion_signatures = beman::execution26::completion_signatures<
        set_value_t(Value), set_error_t(Error), set_stopped_t()>;
    template <typename...>
    static consteval auto get_completion_signatures() noexcept
        -> completion_signatures {
        return {};
    }

    constexpr explicit inline_sender(F fn) : fn_(std::move(fn)) {}

    template <typename Receiver>
    struct op_state {
        using operation_state_concept = operation_state_tag;

        std::remove_cvref_t<Receiver> rec;
        F fn;

        auto start() & noexcept -> void { fn(std::move(rec)); }
    };

    template <typename Receiver>
    [[nodiscard]] auto connect(Receiver &&r) && -> op_state<Receiver> {
        return op_state<Receiver>{std::forward<Receiver>(r), std::move(fn_)};
    }

  private:
    F fn_;
};

template <typename Value, typename Error, typename F>
[[nodiscard]] constexpr auto make_inline_sender(F fn)
    -> inline_sender<Value, Error, F> {
    return inline_sender<Value, Error, F>{std::move(fn)};
}

/// A type-erased receiver for exactly @ref word_sender's own three-channel
/// completion shape (@p Value/@p Error, always @ref block_outcome/
/// @ref control_error<...> in this component). This is the fix for a real
/// compile-cost defect an earlier draft of this component had: @ref
/// word_sender::run is *not* a member template over the connecting
/// receiver's own concrete type (an ordinary generic `connect`/`operation_
/// state` would make it one, per `vendor/execution/examples/sender_demo.cpp`'s
/// own pattern) -- it takes this one abstract interface instead, so it is
/// compiled exactly once per `(MaxCode, ..., DictName)` combination, never
/// once per distinct closure type some caller happens to compose it with.
///
/// The defect this replaces: local lambdas defined inside a function
/// template's own body get a distinct closure type *per instantiation* of
/// the enclosing template, even when textually identical. `run`'s own
/// `CATCH` handling builds a `then`/`upon_error`-composed sender wrapping a
/// nested `word_sender` in exactly such a lambda-typed receiver; if `run`
/// itself were templated on its own connecting receiver, each instantiation
/// would mint new lambda types for that composition, which would need to
/// connect the nested `word_sender` to a receiver embedding *those* new
/// types, requiring another instantiation of `run` that mints newer lambda
/// types still -- unbounded recursive template instantiation with no fixed
/// point, confirmed directly: a single `run_from_via_senders` call over the
/// simplest possible corpus program (`ABS`, one `IF`, no `CATCH`, no calls)
/// exhausted an 11 GB `cc1plus` memory cap without finishing (the original,
/// undiagnosed failure exhausted the whole machine's real memory, 42 GB,
/// before being reaped). Type-erasing @ref word_sender's own receiver breaks
/// the cycle: every internal composition (`CATCH`'s own adapter chain, a
/// nested `call`/`execute`'s own recursive invocation) now connects through
/// @ref receiver_adapter, a small, non-recursive template with nothing of
/// `run`'s own body in it, so instantiating it costs nothing close to
/// instantiating `run` again. See DIV-0026 for the complete record.
template <typename Value, typename Error>
class abstract_receiver {
  public:
    virtual ~abstract_receiver() = default;

    virtual auto value(Value v) -> void = 0;
    virtual auto error(Error e) -> void = 0;
    virtual auto stopped() -> void = 0;
};

/// Adapts a concrete Execution26 receiver @p Receiver to
/// @ref abstract_receiver -- the one place a concrete receiver's own type
/// still exists at all once @ref word_sender::connect hands off to it.
/// Deliberately trivial (three one-line forwarding overrides): the whole
/// point is that instantiating this costs nothing like instantiating
/// @ref word_sender::run.
template <typename Value, typename Error, typename Receiver>
class receiver_adapter final : public abstract_receiver<Value, Error> {
  public:
    explicit receiver_adapter(Receiver rec) : rec_(std::move(rec)) {}

    auto value(Value v) -> void override {
        set_value(std::move(rec_), std::move(v));
    }
    auto error(Error e) -> void override {
        set_error(std::move(rec_), std::move(e));
    }
    auto stopped() -> void override { set_stopped(std::move(rec_)); }

  private:
    Receiver rec_;
};

template <int MaxCode, int MaxWords, int MaxDepth, int MaxRDepth, int MaxData,
          int MaxOut, int MaxBlocks = 128, int DictWords = 256,
          int DictName = 32>
class word_sender;

/// Runs @p program's own instruction range `[entry, word_body_end(entry))`
/// against @p state using `vm.hpp`'s own `run_from` directly -- D24's
/// permitted VM-in-a-sender fallback, for exactly the words
/// @ref word_uses_return_stack_data flags (this header's own top comment
/// names the boundary).
///
/// Two adjustments make a standalone `run_from` call behave correctly when
/// reached this way rather than through `vm.hpp`'s own `op::call`/`op::
/// execute` (this lowering never pushes a return address for either, the
/// whole point of D24's own refunctionalization -- see this header's own
/// top comment):
///
/// - **A manufactured return address.** This word's own trailing `ret`
///   still expects to pop one. `interpreter::compile_buffer::call_word`'s
///   own convention -- push the reserved halt pad (instruction `0`, which
///   every `compile_buffer` constructor always emits first) before calling
///   `run_from` -- is replayed verbatim here, so `ret` lands cleanly on a
///   real `op::halt` instead of underflowing.
/// - **A hidden outer handler.** `vm.hpp`'s own `perform_throw` treats
///   `forth_state::handler_depth` as the base of a *real*, VM-materialized
///   3-cell frame at that exact return-stack depth -- exactly what this
///   lowering's own native `CATCH` (@ref word_sender::run's own
///   `op::catch_mark` case) never pushes; it keeps the same bookkeeping as
///   plain C++ locals instead. An enclosing *sender*-level `CATCH` may
///   still have set a real, non-negative `handler_depth()` before reaching
///   here -- correct for *this* lowering's own upon-error adapter, which
///   only ever truncates past it, but exactly wrong for `perform_throw`,
///   which would set `ip` to that sender-level resume point and let this
///   `run_from`'s own dispatch loop keep fetching from it, corrupting
///   execution by interpreting unrelated code as more of this word's own
///   body. Hiding it (a plain save/restore around the call) makes an
///   escaping `THROW` become `run_from`'s own standard "uncaught"
///   diagnosis instead -- which @ref word_sender::run's own caller
///   recognizes by message text and re-numbers, so an enclosing
///   sender-level `CATCH` still catches it correctly, exactly as if this
///   word had been lowered natively. See DIV-0026 for the complete record,
///   including why the more direct fix (also pushing a real 3-cell frame
///   from the native `CATCH` case) does not work.
template <int MaxCode, int MaxWords, int MaxDepth, int MaxRDepth, int MaxData,
          int MaxOut, int DictWords, int DictName>
[[nodiscard]] auto
run_word_via_vm(machine::compiled_program<MaxCode, MaxWords> const &program,
                machine::forth_state<MaxDepth, MaxRDepth, MaxData, MaxOut>
                    &state,
                int entry, machine::dictionary<DictWords, DictName> *dict,
                int &fuel) -> machine::status {
    auto push_halt_pad = state.returns().push(machine::cell{0});
    if (!push_halt_pad.has_value()) {
        return push_halt_pad;
    }

    int const outer_handler = state.handler_depth();
    state.set_handler_depth(-1);
    auto r = machine::run_from(program, state, entry, fuel, dict);
    state.set_handler_depth(outer_handler);

    // machine::run_from's own fuel parameter is by value (it has no
    // recursion of its own to share a budget across); this component's own
    // fuel is one shared counter across every recursive word_sender level
    // (D22: "fuel covers the interpreter loop, not only the VM"), so the
    // fallback charges its own whole run against that shared budget in one
    // lump sum here rather than per VM instruction -- generous, but never
    // unbounded: run_from itself still enforces its own budget internally
    // and diagnoses exhaustion exactly as it always has.
    fuel = fuel > 0 ? fuel - 1 : 0;
    return r;
}

/// A single word's own execution, lowered to Execution26 senders (D24).
///
/// Constructed with the exact same arguments `vm.hpp`'s own `run_from`
/// takes (@p program, @p state, @p entry, @p dict), plus a *shared* @p fuel
/// reference: unlike `run_from`'s own by-value budget (correct for a single,
/// non-recursive dispatch loop), this component recurses in real C++ for
/// every `call`/`execute`/`CATCH` xt, so one shared counter, threaded by
/// reference through every recursive level, is what makes D22's "fuel
/// covers the interpreter loop" hold here.
///
/// Completes: @ref block_outcome on the value channel (@ref
/// transfer_kind::ret or @ref transfer_kind::halt -- a fresh `word_sender`
/// only ever *returns* one of these two, never `fallthrough`/`jump`, which
/// are purely internal to @ref run's own trampoline loop); a
/// @ref control_error on the error channel (D24: "THROW -> error channel
/// with (n, state)"); or stopped, when @p fuel is exhausted (D22's own stop-
/// channel demonstration).
template <int MaxCode, int MaxWords, int MaxDepth, int MaxRDepth, int MaxData,
          int MaxOut, int MaxBlocks, int DictWords, int DictName>
class word_sender {
  public:
    using state_type =
        machine::forth_state<MaxDepth, MaxRDepth, MaxData, MaxOut>;
    using error_type = control_error<MaxDepth, MaxRDepth, MaxData, MaxOut>;
    using sender_concept = sender_tag;
    using completion_signatures = beman::execution26::completion_signatures<
        set_value_t(block_outcome), set_error_t(error_type), set_stopped_t()>;
    template <typename...>
    static consteval auto get_completion_signatures() noexcept
        -> completion_signatures {
        return {};
    }

    constexpr word_sender(
        machine::compiled_program<MaxCode, MaxWords> const *program,
        state_type *state, int entry,
        machine::dictionary<DictWords, DictName> *dict, int *fuel)
        : program_(program), state_(state), entry_(entry), dict_(dict),
          fuel_(fuel) {}

    // See @ref abstract_receiver's own doc comment: op_state's only job is
    // to adapt whatever concrete Receiver connected to a type-erased
    // interface, so @ref run itself never becomes a member template over
    // it (the fix for a real, confirmed unbounded-template-instantiation
    // defect, DIV-0026).
    template <typename Receiver>
    struct op_state {
        using operation_state_concept = operation_state_tag;

        receiver_adapter<block_outcome, error_type,
                         std::remove_cvref_t<Receiver>>
            rec;
        word_sender snd;

        auto start() & noexcept -> void { snd.run(rec); }
    };

    template <typename Receiver>
    [[nodiscard]] auto connect(Receiver &&r) && -> op_state<Receiver> {
        return op_state<Receiver>{
            receiver_adapter<block_outcome, error_type,
                             std::remove_cvref_t<Receiver>>{
                std::forward<Receiver>(r)},
            *this};
    }

  private:
    auto run(abstract_receiver<block_outcome, error_type> &rec) -> void;

    machine::compiled_program<MaxCode, MaxWords> const *program_;
    state_type *state_;
    int entry_;
    machine::dictionary<DictWords, DictName> *dict_;
    int *fuel_;
};

template <int MaxCode, int MaxWords, int MaxDepth, int MaxRDepth, int MaxData,
          int MaxOut, int MaxBlocks, int DictWords, int DictName>
auto word_sender<MaxCode, MaxWords, MaxDepth, MaxRDepth, MaxData, MaxOut,
                 MaxBlocks, DictWords, DictName>::
    run(abstract_receiver<block_outcome, error_type> &rec) -> void {
    using machine::instr;
    using machine::op;
    auto const &program = *program_;
    auto &state = *state_;

    int const word_end = word_body_end(program, entry_);

    // D24's own named boundary (this header's own top comment): a word that
    // touches the return stack as data anywhere in its own body cannot be
    // trusted to refunctionalize `call`/`ret` correctly, since this
    // lowering elides the return address such a word might be reading.
    // Falls back to `vm.hpp`'s own run_from for this word's *whole* body --
    // never a partial lowering -- wrapped as one value-only step.
    if (word_uses_return_stack_data(program, entry_, word_end)) {
        auto status = run_word_via_vm(program, state, entry_, dict_, *fuel_);
        if (!status.has_value()) {
            auto const &diag = status.error();
            // run_word_via_vm's own doc comment: an escaping THROW comes
            // back as run_from's own standard "uncaught" diagnosis (its
            // own handler_depth was hidden for the call), recognizable by
            // this exact message -- re-number it here so an enclosing
            // sender-level CATCH (this word_sender's own caller, wrapping
            // this whole fallback step with upon_error) still catches it
            // correctly, exactly as a natively-lowered THROW would be.
            bool const was_uncaught_throw =
                diag.message != nullptr &&
                std::string_view{diag.message} ==
                    "uncaught THROW (code in foundation::parse_error::"
                    "where.offset)";
            if (was_uncaught_throw) {
                rec.error(error_type{
                    .numbered = true,
                    .n = static_cast<machine::cell>(diag.where.offset),
                    .state = &state});
            } else {
                rec.error(error_type{
                    .numbered = false, .diag = diag, .state = &state});
            }
            return;
        }
        rec.value(block_outcome{transfer_kind::ret, -1});
        return;
    }

    auto blocks_r =
        interpreter::recover_basic_blocks<MaxBlocks>(program, entry_, word_end);
    if (!blocks_r.has_value()) {
        rec.error( error_type{.numbered = false,
                                             .diag = blocks_r.error(),
                                             .state = &state});
        return;
    }
    auto const &blocks = blocks_r.value();

    int ip = entry_;
    for (;;) {
        if (*fuel_ <= 0) {
            rec.stopped();
            return;
        }
        --*fuel_;

        interpreter::basic_block const *blk = nullptr;
        for (int i = 0; i < blocks.size(); ++i) {
            if (blocks[i].start == ip) {
                blk = &blocks[i];
                break;
            }
        }
        if (blk == nullptr) {
            rec.error(
                error_type{
                    .numbered = false,
                    .diag = foundation::parse_error{
                        foundation::source_pos{},
                        "sender lowering: instruction pointer left its own "
                        "recovered block range"},
                    .state = &state});
            return;
        }


        // Every other block: a straight-line run of ordinary instructions,
        // possibly ending in one genuine terminator this block's own
        // sender resolves into the next @ref block_outcome -- D24's own
        // "one composed sender per block", @ref word_sender::run's own
        // trampoline loop driving exactly one fresh instance of it here per
        // pass.
        auto step = make_inline_sender<block_outcome, error_type>(
            [&](auto rec2) -> void {
                bool const last_is_terminator = [&] {
                    switch (program.code[blk->end - 1].code) {
                    case op::ret:
                    case op::does_enter:
                    case op::halt:
                    case op::branch:
                    case op::branch0:
                    case op::loop_step:
                    case op::plus_loop_step:
                    case op::leave:
                        return true;
                    default:
                        return false;
                    }
                }();
                int const straight_end =
                    last_is_terminator ? blk->end - 1 : blk->end;

                for (int i = blk->start; i < straight_end; ++i) {
                    instr const &in = program.code[i];
                    switch (in.code) {
                    case op::push:
                    case op::push_xt: {
                        auto r = state.data().push(in.operand);
                        if (!r.has_value()) {
                            set_error(std::move(rec2),
                                      error_type{.numbered = false,
                                                 .diag = r.error(),
                                                 .state = &state});
                            return;
                        }
                        break;
                    }
                    case op::prim: {
                        if (static_cast<machine::primitive>(in.operand) ==
                            machine::primitive::catch_ok) {
                            // Defensive only (D7): the block-recovery fix
                            // above (DIV-0025) always isolates catch_ok
                            // immediately after catch_mark into that same
                            // compound block, handled before this sender is
                            // even built -- a generic prim dispatch should
                            // never reach it.
                            set_error(
                                std::move(rec2),
                                error_type{
                                    .numbered = false,
                                    .diag = foundation::parse_error{
                                        foundation::source_pos{},
                                        "sender lowering: catch_ok reached "
                                        "outside CATCH's own compound step"},
                                    .state = &state});
                            return;
                        }
                        auto r = machine::apply_primitive(
                            static_cast<machine::primitive>(in.operand),
                            state);
                        if (!r.has_value()) {
                            if (machine::is_abort_quote_condition(r.error())) {
                                set_error(
                                    std::move(rec2),
                                    error_type{.numbered = true,
                                               .n = machine::cell{-2},
                                               .state = &state});
                                return;
                            }
                            if (state.handler_depth() >= 0) {
                                auto mapped =
                                    machine::machine_fault_throw_code(
                                        r.error());
                                if (mapped.has_value()) {
                                    set_error(std::move(rec2),
                                              error_type{
                                                  .numbered = true,
                                                  .n = mapped.value(),
                                                  .state = &state});
                                    return;
                                }
                            }
                            set_error(std::move(rec2),
                                      error_type{.numbered = false,
                                                 .diag = r.error(),
                                                 .state = &state});
                            return;
                        }
                        break;
                    }
                    case op::call:
                    case op::execute: {
                        int target = 0;
                        if (in.code == op::call) {
                            target = static_cast<int>(in.operand);
                        } else {
                            auto t = state.data().pop();
                            if (!t.has_value()) {
                                set_error(std::move(rec2),
                                          error_type{.numbered = false,
                                                     .diag = t.error(),
                                                     .state = &state});
                                return;
                            }
                            target = static_cast<int>(t.value());
                        }
                        // Refunctionalized `call`/`EXECUTE` (this header's
                        // own top comment): an ordinary recursive C++ call
                        // into a fresh word_sender, not a return-stack
                        // push. Its own value/error/stopped channel is
                        // propagated immediately -- @ref halt in
                        // particular must stop *this* run too, not just
                        // resume the caller (vm.hpp's own run_from would
                        // stop unconditionally at halt regardless of call
                        // depth; D14 requires the same final behavior
                        // here).
                        word_sender callee{program_, state_, target, dict_,
                                           fuel_};
                        auto out =
                            drive<block_outcome, error_type>(std::move(callee));
                        if (out.stopped) {
                            set_stopped(std::move(rec2));
                            return;
                        }
                        if (out.error.has_value()) {
                            set_error(std::move(rec2), *out.error);
                            return;
                        }
                        if (out.value->kind == transfer_kind::halt) {
                            set_value(std::move(rec2),
                                      block_outcome{transfer_kind::halt, -1});
                            return;
                        }
                        break;
                    }
                    case op::create_word: {
                        if (dict_ == nullptr) {
                            set_error(
                                std::move(rec2),
                                error_type{
                                    .numbered = false,
                                    .diag = foundation::parse_error{
                                        foundation::source_pos{},
                                        "CREATE: no dictionary available to "
                                        "this run"},
                                    .state = &state});
                            return;
                        }
                        auto r = machine::create_here(
                            *dict_, state, static_cast<int>(in.operand));
                        if (!r.has_value()) {
                            set_error(std::move(rec2),
                                      error_type{.numbered = false,
                                                 .diag = r.error(),
                                                 .state = &state});
                            return;
                        }
                        break;
                    }
                    case op::do_setup: {
                        auto start = state.data().pop();
                        if (!start.has_value()) {
                            set_error(std::move(rec2),
                                      error_type{.numbered = false,
                                                 .diag = start.error(),
                                                 .state = &state});
                            return;
                        }
                        auto limit = state.data().pop();
                        if (!limit.has_value()) {
                            set_error(std::move(rec2),
                                      error_type{.numbered = false,
                                                 .diag = limit.error(),
                                                 .state = &state});
                            return;
                        }
                        if (auto r = state.returns().push(limit.value());
                            !r.has_value()) {
                            set_error(std::move(rec2),
                                      error_type{.numbered = false,
                                                 .diag = r.error(),
                                                 .state = &state});
                            return;
                        }
                        if (auto r = state.returns().push(start.value());
                            !r.has_value()) {
                            set_error(std::move(rec2),
                                      error_type{.numbered = false,
                                                 .diag = r.error(),
                                                 .state = &state});
                            return;
                        }
                        break;
                    }
                    case op::push_index: {
                        int const level = static_cast<int>(in.operand);
                        auto v = state.returns().peek(2 * level);
                        if (!v.has_value()) {
                            set_error(std::move(rec2),
                                      error_type{.numbered = false,
                                                 .diag = v.error(),
                                                 .state = &state});
                            return;
                        }
                        auto r = state.data().push(v.value());
                        if (!r.has_value()) {
                            set_error(std::move(rec2),
                                      error_type{.numbered = false,
                                                 .diag = r.error(),
                                                 .state = &state});
                            return;
                        }
                        break;
                    }
                    case op::unloop: {
                        if (auto r = state.returns().pop(); !r.has_value()) {
                            set_error(std::move(rec2),
                                      error_type{.numbered = false,
                                                 .diag = r.error(),
                                                 .state = &state});
                            return;
                        }
                        if (auto r = state.returns().pop(); !r.has_value()) {
                            set_error(std::move(rec2),
                                      error_type{.numbered = false,
                                                 .diag = r.error(),
                                                 .state = &state});
                            return;
                        }
                        break;
                    }
                    case op::catch_mark: {
                        // `CATCH` (D24, D11): a compound step handled
                        // inline, wherever it appears within a block --
                        // *not* only at a block's own start. `catch_mark`
                        // is a genuine branch for interpreter::
                        // recover_basic_blocks's own leader computation (it
                        // contributes both its own fallthrough and its own
                        // resume-ip as leaders, DIV-0025's own edge fix),
                        // but that only guarantees `catch_ok`'s own
                        // position and the resume ip are block
                        // boundaries -- not that `catch_mark` itself is
                        // one, since whatever compiles the xt onto the data
                        // stack (`['] BOOM` here, `push_xt`) still precedes
                        // it in the very same block. So this case both pops
                        // the xt *and* consumes `catch_ok` (the very next
                        // instruction, always emitted immediately after by
                        // both of interp.hpp's own `CATCH` cases) itself,
                        // then completes this whole block right here with
                        // `jump(resume_ip)` -- resume_ip is `catch_mark`'s
                        // own paired leader, generally in a *different*
                        // recovered block than whatever precedes `CATCH`
                        // here, so continuing this block's own scan past it
                        // would be wrong regardless.
                        int const resume_ip = static_cast<int>(in.operand);
                        auto xt = state.data().pop();
                        if (!xt.has_value()) {
                            set_error(std::move(rec2),
                                      error_type{.numbered = false,
                                                 .diag = xt.error(),
                                                 .state = &state});
                            return;
                        }
                        int const saved_data_depth = state.data().depth();
                        int const saved_return_depth =
                            state.returns().depth();
                        int const prev_handler = state.handler_depth();
                        // Any non-negative sentinel marks a handler active;
                        // this lowering never materializes a return-stack
                        // frame for CATCH (this header's own top comment),
                        // so unlike vm.hpp's own frame_base this value
                        // names no real stack cell -- it only has to be
                        // restorable and distinct enough to nest correctly,
                        // which the return-stack depth already is.
                        state.set_handler_depth(saved_return_depth);

                        word_sender callee{program_, state_,
                                           static_cast<int>(xt.value()),
                                           dict_, fuel_};
                        auto adapted = upon_error(
                            then(std::move(callee),
                                 [](block_outcome) noexcept -> machine::cell {
                                     return machine::cell{0};
                                 }),
                            [&](error_type const &e) noexcept
                                -> machine::cell {
                                if (!e.numbered) {
                                    // Defensive only (D7): with this
                                    // handler now active, every fault
                                    // reachable inside `callee`'s own
                                    // dynamic extent (short of a
                                    // still-more-nested CATCH intercepting
                                    // it first) resolves through the exact
                                    // same handler_depth()-active path
                                    // vm.hpp's own run_from uses, which
                                    // always yields a numbered code. A raw
                                    // diagnosis reaching here would mean
                                    // that invariant broke; surface it as
                                    // an unmapped, unusual THROW code
                                    // rather than losing the diagnosis.
                                    return machine::cell{-256};
                                }
                                // CATCH as the error-to-value adapter, D24:
                                // restore the Forth stacks to exactly what
                                // this handler saved, discarding whatever
                                // the caught execution left above them,
                                // then hand back its own thrown code.
                                state.data().truncate(saved_data_depth);
                                state.returns().truncate(saved_return_depth);
                                return e.n;
                            });

                        auto landing =
                            drive<machine::cell, std::monostate>(
                                std::move(adapted));
                        if (landing.stopped) {
                            set_stopped(std::move(rec2));
                            return;
                        }
                        state.set_handler_depth(prev_handler);
                        auto pushed =
                            state.data().push(landing.value.value());
                        if (!pushed.has_value()) {
                            set_error(std::move(rec2),
                                      error_type{.numbered = false,
                                                 .diag = pushed.error(),
                                                 .state = &state});
                            return;
                        }
                        set_value(std::move(rec2),
                                  block_outcome{transfer_kind::jump,
                                               resume_ip});
                        return;
                    }
                    case op::throw_op: {
                        // `THROW` (D24, D11): the error channel, always
                        // carrying a numbered code -- even with no handler
                        // active, since @ref to_status renders an
                        // unintercepted numbered code identically to
                        // vm.hpp's own uncaught-THROW diagnosis either way,
                        // so there is no separate "raw" case to construct
                        // here (contrast a primitive fault above, which
                        // only ever numbers when a handler is active).
                        auto n = state.data().pop();
                        if (!n.has_value()) {
                            set_error(std::move(rec2),
                                      error_type{.numbered = false,
                                                 .diag = n.error(),
                                                 .state = &state});
                            return;
                        }
                        if (n.value() == 0) {
                            break;
                        }
                        set_error(std::move(rec2),
                                  error_type{.numbered = true,
                                             .n = n.value(),
                                             .state = &state});
                        return;
                    }
                    default:
                        // Defensive only (D7): every non-terminator,
                        // non-branch opcode is listed above; only a
                        // terminator (handled below) or a genuine branch
                        // (never placed mid-block by interpreter::
                        // recover_basic_blocks's own leader computation)
                        // can reach here otherwise.
                        set_error(
                            std::move(rec2),
                            error_type{
                                .numbered = false,
                                .diag = foundation::parse_error{
                                    foundation::source_pos{},
                                    "sender lowering: unexpected opcode "
                                    "inside a straight-line block"},
                                .state = &state});
                        return;
                    }
                }

                if (!last_is_terminator) {
                    set_value(std::move(rec2),
                              block_outcome{transfer_kind::fallthrough,
                                           blk->end});
                    return;
                }

                instr const &last = program.code[blk->end - 1];
                switch (last.code) {
                case op::ret:
                    set_value(std::move(rec2),
                              block_outcome{transfer_kind::ret, -1});
                    return;
                case op::does_enter: {
                    if (dict_ == nullptr) {
                        set_error(
                            std::move(rec2),
                            error_type{
                                .numbered = false,
                                .diag = foundation::parse_error{
                                    foundation::source_pos{},
                                    "DOES>: no dictionary available to this "
                                    "run"},
                                .state = &state});
                        return;
                    }
                    auto r = dict_->attach_does(blk->end);
                    if (!r.has_value()) {
                        set_error(std::move(rec2),
                                  error_type{.numbered = false,
                                             .diag = r.error(),
                                             .state = &state});
                        return;
                    }
                    set_value(std::move(rec2),
                              block_outcome{transfer_kind::ret, -1});
                    return;
                }
                case op::halt:
                    set_value(std::move(rec2),
                              block_outcome{transfer_kind::halt, -1});
                    return;
                case op::branch:
                    set_value(std::move(rec2),
                              block_outcome{transfer_kind::jump,
                                           static_cast<int>(last.operand)});
                    return;
                case op::branch0: {
                    auto flag = state.data().pop();
                    if (!flag.has_value()) {
                        set_error(std::move(rec2),
                                  error_type{.numbered = false,
                                             .diag = flag.error(),
                                             .state = &state});
                        return;
                    }
                    int const target = flag.value() == 0
                                            ? static_cast<int>(last.operand)
                                            : blk->end;
                    set_value(std::move(rec2),
                              block_outcome{transfer_kind::jump, target});
                    return;
                }
                case op::loop_step: {
                    auto index = state.returns().pop();
                    if (!index.has_value()) {
                        set_error(std::move(rec2),
                                  error_type{.numbered = false,
                                             .diag = index.error(),
                                             .state = &state});
                        return;
                    }
                    auto limit = state.returns().peek(0);
                    if (!limit.has_value()) {
                        set_error(std::move(rec2),
                                  error_type{.numbered = false,
                                             .diag = limit.error(),
                                             .state = &state});
                        return;
                    }
                    machine::cell const next = index.value() + 1;
                    if (next == limit.value()) {
                        if (auto r = state.returns().pop(); !r.has_value()) {
                            set_error(std::move(rec2),
                                      error_type{.numbered = false,
                                                 .diag = r.error(),
                                                 .state = &state});
                            return;
                        }
                        set_value(std::move(rec2),
                                  block_outcome{transfer_kind::jump, blk->end});
                        return;
                    }
                    if (auto r = state.returns().push(next); !r.has_value()) {
                        set_error(std::move(rec2),
                                  error_type{.numbered = false,
                                             .diag = r.error(),
                                             .state = &state});
                        return;
                    }
                    set_value(std::move(rec2),
                              block_outcome{transfer_kind::jump,
                                           static_cast<int>(last.operand)});
                    return;
                }
                case op::plus_loop_step: {
                    auto incr = state.data().pop();
                    if (!incr.has_value()) {
                        set_error(std::move(rec2),
                                  error_type{.numbered = false,
                                             .diag = incr.error(),
                                             .state = &state});
                        return;
                    }
                    auto index = state.returns().pop();
                    if (!index.has_value()) {
                        set_error(std::move(rec2),
                                  error_type{.numbered = false,
                                             .diag = index.error(),
                                             .state = &state});
                        return;
                    }
                    auto limit = state.returns().peek(0);
                    if (!limit.has_value()) {
                        set_error(std::move(rec2),
                                  error_type{.numbered = false,
                                             .diag = limit.error(),
                                             .state = &state});
                        return;
                    }
                    machine::cell const before = index.value() - limit.value();
                    machine::cell const next = index.value() + incr.value();
                    machine::cell const after = next - limit.value();
                    if ((before ^ after) < 0) {
                        if (auto r = state.returns().pop(); !r.has_value()) {
                            set_error(std::move(rec2),
                                      error_type{.numbered = false,
                                                 .diag = r.error(),
                                                 .state = &state});
                            return;
                        }
                        set_value(std::move(rec2),
                                  block_outcome{transfer_kind::jump, blk->end});
                        return;
                    }
                    if (auto r = state.returns().push(next); !r.has_value()) {
                        set_error(std::move(rec2),
                                  error_type{.numbered = false,
                                             .diag = r.error(),
                                             .state = &state});
                        return;
                    }
                    set_value(std::move(rec2),
                              block_outcome{transfer_kind::jump,
                                           static_cast<int>(last.operand)});
                    return;
                }
                case op::leave: {
                    if (auto r = state.returns().pop(); !r.has_value()) {
                        set_error(std::move(rec2),
                                  error_type{.numbered = false,
                                             .diag = r.error(),
                                             .state = &state});
                        return;
                    }
                    if (auto r = state.returns().pop(); !r.has_value()) {
                        set_error(std::move(rec2),
                                  error_type{.numbered = false,
                                             .diag = r.error(),
                                             .state = &state});
                        return;
                    }
                    set_value(std::move(rec2),
                              block_outcome{transfer_kind::jump,
                                           static_cast<int>(last.operand)});
                    return;
                }
                default:
                    // Unreachable (D7, defensive): last_is_terminator's own
                    // switch above lists exactly these opcodes.
                    set_error(
                        std::move(rec2),
                        error_type{
                            .numbered = false,
                            .diag = foundation::parse_error{
                                foundation::source_pos{},
                                "sender lowering: unreachable terminator"},
                            .state = &state});
                    return;
                }
            });

        auto outcome = drive<block_outcome, error_type>(std::move(step));
        if (outcome.stopped) {
            rec.stopped();
            return;
        }
        if (outcome.error.has_value()) {
            rec.error( *outcome.error);
            return;
        }
        auto const &bo = outcome.value.value();
        if (bo.kind == transfer_kind::ret || bo.kind == transfer_kind::halt) {
            rec.value(bo);
            return;
        }
        ip = bo.target;
    }
}

/// Runs @p program against @p state, starting at @p entry, via this
/// component's own sender lowering (D24) rather than `vm.hpp`'s own
/// `run_from` -- a second executor of the exact same instruction range
/// (D14). This is this component's own single, true top-level entry point,
/// and the one place it uses the real @ref sync_wait rather than
/// @ref drive: @p sender_word's own error/stopped channels are both
/// unconditionally adapted to an ordinary @ref machine::status *value*
/// first (`upon_error`/`upon_stopped`, D24's own adapters again), so the
/// sender @ref sync_wait actually observes here always completes on the
/// value channel -- it can never reach its own set_error-to-exception path
/// (D7), and this call site is not "inside" any other sender's own
/// evaluation (the plan's own "never sync_wait inside evaluation"), since
/// nothing composes further with whatever this function returns.
template <int MaxBlocks = 128, int DictWords = 256, int DictName = 32,
          int MaxCode, int MaxWords, int MaxDepth, int MaxRDepth, int MaxData,
          int MaxOut>
[[nodiscard]] auto run_from_via_senders(
    machine::compiled_program<MaxCode, MaxWords> const &program,
    machine::forth_state<MaxDepth, MaxRDepth, MaxData, MaxOut> &state,
    int entry, int fuel = 100000,
    machine::dictionary<DictWords, DictName> *dict = nullptr)
    -> machine::status {
    using word_sender_type =
        word_sender<MaxCode, MaxWords, MaxDepth, MaxRDepth, MaxData, MaxOut,
                   MaxBlocks, DictWords, DictName>;
    using error_type = typename word_sender_type::error_type;

    word_sender_type top{&program, &state, entry, dict, &fuel};
    auto value_adapted = then(std::move(top),
                              [](block_outcome) noexcept -> machine::status {
                                  return std::monostate{};
                              });
    auto error_adapted =
        upon_error(std::move(value_adapted),
                   [](error_type const &e) noexcept -> machine::status {
                       return to_status(e);
                   });
    auto stop_adapted = upon_stopped(
        std::move(error_adapted), []() noexcept -> machine::status {
            return foundation::parse_error{
                foundation::source_pos{},
                "vm execution budget exhausted"};
        });

    auto result = sync_wait(std::move(stop_adapted));
    if (!result.has_value()) {
        // Defensive only (D7): stop_adapted's own value channel is the
        // only channel left by construction (see this function's own doc
        // comment); sync_wait itself only ever returns a disengaged
        // optional for a genuine, unadapted set_stopped.
        return foundation::parse_error{
            foundation::source_pos{},
            "sender lowering: run ended with no result"};
    }
    return std::get<0>(result.value());
}

/// Runs @p program from its own @ref machine::compiled_program::program_entry
/// via senders, seeding @p state's own data space first exactly like
/// `vm.hpp`'s own `run` does (F16, D10) -- the sender-backend counterpart to
/// `machine::run`, for symmetry.
template <int MaxBlocks = 128, int DictWords = 256, int DictName = 32,
          int MaxCode, int MaxWords, int MaxDepth, int MaxRDepth, int MaxData,
          int MaxOut>
[[nodiscard]] auto
run_via_senders(machine::compiled_program<MaxCode, MaxWords> const &program,
                machine::forth_state<MaxDepth, MaxRDepth, MaxData, MaxOut>
                    &state,
                int fuel = 100000) -> machine::status {
    auto data_init = state.data_space().allot(program.data_space_size);
    if (!data_init.has_value()) {
        return data_init.error();
    }
    return run_from_via_senders<MaxBlocks, DictWords, DictName>(
        program, state, program.program_entry, fuel);
}

} // namespace smd::forth::sender

#endif // INCLUDED_SMD_FORTH_SENDER_LOWER
