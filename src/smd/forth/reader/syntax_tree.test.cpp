// src/smd/forth/reader/syntax_tree.test.cpp                        -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <smd/forth/reader/syntax_tree.hpp>
#include <smd/forth/reader/syntax_tree.hpp> // test 2nd include OK

#include <catch2/catch_test_macros.hpp>

#include <variant>

using smd::forth::foundation::make_arena_box;
using smd::forth::reader::make_syn_name;
using smd::forth::reader::syn_body;
using smd::forth::reader::syn_colon_def;
using smd::forth::reader::syn_name_equals;
using smd::forth::reader::syn_word;
using smd::forth::reader::syntax_tree;

TEST_CASE("SyntaxTreeTest - HeaderIsIdempotent") { REQUIRE(true); }

namespace {

// Small capacities: this test never needs the header's production defaults.
constexpr int test_max_nodes = 8;
constexpr int test_max_body = 4;
constexpr int test_max_name = 16;

using test_tree = syntax_tree<test_max_nodes, test_max_body, test_max_name>;
using test_node = test_tree::node;
using test_word = syn_word<test_max_name>;
using test_colon_def =
    syn_colon_def<test_max_nodes, test_max_body, test_max_name>;
using test_body = syn_body<test_max_nodes, test_max_body, test_max_name>;

/// Merge criteria: hand-build `: SQUARED DUP * ;` as a tree, all at compile
/// time, via the immediately-invoked-lambda `static_assert` pattern.
constexpr auto build_squared = [] {
    test_tree tree{};

    auto dup_box = make_arena_box(
        tree.arena,
        test_node{.value =
                      test_word{.name = make_syn_name<test_max_name>("DUP")}});
    auto star_box = make_arena_box(
        tree.arena, test_node{.value = test_word{
                                  .name = make_syn_name<test_max_name>("*")}});

    test_body body{};
    body.push_back(dup_box);
    body.push_back(star_box);

    auto squared_box = make_arena_box(
        tree.arena,
        test_node{.value = test_colon_def{
                      .name = make_syn_name<test_max_name>("SQUARED"),
                      .body = body}});

    tree.program.push_back(squared_box);
    return tree;
}();

static_assert(build_squared.program.size() == 1);

/// Walks the hand-built tree back and verifies its structure: a colon
/// definition named `SQUARED` whose body is exactly the words `DUP` and `*`.
constexpr auto walk_squared_ok = [] {
    auto const &def_node = build_squared.arena.get(build_squared.program[0]);
    auto const &def = std::get<test_colon_def>(def_node.value);
    if (!syn_name_equals(def.name, "SQUARED")) {
        return false;
    }
    if (def.body.size() != 2) {
        return false;
    }

    auto const &first = build_squared.arena.get(def.body[0]);
    auto const &second = build_squared.arena.get(def.body[1]);
    auto const &first_word = std::get<test_word>(first.value);
    auto const &second_word = std::get<test_word>(second.value);

    return syn_name_equals(first_word.name, "DUP") &&
           syn_name_equals(second_word.name, "*");
}();

static_assert(walk_squared_ok);

} // namespace

TEST_CASE("SyntaxTreeTest - HandBuiltSquaredWalksBack") {
    // Runtime re-check of the static_asserts above, for Catch2 visibility.
    CHECK(build_squared.program.size() == 1);
    CHECK(walk_squared_ok);
}
