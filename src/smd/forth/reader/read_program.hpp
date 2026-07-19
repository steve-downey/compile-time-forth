// src/smd/forth/reader/read_program.hpp                             -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#ifndef SRC_SMD_FORTH_READER_READ_PROGRAM_HPP
#define SRC_SMD_FORTH_READER_READ_PROGRAM_HPP

#include <smd/forth/foundation/arena_box.hpp>
#include <smd/forth/foundation/parse_error.hpp>
#include <smd/forth/foundation/result.hpp>
#include <smd/forth/foundation/source_pos.hpp>
#include <smd/forth/foundation/source_span.hpp>
#include <smd/forth/parser/cursor.hpp>
#include <smd/forth/parser/parser.hpp>
#include <smd/forth/reader/forth_chars.hpp>
#include <smd/forth/reader/syntax_tree.hpp>

#include <string_view>

namespace smd::forth::reader {

// The production Forth grammar (docs/forth-plan.md Step F7):
//
//   program     := item* eof
//   item        := colon-def | variable | constant | create | body-item
//   colon-def   := ':' name effect-comment? body-item* ';'
//   body-item   := literal | tick | if | begin-until | begin-while
//                  | do-loop | word
//   if          := 'IF' body-item* ('ELSE' body-item*)? 'THEN'
//   begin-until := 'BEGIN' body-item* 'UNTIL'
//   begin-while := 'BEGIN' body-item* 'WHILE' body-item* 'REPEAT'
//   do-loop     := 'DO' body-item* ('LOOP' | '+LOOP')
//   tick        := ''' name
//   variable    := 'VARIABLE' name
//   constant    := 'CONSTANT' name
//   create      := 'CREATE' name
//
// Token-level scanning (names, numbers, keywords, comment capture) is built
// entirely from the F4/F5 combinators (scan_word, skip_forth_space,
// scan_paren_comment, is_number_token, token_to_cell) per D6 -- see
// scan_token below. Assembling *nested* body-item sequences (if/begin-until/
// begin-while/do-loop, each recursively containing body-item*) is instead a
// small set of mutually recursive plain constexpr functions, not a chain of
// combinator composition (map/lift2/operator|): see DIV-0005 for why a
// combinator-only encoding of this part of the grammar is not expressible in
// C++ without either type erasure (barred: no heap in the compiled pipeline,
// D3) or an inherently self-referential parser<F> type. Every one of these
// functions still calls down into the F4/F5 free-function combinators for
// its own token-level work; only the tree-shaped recursion itself is
// hand-written.

// 390328cc-5700-46d8-8da9-dc1398fe72ba
/// The reserved-word set: control-structure keywords, `:`/`;`, and `'`.
///
/// Recognized after case folding (F5 folds every scanned word to uppercase,
/// so every comparison here is against an already-uppercase spelling). A
/// colon definition may not redefine any of these -- see @ref
/// parse_colon_def.
[[nodiscard]] constexpr auto is_reserved_word(std::string_view text) -> bool {
    constexpr std::string_view reserved[] = {
        ":",        ";",        "IF",     "ELSE", "THEN", "BEGIN",
        "UNTIL",    "WHILE",    "REPEAT", "DO",   "LOOP", "+LOOP",
        "VARIABLE", "CONSTANT", "CREATE", "'"};
    for (auto candidate : reserved) {
        if (text == candidate) {
            return true;
        }
    }
    return false;
}

/// Views a scanned token's characters as a @c std::string_view for
/// comparisons against keyword spellings.
template <int MaxName>
[[nodiscard]] constexpr auto token_view(token_text<MaxName> const &text)
    -> std::string_view {
    return std::string_view{text.begin(),
                            static_cast<std::size_t>(text.size())};
}

/// A scanned token together with the source position of its first character
/// (before any folding or trailing-space skip) -- what @ref scan_word alone
/// does not preserve.
template <int MaxName = 32>
struct scanned_token {
    token_text<MaxName> text{};   ///< Folded token spelling.
    foundation::source_pos pos{}; ///< Position of the token's first character.
};

/// Scans one token via @ref scan_word, additionally recording where it
/// starts. Built directly from F5's combinators (@ref skip_forth_space, @ref
/// scan_word) -- token-level scanning stays inside the combinator library
/// even though the grammar's tree assembly (below) does not.
template <int MaxName = 32>
[[nodiscard]] constexpr auto scan_token(parser::cursor cur)
    -> parser::parse_result<scanned_token<MaxName>> {
    auto const pos = skip_forth_space(cur).position();
    auto r = scan_word<MaxName>(cur);
    if (!r.has_value()) {
        return r.error();
    }
    return parser::parse_state<scanned_token<MaxName>>{
        scanned_token<MaxName>{r.value().value, pos}, r.value().rest};
}

/// Scans one token like @ref scan_token, but without @ref scan_word's
/// trailing @ref skip_forth_space.
///
/// @ref scan_word wraps its scan in @ref forth_lexeme, which skips *both*
/// leading and trailing Forth intertoken space -- and `( ... )` comments are
/// part of that space (D9), so by the time an ordinary @ref scan_token
/// returns, a stack-effect comment immediately following the scanned token
/// would already be silently consumed. @ref parse_colon_def needs to inspect
/// (not consume) whatever immediately follows a definition's name before
/// anything skips it, so it uses this instead: only the *leading* skip runs,
/// and the returned cursor sits exactly at the first character after the
/// token's own text.
template <int MaxName = 32>
[[nodiscard]] constexpr auto scan_token_no_trailing_skip(parser::cursor cur)
    -> parser::parse_result<scanned_token<MaxName>> {
    auto const skipped = skip_forth_space(cur);
    auto const pos = skipped.position();
    auto raw = parser::some<MaxName>(
        parser::satisfy(is_word_char, "expected word"))(skipped);
    if (!raw.has_value()) {
        return raw.error();
    }
    token_text<MaxName> folded{};
    for (char c : raw.value().value) {
        folded.push_back(fold_char(c));
    }
    return parser::parse_state<scanned_token<MaxName>>{
        scanned_token<MaxName>{folded, pos}, raw.value().rest};
}
// 390328cc-5700-46d8-8da9-dc1398fe72ba end

/// Alias for this grammar's arena type, given the tree's three capacities.
template <int MaxNodes, int MaxBody, int MaxName>
using program_arena =
    foundation::tree_arena<syn_node<MaxNodes, MaxBody, MaxName>, MaxNodes>;

/// A successfully parsed body-item or top-level item: its arena handle, and
/// the cursor positioned just past it.
template <int MaxNodes, int MaxBody, int MaxName>
struct item_result {
    syn_box<MaxNodes, MaxBody, MaxName> handle; ///< Handle to the new node.
    parser::cursor rest; ///< Cursor positioned after the parsed item.
};

/// Result of parsing one body-item or top-level item: either an @ref
/// item_result or a @ref foundation::parse_error with a source position.
template <int MaxNodes, int MaxBody, int MaxName>
using item_outcome =
    foundation::result<item_result<MaxNodes, MaxBody, MaxName>>;

/// A successfully parsed `body-item*` sequence, together with which
/// terminating keyword ended it (`;`, `THEN`, `ELSE`, `UNTIL`, `WHILE`,
/// `REPEAT`, `LOOP`, or `+LOOP`) and the cursor positioned just past that
/// terminator.
template <int MaxNodes, int MaxBody, int MaxName>
struct body_result {
    syn_body<MaxNodes, MaxBody, MaxName> forms{}; ///< The parsed body-items.
    token_text<MaxName> terminator{}; ///< Which terminator token matched.
    foundation::source_pos terminator_pos{}; ///< Where the terminator starts.
    parser::cursor rest; ///< Cursor positioned after the terminator.
};

/// Result of parsing a `body-item*` sequence up to one of its terminators.
template <int MaxNodes, int MaxBody, int MaxName>
using body_outcome =
    foundation::result<body_result<MaxNodes, MaxBody, MaxName>>;

// Forward declarations: parse_body_until and parse_body_item_from_token are
// mutually recursive (a body may contain IF/BEGIN/DO, each of which parses
// its own nested body-item* sequences), and parse_if/parse_begin/parse_do
// each call back into parse_body_until. See DIV-0005.

template <int MaxNodes, int MaxBody, int MaxName, class IsTerminator>
constexpr auto
parse_body_until(program_arena<MaxNodes, MaxBody, MaxName> &arena,
                 parser::cursor cur, int depth, int max_depth,
                 IsTerminator is_terminator, char const *eof_message)
    -> body_outcome<MaxNodes, MaxBody, MaxName>;

template <int MaxNodes, int MaxBody, int MaxName>
constexpr auto
parse_body_item_from_token(program_arena<MaxNodes, MaxBody, MaxName> &arena,
                           token_text<MaxName> const &tok,
                           foundation::source_pos tok_pos, parser::cursor rest,
                           int depth, int max_depth)
    -> item_outcome<MaxNodes, MaxBody, MaxName>;

// 26f1c2ad-ec1f-4080-83c0-121f69d835eb
/// Parses an `IF body-item* ('ELSE' body-item*)? THEN` conditional.
///
/// @p if_pos is the position of the already-consumed `IF` token; @p rest is
/// the cursor just past it.
template <int MaxNodes, int MaxBody, int MaxName>
constexpr auto parse_if(program_arena<MaxNodes, MaxBody, MaxName> &arena,
                        foundation::source_pos if_pos, parser::cursor rest,
                        int depth, int max_depth)
    -> item_outcome<MaxNodes, MaxBody, MaxName> {
    if (depth >= max_depth) {
        return foundation::parse_error{if_pos, "max nesting depth exceeded"};
    }
    auto then_part = parse_body_until<MaxNodes, MaxBody, MaxName>(
        arena, rest, depth + 1, max_depth,
        [](std::string_view t) { return t == "ELSE" || t == "THEN"; },
        "unterminated IF (missing THEN)");
    if (!then_part.has_value()) {
        return then_part.error();
    }
    auto const &tp = then_part.value();
    if (token_view(tp.terminator) == "THEN") {
        auto handle = foundation::make_arena_box(
            arena,
            syn_node<MaxNodes, MaxBody, MaxName>{
                .value = syn_if<MaxNodes, MaxBody, MaxName>{
                    .then_body = tp.forms, .else_body = {}, .pos = if_pos}});
        return item_result<MaxNodes, MaxBody, MaxName>{handle, tp.rest};
    }
    auto else_part = parse_body_until<MaxNodes, MaxBody, MaxName>(
        arena, tp.rest, depth + 1, max_depth,
        [](std::string_view t) { return t == "THEN"; },
        "unterminated IF (missing THEN after ELSE)");
    if (!else_part.has_value()) {
        return else_part.error();
    }
    auto const &ep = else_part.value();
    auto handle = foundation::make_arena_box(
        arena,
        syn_node<MaxNodes, MaxBody, MaxName>{
            .value = syn_if<MaxNodes, MaxBody, MaxName>{
                .then_body = tp.forms, .else_body = ep.forms, .pos = if_pos}});
    return item_result<MaxNodes, MaxBody, MaxName>{handle, ep.rest};
}

/// Parses a `BEGIN body-item* UNTIL` or `BEGIN body-item* WHILE body-item*
/// REPEAT` loop -- which shape it is is not known until the first
/// terminator (`UNTIL` or `WHILE`) is reached.
///
/// @p begin_pos is the position of the already-consumed `BEGIN` token; @p
/// rest is the cursor just past it.
template <int MaxNodes, int MaxBody, int MaxName>
constexpr auto parse_begin(program_arena<MaxNodes, MaxBody, MaxName> &arena,
                           foundation::source_pos begin_pos,
                           parser::cursor rest, int depth, int max_depth)
    -> item_outcome<MaxNodes, MaxBody, MaxName> {
    if (depth >= max_depth) {
        return foundation::parse_error{begin_pos, "max nesting depth exceeded"};
    }
    auto first_part = parse_body_until<MaxNodes, MaxBody, MaxName>(
        arena, rest, depth + 1, max_depth,
        [](std::string_view t) { return t == "UNTIL" || t == "WHILE"; },
        "unterminated BEGIN (missing UNTIL or WHILE)");
    if (!first_part.has_value()) {
        return first_part.error();
    }
    auto const &fp = first_part.value();
    if (token_view(fp.terminator) == "UNTIL") {
        auto handle = foundation::make_arena_box(
            arena, syn_node<MaxNodes, MaxBody, MaxName>{
                       .value = syn_begin_until<MaxNodes, MaxBody, MaxName>{
                           .body = fp.forms, .pos = begin_pos}});
        return item_result<MaxNodes, MaxBody, MaxName>{handle, fp.rest};
    }
    auto body_part = parse_body_until<MaxNodes, MaxBody, MaxName>(
        arena, fp.rest, depth + 1, max_depth,
        [](std::string_view t) { return t == "REPEAT"; },
        "unterminated BEGIN...WHILE (missing REPEAT)");
    if (!body_part.has_value()) {
        return body_part.error();
    }
    auto const &bp = body_part.value();
    auto handle = foundation::make_arena_box(
        arena,
        syn_node<MaxNodes, MaxBody, MaxName>{
            .value = syn_begin_while<MaxNodes, MaxBody, MaxName>{
                .condition = fp.forms, .body = bp.forms, .pos = begin_pos}});
    return item_result<MaxNodes, MaxBody, MaxName>{handle, bp.rest};
}

/// Parses a `DO body-item* (LOOP | +LOOP)` counted loop.
///
/// @p do_pos is the position of the already-consumed `DO` token; @p rest is
/// the cursor just past it.
template <int MaxNodes, int MaxBody, int MaxName>
constexpr auto parse_do(program_arena<MaxNodes, MaxBody, MaxName> &arena,
                        foundation::source_pos do_pos, parser::cursor rest,
                        int depth, int max_depth)
    -> item_outcome<MaxNodes, MaxBody, MaxName> {
    if (depth >= max_depth) {
        return foundation::parse_error{do_pos, "max nesting depth exceeded"};
    }
    auto body_part = parse_body_until<MaxNodes, MaxBody, MaxName>(
        arena, rest, depth + 1, max_depth,
        [](std::string_view t) { return t == "LOOP" || t == "+LOOP"; },
        "unterminated DO (missing LOOP or +LOOP)");
    if (!body_part.has_value()) {
        return body_part.error();
    }
    auto const &bp = body_part.value();
    bool const is_plus = token_view(bp.terminator) == "+LOOP";
    auto handle = foundation::make_arena_box(
        arena,
        syn_node<MaxNodes, MaxBody, MaxName>{
            .value = syn_do_loop<MaxNodes, MaxBody, MaxName>{
                .body = bp.forms, .is_plus_loop = is_plus, .pos = do_pos}});
    return item_result<MaxNodes, MaxBody, MaxName>{handle, bp.rest};
}

/// Parses a `' name` tick, taking the (not-yet-resolved) execution token of
/// `name`.
///
/// @p tick_pos is the position of the already-consumed `'` token; @p rest is
/// the cursor just past it.
template <int MaxNodes, int MaxBody, int MaxName>
constexpr auto parse_tick(program_arena<MaxNodes, MaxBody, MaxName> &arena,
                          foundation::source_pos tick_pos, parser::cursor rest)
    -> item_outcome<MaxNodes, MaxBody, MaxName> {
    auto name_scan = scan_token<MaxName>(rest);
    if (!name_scan.has_value()) {
        return foundation::parse_error{name_scan.error().where,
                                       "expected name after '"};
    }
    auto const &named = name_scan.value().value;
    auto handle = foundation::make_arena_box(
        arena, syn_node<MaxNodes, MaxBody, MaxName>{
                   .value = syn_tick<MaxName>{
                       .name = make_syn_name<MaxName>(token_view(named.text)),
                       .pos = tick_pos}});
    return item_result<MaxNodes, MaxBody, MaxName>{handle,
                                                   name_scan.value().rest};
}
// 26f1c2ad-ec1f-4080-83c0-121f69d835eb end

// 0add15bd-dd26-45ae-8f4d-9be58158ff32
/// Dispatches on an already-scanned token to build one body-item node: a
/// literal, a tick, a control structure (recursing into @ref parse_if / @ref
/// parse_begin / @ref parse_do), or a bare word -- or diagnoses a stray
/// closing keyword, a nested `:`, or a declaration keyword that is only
/// legal at the top level.
///
/// This is the one function that turns "what body-item production starts
/// with this token" into a decision; @ref parse_body_until calls it once per
/// non-terminator token, and @ref parse_item (the top-level analogue) falls
/// through to it once it has ruled out `:`/`VARIABLE`/`CONSTANT`/`CREATE`.
template <int MaxNodes, int MaxBody, int MaxName>
constexpr auto
parse_body_item_from_token(program_arena<MaxNodes, MaxBody, MaxName> &arena,
                           token_text<MaxName> const &tok,
                           foundation::source_pos tok_pos, parser::cursor rest,
                           int depth, int max_depth)
    -> item_outcome<MaxNodes, MaxBody, MaxName> {
    auto const text = token_view(tok);

    if (text == ":") {
        return foundation::parse_error{tok_pos, "nested : inside definition"};
    }
    if (text == ";") {
        return foundation::parse_error{tok_pos, "stray ;"};
    }
    if (text == "ELSE") {
        return foundation::parse_error{tok_pos, "ELSE without IF"};
    }
    if (text == "THEN") {
        return foundation::parse_error{tok_pos, "THEN without IF"};
    }
    if (text == "UNTIL") {
        return foundation::parse_error{tok_pos, "UNTIL without BEGIN"};
    }
    if (text == "WHILE") {
        return foundation::parse_error{tok_pos, "WHILE without BEGIN"};
    }
    if (text == "REPEAT") {
        return foundation::parse_error{tok_pos, "REPEAT without BEGIN...WHILE"};
    }
    if (text == "LOOP") {
        return foundation::parse_error{tok_pos, "LOOP without DO"};
    }
    if (text == "+LOOP") {
        return foundation::parse_error{tok_pos, "+LOOP without DO"};
    }
    if (text == "VARIABLE" || text == "CONSTANT" || text == "CREATE") {
        return foundation::parse_error{
            tok_pos,
            "VARIABLE/CONSTANT/CREATE not allowed inside a definition"};
    }
    if (text == "IF") {
        return parse_if<MaxNodes, MaxBody, MaxName>(arena, tok_pos, rest, depth,
                                                    max_depth);
    }
    if (text == "BEGIN") {
        return parse_begin<MaxNodes, MaxBody, MaxName>(arena, tok_pos, rest,
                                                       depth, max_depth);
    }
    if (text == "DO") {
        return parse_do<MaxNodes, MaxBody, MaxName>(arena, tok_pos, rest, depth,
                                                    max_depth);
    }
    if (text == "'") {
        return parse_tick<MaxNodes, MaxBody, MaxName>(arena, tok_pos, rest);
    }
    if (is_number_token(text)) {
        auto handle = foundation::make_arena_box(
            arena, syn_node<MaxNodes, MaxBody, MaxName>{
                       .value = syn_literal{token_to_cell(text), tok_pos}});
        return item_result<MaxNodes, MaxBody, MaxName>{handle, rest};
    }
    auto handle = foundation::make_arena_box(
        arena,
        syn_node<MaxNodes, MaxBody, MaxName>{
            .value = syn_word<MaxName>{make_syn_name<MaxName>(text), tok_pos}});
    return item_result<MaxNodes, MaxBody, MaxName>{handle, rest};
}

/// Parses `body-item*` up to (and consuming) whichever token @p is_terminator
/// accepts, diagnosing @p eof_message if input runs out first.
///
/// @tparam IsTerminator Callable, `bool(std::string_view)`, deciding whether a
///                      scanned token ends this body.
template <int MaxNodes, int MaxBody, int MaxName, class IsTerminator>
constexpr auto
parse_body_until(program_arena<MaxNodes, MaxBody, MaxName> &arena,
                 parser::cursor cur, int depth, int max_depth,
                 IsTerminator is_terminator, char const *eof_message)
    -> body_outcome<MaxNodes, MaxBody, MaxName> {
    syn_body<MaxNodes, MaxBody, MaxName> forms{};
    for (;;) {
        auto scanned = scan_token<MaxName>(cur);
        if (!scanned.has_value()) {
            return foundation::parse_error{scanned.error().where, eof_message};
        }
        auto const &tok = scanned.value().value;
        if (is_terminator(token_view(tok.text))) {
            return body_result<MaxNodes, MaxBody, MaxName>{
                forms, tok.text, tok.pos, scanned.value().rest};
        }
        auto item = parse_body_item_from_token<MaxNodes, MaxBody, MaxName>(
            arena, tok.text, tok.pos, scanned.value().rest, depth, max_depth);
        if (!item.has_value()) {
            return item.error();
        }
        if (forms.size() >= MaxBody) {
            return foundation::parse_error{tok.pos,
                                           "body exceeds MaxBody capacity"};
        }
        forms.push_back(item.value().handle);
        cur = item.value().rest;
    }
}
// 0add15bd-dd26-45ae-8f4d-9be58158ff32 end

// 87192897-166d-4f7b-a24b-0b3542384b4d
/// Parses a `: name effect-comment? body-item* ;` colon definition. @p
/// colon_pos is the position of the already-consumed `:`; @p rest is the
/// cursor just past it.
///
/// The first `( ... )` comment immediately after the name is captured into
/// `declared_effect` if (and only if) it contains `--` (D9); a comment that
/// does not qualify is simply left for ordinary intertoken-space skipping to
/// consume later, same as any other comment.
template <int MaxNodes, int MaxBody, int MaxName>
constexpr auto parse_colon_def(program_arena<MaxNodes, MaxBody, MaxName> &arena,
                               foundation::source_pos colon_pos,
                               parser::cursor rest, int max_depth)
    -> item_outcome<MaxNodes, MaxBody, MaxName> {
    auto name_scan = scan_token_no_trailing_skip<MaxName>(rest);
    if (!name_scan.has_value()) {
        return foundation::parse_error{name_scan.error().where,
                                       "expected name after :"};
    }
    auto const &named = name_scan.value().value;
    auto const name_text = token_view(named.text);
    if (is_reserved_word(name_text)) {
        return foundation::parse_error{named.pos,
                                       "reserved word cannot be redefined"};
    }

    // named's cursor sits right after the name's own text, with nothing
    // skipped yet (scan_token_no_trailing_skip, unlike scan_word/scan_token,
    // never lets a trailing comment get silently consumed) -- so a plain
    // (comment-blind) whitespace skip here can still find an immediately
    // following "(" before anything else has a chance to eat it.
    auto cur = name_scan.value().rest;
    foundation::source_span effect{};
    auto const before_comment = parser::skip_intertoken_space(cur);
    if (!before_comment.empty() && before_comment.peek() == '(') {
        auto comment = scan_paren_comment(before_comment);
        if (comment.has_value()) {
            auto const span = comment.value().value;
            auto const len =
                static_cast<std::size_t>(span.last.offset - span.first.offset);
            auto const comment_text = before_comment.remaining().substr(0, len);
            if (comment_text.find("--") != std::string_view::npos) {
                effect = span;
            }
        }
    }

    auto body_part = parse_body_until<MaxNodes, MaxBody, MaxName>(
        arena, cur, 0, max_depth, [](std::string_view t) { return t == ";"; },
        "unterminated definition (no ;)");
    if (!body_part.has_value()) {
        return body_part.error();
    }
    auto const &bp = body_part.value();
    auto handle = foundation::make_arena_box(
        arena, syn_node<MaxNodes, MaxBody, MaxName>{
                   .value = syn_colon_def<MaxNodes, MaxBody, MaxName>{
                       .name = make_syn_name<MaxName>(name_text),
                       .declared_effect = effect,
                       .body = bp.forms,
                       .pos = colon_pos}});
    return item_result<MaxNodes, MaxBody, MaxName>{handle, bp.rest};
}

/// Parses a `VARIABLE name` declaration. @p pos is the position of the
/// already-consumed `VARIABLE` token; @p rest is the cursor just past it.
template <int MaxNodes, int MaxBody, int MaxName>
constexpr auto parse_variable(program_arena<MaxNodes, MaxBody, MaxName> &arena,
                              foundation::source_pos pos, parser::cursor rest)
    -> item_outcome<MaxNodes, MaxBody, MaxName> {
    auto name_scan = scan_token<MaxName>(rest);
    if (!name_scan.has_value()) {
        return foundation::parse_error{name_scan.error().where,
                                       "expected name after VARIABLE"};
    }
    auto const &named = name_scan.value().value;
    auto handle = foundation::make_arena_box(
        arena, syn_node<MaxNodes, MaxBody, MaxName>{
                   .value = syn_variable<MaxName>{
                       .name = make_syn_name<MaxName>(token_view(named.text)),
                       .pos = pos}});
    return item_result<MaxNodes, MaxBody, MaxName>{handle,
                                                   name_scan.value().rest};
}

/// Parses a `CONSTANT name` declaration. @p pos is the position of the
/// already-consumed `CONSTANT` token; @p rest is the cursor just past it.
///
/// The constant's value is whatever the preceding body form(s) left on the
/// stack (Forth-2012); this node records only the declaration itself, per
/// F6's @ref syn_constant.
template <int MaxNodes, int MaxBody, int MaxName>
constexpr auto parse_constant(program_arena<MaxNodes, MaxBody, MaxName> &arena,
                              foundation::source_pos pos, parser::cursor rest)
    -> item_outcome<MaxNodes, MaxBody, MaxName> {
    auto name_scan = scan_token<MaxName>(rest);
    if (!name_scan.has_value()) {
        return foundation::parse_error{name_scan.error().where,
                                       "expected name after CONSTANT"};
    }
    auto const &named = name_scan.value().value;
    auto handle = foundation::make_arena_box(
        arena, syn_node<MaxNodes, MaxBody, MaxName>{
                   .value = syn_constant<MaxName>{
                       .name = make_syn_name<MaxName>(token_view(named.text)),
                       .pos = pos}});
    return item_result<MaxNodes, MaxBody, MaxName>{handle,
                                                   name_scan.value().rest};
}

/// Parses a `CREATE name` declaration. @p pos is the position of the
/// already-consumed `CREATE` token; @p rest is the cursor just past it.
template <int MaxNodes, int MaxBody, int MaxName>
constexpr auto parse_create(program_arena<MaxNodes, MaxBody, MaxName> &arena,
                            foundation::source_pos pos, parser::cursor rest)
    -> item_outcome<MaxNodes, MaxBody, MaxName> {
    auto name_scan = scan_token<MaxName>(rest);
    if (!name_scan.has_value()) {
        return foundation::parse_error{name_scan.error().where,
                                       "expected name after CREATE"};
    }
    auto const &named = name_scan.value().value;
    auto handle = foundation::make_arena_box(
        arena, syn_node<MaxNodes, MaxBody, MaxName>{
                   .value = syn_create<MaxName>{
                       .name = make_syn_name<MaxName>(token_view(named.text)),
                       .pos = pos}});
    return item_result<MaxNodes, MaxBody, MaxName>{handle,
                                                   name_scan.value().rest};
}

/// Parses one top-level `item`: a colon definition, a `VARIABLE`/`CONSTANT`/
/// `CREATE` declaration, or a body-item (literal, tick, control structure, or
/// bare word).
template <int MaxNodes, int MaxBody, int MaxName>
constexpr auto parse_item(program_arena<MaxNodes, MaxBody, MaxName> &arena,
                          token_text<MaxName> const &tok,
                          foundation::source_pos tok_pos, parser::cursor rest,
                          int max_depth)
    -> item_outcome<MaxNodes, MaxBody, MaxName> {
    auto const text = token_view(tok);
    if (text == ":") {
        return parse_colon_def<MaxNodes, MaxBody, MaxName>(arena, tok_pos, rest,
                                                           max_depth);
    }
    if (text == "VARIABLE") {
        return parse_variable<MaxNodes, MaxBody, MaxName>(arena, tok_pos, rest);
    }
    if (text == "CONSTANT") {
        return parse_constant<MaxNodes, MaxBody, MaxName>(arena, tok_pos, rest);
    }
    if (text == "CREATE") {
        return parse_create<MaxNodes, MaxBody, MaxName>(arena, tok_pos, rest);
    }
    return parse_body_item_from_token<MaxNodes, MaxBody, MaxName>(
        arena, tok, tok_pos, rest, 0, max_depth);
}
// 87192897-166d-4f7b-a24b-0b3542384b4d end

// 3a09b40c-8b43-476a-b16d-a7b54249ca95
/// Parses a complete Forth program (`item* eof`) into a @ref syntax_tree.
///
/// @tparam MaxNodes Arena capacity shared by every handle in the tree.
/// @tparam MaxBody  Maximum number of forms in any one node's body, and of
///                  top-level items in the program.
/// @tparam MaxName  Maximum name/word length.
/// @tparam MaxDepth Maximum nesting depth of control structures (IF/BEGIN/DO
///                  nested inside one another); bounds the recursive-descent
///                  region's own recursion (DIV-0005).
/// @param  source   The complete program text.
/// @return The parsed @ref syntax_tree, or the first @ref
///         foundation::parse_error encountered, carrying its source
///         position.
template <int MaxNodes = 1024, int MaxBody = 64, int MaxName = 32,
          int MaxDepth = 32>
constexpr auto read_program(std::string_view source)
    -> foundation::result<syntax_tree<MaxNodes, MaxBody, MaxName>> {
    syntax_tree<MaxNodes, MaxBody, MaxName> tree{};
    parser::cursor cur{source};
    for (;;) {
        if (skip_forth_space(cur).empty()) {
            return tree;
        }
        auto scanned = scan_token<MaxName>(cur);
        if (!scanned.has_value()) {
            return scanned.error();
        }
        auto const &tok = scanned.value().value;
        auto item = parse_item<MaxNodes, MaxBody, MaxName>(
            tree.arena, tok.text, tok.pos, scanned.value().rest, MaxDepth);
        if (!item.has_value()) {
            return item.error();
        }
        if (tree.program.size() >= MaxBody) {
            return foundation::parse_error{
                tok.pos, "program exceeds top-level capacity"};
        }
        tree.program.push_back(item.value().handle);
        cur = item.value().rest;
    }
}
// 3a09b40c-8b43-476a-b16d-a7b54249ca95 end

} // namespace smd::forth::reader

#endif
