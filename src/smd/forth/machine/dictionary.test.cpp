// src/smd/forth/machine/dictionary.test.cpp                         -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <smd/forth/machine/dictionary.hpp>
#include <smd/forth/machine/dictionary.hpp> // test 2nd include OK

#include <catch2/catch_test_macros.hpp>

#include <variant>

using smd::forth::machine::addr;
using smd::forth::machine::colon_word;
using smd::forth::machine::constant_word;
using smd::forth::machine::default_dictionary;
using smd::forth::machine::dictionary;
using smd::forth::machine::foreign_word;
using smd::forth::machine::primitive;
using smd::forth::machine::stack_effect;
using smd::forth::machine::variable_word;

TEST_CASE("DictionaryTest - HeaderIsIdempotent") { REQUIRE(true); }

// -- Merge criteria (static_assert, immediately-invoked-lambda pattern) -----

// Lookup finds an installed word.
static_assert([] {
    auto dict = default_dictionary<>();
    auto const *entry = dict.lookup("DUP");
    return entry != nullptr &&
           std::holds_alternative<primitive>(entry->binding) &&
           std::get<primitive>(entry->binding) == primitive::dup;
}());

// Shadowing: after redefining a name, lookup returns the newest entry, and
// the earlier (shadowed) entry is still present, just unreachable by name.
static_assert([] {
    dictionary<4> dict;
    (void)dict.define_constant("X", constant_word{1});
    (void)dict.define_constant("X", constant_word{2});
    auto const *entry = dict.lookup("X");
    return entry != nullptr &&
           std::holds_alternative<constant_word>(entry->binding) &&
           std::get<constant_word>(entry->binding).value == 2 &&
           dict.size() == 2;
}());

// Case-folded lookup: "dup" finds "DUP".
static_assert([] {
    auto dict = default_dictionary<>();
    auto const *entry = dict.lookup("dup");
    return entry != nullptr &&
           std::holds_alternative<primitive>(entry->binding) &&
           std::get<primitive>(entry->binding) == primitive::dup;
}());

// -- Runtime-visible mirrors of the merge criteria, plus a few extras -------

TEST_CASE("DictionaryTest - DefaultDictionaryInstallsAllPrimitives") {
    auto dict = default_dictionary<>();
    CHECK(dict.size() == 46);
    auto const *entry = dict.lookup("SWAP");
    REQUIRE(entry != nullptr);
    REQUIRE(std::holds_alternative<primitive>(entry->binding));
    CHECK(std::get<primitive>(entry->binding) == primitive::swap);
}

TEST_CASE("DictionaryTest - F13OutputAndOneMinusPrimitivesInstalled") {
    auto dict = default_dictionary<>();
    auto const *dot_entry = dict.lookup(".");
    REQUIRE(dot_entry != nullptr);
    REQUIRE(std::holds_alternative<primitive>(dot_entry->binding));
    CHECK(std::get<primitive>(dot_entry->binding) == primitive::dot);

    auto const *one_minus_entry = dict.lookup("1-");
    REQUIRE(one_minus_entry != nullptr);
    REQUIRE(std::holds_alternative<primitive>(one_minus_entry->binding));
    CHECK(std::get<primitive>(one_minus_entry->binding) ==
          primitive::one_minus);
}

TEST_CASE("DictionaryTest - F16MemoryPrimitivesInstalled") {
    auto dict = default_dictionary<>();
    auto const *fetch_entry = dict.lookup("@");
    REQUIRE(fetch_entry != nullptr);
    REQUIRE(std::holds_alternative<primitive>(fetch_entry->binding));
    CHECK(std::get<primitive>(fetch_entry->binding) == primitive::fetch);

    auto const *store_entry = dict.lookup("!");
    REQUIRE(store_entry != nullptr);
    REQUIRE(std::holds_alternative<primitive>(store_entry->binding));
    CHECK(std::get<primitive>(store_entry->binding) == primitive::store);

    auto const *plus_store_entry = dict.lookup("+!");
    REQUIRE(plus_store_entry != nullptr);
    REQUIRE(std::holds_alternative<primitive>(plus_store_entry->binding));
    CHECK(std::get<primitive>(plus_store_entry->binding) ==
          primitive::plus_store);

    auto const *allot_entry = dict.lookup("ALLOT");
    REQUIRE(allot_entry != nullptr);
    REQUIRE(std::holds_alternative<primitive>(allot_entry->binding));
    CHECK(std::get<primitive>(allot_entry->binding) == primitive::allot);
}

TEST_CASE("DictionaryTest - CaseFoldedLookup") {
    auto dict = default_dictionary<>();
    auto const *upper = dict.lookup("DROP");
    auto const *lower = dict.lookup("drop");
    auto const *mixed = dict.lookup("DroP");
    REQUIRE(upper != nullptr);
    CHECK(upper == lower);
    CHECK(upper == mixed);
}

TEST_CASE("DictionaryTest - RedefinitionShadowsButDoesNotErase") {
    dictionary<4> dict;
    CHECK(dict.define_constant("X", constant_word{1}).has_value());
    CHECK(dict.define_constant("X", constant_word{2}).has_value());
    CHECK(dict.size() == 2);
    auto const *entry = dict.lookup("X");
    REQUIRE(entry != nullptr);
    REQUIRE(std::holds_alternative<constant_word>(entry->binding));
    CHECK(std::get<constant_word>(entry->binding).value == 2);
}

TEST_CASE("DictionaryTest - ColonVariableAndForeignBindingsRoundTrip") {
    dictionary<4> dict;
    CHECK(dict.define_colon("SQUARED", colon_word{7, stack_effect{1, 1, true}})
              .has_value());
    CHECK(dict.define_variable("COUNTER", variable_word{addr{3}}).has_value());
    CHECK(dict.define_foreign("PUTS", foreign_word{2}).has_value());

    auto const *colon_entry = dict.lookup("SQUARED");
    REQUIRE(colon_entry != nullptr);
    REQUIRE(std::holds_alternative<colon_word>(colon_entry->binding));
    CHECK(std::get<colon_word>(colon_entry->binding).core_id == 7);
    CHECK(std::get<colon_word>(colon_entry->binding).effect.known);

    auto const *variable_entry = dict.lookup("COUNTER");
    REQUIRE(variable_entry != nullptr);
    REQUIRE(std::holds_alternative<variable_word>(variable_entry->binding));
    CHECK(std::get<variable_word>(variable_entry->binding).address == addr{3});

    auto const *foreign_entry = dict.lookup("PUTS");
    REQUIRE(foreign_entry != nullptr);
    REQUIRE(std::holds_alternative<foreign_word>(foreign_entry->binding));
    CHECK(std::get<foreign_word>(foreign_entry->binding).index == 2);
}

TEST_CASE("DictionaryTest - FullDictionaryIsDiagnosed") {
    dictionary<1> dict;
    CHECK(dict.define_constant("A", constant_word{1}).has_value());
    auto result = dict.define_constant("B", constant_word{2});
    CHECK_FALSE(result.has_value());
}

TEST_CASE("DictionaryTest - LookupMissingWordReturnsNull") {
    auto dict = default_dictionary<>();
    CHECK(dict.lookup("NOSUCHWORD") == nullptr);
}

// lookup_index/entry_at: F11's elaborator resolves a word reference to a
// dictionary index (for core_call/core_push_xt), not just a pointer.
static_assert([] {
    dictionary<4> dict;
    (void)dict.define_constant("X", constant_word{1});
    (void)dict.define_constant("X", constant_word{2});
    int const index = dict.lookup_index("X");
    return index == 1 &&
           std::get<constant_word>(dict.entry_at(index).binding).value == 2;
}());

TEST_CASE("DictionaryTest - LookupIndexAndEntryAt") {
    auto dict = default_dictionary<>();
    int const index = dict.lookup_index("SWAP");
    REQUIRE(index >= 0);
    auto const &entry = dict.entry_at(index);
    REQUIRE(std::holds_alternative<primitive>(entry.binding));
    CHECK(std::get<primitive>(entry.binding) == primitive::swap);
    CHECK(dict.lookup_index("NOSUCHWORD") == -1);
}
