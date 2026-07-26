// src/smd/forth/machine/vm.test.cpp                                 -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <smd/forth/machine/vm.hpp>
#include <smd/forth/machine/vm.hpp> // test 2nd include OK

#include <smd/forth/foundation/source_pos.hpp>
#include <smd/forth/interpreter/session.hpp>
#include <smd/forth/machine/cell.hpp>
#include <smd/forth/machine/dictionary.hpp>
#include <smd/forth/machine/emit.hpp>
#include <smd/forth/machine/forth_state.hpp>
#include <smd/forth/machine/instruction.hpp>

#include <catch2/catch_test_macros.hpp>

#include <variant>

using smd::forth::foundation::source_pos;
using smd::forth::interpreter::build_session;
using smd::forth::interpreter::call_defined_word;
using smd::forth::interpreter::seed_from_session;
using smd::forth::machine::cell;
using smd::forth::machine::compiled_program;
using smd::forth::machine::dictionary;
using smd::forth::machine::emit;
using smd::forth::machine::forth_state;
using smd::forth::machine::op;
using smd::forth::machine::primitive;
using smd::forth::machine::run;
using smd::forth::machine::run_from;
using smd::forth::machine::variable_word;

TEST_CASE("VmTest - HeaderIsIdempotent") { REQUIRE(true); }

// Step F26 ("the cut"): machine::codegen (source text -> compiled_program,
// via the R1 elaborated core) is deleted along with the rest of the R1
// pipeline (docs/divergences/DIV-0011); this file no longer has a "compile
// this source string" helper. Every compiled_program below is instead
// hand-built directly with machine::emit, the same technique
// interpreter/compilebuf.test.cpp's own hand-compiled "DUP *" example
// already uses for compile_buffer -- proving the VM's own opcode semantics
// (push/prim/call/ret/branch/branch0/halt, run vs. run_from, fuel,
// data-space seeding) does not need a front end at all, only instr values.
//
// Programs that used to reach the VM through IF/BEGIN/DO *source syntax*
// (ABS, COUNTDOWN, SPIN, the counted-loop set, ...) had their source text
// and expected results preserved verbatim in
// interpreter/control_flow_corpus.hpp for F27 -- interpret() is what will
// give the text interpreter those words back; this file does not attempt to
// hand-emit IF/BEGIN/DO bytecode to keep exercising them here. The F16
// memory-word and CREATE/ALLOT merge criteria likewise moved to the layer
// that now actually compiles source text -- interp.test.cpp's own
// MemoryWordsMergeCriterion/CreateAllotMergeCriterion -- rather than staying
// duplicated at this hand-built-bytecode layer.

namespace {

constexpr int test_max_code = 64;
constexpr int test_max_words = 8;
constexpr int test_stack_depth = 16;
constexpr int test_rstack_depth = 8;
constexpr int test_max_data = 16;
constexpr int test_out = 64;

using test_state =
    forth_state<test_stack_depth, test_rstack_depth, test_max_data, test_out>;
using test_program = compiled_program<test_max_code, test_max_words>;

/// Hand-builds "SQUARED": `: SQUARED DUP * ;  4 SQUARED`'s own shape --
/// `dup`, `star`, `ret` at instruction 0, then a top-level `push 4`,
/// `call 0`, `halt` -- without any source-text pipeline. Dictionary index 0
/// (@ref test_program::entry_points[0]) is SQUARED's own entry point, for
/// tests that call into it directly via @ref run_from.
constexpr auto squared_program() -> test_program {
    test_program p{};
    int const squared_entry = p.code.size(); // 0
    (void)emit(p, op::prim, static_cast<cell>(primitive::dup), source_pos{});
    (void)emit(p, op::prim, static_cast<cell>(primitive::star), source_pos{});
    (void)emit(p, op::ret, cell{0}, source_pos{});
    p.program_entry = p.code.size();
    (void)emit(p, op::push, cell{4}, source_pos{});
    (void)emit(p, op::call, static_cast<cell>(squared_entry), source_pos{});
    (void)emit(p, op::halt, cell{0}, source_pos{});
    p.entry_points.push_back(squared_entry);
    return p;
}

/// A program that never halts: `branch` back to its own start, forever --
/// the VM-level, hand-emitted analogue of `BEGIN FALSE UNTIL` (which is what
/// interpreter/control_flow_corpus.hpp's own spin_program preserves at the
/// source-text level for F27). Exercises @ref consume_vm_fuel's own
/// diagnosis directly, independent of any front end.
constexpr auto spin_program() -> test_program {
    test_program p{};
    (void)emit(p, op::branch, cell{0}, source_pos{});
    p.program_entry = 0;
    return p;
}

/// A program whose top level declares one cell of data space
/// (`data_space_size = 1`, set directly rather than via `VARIABLE`) and
/// stores/fetches through address 0: `run` seeds that cell before
/// executing, so `42 0 !  0 @` (Forth's own `( x a-addr -- )` argument
/// order for `!`) leaves `[42]`; @ref run_from does not, so the same
/// program run from its own entry point diagnoses the first store as out of
/// bounds against a data space nobody has seeded.
constexpr auto memory_program() -> test_program {
    test_program p{};
    p.data_space_size = 1;
    p.program_entry = p.code.size();
    (void)emit(p, op::push, cell{42}, source_pos{});
    (void)emit(p, op::push, cell{0}, source_pos{});
    (void)emit(p, op::prim, static_cast<cell>(primitive::store), source_pos{});
    (void)emit(p, op::push, cell{0}, source_pos{});
    (void)emit(p, op::prim, static_cast<cell>(primitive::fetch), source_pos{});
    (void)emit(p, op::halt, cell{0}, source_pos{});
    return p;
}

/// Step F28 (D18): `op::execute` hand-built directly, independent of `'`/
/// `[']` (which live in interp.hpp and produce these same values by scanning
/// a name): a squared-shaped stub at instruction 0 (`prim dup`, `prim star`,
/// `ret`), then a top level that pushes 6, pushes that stub's own index as
/// an execution token (`push_xt`), and `execute`s it.
constexpr auto execute_program() -> test_program {
    test_program p{};
    int const squared_entry = p.code.size(); // 0
    (void)emit(p, op::prim, static_cast<cell>(primitive::dup), source_pos{});
    (void)emit(p, op::prim, static_cast<cell>(primitive::star), source_pos{});
    (void)emit(p, op::ret, cell{0}, source_pos{});
    p.program_entry = p.code.size();
    (void)emit(p, op::push, cell{6}, source_pos{});
    (void)emit(p, op::push_xt, static_cast<cell>(squared_entry), source_pos{});
    (void)emit(p, op::execute, cell{0}, source_pos{});
    (void)emit(p, op::halt, cell{0}, source_pos{});
    return p;
}

/// Step F28 (D18/D10): `op::create_word`/`op::does_enter` hand-built
/// directly -- the VM-level shape of `: CONSTANT2 CREATE , DOES> @ ;`'s own
/// body (`interp.hpp`'s own `compile_entry` is what actually emits this
/// sequence from source text; this is the same four instructions, by hand).
/// Instruction 0 is `CONSTANT2`'s own entry point; the trailing `halt` at
/// @ref compiled_program::program_entry doubles as the "halt landing pad" a
/// caller pushes as its own return address, exactly like `interpreter::
/// compile_buffer::halt_pad` does for the real front end.
constexpr auto create_does_program() -> test_program {
    test_program p{};
    int const constant2_entry = p.code.size(); // 0
    (void)emit(p, op::create_word, cell{0}, source_pos{});
    (void)emit(p, op::prim, static_cast<cell>(primitive::comma), source_pos{});
    (void)emit(p, op::does_enter, cell{0}, source_pos{});
    (void)emit(p, op::prim, static_cast<cell>(primitive::fetch), source_pos{});
    (void)emit(p, op::ret, cell{0}, source_pos{});
    (void)constant2_entry; // == 0; named for the comment above, unused here.
    p.program_entry = p.code.size();
    (void)emit(p, op::halt, cell{0}, source_pos{});
    return p;
}

constexpr test_program squared = squared_program();
constexpr test_program spin = spin_program();
constexpr test_program memory = memory_program();
constexpr test_program execute_prog = execute_program();
constexpr test_program create_does = create_does_program();

} // namespace

// -- The survives-to-runtime proof (docs/forth-plan.md Step F14), on a
// hand-built compiled_program -----------------------------------------------

static_assert([] {
    test_state state{};
    auto r = run(squared, state, /*fuel=*/1000);
    if (!r.has_value()) {
        return false;
    }
    return state.data().depth() == 1 && state.data().peek(0).value() == 16;
}());

TEST_CASE("VmTest - SquaredMergeCriterion") {
    test_state state{};
    auto r = run(squared, state, 1000);
    REQUIRE(r.has_value());
    CHECK(state.data().depth() == 1);
    CHECK(state.data().peek(0).value() == 16);
}

static_assert([] {
    test_state state{};
    auto r = run(spin, state, /*fuel=*/10);
    return !r.has_value();
}());

TEST_CASE("VmTest - BudgetExhaustionIsDiagnosed") {
    test_state state{};
    auto r = run(spin, state, 10);
    REQUIRE_FALSE(r.has_value());
}

// A generous fuel budget does not falsely diagnose an ordinary terminating
// program.
static_assert([] {
    test_state state{};
    return run(squared, state, /*fuel=*/1000).has_value();
}());

// -- run_from: the additive entry point (step F25, DIV-0013) ---------------

static_assert([] {
    int const entry = squared.entry_points[0];
    if (entry < 0) {
        return false;
    }
    int const halt_index = squared.code.size() - 1;
    if (squared.code[halt_index].code != op::halt) {
        return false;
    }

    test_state state{};
    auto push_ret = state.returns().push(static_cast<cell>(halt_index));
    auto push_arg = state.data().push(5);
    if (!push_ret.has_value() || !push_arg.has_value()) {
        return false;
    }
    auto r = run_from(squared, state, entry, 1000);
    return r.has_value() && state.data().depth() == 1 &&
           state.data().peek(0).value() == 25;
}());

TEST_CASE("VmTest - RunFromCallsAWordsEntryPointDirectly") {
    int const entry = squared.entry_points[0];
    REQUIRE(entry >= 0);
    int const halt_index = squared.code.size() - 1;
    REQUIRE(squared.code[halt_index].code == op::halt);

    test_state state{};
    REQUIRE(state.returns().push(static_cast<cell>(halt_index)).has_value());
    REQUIRE(state.data().push(5).has_value());
    auto r = run_from(squared, state, entry, 1000);
    REQUIRE(r.has_value());
    CHECK(state.data().depth() == 1);
    CHECK(state.data().peek(0).value() == 25);
}

// memory declares one cell of data space (data_space_size == 1); run seeds
// it before its own fetch-execute loop starts, so the store/fetch succeed.
static_assert([] {
    test_state state{};
    auto r = run(memory, state, /*fuel=*/1000);
    return r.has_value() && state.data().depth() == 1 &&
           state.data().peek(0).value() == 42;
}());

TEST_CASE("VmTest - RunSeedsDataSpace") {
    test_state state{};
    auto r = run(memory, state, 1000);
    REQUIRE(r.has_value());
    CHECK(state.data().depth() == 1);
    CHECK(state.data().peek(0).value() == 42);
}

// Calling run_from directly at the same entry point, against a fresh state
// nobody has seeded, diagnoses the first store as out of bounds instead of
// silently succeeding -- the concrete evidence that run_from does not repeat
// run's own F16 seeding step.
static_assert([] {
    test_state state{};
    if (state.data_space().size() != 0) {
        return false;
    }
    auto r = run_from(memory, state, memory.program_entry, 1000);
    return !r.has_value() && state.data_space().size() == 0;
}());

TEST_CASE("VmTest - RunFromDoesNotSeedDataSpace") {
    test_state state{};
    REQUIRE(state.data_space().size() == 0);
    auto r = run_from(memory, state, memory.program_entry, 1000);
    CHECK_FALSE(r.has_value());
    CHECK(state.data_space().size() == 0);
}

// -- The survives-to-runtime proof, re-established on a session image
// (F26's own explicit merge criterion) --------------------------------------
//
// D15's session image is now what a real front end builds (interpreter::
// build_session, interpreter/session.hpp), so this is the shape F14's own
// "same compiled object, run once at compile time and once at runtime"
// proof takes going forward: build once, at namespace-scope constexpr
// initialization, from source text through the real text interpreter; run
// its own SQUARED definition again via call_defined_word (which reaches
// this same vm.hpp's own run_from under the hood, interpreter/
// compilebuf.hpp's call_word), both inside a static_assert and inside a
// Catch2 TEST_CASE, against the identical session object. (session.test.cpp
// already carries this exact proof as its own dedicated merge criterion,
// RoundTripsAtRuntimeFromTheSameConstexprObject -- it is repeated here,
// briefly, because this file is where the VM-level survives-to-runtime
// proof has always lived, and F26's own merge criteria name this file's
// tradition by name.)

namespace {

constexpr auto built_session =
    build_session<64, 96, 256, 128>(": SQUARED DUP * ;");
static_assert(built_session.has_value());

inline constexpr auto squared_session = built_session.value();

} // namespace

static_assert([] {
    auto sess = squared_session; // call_defined_word mutates sess.code's own
                                 // program_entry field; copy first.
    smd::forth::machine::forth_state<64, 64, 256, 128> state{};
    if (!seed_from_session(sess, state).has_value()) {
        return false;
    }
    if (!state.data().push(4).has_value()) {
        return false;
    }
    auto r = call_defined_word(sess, state, "SQUARED");
    return r.has_value() && state.data().depth() == 1 &&
           state.data().peek().value() == 16;
}());

TEST_CASE("VmTest - SurvivesToRuntimeProofOnASessionImage") {
    auto sess = squared_session;
    smd::forth::machine::forth_state<64, 64, 256, 128> state{};
    REQUIRE(seed_from_session(sess, state).has_value());
    REQUIRE(state.data().push(4).has_value());
    auto r = call_defined_word(sess, state, "SQUARED");
    REQUIRE(r.has_value());
    REQUIRE(state.data().depth() == 1);
    CHECK(state.data().peek().value() == 16);
}

// -- Step F28: op::execute/op::create_word/op::does_enter (D18/D10), hand-
// built directly at the VM level, independent of interp.hpp's own `'`/
// `[']`/`CREATE`/`DOES>` ---------------------------------------------------

static_assert([] {
    test_state state{};
    auto r = run(execute_prog, state, /*fuel=*/1000);
    return r.has_value() && state.data().depth() == 1 &&
           state.data().peek().value() == 36;
}());

TEST_CASE("VmTest - ExecuteJumpsToAPoppedExecutionToken") {
    test_state state{};
    auto r = run(execute_prog, state, 1000);
    REQUIRE(r.has_value());
    REQUIRE(state.data().depth() == 1);
    CHECK(state.data().peek().value() == 36);
}

// create_word/does_enter, given a real dictionary: CREATE scans "LIFE" off
// state's own SOURCE, `,` stores the pushed value, DOES> attaches the
// does-field, and invoking LIFE's own does-action afterward (a second
// run_from call, at the attached entry point) leaves the stored value.
static_assert([] {
    test_state state{"LIFE"};
    dictionary<8> dict;

    auto push_ret =
        state.returns().push(static_cast<cell>(create_does.program_entry));
    auto push_val = state.data().push(42);
    if (!push_ret.has_value() || !push_val.has_value()) {
        return false;
    }
    auto r = run_from(create_does, state, 0, 1000, &dict);
    if (!r.has_value()) {
        return false;
    }

    auto const *entry = dict.lookup("LIFE");
    if (entry == nullptr) {
        return false;
    }
    auto const *vw = std::get_if<variable_word>(&entry->binding);
    if (vw == nullptr || vw->does_entry < 0) {
        return false;
    }

    auto push_ret2 =
        state.returns().push(static_cast<cell>(create_does.program_entry));
    auto push_addr = state.data().push(static_cast<cell>(vw->address));
    if (!push_ret2.has_value() || !push_addr.has_value()) {
        return false;
    }
    auto r2 = run_from(create_does, state, vw->does_entry, 1000, &dict);
    return r2.has_value() && state.data().depth() == 1 &&
           state.data().peek().value() == 42;
}());

TEST_CASE("VmTest - CreateWordAndDoesEnterDefineAndRunANewWord") {
    test_state state{"LIFE"};
    dictionary<8> dict;

    REQUIRE(state.returns()
                .push(static_cast<cell>(create_does.program_entry))
                .has_value());
    REQUIRE(state.data().push(42).has_value());
    auto r = run_from(create_does, state, 0, 1000, &dict);
    REQUIRE(r.has_value());

    auto const *entry = dict.lookup("LIFE");
    REQUIRE(entry != nullptr);
    auto const *vw = std::get_if<variable_word>(&entry->binding);
    REQUIRE(vw != nullptr);
    REQUIRE(vw->does_entry >= 0);

    REQUIRE(state.returns()
                .push(static_cast<cell>(create_does.program_entry))
                .has_value());
    REQUIRE(state.data().push(static_cast<cell>(vw->address)).has_value());
    auto r2 = run_from(create_does, state, vw->does_entry, 1000, &dict);
    REQUIRE(r2.has_value());
    REQUIRE(state.data().depth() == 1);
    CHECK(state.data().peek().value() == 42);
}

// CREATE/DOES> without a dictionary (the default run_from/run argument, what
// every caller before this step implicitly assumed) is diagnosed, not UB.
static_assert([] {
    test_state state{"LIFE"};
    auto push_ret =
        state.returns().push(static_cast<cell>(create_does.program_entry));
    auto push_val = state.data().push(42);
    if (!push_ret.has_value() || !push_val.has_value()) {
        return false;
    }
    auto r = run_from(create_does, state, 0, 1000);
    return !r.has_value();
}());

TEST_CASE("VmTest - CreateWithoutADictionaryIsDiagnosed") {
    test_state state{"LIFE"};
    REQUIRE(state.returns()
                .push(static_cast<cell>(create_does.program_entry))
                .has_value());
    REQUIRE(state.data().push(42).has_value());
    auto r = run_from(create_does, state, 0, 1000);
    CHECK_FALSE(r.has_value());
}
