// src/smd/forth/machine/stacks.test.cpp                             -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <smd/forth/machine/stacks.hpp>
#include <smd/forth/machine/stacks.hpp> // test 2nd include OK

#include <catch2/catch_test_macros.hpp>

using smd::forth::machine::cell_stack;
using smd::forth::machine::data_stack;
using smd::forth::machine::return_stack;

static_assert([] {
    data_stack<4> s;
    return s.depth() == 0;
}());

static_assert([] {
    data_stack<4> s;
    return s.push(10).has_value();
}());

static_assert([] {
    data_stack<4> s;
    (void)s.push(1);
    (void)s.push(2);
    return s.depth() == 2;
}());

static_assert([] {
    data_stack<4> s;
    (void)s.push(1);
    (void)s.push(2);
    auto top = s.pop();
    return top.has_value() && top.value() == 2 && s.depth() == 1;
}());

static_assert([] {
    data_stack<4> s;
    (void)s.push(1);
    (void)s.push(2);
    (void)s.push(3);
    auto below_top = s.peek(1);
    return below_top.has_value() && below_top.value() == 2 && s.depth() == 3;
}());

static_assert([] {
    // Underflow: pop from an empty stack is a diagnosed error, not UB.
    data_stack<4> s;
    return !s.pop().has_value();
}());

static_assert([] {
    // Underflow: peek deeper than the current depth is a diagnosed error.
    data_stack<4> s;
    (void)s.push(1);
    return !s.peek(1).has_value();
}());

static_assert([] {
    // Overflow: pushing beyond MaxDepth is a diagnosed error, not UB.
    data_stack<2> s;
    (void)s.push(1);
    (void)s.push(2);
    return !s.push(3).has_value();
}());

static_assert([] {
    // return_stack behaves identically to data_stack (same underlying
    // cell_stack); only the register it occupies in forth_state differs.
    return_stack<4> r;
    (void)r.push(7);
    auto top = r.pop();
    return top.has_value() && top.value() == 7 && r.depth() == 0;
}());

TEST_CASE("StacksTest - HeaderIsIdempotent") { REQUIRE(true); }

TEST_CASE("StacksTest - PushPopOrder") {
    data_stack<8> s;
    CHECK(s.push(1).has_value());
    CHECK(s.push(2).has_value());
    CHECK(s.push(3).has_value());
    CHECK(s.depth() == 3);

    auto top = s.pop();
    REQUIRE(top.has_value());
    CHECK(top.value() == 3);
    CHECK(s.depth() == 2);
}

TEST_CASE("StacksTest - PeekDoesNotRemove") {
    data_stack<8> s;
    (void)s.push(42);
    auto p = s.peek();
    REQUIRE(p.has_value());
    CHECK(p.value() == 42);
    CHECK(s.depth() == 1);
}

TEST_CASE("StacksTest - UnderflowIsDiagnosed") {
    data_stack<8> s;
    auto r = s.pop();
    CHECK(!r.has_value());
    CHECK(r.error().message != nullptr);
}

TEST_CASE("StacksTest - OverflowIsDiagnosed") {
    data_stack<1> s;
    CHECK(s.push(1).has_value());
    auto r = s.push(2);
    CHECK(!r.has_value());
    CHECK(r.error().message != nullptr);
}

TEST_CASE("StacksTest - ReturnStackIsIndependent") {
    return_stack<8> r;
    data_stack<8> d;
    (void)r.push(1);
    CHECK(r.depth() == 1);
    CHECK(d.depth() == 0);
}
