// src/smd/forth/sender/vocab.test.cpp                               -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <smd/forth/sender/vocab.hpp>

#include <smd/forth/sender/vocab.hpp> // test 2nd include OK

#include <catch2/catch_test_macros.hpp>

TEST_CASE("SenderVocabTest - HeaderIsIdempotent") {
    // Placeholder: verifies header re-inclusion safety and build coherency.
    // This test always passes if the file compiles.
    REQUIRE(true);
}

TEST_CASE("SenderVocabTest - JustThenSyncWait") {
    using smd::forth::sender::just;
    using smd::forth::sender::sync_wait;
    using smd::forth::sender::then;

    auto result = sync_wait(then(just(20), [](int x) { return x + 22; }));
    REQUIRE(result.has_value());
    CHECK(std::get<0>(result.value()) == 42);
}
