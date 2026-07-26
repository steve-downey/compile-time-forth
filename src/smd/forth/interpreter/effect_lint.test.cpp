// src/smd/forth/interpreter/effect_lint.test.cpp                    -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <smd/forth/interpreter/effect_lint.hpp>
#include <smd/forth/interpreter/effect_lint.hpp> // test 2nd include OK

#include <catch2/catch_test_macros.hpp>

#include <string_view>

using smd::forth::foundation::source_pos;
using smd::forth::foundation::source_span;
using smd::forth::interpreter::combine_branch;
using smd::forth::interpreter::combine_sequential;
using smd::forth::interpreter::has_declared_effect;
using smd::forth::interpreter::identity_effect;
using smd::forth::interpreter::known;
using smd::forth::interpreter::parse_declared_effect;
using smd::forth::interpreter::primitive_data_effect;
using smd::forth::interpreter::primitive_return_delta;
using smd::forth::interpreter::unknown_effect;
using smd::forth::machine::primitive;

TEST_CASE("EffectLintTest - HeaderIsIdempotent") { REQUIRE(true); }

// -- Lattice primitives, in isolation --------------------------------------
//
// Ported verbatim (values and comments unchanged) from the R1 elaborator's
// own elaborator/stack_effect.test.cpp, which step F26 deletes along with
// the rest of elaborator/ -- these static_asserts exercise only the
// tree-independent lattice this header preserves for F30, not the deleted
// tree-walking analyze_body.

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
// net 0 -- same net, different shape; combine_branch reflects "the flag plus
// whichever arm needs more, net-adjusted".
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

// Memory (Step F16): none of the four are input-dependent, unlike ?DUP.
static_assert(primitive_data_effect(primitive::fetch) == known(1, 1));
static_assert(primitive_data_effect(primitive::store) == known(2, 0));
static_assert(primitive_data_effect(primitive::plus_store) == known(2, 0));
static_assert(primitive_data_effect(primitive::allot) == known(1, 0));

// Step F28: `,` is likewise not input-dependent.
static_assert(primitive_data_effect(primitive::comma) == known(1, 0));

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
