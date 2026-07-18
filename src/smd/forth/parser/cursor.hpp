// src/smd/forth/parser/cursor.hpp                                   -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Adapted by copy from compile-time-scheme (smd::smdscheme):
// src/smd/smdscheme/parser/cursor.hpp
#ifndef SRC_SMD_FORTH_PARSER_CURSOR_HPP
#define SRC_SMD_FORTH_PARSER_CURSOR_HPP

#include <smd/forth/foundation/source_pos.hpp>

#include <string_view>

namespace smd::forth::parser {

// 98d9b991-d8d6-4430-9571-489c17437a3d
/// An immutable view into the remaining input with an associated source
/// position.
///
/// All advancing operations return a new @c cursor rather than mutating this
/// one, so parsers can checkpoint and backtrack freely.
class cursor {
    std::string_view input_{};
    foundation::source_pos pos_{};

  public:
    /// Constructs a cursor at the beginning of @p input.
    constexpr explicit cursor(std::string_view input) : input_{input} {}

    /// Returns true when no input remains.
    constexpr auto empty() const -> bool { return input_.empty(); }

    /// Returns the next character without consuming it.
    /// @pre !empty()
    constexpr auto peek() const -> char { return input_.front(); }

    /// Returns a new cursor that has consumed the next character, updating
    /// the position (line/column/offset).
    /// @pre !empty()
    constexpr auto bump() const -> cursor {
        cursor next{*this};
        if (!input_.empty()) {
            char c = input_.front();
            next.input_.remove_prefix(1);
            ++next.pos_.offset;
            if (c == '\n') {
                ++next.pos_.line;
                next.pos_.column = 1;
            } else {
                ++next.pos_.column;
            }
        }
        return next;
    }

    /// Returns the current source position.
    constexpr auto position() const -> foundation::source_pos { return pos_; }

    /// Returns the unconsumed portion of the input as a string_view.
    constexpr auto remaining() const -> std::string_view { return input_; }
};
// 98d9b991-d8d6-4430-9571-489c17437a3d end

// 2c5cb3d8-d60d-43f3-aaa9-fde7f983aee3
/// Returns true if @p c is ASCII whitespace.
///
/// This is the only character predicate kept from the Scheme reader's
/// cursor: it is generic ASCII/position machinery, not a Scheme-specific
/// token predicate. The Scheme-flavored predicates (symbol-initial-char,
/// symbol-char, s-expression delimiter) are left behind per this project's
/// import plan (docs/forth-plan.md Step F4); Forth's own word/number/comment
/// predicates arrive in F5's `forth_chars.hpp`.
constexpr auto is_space(char c) -> bool {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

/// Advances @p cur past all leading whitespace, returning the updated cursor.
constexpr auto skip_intertoken_space(cursor cur) -> cursor {
    while (!cur.empty() && is_space(cur.peek())) {
        cur = cur.bump();
    }
    return cur;
}
// 2c5cb3d8-d60d-43f3-aaa9-fde7f983aee3 end

} // namespace smd::forth::parser

#endif
