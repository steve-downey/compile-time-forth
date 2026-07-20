// src/smd/forth/elaborator/stack_effect.test.cpp                    -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <smd/forth/elaborator/stack_effect.hpp>
#include <smd/forth/elaborator/stack_effect.hpp> // test 2nd include OK

#include <smd/forth/elaborator/elaborate.hpp>
#include <smd/forth/reader/read_program.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string_view>

using smd::forth::elaborator::combine_branch;
using smd::forth::elaborator::combine_sequential;
using smd::forth::elaborator::elaborate;
using smd::forth::elaborator::has_declared_effect;
using smd::forth::elaborator::identity_effect;
using smd::forth::elaborator::known;
using smd::forth::elaborator::parse_declared_effect;
using smd::forth::elaborator::primitive_data_effect;
using smd::forth::elaborator::primitive_return_delta;
using smd::forth::elaborator::unknown_effect;
using smd::forth::foundation::parse_error;
using smd::forth::foundation::source_pos;
using smd::forth::foundation::source_span;
using smd::forth::machine::colon_word;
using smd::forth::machine::primitive;
using smd::forth::machine::stack_effect;
using smd::forth::reader::read_program;

TEST_CASE("StackEffectTest - HeaderIsIdempotent") { REQUIRE(true); }

// -- Lattice primitives, in isolation --------------------------------------

static_assert(known(1, 1).net() == 0);
static_assert(known(1, 2).net() == 1);
static_assert(!unknown_effect.known);
static_assert(identity_effect == known(0, 0));

// Identity element: combining with identity_effect on either side is a no-op.
static_assert(combine_sequential(identity_effect, known(2, 3)) == known(2, 3));
static_assert(combine_sequential(known(2, 3), identity_effect) == known(2, 3));

// "DUP *" == SQUARED's body: (1,2) then (2,1) composes to (1,1).
static_assert(combine_sequential(known(1, 2), known(2, 1)) == known(1, 1));

// "1 +": needs one operand already on the stack, leaves one behind.
static_assert(combine_sequential(known(0, 1), known(2, 1)) == known(1, 1));

// Unknown poisons sequencing from either side.
static_assert(!combine_sequential(unknown_effect, known(1, 1)).known);
static_assert(!combine_sequential(known(1, 1), unknown_effect).known);

// IF/ELSE combination: NEGATE (1,1), net 0, vs. an empty ELSE-less arm (0,0),
// net 0 -- same net, different shape (see ABS below); combine_branch reflects
// "the flag plus whichever arm needs more, net-adjusted".
static_assert(combine_branch(known(1, 1), known(0, 0)) == known(2, 1));

// -- Primitive net-effect table ---------------------------------------------

static_assert(primitive_data_effect(primitive::plus) == known(2, 1));
static_assert(primitive_data_effect(primitive::dup) == known(1, 2));
static_assert(primitive_data_effect(primitive::drop) == known(1, 0));
static_assert(primitive_data_effect(primitive::swap) == known(2, 2));
static_assert(primitive_data_effect(primitive::over) == known(2, 3));
static_assert(primitive_data_effect(primitive::rot) == known(3, 3));
static_assert(primitive_data_effect(primitive::depth) == known(0, 1));
static_assert(primitive_data_effect(primitive::true_) == known(0, 1));
static_assert(!primitive_data_effect(primitive::question_dup).known);
static_assert(primitive_data_effect(primitive::to_r) == known(1, 0));
static_assert(primitive_data_effect(primitive::r_from) == known(0, 1));
static_assert(primitive_data_effect(primitive::r_fetch) == known(0, 1));

static_assert(primitive_return_delta(primitive::to_r) == 1);
static_assert(primitive_return_delta(primitive::r_from) == -1);
static_assert(primitive_return_delta(primitive::r_fetch) == 0);
static_assert(primitive_return_delta(primitive::dup) == 0);

// -- Declared-effect parsing --------------------------------------------------

static_assert(!has_declared_effect(source_span{}));
static_assert(has_declared_effect(source_span{source_pos{0, 1, 1},
                                              source_pos{1, 1, 2}}));

static_assert([] {
    constexpr std::string_view text = "( n -- n )";
    constexpr source_span span{source_pos{0, 1, 1},
                               source_pos{static_cast<int>(text.size()), 1,
                                          static_cast<int>(text.size()) + 1}};
    return parse_declared_effect(text, span) == known(1, 1);
}());

static_assert([] {
    constexpr std::string_view text = "( -- n )";
    constexpr source_span span{source_pos{0, 1, 1},
                               source_pos{static_cast<int>(text.size()), 1,
                                          static_cast<int>(text.size()) + 1}};
    return parse_declared_effect(text, span) == known(0, 1);
}());

namespace {

// Small capacities: these tests never need the header's production defaults.
constexpr int test_max_nodes = 128;
constexpr int test_max_body = 16;
constexpr int test_max_name = 16;
constexpr int test_max_depth = 8;
constexpr int test_max_words = 64;
constexpr int test_max_data = 16;
constexpr int test_max_warnings = 8;

constexpr auto elaborate_source(std::string_view source) {
    auto tree = read_program<test_max_nodes, test_max_body, test_max_name,
                             test_max_depth>(source);
    return elaborate<test_max_nodes, test_max_body, test_max_name,
                     test_max_words, test_max_data, test_max_warnings>(
        tree.value(), source);
}

constexpr auto effect_of(std::string_view source, std::string_view name)
    -> stack_effect {
    auto unit = elaborate_source(source);
    auto const *entry = unit.value().dictionary.lookup(name);
    return std::get<colon_word>(entry->binding).effect;
}

} // namespace

// -- Positive merge criteria: declared-effect verification -------------------

// `: SQUARED ( n -- n ) DUP * ;` -- the plan's own merge-criterion example.
static_assert(effect_of(": SQUARED ( n -- n ) DUP * ;", "SQUARED") ==
              stack_effect{.inputs = 1, .outputs = 1, .known = true});

// `: ABS ( n -- n ) DUP 0< IF NEGATE THEN ;` -- exercises the "IF/ELSE arms
// compared by net effect, not full (inputs,outputs) shape" design decision:
// NEGATE (1,1) vs. the implicit empty ELSE (0,0) have equal net effect (0)
// despite different shapes, so this canonical Forth-2012 idiom type-checks.
static_assert(effect_of(": ABS ( n -- n ) DUP 0< IF NEGATE THEN ;", "ABS") ==
              stack_effect{.inputs = 1, .outputs = 1, .known = true});

// A correct multi-word program: DOUBLE has no declared effect (computed from
// its primitives), QUADRUPLE calls it twice and its own computed effect
// composes correctly through two core_call lookups.
static_assert([] {
    auto unit =
        elaborate_source(": DOUBLE DUP + ; : QUADRUPLE DOUBLE DOUBLE ;");
    if (!unit.has_value()) {
        return false;
    }
    auto const &u = unit.value();
    auto const &double_cw =
        std::get<colon_word>(u.dictionary.lookup("DOUBLE")->binding);
    auto const &quad_cw =
        std::get<colon_word>(u.dictionary.lookup("QUADRUPLE")->binding);
    return double_cw.effect ==
               stack_effect{.inputs = 1, .outputs = 1, .known = true} &&
           quad_cw.effect ==
               stack_effect{.inputs = 1, .outputs = 1, .known = true};
}());

// RECURSE's self-effect fixed-point question (rule 6): a declared effect is
// trusted as the assumed effect of the RECURSE call site.
static_assert(effect_of(": LOOP1 ( n -- n ) RECURSE ;", "LOOP1") ==
              stack_effect{.inputs = 1, .outputs = 1, .known = true});

// -- Diagnosis 1: IF/ELSE arms with unequal net effect -----------------------

static_assert([] {
    auto unit = elaborate_source(": BADCOND IF DROP THEN ;");
    return !unit.has_value() &&
           unit.error() ==
               parse_error{source_pos{10, 1, 11},
                           "IF/ELSE arms have unequal net stack effect"};
}());

// -- Diagnosis 2: loop bodies with nonzero net effect ------------------------

static_assert([] {
    auto unit = elaborate_source(": BADLOOP DO DUP LOOP ;");
    return !unit.has_value() &&
           unit.error() ==
               parse_error{source_pos{10, 1, 11},
                           "DO loop body must have a net-zero stack effect"};
}());

static_assert([] {
    auto unit = elaborate_source(": BADUNTIL BEGIN DUP DUP UNTIL ;");
    return !unit.has_value() &&
           unit.error() ==
               parse_error{
                   source_pos{11, 1, 12},
                   "BEGIN...UNTIL body must leave exactly one flag for UNTIL"};
}());

static_assert([] {
    auto unit =
        elaborate_source(": BADWHILE BEGIN DUP DUP WHILE DROP REPEAT ;");
    return !unit.has_value() &&
           unit.error() ==
               parse_error{source_pos{11, 1, 12},
                           "BEGIN...WHILE condition must leave exactly one "
                           "flag for WHILE"};
}());

// -- Diagnosis 3: >R/R> imbalance across a control boundary ------------------

static_assert([] {
    auto unit = elaborate_source(": BADRET IF >R THEN ;");
    return !unit.has_value() &&
           unit.error() == parse_error{source_pos{9, 1, 10},
                                       "return stack is unbalanced across a "
                                       "control-structure body"};
}());

static_assert([] {
    auto unit = elaborate_source(": BADRETEND >R ;");
    return !unit.has_value() &&
           unit.error() ==
               parse_error{
                   source_pos{0, 1, 1},
                   "return stack is unbalanced at the end of a definition"};
}());

// -- Diagnosis 4: EXIT inside a DO loop without UNLOOP -----------------------

static_assert([] {
    auto unit = elaborate_source(": DOEX DO EXIT LOOP ;");
    return !unit.has_value() &&
           unit.error() == parse_error{source_pos{10, 1, 11},
                                       "EXIT inside a DO loop requires UNLOOP"};
}());

// -- Diagnosis 5: declared effect mismatch -----------------------------------

static_assert([] {
    auto unit = elaborate_source(": BADDECL ( n -- n n ) DUP DROP ;");
    return !unit.has_value() &&
           unit.error() ==
               parse_error{source_pos{10, 1, 11},
                           "declared stack effect does not match computed "
                           "stack effect"};
}());

// -- Runtime-visible mirrors, for Catch2 visibility --------------------------

TEST_CASE("StackEffectTest - SquaredDeclaredEffectVerified") {
    CHECK(effect_of(": SQUARED ( n -- n ) DUP * ;", "SQUARED") ==
          stack_effect{.inputs = 1, .outputs = 1, .known = true});
}

TEST_CASE("StackEffectTest - AbsIfNoElseTypeChecks") {
    CHECK(effect_of(": ABS ( n -- n ) DUP 0< IF NEGATE THEN ;", "ABS") ==
          stack_effect{.inputs = 1, .outputs = 1, .known = true});
}

TEST_CASE("StackEffectTest - RecurseUsesDeclaredEffect") {
    CHECK(effect_of(": LOOP1 ( n -- n ) RECURSE ;", "LOOP1") ==
          stack_effect{.inputs = 1, .outputs = 1, .known = true});
}

TEST_CASE("StackEffectTest - IfArmsMismatchDiagnosed") {
    auto unit = elaborate_source(": BADCOND IF DROP THEN ;");
    REQUIRE_FALSE(unit.has_value());
    CHECK(unit.error() ==
          parse_error{source_pos{10, 1, 11},
                      "IF/ELSE arms have unequal net stack effect"});
}

TEST_CASE("StackEffectTest - DoLoopBodyNonzeroDiagnosed") {
    auto unit = elaborate_source(": BADLOOP DO DUP LOOP ;");
    REQUIRE_FALSE(unit.has_value());
    CHECK(unit.error() ==
          parse_error{source_pos{10, 1, 11},
                      "DO loop body must have a net-zero stack effect"});
}

TEST_CASE("StackEffectTest - UntilBodyWrongFlagCountDiagnosed") {
    auto unit = elaborate_source(": BADUNTIL BEGIN DUP DUP UNTIL ;");
    REQUIRE_FALSE(unit.has_value());
    CHECK(unit.error() ==
          parse_error{
              source_pos{11, 1, 12},
              "BEGIN...UNTIL body must leave exactly one flag for UNTIL"});
}

TEST_CASE("StackEffectTest - WhileConditionWrongFlagCountDiagnosed") {
    auto unit =
        elaborate_source(": BADWHILE BEGIN DUP DUP WHILE DROP REPEAT ;");
    REQUIRE_FALSE(unit.has_value());
    CHECK(unit.error() ==
          parse_error{
              source_pos{11, 1, 12},
              "BEGIN...WHILE condition must leave exactly one flag for WHILE"});
}

TEST_CASE("StackEffectTest - ReturnStackImbalanceAcrossControlDiagnosed") {
    auto unit = elaborate_source(": BADRET IF >R THEN ;");
    REQUIRE_FALSE(unit.has_value());
    CHECK(unit.error() ==
          parse_error{source_pos{9, 1, 10},
                      "return stack is unbalanced across a control-structure "
                      "body"});
}

TEST_CASE("StackEffectTest - ReturnStackImbalanceAtDefinitionEndDiagnosed") {
    auto unit = elaborate_source(": BADRETEND >R ;");
    REQUIRE_FALSE(unit.has_value());
    CHECK(unit.error() ==
          parse_error{source_pos{0, 1, 1},
                      "return stack is unbalanced at the end of a "
                      "definition"});
}

TEST_CASE("StackEffectTest - ExitInsideDoLoopDiagnosed") {
    auto unit = elaborate_source(": DOEX DO EXIT LOOP ;");
    REQUIRE_FALSE(unit.has_value());
    CHECK(unit.error() == parse_error{source_pos{10, 1, 11},
                                      "EXIT inside a DO loop requires "
                                      "UNLOOP"});
}

TEST_CASE("StackEffectTest - DeclaredEffectMismatchDiagnosed") {
    auto unit = elaborate_source(": BADDECL ( n -- n n ) DUP DROP ;");
    REQUIRE_FALSE(unit.has_value());
    CHECK(unit.error() ==
          parse_error{source_pos{10, 1, 11},
                      "declared stack effect does not match computed stack "
                      "effect"});
}
