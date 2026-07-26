// src/smd/forth/parser/forth_chars.hpp                               -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#ifndef SRC_SMD_FORTH_PARSER_FORTH_CHARS_HPP
#define SRC_SMD_FORTH_PARSER_FORTH_CHARS_HPP

#include <smd/forth/foundation/parse_error.hpp>
#include <smd/forth/foundation/source_pos.hpp>
#include <smd/forth/foundation/source_span.hpp>
#include <smd/forth/foundation/static_vector.hpp>
#include <smd/forth/parser/alt.hpp>
#include <smd/forth/parser/cursor.hpp>
#include <smd/forth/parser/parser.hpp>

#include <cstdint>
#include <string_view>

namespace smd::forth::parser {

// Step F5 (docs/forth-plan.md): the Forth-aware token layer, D19's "below the
// word" combinator-library machinery. Relocated here from
// src/smd/forth/reader/forth_chars.hpp at step F26 ("the cut",
// docs/forth-plan-2.md): D19 always placed this file's own contents under
// parser/ conceptually ("combinator library machinery several consumers sit
// on top of"), and DIV-0012 deferred the actual move to this step because
// reader/ was still alive and every one of this file's own consumers still
// had to keep building. reader/ is deleted wholesale by this same step, so
// the deferral's own reason is gone; this is a pure relocation plus a
// namespace rename (smd::forth::reader -> smd::forth::parser) -- no
// declaration below changed behavior.

// e9a1c6a1-9d3d-4a6a-9f2e-7d3f2b3c8a1c
/// Uppercase-folds a single ASCII letter; every other character passes
/// through unchanged.
///
/// Word names fold to uppercase at scan time and are stored folded (D8) --
/// this is the single character-level primitive that folding builds on.
[[nodiscard]] constexpr auto fold_char(char c) -> char {
    if (c >= 'a' && c <= 'z') {
        return static_cast<char>(c - 'a' + 'A');
    }
    return c;
}

/// True for any character that is not ASCII whitespace.
///
/// A word is any run of non-whitespace characters (the plan's literal
/// definition); by the time a scanner reaches a word's first character,
/// @ref skip_forth_space has already consumed any leading `\` or `( ... )`
/// comment, so no comment-starting character needs special-casing here.
[[nodiscard]] constexpr auto is_word_char(char c) -> bool {
    return !is_space(c);
}

/// True for an ASCII decimal digit.
[[nodiscard]] constexpr auto is_digit(char c) -> bool {
    return c >= '0' && c <= '9';
}
// e9a1c6a1-9d3d-4a6a-9f2e-7d3f2b3c8a1c end

// b2f4d9a0-6c1e-4f8b-9a2d-3e5c7f1a9b4d
/// Scans a `( ... )` comment starting exactly at the opening `(` at @p cur,
/// preserving its source span rather than discarding it.
///
/// Fails (without consuming) if @p cur is not positioned at `(`; fails at the
/// comment's start position if the comment is never closed. The returned
/// span covers from the opening `(` up to and including the closing `)`, so
/// re-slicing the original source text through it reproduces the comment
/// verbatim, e.g. `( a b -- c )`.
///
/// This is the comment-capture support F7 uses to grab a colon-definition's
/// declared stack-effect comment (D9); @ref skip_forth_space uses this same
/// scanner internally but discards the span, since ordinary intertoken space
/// only needs comments skipped, not preserved.
[[nodiscard]] constexpr auto scan_paren_comment(cursor cur)
    -> parse_result<foundation::source_span> {
    if (cur.empty() || cur.peek() != '(') {
        return foundation::parse_error{cur.position(), "expected ( comment"};
    }
    auto const start = cur.position();
    cur = cur.bump();
    while (!cur.empty() && cur.peek() != ')') {
        cur = cur.bump();
    }
    if (cur.empty()) {
        return foundation::parse_error{start, "unterminated ( comment"};
    }
    cur = cur.bump();
    return parse_state<foundation::source_span>{
        foundation::source_span{start, cur.position()}, cur};
}
// b2f4d9a0-6c1e-4f8b-9a2d-3e5c7f1a9b4d end

// c3d5e8b2-7f2a-4b9c-8d3e-4f6a8c2b1e9f
/// Skips Forth's own notion of intertoken space: ASCII whitespace, `\` line
/// comments (a backslash through the end of its line), and `( ... )`
/// comments (a space-delimited `(` up to its closing `)`), repeating until
/// none of the three remain.
///
/// Distinct from @ref skip_intertoken_space, which only skips plain
/// ASCII whitespace and knows nothing about either comment form.
[[nodiscard]] constexpr auto skip_forth_space(cursor cur)
    -> cursor {
    for (;;) {
        cur = skip_intertoken_space(cur);
        if (cur.empty()) {
            return cur;
        }
        if (cur.peek() == '\\') {
            while (!cur.empty() && cur.peek() != '\n') {
                cur = cur.bump();
            }
            continue;
        }
        if (cur.peek() == '(') {
            auto comment = scan_paren_comment(cur);
            if (!comment.has_value()) {
                // Unterminated '(' comment: stop skipping here and let the
                // caller's next parse step fail in context, rather than
                // consuming to end-of-input silently.
                return cur;
            }
            cur = comment.value().rest;
            continue;
        }
        return cur;
    }
}
// c3d5e8b2-7f2a-4b9c-8d3e-4f6a8c2b1e9f end

/// Wraps @p p so it first skips @ref skip_forth_space, then runs @p p, then
/// skips @ref skip_forth_space again -- the Forth-aware analogue of
/// @ref lexeme, which only skips plain whitespace around @p p.
template <parser_like P>
[[nodiscard]] constexpr auto forth_lexeme(P p) {
    return parser{[p](cursor cur) {
        auto start = skip_forth_space(cur);
        auto r = p(start);
        if (!r.has_value()) {
            return r;
        }
        auto rest = skip_forth_space(r.value().rest);
        using V = decltype(r.value().value);
        return parse_result<V>{
            parse_state<V>{r.value().value, rest}};
    }};
}

// d4e6f9c3-8a3b-4c9d-9e4f-5a7b9d3c2f8a
/// Folded token text, capped at @p MaxName characters.
template <int MaxName = 32>
using token_text = foundation::static_vector<char, MaxName>;

/// Scans one word token: skips surrounding @ref skip_forth_space, then reads
/// a run of @ref is_word_char characters, folding each to uppercase as it
/// goes (D8) -- scanning `dup` yields `DUP`.
///
/// @tparam MaxName Maximum number of characters in the scanned token.
template <int MaxName = 32>
[[nodiscard]] constexpr auto scan_word(cursor cur)
    -> parse_result<token_text<MaxName>> {
    auto p = forth_lexeme(map(
        some<MaxName>(satisfy(is_word_char, "expected word")),
        [](token_text<MaxName> raw) {
            token_text<MaxName> folded{};
            for (char c : raw) {
                folded.push_back(fold_char(c));
            }
            return folded;
        }));
    return p(cur);
}
// d4e6f9c3-8a3b-4c9d-9e4f-5a7b9d3c2f8a end

// e5f7a0d4-9b4c-4d0e-af5a-6b8c0e4d3a9b
/// True iff @p text is an optional leading `-` followed by one or more
/// decimal digits, and nothing else.
///
/// Numbers are signed decimal only (D8): `-1` is a number; `1-` is a word
/// (the `-` is not the first character); `-` alone is a word (a sign with no
/// digits after it is not a number).
[[nodiscard]] constexpr auto is_number_token(std::string_view text) -> bool {
    std::size_t i = 0;
    if (!text.empty() && text[0] == '-') {
        i = 1;
    }
    if (i == text.size()) {
        return false;
    }
    for (; i < text.size(); ++i) {
        if (!is_digit(text[i])) {
            return false;
        }
    }
    return true;
}

/// Converts @p text into its signed decimal value.
/// @pre is_number_token(text)
[[nodiscard]] constexpr auto token_to_cell(std::string_view text)
    -> std::int64_t {
    bool const negative = !text.empty() && text[0] == '-';
    std::size_t i = negative ? 1 : 0;
    std::int64_t value = 0;
    for (; i < text.size(); ++i) {
        value = value * 10 + (text[i] - '0');
    }
    return negative ? -value : value;
}
// e5f7a0d4-9b4c-4d0e-af5a-6b8c0e4d3a9b end

} // namespace smd::forth::parser

#endif
