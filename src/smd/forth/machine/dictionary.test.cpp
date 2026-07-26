// src/smd/forth/machine/dictionary.test.cpp                         -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <smd/forth/machine/dictionary.hpp>
#include <smd/forth/machine/dictionary.hpp> // test 2nd include OK

#include <smd/forth/foundation/source_pos.hpp>
#include <smd/forth/foundation/source_span.hpp>

#include <catch2/catch_test_macros.hpp>

#include <variant>

namespace foundation = smd::forth::foundation;

using smd::forth::machine::addr;
using smd::forth::machine::compiled_colon_word;
using smd::forth::machine::constant_word;
using smd::forth::machine::control_builtin;
using smd::forth::machine::control_word;
using smd::forth::machine::default_dictionary;
using smd::forth::machine::defer_word;
using smd::forth::machine::dictionary;
using smd::forth::machine::foreign_word;
using smd::forth::machine::primitive;
using smd::forth::machine::value_word;
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
    // 48 primitives (47 + step F28's `,`) + 20 step F27 control words (D17)
    // + 9 step F28 control words (D18).
    CHECK(dict.size() == 77);
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

    // 1+ (DIV-0010), installed alongside 1- for F17's counted-loop criteria.
    auto const *one_plus_entry = dict.lookup("1+");
    REQUIRE(one_plus_entry != nullptr);
    REQUIRE(std::holds_alternative<primitive>(one_plus_entry->binding));
    CHECK(std::get<primitive>(one_plus_entry->binding) == primitive::one_plus);
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

TEST_CASE("DictionaryTest - VariableAndForeignBindingsRoundTrip") {
    dictionary<4> dict;
    CHECK(dict.define_variable("COUNTER", variable_word{addr{3}}).has_value());
    CHECK(dict.define_foreign("PUTS", foreign_word{2}).has_value());

    auto const *variable_entry = dict.lookup("COUNTER");
    REQUIRE(variable_entry != nullptr);
    REQUIRE(std::holds_alternative<variable_word>(variable_entry->binding));
    CHECK(std::get<variable_word>(variable_entry->binding).address == addr{3});

    auto const *foreign_entry = dict.lookup("PUTS");
    REQUIRE(foreign_entry != nullptr);
    REQUIRE(std::holds_alternative<foreign_word>(foreign_entry->binding));
    CHECK(std::get<foreign_word>(foreign_entry->binding).index == 2);
}

// Step F25: compiled_colon_word is the colon compiler's own (and, since
// step F26 deletes the R1 elaborator's own colon_word alongside it, now the
// *only*) colon-definition binding kind -- an entry point into
// interpreter::compile_buffer's code space.
TEST_CASE("DictionaryTest - CompiledColonWordRoundTrips") {
    dictionary<4> dict;
    foundation::source_span effect{foundation::source_pos{5, 1, 6},
                                   foundation::source_pos{17, 1, 18}};
    CHECK(dict.define_compiled_colon("SQUARED",
                                     compiled_colon_word{.entry_point = 3,
                                                         .effect_span = effect,
                                                         .has_effect = true})
              .has_value());

    auto const *entry = dict.lookup("SQUARED");
    REQUIRE(entry != nullptr);
    REQUIRE(std::holds_alternative<compiled_colon_word>(entry->binding));
    auto const &word = std::get<compiled_colon_word>(entry->binding);
    CHECK(word.entry_point == 3);
    CHECK(word.has_effect);
    CHECK(word.effect_span == effect);
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

// -- Step F27: control words and IMMEDIATE (D17/D18) ------------------------

// Every ordinary define_* installs a non-immediate entry by default.
static_assert([] {
    dictionary<4> dict;
    (void)dict.define_constant("X", constant_word{1});
    return dict.lookup("X")->immediate == false;
}());

// define_control installs the requested control_builtin tag, immediate flag
// and all.
static_assert([] {
    dictionary<4> dict;
    (void)dict.define_control("IF", control_word{control_builtin::if_},
                              /* immediate */ true);
    auto const *entry = dict.lookup("IF");
    return entry != nullptr && entry->immediate &&
           std::holds_alternative<control_word>(entry->binding) &&
           std::get<control_word>(entry->binding).which == control_builtin::if_;
}());

// mark_last_immediate flags whichever entry was defined most recently,
// regardless of binding kind, and diagnoses an empty dictionary rather than
// indexing before the first entry.
static_assert([] {
    dictionary<4> dict;
    (void)dict.define_constant("X", constant_word{1});
    if (dict.lookup("X")->immediate) {
        return false;
    }
    auto r = dict.mark_last_immediate();
    return r.has_value() && dict.lookup("X")->immediate;
}());

static_assert([] {
    dictionary<4> dict;
    auto r = dict.mark_last_immediate();
    return !r.has_value();
}());

TEST_CASE("DictionaryTest - DefaultDictionaryControlWordsAreInstalled") {
    auto dict = default_dictionary<>();

    auto const *if_entry = dict.lookup("IF");
    REQUIRE(if_entry != nullptr);
    REQUIRE(std::holds_alternative<control_word>(if_entry->binding));
    CHECK(std::get<control_word>(if_entry->binding).which ==
          control_builtin::if_);
    CHECK(if_entry->immediate);

    auto const *then_entry = dict.lookup("THEN");
    REQUIRE(then_entry != nullptr);
    CHECK(then_entry->immediate);

    auto const *plus_loop_entry = dict.lookup("+LOOP");
    REQUIRE(plus_loop_entry != nullptr);
    REQUIRE(std::holds_alternative<control_word>(plus_loop_entry->binding));
    CHECK(std::get<control_word>(plus_loop_entry->binding).which ==
          control_builtin::plus_loop_);

    // `]` and `IMMEDIATE` are the two control words installed non-immediate
    // (see default_dictionary's own doc comment).
    auto const *close_bracket_entry = dict.lookup("]");
    REQUIRE(close_bracket_entry != nullptr);
    CHECK_FALSE(close_bracket_entry->immediate);

    auto const *immediate_entry = dict.lookup("IMMEDIATE");
    REQUIRE(immediate_entry != nullptr);
    CHECK_FALSE(immediate_entry->immediate);
}

TEST_CASE("DictionaryTest - MarkLastImmediateFlagsNewestEntry") {
    dictionary<4> dict;
    CHECK(dict.define_compiled_colon("ENDIF",
                                     compiled_colon_word{.entry_point = 1})
              .has_value());
    CHECK_FALSE(dict.lookup("ENDIF")->immediate);
    CHECK(dict.mark_last_immediate().has_value());
    CHECK(dict.lookup("ENDIF")->immediate);
}

// -- Step F28: execution tokens and defining words (D18) --------------------

// variable_word gains a does-field (default -1, "no DOES> yet").
static_assert([] {
    dictionary<4> dict;
    (void)dict.define_variable("X", variable_word{.address = addr{3}});
    auto const *entry = dict.lookup("X");
    return entry != nullptr &&
           std::get<variable_word>(entry->binding).does_entry == -1;
}());

// attach_does installs a does-field onto the most recently defined entry,
// and only that one: an earlier CREATE'd word is untouched.
static_assert([] {
    dictionary<4> dict;
    (void)dict.define_variable("EARLIER", variable_word{.address = addr{1}});
    (void)dict.define_variable("LATEST", variable_word{.address = addr{2}});
    auto r = dict.attach_does(9);
    if (!r.has_value()) {
        return false;
    }
    return std::get<variable_word>(dict.lookup("LATEST")->binding).does_entry ==
               9 &&
           std::get<variable_word>(dict.lookup("EARLIER")->binding)
                   .does_entry == -1;
}());

// attach_does diagnoses an empty dictionary and a most-recent entry that was
// not CREATE'd (DOES> misapplied to the wrong kind of word).
static_assert([] {
    dictionary<4> dict;
    return !dict.attach_does(9).has_value();
}());

static_assert([] {
    dictionary<4> dict;
    (void)dict.define_constant("X", constant_word{1});
    return !dict.attach_does(9).has_value();
}());

// define_value/define_defer round-trip like every other define_*.
static_assert([] {
    dictionary<4> dict;
    (void)dict.define_value("SPEED", value_word{.address = addr{5}});
    (void)dict.define_defer("HOOK", defer_word{.address = addr{6}});
    auto const *value_entry = dict.lookup("SPEED");
    auto const *defer_entry = dict.lookup("HOOK");
    return value_entry != nullptr &&
           std::get<value_word>(value_entry->binding).address == addr{5} &&
           defer_entry != nullptr &&
           std::get<defer_word>(defer_entry->binding).address == addr{6};
}());

TEST_CASE("DictionaryTest - DefaultDictionaryF28WordsAreInstalled") {
    auto dict = default_dictionary<>();

    auto const *comma_entry = dict.lookup(",");
    REQUIRE(comma_entry != nullptr);
    REQUIRE(std::holds_alternative<primitive>(comma_entry->binding));
    CHECK(std::get<primitive>(comma_entry->binding) == primitive::comma);

    // `[']` and `TO` are the two step F28 control words installed immediate
    // (default_dictionary's own doc comment); `'`/`EXECUTE`/`CREATE`/
    // `DOES>`/`VALUE`/`DEFER`/`IS` are ordinary.
    auto const *bracket_tick_entry = dict.lookup("[']");
    REQUIRE(bracket_tick_entry != nullptr);
    CHECK(bracket_tick_entry->immediate);
    REQUIRE(std::holds_alternative<control_word>(bracket_tick_entry->binding));
    CHECK(std::get<control_word>(bracket_tick_entry->binding).which ==
          control_builtin::bracket_tick_);

    auto const *to_entry = dict.lookup("TO");
    REQUIRE(to_entry != nullptr);
    CHECK(to_entry->immediate);

    auto const *tick_entry = dict.lookup("'");
    REQUIRE(tick_entry != nullptr);
    CHECK_FALSE(tick_entry->immediate);
    REQUIRE(std::holds_alternative<control_word>(tick_entry->binding));
    CHECK(std::get<control_word>(tick_entry->binding).which ==
          control_builtin::tick_);

    auto const *create_entry = dict.lookup("CREATE");
    REQUIRE(create_entry != nullptr);
    CHECK_FALSE(create_entry->immediate);
    REQUIRE(std::holds_alternative<control_word>(create_entry->binding));
    CHECK(std::get<control_word>(create_entry->binding).which ==
          control_builtin::create_);

    auto const *does_entry_word = dict.lookup("DOES>");
    REQUIRE(does_entry_word != nullptr);
    CHECK_FALSE(does_entry_word->immediate);

    for (auto const *name : {"EXECUTE", "VALUE", "DEFER", "IS"}) {
        auto const *entry = dict.lookup(name);
        REQUIRE(entry != nullptr);
        CHECK_FALSE(entry->immediate);
    }
}
