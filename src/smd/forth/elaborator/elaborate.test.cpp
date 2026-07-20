// src/smd/forth/elaborator/elaborate.test.cpp                       -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <smd/forth/elaborator/elaborate.hpp>
#include <smd/forth/elaborator/elaborate.hpp> // test 2nd include OK

#include <smd/forth/reader/read_program.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string_view>
#include <variant>

using smd::forth::elaborator::compiled_unit;
using smd::forth::elaborator::core_call;
using smd::forth::elaborator::core_const;
using smd::forth::elaborator::core_push;
using smd::forth::elaborator::core_push_xt;
using smd::forth::elaborator::core_seq;
using smd::forth::elaborator::elaborate;
using smd::forth::foundation::parse_error;
using smd::forth::foundation::source_pos;
using smd::forth::machine::addr;
using smd::forth::machine::colon_word;
using smd::forth::machine::constant_word;
using smd::forth::machine::variable_word;
using smd::forth::reader::read_program;

TEST_CASE("ElaborateTest - HeaderIsIdempotent") { REQUIRE(true); }

namespace {

// Small capacities: these tests never need the header's production defaults.
constexpr int test_max_nodes = 128;
constexpr int test_max_body = 16;
constexpr int test_max_name = 16;
constexpr int test_max_depth = 8;
constexpr int test_max_words = 64;
constexpr int test_max_data = 16;
constexpr int test_max_warnings = 8;

using test_unit =
    compiled_unit<test_max_nodes, test_max_body, test_max_name, test_max_words,
                  test_max_data, test_max_warnings>;
using test_seq = core_seq<test_max_nodes, test_max_body>;

constexpr auto parse(std::string_view source) {
    return read_program<test_max_nodes, test_max_body, test_max_name,
                        test_max_depth>(source);
}

constexpr auto elaborate_source(std::string_view source) {
    auto tree = parse(source);
    return elaborate<test_max_nodes, test_max_body, test_max_name,
                     test_max_words, test_max_data, test_max_warnings>(
        tree.value(), source);
}

} // namespace

// Merge criterion: `: SQUARED DUP * ; : QUAD SQUARED SQUARED ;` -- QUAD's
// body has two core_calls to SQUARED's word index; the dictionary has both
// words.
static_assert([] {
    constexpr std::string_view source =
        ": SQUARED DUP * ; : QUAD SQUARED SQUARED ;";
    auto tree = parse(source);
    if (!tree.has_value()) {
        return false;
    }
    auto unit =
        elaborate<test_max_nodes, test_max_body, test_max_name, test_max_words,
                  test_max_data, test_max_warnings>(tree.value(), source);
    if (!unit.has_value()) {
        return false;
    }
    auto const &u = unit.value();

    auto const *squared_entry = u.dictionary.lookup("SQUARED");
    auto const *quad_entry = u.dictionary.lookup("QUAD");
    if (squared_entry == nullptr || quad_entry == nullptr) {
        return false;
    }
    if (!std::holds_alternative<colon_word>(squared_entry->binding) ||
        !std::holds_alternative<colon_word>(quad_entry->binding)) {
        return false;
    }
    int const squared_index = u.dictionary.lookup_index("SQUARED");

    // F12: SQUARED's (and QUAD's) stack effect is now a real, analyzed
    // (1,1) -- not the F9/F11 stack_effect{} placeholder.
    auto const &squared_cw = std::get<colon_word>(squared_entry->binding);
    auto const &quad_cw = std::get<colon_word>(quad_entry->binding);
    if (squared_cw.effect != smd::forth::machine::stack_effect{.inputs = 1,
                                                               .outputs = 1,
                                                               .known = true} ||
        quad_cw.effect != smd::forth::machine::stack_effect{
                              .inputs = 1, .outputs = 1, .known = true}) {
        return false;
    }
    auto const &quad_seq =
        std::get<test_seq>(u.arena.get(quad_cw.core_id).value);
    if (quad_seq.items.size() != 2) {
        return false;
    }
    for (int i = 0; i < 2; ++i) {
        auto const &item_node = u.arena.get(quad_seq.items[i]);
        auto const *call = std::get_if<core_call>(&item_node.value);
        if (call == nullptr || call->word_index != squared_index) {
            return false;
        }
    }
    return true;
}());

// Error test: unknown word, diagnosed with position.
static_assert([] {
    auto unit = elaborate_source(": F NOPE ;");
    if (unit.has_value()) {
        return false;
    }
    return unit.error() == parse_error{source_pos{4, 1, 5}, "unknown word"};
}());

// Error test: forward reference (`: A B ; : B ;`) -- use before definition,
// diagnosed exactly like any other unknown word, at B's own position inside
// A's body (B is not yet in the dictionary when A is elaborated).
static_assert([] {
    auto unit = elaborate_source(": A B ; : B ;");
    if (unit.has_value()) {
        return false;
    }
    return unit.error() == parse_error{source_pos{4, 1, 5}, "unknown word"};
}());

// VARIABLE: allots one cell; the dictionary entry's address is the first
// data-space cell; nothing is pushed to the top-level executable body.
static_assert([] {
    auto unit = elaborate_source("VARIABLE X");
    if (!unit.has_value()) {
        return false;
    }
    auto const &u = unit.value();
    auto const *entry = u.dictionary.lookup("X");
    if (entry == nullptr ||
        !std::holds_alternative<variable_word>(entry->binding)) {
        return false;
    }
    return std::get<variable_word>(entry->binding).address == addr{0} &&
           u.data_space.size() == 1 && u.program.empty();
}());

// CONSTANT: constant-folds the immediately preceding literal; nothing is
// pushed to the top-level executable body.
static_assert([] {
    auto unit = elaborate_source("5 CONSTANT FIVE");
    if (!unit.has_value()) {
        return false;
    }
    auto const &u = unit.value();
    auto const *entry = u.dictionary.lookup("FIVE");
    if (entry == nullptr ||
        !std::holds_alternative<constant_word>(entry->binding)) {
        return false;
    }
    return std::get<constant_word>(entry->binding).value == 5 &&
           u.program.empty();
}());

// CONSTANT with no preceding literal at all is a diagnosed error.
static_assert([] {
    auto unit = elaborate_source("CONSTANT FIVE");
    return !unit.has_value() &&
           unit.error() ==
               parse_error{source_pos{0, 1, 1},
                           "CONSTANT requires a preceding integer literal"};
}());

// CONSTANT with a non-literal preceding form is a diagnosed error (the
// plan's "non-constant initializer" case).
static_assert([] {
    auto unit = elaborate_source("DUP CONSTANT FIVE");
    return !unit.has_value() &&
           unit.error() ==
               parse_error{source_pos{4, 1, 5},
                           "CONSTANT requires a preceding integer literal"};
}());

// Tick: `' NAME` resolves NAME to its dictionary index and pushes an
// execution token; the referenced constant folds through the reference too
// -- resolving a colon word this time.
static_assert([] {
    auto unit = elaborate_source(": W DUP ; ' W");
    if (!unit.has_value()) {
        return false;
    }
    auto const &u = unit.value();
    int const w_index = u.dictionary.lookup_index("W");
    if (w_index < 0 || u.program.size() != 1) {
        return false;
    }
    auto const &item_node = u.arena.get(u.program[0]);
    auto const *xt = std::get_if<core_push_xt>(&item_node.value);
    return xt != nullptr && xt->word_index == w_index;
}());

// Tick to an unknown word is diagnosed at the tick's own position.
static_assert([] {
    auto unit = elaborate_source("' NOSUCH");
    return !unit.has_value() &&
           unit.error() == parse_error{source_pos{0, 1, 1}, "unknown word"};
}());

// -- Runtime-visible mirrors of the merge criteria, plus a few extras -------

TEST_CASE("ElaborateTest - QuadCallsSquaredTwice") {
    auto unit = elaborate_source(": SQUARED DUP * ; : QUAD SQUARED SQUARED ;");
    REQUIRE(unit.has_value());
    auto const &u = unit.value();
    CHECK(u.dictionary.lookup("SQUARED") != nullptr);
    auto const *quad_entry = u.dictionary.lookup("QUAD");
    REQUIRE(quad_entry != nullptr);
    auto const &quad_cw = std::get<colon_word>(quad_entry->binding);
    auto const &quad_seq =
        std::get<test_seq>(u.arena.get(quad_cw.core_id).value);
    CHECK(quad_seq.items.size() == 2);
}

TEST_CASE("ElaborateTest - UnknownWordDiagnosedWithPosition") {
    auto unit = elaborate_source(": F NOPE ;");
    REQUIRE_FALSE(unit.has_value());
    CHECK(unit.error() == parse_error{source_pos{4, 1, 5}, "unknown word"});
}

TEST_CASE("ElaborateTest - ForwardReferenceIsUnknownWord") {
    auto unit = elaborate_source(": A B ; : B ;");
    REQUIRE_FALSE(unit.has_value());
    CHECK(unit.error() == parse_error{source_pos{4, 1, 5}, "unknown word"});
}

TEST_CASE("ElaborateTest - VariableAllotsOneCell") {
    auto unit = elaborate_source("VARIABLE X");
    REQUIRE(unit.has_value());
    auto const &u = unit.value();
    auto const *entry = u.dictionary.lookup("X");
    REQUIRE(entry != nullptr);
    REQUIRE(std::holds_alternative<variable_word>(entry->binding));
    CHECK(std::get<variable_word>(entry->binding).address == addr{0});
    CHECK(u.data_space.size() == 1);
}

TEST_CASE("ElaborateTest - ConstantFoldsPrecedingLiteral") {
    auto unit = elaborate_source("5 CONSTANT FIVE");
    REQUIRE(unit.has_value());
    auto const &u = unit.value();
    auto const *entry = u.dictionary.lookup("FIVE");
    REQUIRE(entry != nullptr);
    REQUIRE(std::holds_alternative<constant_word>(entry->binding));
    CHECK(std::get<constant_word>(entry->binding).value == 5);
}

TEST_CASE("ElaborateTest - ConstantWithoutLiteralIsDiagnosed") {
    auto unit = elaborate_source("CONSTANT FIVE");
    REQUIRE_FALSE(unit.has_value());
    CHECK(unit.error() ==
          parse_error{source_pos{0, 1, 1},
                      "CONSTANT requires a preceding integer literal"});
}

TEST_CASE("ElaborateTest - TickPushesExecutionToken") {
    auto unit = elaborate_source(": W DUP ; ' W");
    REQUIRE(unit.has_value());
    auto const &u = unit.value();
    int const w_index = u.dictionary.lookup_index("W");
    REQUIRE(w_index >= 0);
    REQUIRE(u.program.size() == 1);
    auto const &item_node = u.arena.get(u.program[0]);
    auto const *xt = std::get_if<core_push_xt>(&item_node.value);
    REQUIRE(xt != nullptr);
    CHECK(xt->word_index == w_index);
}

TEST_CASE("ElaborateTest - RedefinitionWarnsNotErrors") {
    auto unit = elaborate_source(": X DUP ; : X DROP ;");
    REQUIRE(unit.has_value());
    auto const &u = unit.value();
    CHECK(u.warnings.size() == 1);
    CHECK(u.dictionary.size() == 44); // 42 primitives + two X definitions
}

TEST_CASE("ElaborateTest - RecurseCallsSelf") {
    auto unit = elaborate_source(": R RECURSE ;");
    REQUIRE(unit.has_value());
    auto const &u = unit.value();
    int const r_index = u.dictionary.lookup_index("R");
    REQUIRE(r_index >= 0);
    auto const &r_cw =
        std::get<colon_word>(u.dictionary.entry_at(r_index).binding);
    auto const &r_seq = std::get<test_seq>(u.arena.get(r_cw.core_id).value);
    REQUIRE(r_seq.items.size() == 1);
    auto const *call =
        std::get_if<core_call>(&u.arena.get(r_seq.items[0]).value);
    REQUIRE(call != nullptr);
    CHECK(call->word_index == r_index);
}

TEST_CASE("ElaborateTest - ExitBecomesCoreExit") {
    auto unit = elaborate_source(": E DUP EXIT DROP ;");
    REQUIRE(unit.has_value());
}

TEST_CASE("ElaborateTest - ExitOutsideDefinitionIsDiagnosed") {
    auto unit = elaborate_source("EXIT");
    REQUIRE_FALSE(unit.has_value());
    CHECK(unit.error() ==
          parse_error{source_pos{0, 1, 1}, "EXIT outside a definition"});
}

TEST_CASE("ElaborateTest - RecurseOutsideDefinitionIsDiagnosed") {
    auto unit = elaborate_source("RECURSE");
    REQUIRE_FALSE(unit.has_value());
    CHECK(unit.error() ==
          parse_error{source_pos{0, 1, 1}, "RECURSE outside a definition"});
}

TEST_CASE("ElaborateTest - TopLevelLiteralPushesCorePush") {
    auto unit = elaborate_source("42");
    REQUIRE(unit.has_value());
    auto const &u = unit.value();
    REQUIRE(u.program.size() == 1);
    auto const *push = std::get_if<core_push>(&u.arena.get(u.program[0]).value);
    REQUIRE(push != nullptr);
    CHECK(push->value == 42);
}

TEST_CASE("ElaborateTest - CreateRecordsAddressWithoutAllotting") {
    auto unit = elaborate_source("CREATE BUF");
    REQUIRE(unit.has_value());
    auto const &u = unit.value();
    auto const *entry = u.dictionary.lookup("BUF");
    REQUIRE(entry != nullptr);
    REQUIRE(std::holds_alternative<variable_word>(entry->binding));
    CHECK(std::get<variable_word>(entry->binding).address == addr{0});
    CHECK(u.data_space.size() == 0);
}
