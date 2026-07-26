// src/smd/forth/interpreter/input_source.hpp                        -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#ifndef SRC_SMD_FORTH_INTERPRETER_INPUT_SOURCE_HPP
#define SRC_SMD_FORTH_INTERPRETER_INPUT_SOURCE_HPP

#include <smd/forth/parser/cursor.hpp>

#include <string_view>
#include <type_traits>

namespace smd::forth::interpreter {

// Step F24 (docs/forth-plan-2.md): the Forth-2012 SOURCE/>IN pair (D13). The
// text interpreter's outer loop (interp.hpp) is what actually advances >IN;
// this type is only the storage -- a non-owning view over the program text
// (D3: no heap-backed types) plus a plain byte offset -- so >IN is ordinary,
// inspectable machine state rather than a cursor buried inside a scanner's
// local variables. That is what F29's user-defined parsing words will later
// need: a word like PARSE reads and writes >IN exactly the way the outer
// loop itself does, through this same interface, not through some
// interpreter-private mechanism a Forth-level word could never reach.

// 6a8b2e4f-1c9d-4a3e-8f5b-2d7c9e1a4b6f
/// The input source an interpreter loop scans: @ref text is the whole
/// program text (Forth's `SOURCE`), and @ref in is the byte offset of the
/// next character not yet consumed (Forth's `>IN`).
class input_source {
  public:
    constexpr input_source() = default;

    /// Constructs an input_source positioned at the start of @p text.
    constexpr explicit input_source(std::string_view text) : text_{text} {}

    /// `SOURCE`: the whole program text this input_source was constructed
    /// over. Never mutated after construction.
    [[nodiscard]] constexpr auto text() const -> std::string_view;

    /// `>IN`: the byte offset, from the start of @ref text, of the next
    /// character not yet consumed.
    [[nodiscard]] constexpr auto in() const -> int;

    /// Sets `>IN` directly. What the interpreter loop does after consuming
    /// a token, and what a parsing word (`WORD`, `PARSE`, F29) does to claim
    /// or release input for itself.
    constexpr auto set_in(int value) -> void;

    /// A @ref parser::cursor positioned at `>IN`, built by replaying
    /// @ref parser::cursor::bump from the start of @ref text. `>IN` is
    /// stored as a bare offset rather than a cursor (see this header's own
    /// top comment), so this replay is what keeps a cursor's line/column
    /// bookkeeping correct for diagnostics without duplicating that
    /// bookkeeping in a second place here.
    [[nodiscard]] constexpr auto cursor_at_in() const -> parser::cursor;

  private:
    std::string_view text_{};
    int in_{0};
};

constexpr auto input_source::text() const -> std::string_view { return text_; }

constexpr auto input_source::in() const -> int { return in_; }

constexpr auto input_source::set_in(int value) -> void { in_ = value; }

constexpr auto input_source::cursor_at_in() const -> parser::cursor {
    parser::cursor cur{text_};
    for (int i = 0; i < in_ && !cur.empty(); ++i) {
        cur = cur.bump();
    }
    return cur;
}
// 6a8b2e4f-1c9d-4a3e-8f5b-2d7c9e1a4b6f end

namespace detail {

// input_source must stay a literal, trivially destructible type: it is a
// field of interpreter::forth_state (interp.hpp), which in turn must stay
// usable as a constexpr local exactly like machine::forth_state already is
// (D3).
static_assert(std::is_trivially_destructible_v<input_source>);

} // namespace detail

// Merge criteria (static_assert, immediately-invoked-lambda pattern): >IN
// starts at 0, set_in/in round-trip, and cursor_at_in tracks line/column
// correctly across a consumed newline -- the replay this header's own doc
// comment promises, not just an offset into a flat buffer.

static_assert([] {
    input_source src{"dup swap"};
    return src.text() == "dup swap" && src.in() == 0;
}());

static_assert([] {
    input_source src{"dup swap"};
    src.set_in(4);
    return src.in() == 4 && src.cursor_at_in().remaining() == "swap";
}());

static_assert([] {
    input_source src{"dup\nswap"};
    src.set_in(4); // just past the newline, at 's' of "swap"
    auto cur = src.cursor_at_in();
    auto pos = cur.position();
    return cur.remaining() == "swap" && pos.offset == 4 && pos.line == 2 &&
           pos.column == 1;
}());

} // namespace smd::forth::interpreter

#endif
