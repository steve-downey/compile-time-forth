// src/smd/forth/parser/forth_chars.test.cpp                        -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <smd/forth/parser/forth_chars.hpp>
#include <smd/forth/parser/forth_chars.hpp> // test 2nd include OK

#include <catch2/catch_test_macros.hpp>

#include <string_view>

using smd::forth::parser::cursor;
using smd::forth::parser::fold_char;
using smd::forth::parser::is_digit;
using smd::forth::parser::is_number_token;
using smd::forth::parser::is_word_char;
using smd::forth::parser::scan_bare_name;
using smd::forth::parser::scan_delimited;
using smd::forth::parser::scan_paren_comment;
using smd::forth::parser::scan_word;
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

// scan_word no longer treats `\`/`(` specially (step F29, D19): each scans
// as an ordinary one-character word whenever followed by whitespace, since
// the reexpressed `\`/`(` dictionary words (interp.hpp) are what consumes
// the comment text now, not this scanner.

static_assert([] {
    auto r = scan_word<32>(cursor{"( a comment"});
    return r.has_value() && view_of(r.value().value) == "(" &&
           r.value().rest.remaining() == "a comment";
}());

static_assert([] {
    auto r = scan_word<32>(cursor{"\\ rest of line"});
    return r.has_value() && view_of(r.value().value) == "\\" &&
           r.value().rest.remaining() == "rest of line";
}());

// Merge criterion: scan_delimited -- PARSE's own shape (no leading skip) and
// WORD's own shape (skip leading delimiters first), both consuming past the
// trailing delimiter when found and reporting when it is not.

static_assert([] {
    auto r = scan_delimited(cursor{"hello\" world"}, '"', false);
    return r.text == "hello" && r.found_delim && r.rest.remaining() == " world";
}());

static_assert([] {
    // No leading skip: a leading delimiter occurrence ends the run at once,
    // with an empty result -- PARSE's own contract, not WORD's.
    auto r = scan_delimited(cursor{"  x  "}, ' ', false);
    return r.text.empty() && r.found_delim && r.rest.remaining() == " x  ";
}());

static_assert([] {
    // Leading skip: WORD's own extra step over PARSE.
    auto r = scan_delimited(cursor{"  x  y"}, ' ', true);
    return r.text == "x" && r.found_delim && r.rest.remaining() == " y";
}());

static_assert([] {
    // Delimiter never found: reports found_delim == false, text is whatever
    // remained, rest is positioned at end of input.
    auto r = scan_delimited(cursor{"no closing quote"}, '"', false);
    return !r.found_delim && r.text == "no closing quote" && r.rest.empty();
}());

// scan_bare_name: CHAR/[CHAR]'s own shape -- skip leading whitespace, return
// the whole raw (unfolded) run up to the next whitespace or end of input.

static_assert([] {
    auto r = scan_bare_name(cursor{"  abc def"});
    return r.has_value() && r.value().value == "abc" &&
           r.value().rest.remaining() == " def";
}());

static_assert([] {
    auto r = scan_bare_name(cursor{"   "});
    return !r.has_value();
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

TEST_CASE("ForthCharsTest - HeaderIsIdempotent") { REQUIRE(true); }

TEST_CASE("ForthCharsTest - ScanWordFoldsCase") {
    auto r = scan_word<32>(cursor{"Dup"});
    REQUIRE(r.has_value());
    REQUIRE(view_of(r.value().value) == "DUP");
}

TEST_CASE("ForthCharsTest - ScanDelimitedSkipsLeadingOnlyWhenAsked") {
    auto no_skip = scan_delimited(cursor{"  x  "}, ' ', false);
    CHECK(no_skip.text.empty());
    auto skip = scan_delimited(cursor{"  x  y"}, ' ', true);
    CHECK(skip.text == "x");
}

TEST_CASE("ForthCharsTest - ScanBareNameReturnsRawFirstWord") {
    auto r = scan_bare_name(cursor{"  abc def"});
    REQUIRE(r.has_value());
    CHECK(r.value().value == "abc");
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
