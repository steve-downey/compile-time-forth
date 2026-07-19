// src/smd/forth/elaborator/elaborated_core.test.cpp                 -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <smd/forth/elaborator/elaborated_core.hpp>
#include <smd/forth/elaborator/elaborated_core.hpp> // test 2nd include OK

#include <catch2/catch_test_macros.hpp>

#include <variant>

using smd::forth::elaborator::compiled_unit;
using smd::forth::elaborator::core_body;
using smd::forth::elaborator::core_prim;
using smd::forth::elaborator::core_push;
using smd::forth::elaborator::core_seq;
using smd::forth::foundation::make_arena_box;
using smd::forth::machine::primitive;

TEST_CASE("ElaboratedCoreTest - HeaderIsIdempotent") { REQUIRE(true); }

namespace {

// Small capacities: this test never needs the header's production defaults.
constexpr int test_max_nodes = 16;
constexpr int test_max_body = 4;

using test_unit = compiled_unit<test_max_nodes, test_max_body>;
using test_node = test_unit::node;
using test_body = core_body<test_max_nodes, test_max_body>;
using test_seq = core_seq<test_max_nodes, test_max_body>;

/// Merge criterion: hand-build a two-item `core_seq` -- a literal push
/// followed by a primitive call, the elaborated shape of `42 DUP` -- directly
/// in a @ref compiled_unit's own arena, all at compile time. Same
/// arena_box/tree_arena round-trip pattern as reader::syntax_tree.test.cpp
/// (F6), one level down the pipeline.
constexpr auto build_unit = [] {
    test_unit unit{};

    auto push_box =
        make_arena_box(unit.arena, test_node{.value = core_push{.value = 42}});
    auto prim_box = make_arena_box(
        unit.arena, test_node{.value = core_prim{.op = primitive::dup}});

    test_body items{};
    items.push_back(push_box);
    items.push_back(prim_box);

    auto seq_box = make_arena_box(unit.arena,
                                  test_node{.value = test_seq{.items = items}});
    unit.program.push_back(seq_box);
    return unit;
}();

static_assert(build_unit.program.size() == 1);

/// Walks the hand-built unit back and verifies its structure.
constexpr auto walk_unit_ok = [] {
    auto const &seq_node = build_unit.arena.get(build_unit.program[0]);
    auto const &seq = std::get<test_seq>(seq_node.value);
    if (seq.items.size() != 2) {
        return false;
    }
    auto const &first = build_unit.arena.get(seq.items[0]);
    auto const &second = build_unit.arena.get(seq.items[1]);
    auto const &push = std::get<core_push>(first.value);
    auto const &prim = std::get<core_prim>(second.value);
    return push.value == 42 && prim.op == primitive::dup;
}();

static_assert(walk_unit_ok);

} // namespace

TEST_CASE("ElaboratedCoreTest - HandBuiltSeqWalksBack") {
    // Runtime re-check of the static_asserts above, for Catch2 visibility.
    CHECK(build_unit.program.size() == 1);
    CHECK(walk_unit_ok);
}
