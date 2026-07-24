// src/smd/forth/machine/vm.test.cpp                                 -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <smd/forth/machine/vm.hpp>
#include <smd/forth/machine/vm.hpp> // test 2nd include OK

#include <smd/forth/elaborator/elaborate.hpp>
#include <smd/forth/foundation/static_vector.hpp>
#include <smd/forth/machine/codegen.hpp>
#include <smd/forth/machine/forth_state.hpp>
#include <smd/forth/reader/read_program.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string_view>

using smd::forth::elaborator::elaborate;
using smd::forth::machine::codegen;
using smd::forth::machine::compiled_program;
using smd::forth::machine::forth_state;
using smd::forth::machine::run;
using smd::forth::reader::read_program;

TEST_CASE("VmTest - HeaderIsIdempotent") { REQUIRE(true); }

namespace {

// Small capacities: these tests never need the header's production defaults.
constexpr int test_max_code = 512;
constexpr int test_max_nodes = 256;
constexpr int test_max_body = 32;
constexpr int test_max_name = 16;
constexpr int test_max_parse_depth = 8;
constexpr int test_max_words = 64;
constexpr int test_max_data = 16;
constexpr int test_max_warnings = 8;
constexpr int test_stack_depth = 16;
constexpr int test_rstack_depth = 8;
constexpr int test_out = 64;

using test_state =
    forth_state<test_stack_depth, test_rstack_depth, test_max_data, test_out>;
using test_program = compiled_program<test_max_code, test_max_words>;

/// Runs the whole read -> elaborate -> codegen pipeline. @pre parsing,
/// elaboration, and codegen all succeed for @p source -- the same
/// precondition @c eval_direct.test.cpp's own @c run_program documents for
/// its analogous helper.
constexpr auto compile(std::string_view source) -> test_program {
    auto tree = read_program<test_max_nodes, test_max_body, test_max_name,
                             test_max_parse_depth>(source);
    auto unit =
        elaborate<test_max_nodes, test_max_body, test_max_name, test_max_words,
                  test_max_data, test_max_warnings>(tree.value(), source);
    auto program =
        codegen<test_max_code, test_max_nodes, test_max_body, test_max_name,
                test_max_words, test_max_data, test_max_warnings>(unit.value());
    return program.value();
}

/// True if @p out's contents equal @p expect, character for character.
/// Mirrors @c eval_direct.test.cpp's identical helper -- both backends'
/// tests check output the same way, deliberately, so the two never quietly
/// drift apart on what "the same output" means.
template <int MaxOut>
constexpr auto
output_equals(smd::forth::foundation::static_vector<char, MaxOut> const &out,
              std::string_view expect) -> bool {
    if (out.size() != static_cast<int>(expect.size())) {
        return false;
    }
    for (int i = 0; i < out.size(); ++i) {
        if (out[i] != expect[static_cast<std::size_t>(i)]) {
            return false;
        }
    }
    return true;
}

// -- The survives-to-runtime proof (docs/forth-plan.md Step F14) -----------
//
// Every program below is a `constexpr` object at namespace scope, built
// once by the read -> elaborate -> codegen pipeline. Each one is then run
// through the VM twice: once inside a `static_assert` (compile time) and
// once inside a `TEST_CASE` (ordinary runtime) -- the *same* compiled
// object both times, not just the same source text re-compiled twice. This
// is F14's own merge criterion, verbatim: "the entire F13 test set passes
// through codegen+VM at compile time via static_assert and at runtime via
// Catch2 REQUIRE on the same program object declared constexpr at namespace
// scope."

// `: SQUARED DUP * ;  4 SQUARED` -- stack [16].
constexpr test_program squared_program =
    compile(": SQUARED DUP * ;  4 SQUARED");

// `: ABS DUP 0< IF NEGATE THEN ;  -7 ABS` -- stack [7].
constexpr test_program abs_program =
    compile(": ABS DUP 0< IF NEGATE THEN ;  -7 ABS");

// `: COUNTDOWN BEGIN DUP . 1- DUP 0= UNTIL DROP ;  3 COUNTDOWN` -- stack [],
// output "3 2 1 ".
constexpr test_program countdown_program =
    compile(": COUNTDOWN BEGIN DUP . 1- DUP 0= UNTIL DROP ;  3 COUNTDOWN");

// `: SPIN BEGIN FALSE UNTIL ; SPIN` -- budget exhaustion under a small fuel.
constexpr test_program spin_program =
    compile(": SPIN BEGIN FALSE UNTIL ; SPIN");

// `: UPTO3 0 BEGIN DUP 3 < WHILE 1 + REPEAT ; UPTO3` -- stack [3]. Exercises
// BEGIN...WHILE...REPEAT as real repetition through the VM, matching
// EvalDirectTest - WhileLoopIsRealRepetition.
constexpr test_program upto3_program =
    compile(": UPTO3 0 BEGIN DUP 3 < WHILE 1 + REPEAT ; UPTO3");

// `: INNER DUP 0< IF EXIT THEN DROP ; : OUTER INNER 99 ; -3 OUTER` -- stack
// [-3 99]. Exercises `ret` as EXIT and a call as its own definition
// boundary, matching EvalDirectTest - ExitUnwindsOnlyToItsOwnCallBoundary.
constexpr test_program exit_boundary_program =
    compile(": INNER DUP 0< IF EXIT THEN DROP ; : OUTER INNER 99 ; -3 OUTER");

// `VARIABLE X  5 X !  X @ 3 + X !  X @` -- stack [8], and
// `7 CONSTANT LUCKY  LUCKY LUCKY +` -- stack [14], step F16's own merge
// criterion, matching EvalDirectTest - MemoryWordsMergeCriterion.
constexpr test_program memory_words_program =
    compile("VARIABLE X  5 X !  X @ 3 + X !  X @ "
            "7 CONSTANT LUCKY  LUCKY LUCKY +");

// `CREATE BUF 4 ALLOT` -- BUF is usable as a base address; stack [30] once
// both allotted cells are stored through and summed, matching
// EvalDirectTest - CreateAllotMergeCriterion.
constexpr test_program create_allot_program = compile("CREATE BUF 4 ALLOT "
                                                      "10 BUF ! 20 BUF 3 + ! "
                                                      "BUF @ BUF 3 + @ +");

// `VARIABLE X  99 X 5 + !` -- codegen succeeds (an ordinary, well-formed
// program); the store itself is what fails at runtime, since X is a
// one-cell VARIABLE and X + 5 lands past the data space's own high-water
// mark. Matches EvalDirectTest - OutOfBoundsStoreIsDiagnosed.
constexpr test_program oob_store_program = compile("VARIABLE X  99 X 5 + !");

} // namespace

static_assert([] {
    test_state state{};
    auto r = run(squared_program, state, /*fuel=*/100000);
    if (!r.has_value()) {
        return false;
    }
    return state.data().depth() == 1 && state.data().peek(0).value() == 16;
}());

TEST_CASE("VmTest - SquaredMergeCriterion") {
    test_state state{};
    auto r = run(squared_program, state, 100000);
    REQUIRE(r.has_value());
    CHECK(state.data().depth() == 1);
    CHECK(state.data().peek(0).value() == 16);
}

static_assert([] {
    test_state state{};
    auto r = run(abs_program, state, /*fuel=*/100000);
    if (!r.has_value()) {
        return false;
    }
    return state.data().depth() == 1 && state.data().peek(0).value() == 7;
}());

TEST_CASE("VmTest - AbsMergeCriterion") {
    test_state state{};
    auto r = run(abs_program, state, 100000);
    REQUIRE(r.has_value());
    CHECK(state.data().depth() == 1);
    CHECK(state.data().peek(0).value() == 7);
}

static_assert([] {
    test_state state{};
    auto r = run(countdown_program, state, /*fuel=*/100000);
    if (!r.has_value()) {
        return false;
    }
    return state.data().depth() == 0 && output_equals(state.output(), "3 2 1 ");
}());

TEST_CASE("VmTest - CountdownMergeCriterion") {
    test_state state{};
    auto r = run(countdown_program, state, 100000);
    REQUIRE(r.has_value());
    CHECK(state.data().depth() == 0);
    CHECK(output_equals(state.output(), "3 2 1 "));
}

static_assert([] {
    test_state state{};
    auto r = run(spin_program, state, /*fuel=*/10);
    return !r.has_value();
}());

TEST_CASE("VmTest - BudgetExhaustionIsDiagnosed") {
    test_state state{};
    auto r = run(spin_program, state, 10);
    REQUIRE_FALSE(r.has_value());
}

// A generous fuel budget does not falsely diagnose an ordinary terminating
// program.
static_assert([] {
    test_state state{};
    return run(squared_program, state, /*fuel=*/1000).has_value();
}());

static_assert([] {
    test_state state{};
    auto r = run(upto3_program, state, /*fuel=*/100000);
    if (!r.has_value()) {
        return false;
    }
    return state.data().depth() == 1 && state.data().peek(0).value() == 3;
}());

TEST_CASE("VmTest - WhileLoopIsRealRepetition") {
    test_state state{};
    auto r = run(upto3_program, state, 100000);
    REQUIRE(r.has_value());
    CHECK(state.data().depth() == 1);
    CHECK(state.data().peek(0).value() == 3);
}

static_assert([] {
    test_state state{};
    auto r = run(exit_boundary_program, state, /*fuel=*/100000);
    if (!r.has_value()) {
        return false;
    }
    return state.data().depth() == 2 && state.data().peek(0).value() == 99 &&
           state.data().peek(1).value() == -3;
}());

TEST_CASE("VmTest - ExitUnwindsOnlyToItsOwnCallBoundary") {
    test_state state{};
    auto r = run(exit_boundary_program, state, 100000);
    REQUIRE(r.has_value());
    REQUIRE(state.data().depth() == 2);
    CHECK(state.data().peek(0).value() == 99);
    CHECK(state.data().peek(1).value() == -3);
}

// -- Memory words (Step F16 merge criteria) ----------------------------------

static_assert([] {
    test_state state{};
    auto r = run(memory_words_program, state, /*fuel=*/100000);
    if (!r.has_value()) {
        return false;
    }
    return state.data().depth() == 2 && state.data().peek(0).value() == 14 &&
           state.data().peek(1).value() == 8;
}());

TEST_CASE("VmTest - MemoryWordsMergeCriterion") {
    test_state state{};
    auto r = run(memory_words_program, state, 100000);
    REQUIRE(r.has_value());
    REQUIRE(state.data().depth() == 2);
    CHECK(state.data().peek(0).value() == 14);
    CHECK(state.data().peek(1).value() == 8);
}

static_assert([] {
    test_state state{};
    auto r = run(create_allot_program, state, /*fuel=*/100000);
    if (!r.has_value()) {
        return false;
    }
    return state.data().depth() == 1 && state.data().peek(0).value() == 30;
}());

TEST_CASE("VmTest - CreateAllotMergeCriterion") {
    test_state state{};
    auto r = run(create_allot_program, state, 100000);
    REQUIRE(r.has_value());
    CHECK(state.data().depth() == 1);
    CHECK(state.data().peek(0).value() == 30);
}

static_assert([] {
    test_state state{};
    auto r = run(oob_store_program, state, /*fuel=*/100000);
    return !r.has_value();
}());

TEST_CASE("VmTest - OutOfBoundsStoreIsDiagnosed") {
    test_state state{};
    auto r = run(oob_store_program, state, 100000);
    CHECK_FALSE(r.has_value());
}
