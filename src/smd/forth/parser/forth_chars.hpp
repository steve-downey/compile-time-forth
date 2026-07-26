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
/// definition). Step F29 (docs/forth-plan-2.md), D19: `\` and `(` are
/// ordinary dictionary words now (see @ref scan_delimited and
/// `interpreter::apply_control_word`'s own `paren_`/`backslash_` cases),
/// not scanner-level special cases, so a `(` or `\` character is an
/// ordinary word char exactly like any other -- @ref scan_word (below)
/// scans `(` or `\` as a one-character token whenever the very next
/// character is whitespace, the same self-delimiting convention every
/// other Forth-2012 system relies on for both words.
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
/// declared stack-effect comment (D9): @ref interpreter::scan_colon_header
/// (`interp.hpp`) is its one remaining caller since step F29 re-expresses
/// ordinary `( ... )` comments as the dictionary word `(` instead
/// (@ref scan_delimited is what that word's own action uses, since by the
/// time it runs the opening `(` token itself has already been consumed by
/// the ordinary text-interpreter loop, D19).
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

// d4e6f9c3-8a3b-4c9d-9e4f-5a7b9d3c2f8a
/// Folded token text, capped at @p MaxName characters.
template <int MaxName = 32>
using token_text = foundation::static_vector<char, MaxName>;

/// Scans one word token: skips surrounding @ref lexeme whitespace (plain
/// ASCII intertoken space only -- step F29, D19, re-expresses `\` and `(`
/// comments as ordinary dictionary words rather than scanner-level skipping,
/// so this scanner no longer treats either specially; a source `(` or `\`
/// character is scanned as an ordinary one-character word exactly like any
/// other name would be), then reads a run of @ref is_word_char characters,
/// folding each to uppercase as it goes (D8) -- scanning `dup` yields `DUP`.
///
/// @tparam MaxName Maximum number of characters in the scanned token.
template <int MaxName = 32>
[[nodiscard]] constexpr auto scan_word(cursor cur)
    -> parse_result<token_text<MaxName>> {
    auto p = lexeme(map(some<MaxName>(satisfy(is_word_char, "expected word")),
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

// 9cfdb6e6-c3b2-467a-a80c-50a53628f424
/// The result of @ref scan_delimited: the delimited run's own raw text
/// (sliced directly out of the scanned cursor's underlying view, no
/// case-folding, unlike @ref scan_word) and the cursor positioned just past
/// it -- past the trailing delimiter if @ref found_delim, or at end of
/// input otherwise. This is Forth-2012 `PARSE`'s own `>IN` update either
/// way, and every parsing word this project defines computes it the same
/// way (D19).
struct delimited_scan {
    std::string_view text{};  ///< The scanned run, excluding the delimiter.
    cursor rest{cursor{""}};  ///< Positioned just past the run (see above).
    bool found_delim = false; ///< False if input ran out before @p delim.
};

/// Scans @p cur for a run of characters delimited by @p delim -- Forth-2012
/// `PARSE`'s own definition, and D19's shared "below the word" scanning
/// primitive every parsing word this project defines is built from. If
/// @p skip_leading, first skips any leading occurrences of @p delim (`WORD`'s
/// own extra step over plain `PARSE`); then collects every character up to
/// the next occurrence of @p delim, or to the end of input, whichever comes
/// first.
///
/// Never fails: running out of input before finding @p delim is an ordinary
/// outcome for `PARSE`/`WORD` (@ref delimited_scan::found_delim reports it,
/// for callers -- `(`, `S"`, `."`, `ABORT"` -- that choose to diagnose it as
/// an unterminated form instead).
///
/// `PARSE`, `WORD`, `S"`, `."`, `ABORT"`, and the reexpressed `(` (step F29,
/// D19's own demonstration that a parsing word needs nothing but the input
/// stream) all consume the same stream through this one scan, differing
/// only in @p delim, @p skip_leading, and what each does with the resulting
/// @ref delimited_scan::text afterward -- which is what makes a user-defined
/// parsing word written in ordinary Forth, calling `PARSE` or `WORD`
/// directly, work identically to any of this project's own built-in ones.
[[nodiscard]] constexpr auto scan_delimited(cursor cur, char delim,
                                            bool skip_leading)
    -> delimited_scan {
    if (skip_leading) {
        while (!cur.empty() && cur.peek() == delim) {
            cur = cur.bump();
        }
    }
    auto const text_begin = cur;
    int len = 0;
    while (!cur.empty() && cur.peek() != delim) {
        cur = cur.bump();
        ++len;
    }
    bool const found = !cur.empty();
    std::string_view text =
        text_begin.remaining().substr(0, static_cast<std::size_t>(len));
    return delimited_scan{text, found ? cur.bump() : cur, found};
}
// 9cfdb6e6-c3b2-467a-a80c-50a53628f424 end

/// Scans one blank-delimited name, raw (no case-folding, unlike
/// @ref scan_word, and no @ref token_text capacity limit -- only its own
/// first character is ever consulted by this project's `CHAR`/`[CHAR]`, the
/// two callers this exists for): skips leading @ref is_space characters,
/// then collects every character up to the next @ref is_space character or
/// end of input. Fails if nothing but whitespace remains.
[[nodiscard]] constexpr auto scan_bare_name(cursor cur)
    -> parse_result<std::string_view> {
    cur = skip_intertoken_space(cur);
    if (cur.empty()) {
        return foundation::parse_error{cur.position(), "expected a name"};
    }
    auto const begin = cur;
    int len = 0;
    while (!cur.empty() && !is_space(cur.peek())) {
        cur = cur.bump();
        ++len;
    }
    std::string_view text =
        begin.remaining().substr(0, static_cast<std::size_t>(len));
    return parse_state<std::string_view>{text, cur};
}

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
