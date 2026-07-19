// src/smd/forth/reader/read_program.test.cpp                       -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <smd/forth/reader/read_program.hpp>
#include <smd/forth/reader/read_program.hpp> // test 2nd include OK

#include <catch2/catch_test_macros.hpp>

#include <string_view>
#include <variant>

using smd::forth::foundation::parse_error;
using smd::forth::foundation::source_pos;
using smd::forth::reader::read_program;
using smd::forth::reader::syn_begin_until;
using smd::forth::reader::syn_colon_def;
using smd::forth::reader::syn_if;
using smd::forth::reader::syn_literal;
using smd::forth::reader::syn_name_equals;
using smd::forth::reader::syn_word;

TEST_CASE("ReadProgramTest - HeaderIsIdempotent") { REQUIRE(true); }

namespace {

// Small capacities: these tests never need the header's production
// defaults.
constexpr int test_max_nodes = 64;
constexpr int test_max_body = 8;
constexpr int test_max_name = 16;
constexpr int test_max_depth = 8;

using test_word = syn_word<test_max_name>;
using test_colon_def =
    syn_colon_def<test_max_nodes, test_max_body, test_max_name>;
using test_if = syn_if<test_max_nodes, test_max_body, test_max_name>;
using test_begin_until =
    syn_begin_until<test_max_nodes, test_max_body, test_max_name>;

template <class T>
constexpr auto get(auto const &node_value) -> T const & {
    return std::get<T>(node_value);
}

// Merge criterion 1: round-trip
// `: ABS ( n -- n ) DUP 0< IF NEGATE THEN ;`
//
// Verifies: colon def named ABS, declared_effect span capturing
// "( n -- n )", body [DUP, 0< as word, IF [NEGATE] no-else]. `0<` is a word,
// not a number.

constexpr std::string_view abs_src = ": ABS ( n -- n ) DUP 0< IF NEGATE THEN ;";

constexpr auto abs_ok = [] {
    auto result = read_program<test_max_nodes, test_max_body, test_max_name,
                               test_max_depth>(abs_src);
    if (!result.has_value()) {
        return false;
    }
    auto const &tree = result.value();
    if (tree.program.size() != 1) {
        return false;
    }
    auto const &def =
        get<test_colon_def>(tree.arena.get(tree.program[0]).value);
    if (!syn_name_equals(def.name, "ABS")) {
        return false;
    }
    auto const effect_len = static_cast<std::size_t>(
        def.declared_effect.last.offset - def.declared_effect.first.offset);
    auto const effect_text = abs_src.substr(
        static_cast<std::size_t>(def.declared_effect.first.offset), effect_len);
    if (effect_text != "( n -- n )") {
        return false;
    }
    if (def.body.size() != 3) {
        return false;
    }
    auto const &dup_word = get<test_word>(tree.arena.get(def.body[0]).value);
    if (!syn_name_equals(dup_word.name, "DUP")) {
        return false;
    }
    auto const &lt_word = get<test_word>(tree.arena.get(def.body[1]).value);
    if (!syn_name_equals(lt_word.name, "0<")) {
        return false;
    }
    auto const &if_node = get<test_if>(tree.arena.get(def.body[2]).value);
    if (if_node.then_body.size() != 1 || !if_node.else_body.empty()) {
        return false;
    }
    auto const &negate_word =
        get<test_word>(tree.arena.get(if_node.then_body[0]).value);
    return syn_name_equals(negate_word.name, "NEGATE");
}();

static_assert(abs_ok);

// Merge criterion 2: two-level nesting -- IF inside BEGIN ... UNTIL inside a
// definition: `: F BEGIN DUP IF DROP THEN UNTIL ;`.

constexpr std::string_view nested_src = ": F BEGIN DUP IF DROP THEN UNTIL ;";

constexpr auto nested_ok = [] {
    auto result = read_program<test_max_nodes, test_max_body, test_max_name,
                               test_max_depth>(nested_src);
    if (!result.has_value()) {
        return false;
    }
    auto const &tree = result.value();
    auto const &def =
        get<test_colon_def>(tree.arena.get(tree.program[0]).value);
    if (!syn_name_equals(def.name, "F") || def.body.size() != 1) {
        return false;
    }
    auto const &begin_node =
        get<test_begin_until>(tree.arena.get(def.body[0]).value);
    if (begin_node.body.size() != 2) {
        return false;
    }
    auto const &dup_word =
        get<test_word>(tree.arena.get(begin_node.body[0]).value);
    if (!syn_name_equals(dup_word.name, "DUP")) {
        return false;
    }
    auto const &if_node =
        get<test_if>(tree.arena.get(begin_node.body[1]).value);
    if (if_node.then_body.size() != 1 || !if_node.else_body.empty()) {
        return false;
    }
    auto const &drop_word =
        get<test_word>(tree.arena.get(if_node.then_body[0]).value);
    return syn_name_equals(drop_word.name, "DROP");
}();

static_assert(nested_ok);

// Failure: unterminated definition (no ;) -- position is where scanning ran
// out of input looking for the next token.

constexpr std::string_view unterminated_src = ": F DUP";

constexpr auto unterminated_error_ok = [] {
    auto result = read_program<test_max_nodes, test_max_body, test_max_name,
                               test_max_depth>(unterminated_src);
    if (result.has_value()) {
        return false;
    }
    auto const offset = static_cast<int>(unterminated_src.size());
    return result.error() == parse_error{source_pos{offset, 1, offset + 1},
                                         "unterminated definition (no ;)"};
}();

static_assert(unterminated_error_ok);

// Failure: ELSE without IF.

constexpr std::string_view else_without_if_src = ": F ELSE ;";

constexpr auto else_without_if_ok = [] {
    auto result = read_program<test_max_nodes, test_max_body, test_max_name,
                               test_max_depth>(else_without_if_src);
    if (result.has_value()) {
        return false;
    }
    return result.error() ==
           parse_error{source_pos{4, 1, 5}, "ELSE without IF"};
}();

static_assert(else_without_if_ok);

// Failure: THEN without IF.

constexpr std::string_view then_without_if_src = ": F THEN ;";

constexpr auto then_without_if_ok = [] {
    auto result = read_program<test_max_nodes, test_max_body, test_max_name,
                               test_max_depth>(then_without_if_src);
    if (result.has_value()) {
        return false;
    }
    return result.error() ==
           parse_error{source_pos{4, 1, 5}, "THEN without IF"};
}();

static_assert(then_without_if_ok);

// Failure (bonus, "and similar stray control words"): UNTIL without BEGIN.

constexpr std::string_view until_without_begin_src = ": F UNTIL ;";

constexpr auto until_without_begin_ok = [] {
    auto result = read_program<test_max_nodes, test_max_body, test_max_name,
                               test_max_depth>(until_without_begin_src);
    if (result.has_value()) {
        return false;
    }
    return result.error() ==
           parse_error{source_pos{4, 1, 5}, "UNTIL without BEGIN"};
}();

static_assert(until_without_begin_ok);

// Failure: stray ; (no open colon definition at all).

constexpr std::string_view stray_semicolon_src = ";";

constexpr auto stray_semicolon_ok = [] {
    auto result = read_program<test_max_nodes, test_max_body, test_max_name,
                               test_max_depth>(stray_semicolon_src);
    if (result.has_value()) {
        return false;
    }
    return result.error() == parse_error{source_pos{0, 1, 1}, "stray ;"};
}();

static_assert(stray_semicolon_ok);

// Failure: nested : inside a definition.

constexpr std::string_view nested_colon_src = ": F : G ;";

constexpr auto nested_colon_ok = [] {
    auto result = read_program<test_max_nodes, test_max_body, test_max_name,
                               test_max_depth>(nested_colon_src);
    if (result.has_value()) {
        return false;
    }
    return result.error() ==
           parse_error{source_pos{4, 1, 5}, "nested : inside definition"};
}();

static_assert(nested_colon_ok);

// Failure: a colon definition may not redefine a reserved word.

constexpr std::string_view reserved_name_src = ": IF DUP ;";

constexpr auto reserved_name_ok = [] {
    auto result = read_program<test_max_nodes, test_max_body, test_max_name,
                               test_max_depth>(reserved_name_src);
    if (result.has_value()) {
        return false;
    }
    return result.error() == parse_error{source_pos{2, 1, 3},
                                         "reserved word cannot be redefined"};
}();

static_assert(reserved_name_ok);

// Bonus: nesting depth is bounded -- with MaxDepth = 0, even one level of
// control-structure nesting inside a definition is diagnosed rather than
// silently accepted or blowing template-instantiation depth.

constexpr std::string_view one_level_src = ": F IF DUP THEN ;";

constexpr auto max_depth_exceeded_ok = [] {
    auto result = read_program<test_max_nodes, test_max_body, test_max_name, 0>(
        one_level_src);
    if (result.has_value()) {
        return false;
    }
    return result.error() ==
           parse_error{source_pos{4, 1, 5}, "max nesting depth exceeded"};
}();

static_assert(max_depth_exceeded_ok);

} // namespace

TEST_CASE("ReadProgramTest - RoundTripsAbs") { CHECK(abs_ok); }

TEST_CASE("ReadProgramTest - TwoLevelNesting") { CHECK(nested_ok); }

TEST_CASE("ReadProgramTest - UnterminatedDefinition") {
    CHECK(unterminated_error_ok);
}

TEST_CASE("ReadProgramTest - ElseWithoutIf") { CHECK(else_without_if_ok); }

TEST_CASE("ReadProgramTest - ThenWithoutIf") { CHECK(then_without_if_ok); }

TEST_CASE("ReadProgramTest - UntilWithoutBegin") {
    CHECK(until_without_begin_ok);
}

TEST_CASE("ReadProgramTest - StraySemicolon") { CHECK(stray_semicolon_ok); }

TEST_CASE("ReadProgramTest - NestedColon") { CHECK(nested_colon_ok); }

TEST_CASE("ReadProgramTest - ReservedNameRejected") { CHECK(reserved_name_ok); }

TEST_CASE("ReadProgramTest - MaxDepthExceeded") {
    CHECK(max_depth_exceeded_ok);
}
