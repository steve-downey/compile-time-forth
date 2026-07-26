// src/smd/forth/parser/forth_chars.test.cpp                        -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <smd/forth/parser/forth_chars.hpp>
#include <smd/forth/parser/forth_chars.hpp> // test 2nd include OK

#include <catch2/catch_test_macros.hpp>

#include <string_view>

using smd::forth::parser::cursor;
using smd::forth::parser::fold_char;
using smd::forth::parser::forth_lexeme;
using smd::forth::parser::is_digit;
using smd::forth::parser::is_number_token;
using smd::forth::parser::is_word_char;
using smd::forth::parser::scan_paren_comment;
using smd::forth::parser::scan_word;
using smd::forth::parser::skip_forth_space;
using smd::forth::parser::token_text;
using smd::forth::parser::token_to_cell;

namespace {

/// Views a scanned token's characters as a @c std::string_view for test
/// comparisons; production code never needs to do this (@ref
/// is_number_token and @ref token_to_cell both take @c std::string_view
/// directly).
template <int MaxName>
constexpr auto view_of(token_text<MaxName> const &text) -> std::string_view {
    return std::string_view{text.begin(),
                            static_cast<std::size_t>(text.size())};
}

} // namespace

// fold_char: lowercase letters fold to uppercase; everything else is
// unchanged.

static_assert(fold_char('a') == 'A');
static_assert(fold_char('z') == 'Z');
static_assert(fold_char('A') == 'A');
static_assert(fold_char('*') == '*');
static_assert(fold_char('1') == '1');

// is_word_char / is_digit.

static_assert(is_word_char('x'));
static_assert(!is_word_char(' '));
static_assert(!is_word_char('\t'));
static_assert(!is_word_char('\n'));
static_assert(is_digit('0'));
static_assert(is_digit('9'));
static_assert(!is_digit('a'));
static_assert(!is_digit('-'));

// Merge criterion: folding -- scanning `dup` yields `DUP`.

static_assert([] {
    auto r = scan_word<32>(cursor{"dup"});
    return r.has_value() && view_of(r.value().value) == "DUP" &&
           r.value().rest.empty();
}());

static_assert([] {
    auto r = scan_word<32>(cursor{"Dup2"});
    return r.has_value() && view_of(r.value().value) == "DUP2";
}());

// scan_word skips leading and trailing intertoken space (plain whitespace).

static_assert([] {
    auto r = scan_word<32>(cursor{"   dup  swap"});
    return r.has_value() && view_of(r.value().value) == "DUP" &&
           r.value().rest.remaining() == "swap";
}());

// Merge criterion: comment skipping -- both `\ ...` and `( ... )` kinds.

static_assert([] {
    // A `\` comment runs to end of line; the next word is on the next line.
    auto cur = skip_forth_space(cursor{"\\ ignored to eol\ndup"});
    return cur.remaining() == "dup";
}());

static_assert([] {
    // A `\` comment that runs to end of input (no trailing newline).
    auto cur = skip_forth_space(cursor{"\\ ignored to end"});
    return cur.empty();
}());

static_assert([] {
    // A `( ... )` comment is skipped like whitespace.
    auto cur = skip_forth_space(cursor{"( a b -- c ) dup"});
    return cur.remaining() == "dup";
}());

static_assert([] {
    // Runs of whitespace, `\` comments, and `( ... )` comments interleave.
    auto cur =
        skip_forth_space(cursor{"  \\ line comment\n  ( paren comment )  dup"});
    return cur.remaining() == "dup";
}());

static_assert([] {
    // scan_word composes with comment skipping via forth_lexeme.
    auto r = scan_word<32>(cursor{"( comment ) dup ( trailing )"});
    return r.has_value() && view_of(r.value().value) == "DUP" &&
           r.value().rest.empty();
}());

// scan_paren_comment: preserves the comment's own text as a source_span, for
// F7's stack-effect declaration capture (D9).

static_assert([] {
    std::string_view src = "( a b -- c ) DUP";
    auto r = scan_paren_comment(cursor{src});
    if (!r.has_value()) {
        return false;
    }
    auto span = r.value().value;
    auto text = src.substr(
        static_cast<std::size_t>(span.first.offset),
        static_cast<std::size_t>(span.last.offset - span.first.offset));
    return text == "( a b -- c )" && r.value().rest.remaining() == " DUP";
}());

static_assert([] {
    // Failure: not positioned at '('.
    auto r = scan_paren_comment(cursor{"dup"});
    return !r.has_value();
}());

static_assert([] {
    // Failure: unterminated comment.
    auto r = scan_paren_comment(cursor{"( never closed"});
    return !r.has_value();
}());

// Merge criterion: number/word discrimination -- `-1` number, `1-` word,
// `-` word.

static_assert(is_number_token("-1"));
static_assert(!is_number_token("1-"));
static_assert(!is_number_token("-"));
static_assert(is_number_token("0"));
static_assert(is_number_token("42"));
static_assert(!is_number_token(""));
static_assert(!is_number_token("dup"));

// token_to_cell: signed decimal value of an already-confirmed number token.

static_assert(token_to_cell("42") == 42);
static_assert(token_to_cell("-1") == -1);
static_assert(token_to_cell("0") == 0);
static_assert(token_to_cell("-123") == -123);

// forth_lexeme: strips Forth intertoken space (including comments) around an
// arbitrary parser, not just scan_word.

static_assert([] {
    auto p = forth_lexeme(smd::forth::parser::char_p('x'));
    auto r = p(cursor{" \\ comment\n ( paren ) x ( trailing )"});
    return r.has_value() && r.value().value == 'x' && r.value().rest.empty();
}());

TEST_CASE("ForthCharsTest - HeaderIsIdempotent") { REQUIRE(true); }

TEST_CASE("ForthCharsTest - ScanWordFoldsCase") {
    auto r = scan_word<32>(cursor{"Dup"});
    REQUIRE(r.has_value());
    REQUIRE(view_of(r.value().value) == "DUP");
}

TEST_CASE("ForthCharsTest - SkipForthSpaceHandlesBothCommentKinds") {
    auto cur =
        skip_forth_space(cursor{"\\ eol comment\n( paren comment ) dup"});
    REQUIRE(cur.remaining() == "dup");
}

TEST_CASE("ForthCharsTest - NumberWordDiscrimination") {
    REQUIRE(is_number_token("-1"));
    REQUIRE_FALSE(is_number_token("1-"));
    REQUIRE_FALSE(is_number_token("-"));
}

TEST_CASE("ForthCharsTest - ScanParenCommentPreservesText") {
    std::string_view src = "( a b -- c ) DUP";
    auto r = scan_paren_comment(cursor{src});
    REQUIRE(r.has_value());
    auto span = r.value().value;
    auto text = src.substr(
        static_cast<std::size_t>(span.first.offset),
        static_cast<std::size_t>(span.last.offset - span.first.offset));
    REQUIRE(text == "( a b -- c )");
}

TEST_CASE("ForthCharsTest - ScanParenCommentFailsWhenUnterminated") {
    auto r = scan_paren_comment(cursor{"( never closed"});
    REQUIRE_FALSE(r.has_value());
}
