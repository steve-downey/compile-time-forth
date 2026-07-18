// forth/forth.test.cpp                                              -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <smd/forth/forth.hpp>

#include <smd/forth/forth.hpp> // test 2nd include OK

#include <catch2/catch_test_macros.hpp>

// 03013d1f-bcc1-4d3e-9701-3ed1a15c6370
TEST_CASE("forth returns Steve", "forth") {
    REQUIRE(forth::forth() == "Steve");
}
// 03013d1f-bcc1-4d3e-9701-3ed1a15c6370 end
