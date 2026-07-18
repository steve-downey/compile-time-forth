// src/smd/forth/parser/parser_ops.test.cpp                          -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Adapted by copy from compile-time-scheme (smd::smdscheme):
// src/smd/smdscheme/parser/parser_ops.test.cpp
// Reworked for DIV-0003: parser<F> registers against
// smd::forth::foundation's functor_typeclass/applicative_typeclass/
// alternative_typeclass rather than a locally reimplemented lookup, so this
// file exercises foundation's CPOs (fmap/invoke/alt) dispatching to
// parsers, in addition to the parser_v typeclass-object surface, plus the
// F4 merge criterion: an integer parser rebuilt from primitives.

#include <smd/forth/parser/parser_ops.hpp>
#include <smd/forth/parser/parser_ops.hpp> // test 2nd include OK

#include <catch2/catch_test_macros.hpp>

using smd::forth::foundation::alt;
using smd::forth::foundation::alternative_typeclass;
using smd::forth::foundation::applicative_typeclass;
using smd::forth::foundation::fmap;
using smd::forth::foundation::functor_typeclass;
using smd::forth::foundation::invoke;
using smd::forth::parser::char_p;
using smd::forth::parser::cursor;
using smd::forth::parser::parser_v;

// parser_v member surface (backward-compatible with the Scheme reference's
// direct parser_v.map/alt/pure usage).

static_assert([] {
    auto p = parser_v.fmap([](char) { return 42; }, char_p('x'));
    auto r = p(cursor{"x"});
    return r.has_value() && r.value().value == 42;
}());

static_assert([] {
    auto p = parser_v.alt(char_p('a'), char_p('b'));
    auto ra = p(cursor{"a"});
    auto rb = p(cursor{"b"});
    return ra.has_value() && ra.value().value == 'a' && rb.has_value() &&
           rb.value().value == 'b';
}());

static_assert([] {
    auto p = parser_v.pure(7);
    auto r = p(cursor{"anything"});
    return r.has_value() && r.value().value == 7;
}());

// Typeclass lookup: parser<F> resolves to parser_ops against all three of
// foundation's Functor/Applicative/Alternative typeclass variables.

static_assert([] {
    auto base_parser = char_p('x');
    using P = decltype(base_parser);
    const auto &tc = functor_typeclass<P>;
    auto p = tc.fmap([](char) { return 99; }, base_parser);
    auto r = p(cursor{"x"});
    return r.has_value() && r.value().value == 99;
}());

static_assert([] {
    auto base_parser = char_p('x');
    using P = decltype(base_parser);
    const auto &tc = applicative_typeclass<P>;
    auto r = tc.pure(3)(cursor{"anything"});
    return r.has_value() && r.value().value == 3;
}());

static_assert([] {
    auto base_parser = char_p('x');
    using P = decltype(base_parser);
    const auto &tc = alternative_typeclass<P>;
    auto p = tc.alt(char_p('a'), char_p('b'));
    return p(cursor{"a"}).value().value == 'a' &&
           p(cursor{"b"}).value().value == 'b';
}());

// foundation CPO dispatch: fmap/invoke/alt reach parser<F> through the same
// customization points as any other registered type.

static_assert([] {
    auto p = fmap([](char c) { return int(c); }, char_p('x'));
    auto r = p(cursor{"x"});
    return r.has_value() && r.value().value == int('x');
}());

static_assert([] {
    auto pa = char_p('a');
    auto pb = char_p('b');
    auto p = invoke([](char a, char b) { return a == 'a' && b == 'b'; }, pa,
                    pb);
    auto r = p(cursor{"ab"});
    return r.has_value() && r.value().value == true;
}());

static_assert([] {
    auto p = alt(char_p('a'), char_p('b'));
    return p(cursor{"a"}).value().value == 'a' &&
           p(cursor{"b"}).value().value == 'b' &&
           !p(cursor{"c"}).has_value();
}());

// Derived operations: apply from lift2, invoke from pure+apply (via
// foundation::applicative's terminating-partial derivation).

static_assert([] {
    auto pf = parser_v.pure([](char c) { return c == 'y' ? 1 : 0; });
    auto pa = char_p('y');
    auto p = parser_v.apply(pf, pa);
    auto r = p(cursor{"y"});
    return r.has_value() && r.value().value == 1;
}());

static_assert([] {
    auto pa = char_p('a');
    auto pb = char_p('b');
    auto three = char_p('c');
    auto p = parser_v.invoke(
        [](char a, char b, char c) { return a == 'a' && b == 'b' && c == 'c'; },
        pa, pb, three);
    auto r = p(cursor{"abc"});
    return r.has_value() && r.value().value == true;
}());

// discard_first / discard_second: sequencing derived from invoke.

static_assert([] {
    auto pa = char_p('a');
    auto pb = char_p('b');
    auto keep_second = parser_v.discard_first(pa, pb);
    auto keep_first = parser_v.discard_second(pa, pb);
    auto rs = keep_second(cursor{"ab"});
    auto rf = keep_first(cursor{"ab"});
    return rs.has_value() && rs.value().value == 'b' && rf.has_value() &&
           rf.value().value == 'a';
}());

// Repetition/whitespace combinators on the typeclass object.

static_assert([] {
    auto p = parser_v.many<4>(char_p('a'));
    return p(cursor{"aaa"}).value().value.size() == 3;
}());

static_assert([] {
    auto p = parser_v.some<4>(char_p('a'));
    return !p(cursor{""}).has_value() &&
           p(cursor{"aa"}).value().value.size() == 2;
}());

static_assert([] {
    auto p = parser_v.optional(char_p('a'));
    return !p(cursor{"b"}).value().value.has_value() &&
           p(cursor{"a"}).value().value.has_value();
}());

static_assert([] {
    auto p = parser_v.lexeme(char_p('x'));
    return p(cursor{"  x  "}).value().value == 'x';
}());

// Alternative identity element: empty<T>() always fails without consuming,
// so it never wins an ordered choice against a parser that can succeed.
// empty<T>() is only reachable via the typeclass instance's own explicit
// template argument (see parser_alternative_impl::empty's doc comment), not
// through foundation::empty<T>()'s zero-argument calling convention.

static_assert([] {
    auto none = parser_v.empty<char>();
    auto p = parser_v.alt(char_p('a'), none);
    auto q = parser_v.alt(none, char_p('a'));
    return p(cursor{"a"}).value().value == 'a' &&
           q(cursor{"a"}).value().value == 'a' && !none(cursor{"a"}).has_value();
}());

// NTTP pinning: explicit typeclass override at call site.

template <class P,
          const auto &TC = smd::forth::foundation::functor_typeclass<P>>
constexpr auto map_via_typeclass(P p, auto f) {
    return TC.fmap(f, p);
}

static_assert([] {
    auto p = map_via_typeclass(char_p('z'), [](char) { return 42; });
    auto r = p(cursor{"z"});
    return r.has_value() && r.value().value == 42;
}());

// Functor law: fmap(id, p) parses identically to p.

static_assert([] {
    auto p = char_p('q');
    auto mapped = fmap([](char c) { return c; }, p);
    auto r1 = p(cursor{"q"});
    auto r2 = mapped(cursor{"q"});
    return r1.has_value() == r2.has_value() && r1.value().value == r2.value().value;
}());

// Functor law: fmap(g . f, p) == fmap(g, fmap(f, p)).

static_assert([] {
    auto p = char_p('q');
    auto f = [](char c) { return int(c); };
    auto g = [](int i) { return i + 1; };
    auto composed = fmap([f, g](char c) { return g(f(c)); }, p);
    auto chained = fmap(g, fmap(f, p));
    auto r1 = composed(cursor{"q"});
    auto r2 = chained(cursor{"q"});
    return r1.value().value == r2.value().value;
}());

// Merge criterion (docs/forth-plan.md Step F4): parse "42" with an
// integer_p-style parser composed from primitives in this test — not the
// Scheme atom parser. digit_p is satisfy() over a local digit predicate;
// integer_p folds one-or-more digits (via some<>) into an int with map().
static_assert([] {
    auto is_digit = [](char c) { return c >= '0' && c <= '9'; };
    auto digit_p = smd::forth::parser::satisfy(is_digit, "digit");
    auto integer_p = smd::forth::parser::map(
        smd::forth::parser::some<8>(digit_p), [](auto digits) {
            int value = 0;
            for (auto d : digits) {
                value = value * 10 + (d - '0');
            }
            return value;
        });
    auto r = integer_p(cursor{"42"});
    return r.has_value() && r.value().value == 42 && r.value().rest.empty();
}());

TEST_CASE("ParserOpsTest - HeaderIsIdempotent") { REQUIRE(true); }

TEST_CASE("ParserOpsTest - FunctorTypeclassLookup") {
    auto base_parser = char_p('a');
    using P = decltype(base_parser);
    const auto &tc = functor_typeclass<P>;
    auto p = tc.fmap([](char c) { return c; }, base_parser);
    auto r = p(cursor{"a"});
    REQUIRE(r.has_value());
    REQUIRE(r.value().value == 'a');
}

TEST_CASE("ParserOpsTest - DerivedApply") {
    auto pf = parser_v.pure([](char c) -> int { return c - '0'; });
    auto pa = char_p('5');
    auto p = parser_v.apply(pf, pa);
    auto r = p(cursor{"5"});
    REQUIRE(r.has_value());
    REQUIRE(r.value().value == 5);
}

TEST_CASE("ParserOpsTest - DiscardFirstKeepsSecond") {
    auto p = parser_v.discard_first(char_p('a'), char_p('b'));
    auto r = p(cursor{"ab"});
    REQUIRE(r.has_value());
    REQUIRE(r.value().value == 'b');
}

TEST_CASE("ParserOpsTest - DiscardSecondKeepsFirst") {
    auto p = parser_v.discard_second(char_p('a'), char_p('b'));
    auto r = p(cursor{"ab"});
    REQUIRE(r.has_value());
    REQUIRE(r.value().value == 'a');
}

TEST_CASE("ParserOpsTest - NttpPinning") {
    auto p = map_via_typeclass(char_p('q'), [](char) { return 77; });
    auto r = p(cursor{"q"});
    REQUIRE(r.has_value());
    REQUIRE(r.value().value == 77);
}

TEST_CASE("ParserOpsTest - AlternativeEmptyIsIdentity") {
    auto none = parser_v.empty<char>();
    REQUIRE(!none(cursor{"a"}).has_value());
    auto p = parser_v.alt(none, char_p('a'));
    REQUIRE(p(cursor{"a"}).value().value == 'a');
}

TEST_CASE("ParserOpsTest - IntegerFromPrimitives") {
    auto is_digit = [](char c) { return c >= '0' && c <= '9'; };
    auto digit_p = smd::forth::parser::satisfy(is_digit, "digit");
    auto integer_p = smd::forth::parser::map(
        smd::forth::parser::some<8>(digit_p), [](auto digits) {
            int value = 0;
            for (auto d : digits) {
                value = value * 10 + (d - '0');
            }
            return value;
        });
    auto r = integer_p(cursor{"42"});
    REQUIRE(r.has_value());
    REQUIRE(r.value().value == 42);
    REQUIRE(r.value().rest.empty());
}
