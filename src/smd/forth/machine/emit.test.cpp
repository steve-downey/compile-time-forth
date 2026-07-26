// src/smd/forth/machine/emit.test.cpp                               -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <smd/forth/machine/emit.hpp>
#include <smd/forth/machine/emit.hpp> // test 2nd include OK

#include <smd/forth/foundation/source_pos.hpp>

#include <catch2/catch_test_macros.hpp>

using smd::forth::foundation::source_pos;
using smd::forth::machine::compiled_program;
using smd::forth::machine::emit;
using smd::forth::machine::op;

TEST_CASE("EmitTest - HeaderIsIdempotent") { REQUIRE(true); }

TEST_CASE("EmitTest - AppendsAndReturnsIndex") {
    compiled_program<8, 4> out{};
    auto r0 = emit(out, op::push, 1, source_pos{});
    auto r1 = emit(out, op::push, 2, source_pos{});
    REQUIRE(r0.has_value());
    REQUIRE(r1.has_value());
    CHECK(r0.value() == 0);
    CHECK(r1.value() == 1);
    REQUIRE(out.code.size() == 2);
    CHECK(out.code[0].code == op::push);
    CHECK(out.code[0].operand == 1);
    CHECK(out.code[1].code == op::push);
    CHECK(out.code[1].operand == 2);
}

TEST_CASE("EmitTest - DiagnosesMaxCodeOverflow") {
    compiled_program<1, 4> out{};
    auto r0 = emit(out, op::halt, 0, source_pos{});
    REQUIRE(r0.has_value());
    auto r1 = emit(out, op::halt, 0, source_pos{5, 1, 6});
    REQUIRE_FALSE(r1.has_value());
    CHECK(r1.error().where.offset == 5);
}
