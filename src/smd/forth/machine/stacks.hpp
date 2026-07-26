// src/smd/forth/machine/stacks.hpp                                  -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#ifndef SRC_SMD_FORTH_MACHINE_STACKS_HPP
#define SRC_SMD_FORTH_MACHINE_STACKS_HPP

#include <smd/forth/foundation/parse_error.hpp>
#include <smd/forth/foundation/result.hpp>
#include <smd/forth/foundation/source_pos.hpp>
#include <smd/forth/foundation/static_vector.hpp>
#include <smd/forth/machine/cell.hpp>

#include <variant>

namespace smd::forth::machine {

/// A fixed-capacity stack of @ref cell values (D7).
///
/// Backs both @ref data_stack and @ref return_stack: the two only differ in
/// which register of @c forth_state holds them, not in behavior. Overflow
/// and underflow are diagnosed via @c foundation::result, never undefined
/// behavior.
///
/// The backing @c foundation::static_vector is filled to @p MaxDepth once,
/// at construction, so its own precondition-checked @c operator[] is always
/// exercised in-bounds; this type tracks the logical depth of the stack
/// separately and diagnoses overflow/underflow itself before ever touching
/// the backing storage out of range.
///
/// @tparam MaxDepth Maximum number of cells the stack may hold.
template <int MaxDepth>
class cell_stack {
  public:
    constexpr cell_stack();

    /// Pushes @p value onto the stack.
    /// Diagnoses stack overflow if the stack already holds @ref MaxDepth
    /// cells.
    constexpr auto push(cell value) -> status;

    /// Pops and returns the top cell.
    /// Diagnoses stack underflow if the stack is empty.
    constexpr auto pop() -> foundation::result<cell>;

    /// Returns the cell @p offset positions below the top, without
    /// removing it (@p offset == 0 names the top).
    /// Diagnoses stack underflow if the stack does not hold that many
    /// cells.
    [[nodiscard]] constexpr auto peek(int offset = 0) const
        -> foundation::result<cell>;

    /// Returns the current number of cells on the stack.
    [[nodiscard]] constexpr auto depth() const -> int;

    /// Shrinks the stack to exactly @p new_depth cells, discarding whatever
    /// was above that depth without inspecting it (step F31, D11): the
    /// primitive `THROW`'s own unwind needs, since a handler frame's own
    /// return-stack depth is recorded once, at `CATCH` time, and everything
    /// pushed above it since -- call frames, `DO`-loop frames, `>R` values,
    /// nested handler frames, in any mix -- must be discarded in one step
    /// rather than popped one shape-aware frame at a time (see `vm.hpp`'s own
    /// `perform_throw`). Diagnoses @p new_depth outside `[0, depth()]` rather
    /// than under- or over-shrinking (D7).
    constexpr auto truncate(int new_depth) -> status;

  private:
    foundation::static_vector<cell, MaxDepth> storage_{};
    int depth_{};
};

template <int MaxDepth>
constexpr cell_stack<MaxDepth>::cell_stack() {
    for (int i = 0; i < MaxDepth; ++i) {
        storage_.push_back(cell{0});
    }
}

template <int MaxDepth>
constexpr auto cell_stack<MaxDepth>::push(cell value) -> status {
    if (depth_ >= MaxDepth) {
        return foundation::parse_error{foundation::source_pos{},
                                       "stack overflow"};
    }
    storage_[depth_] = value;
    ++depth_;
    return std::monostate{};
}

template <int MaxDepth>
constexpr auto cell_stack<MaxDepth>::pop() -> foundation::result<cell> {
    if (depth_ <= 0) {
        return foundation::parse_error{foundation::source_pos{},
                                       "stack underflow"};
    }
    --depth_;
    return storage_[depth_];
}

template <int MaxDepth>
constexpr auto cell_stack<MaxDepth>::peek(int offset) const
    -> foundation::result<cell> {
    if (offset < 0 || offset >= depth_) {
        return foundation::parse_error{foundation::source_pos{},
                                       "stack underflow"};
    }
    return storage_[depth_ - 1 - offset];
}

template <int MaxDepth>
constexpr auto cell_stack<MaxDepth>::depth() const -> int {
    return depth_;
}

// 9c2f5a7d-4b8e-4d3a-9f6c-2e8b5a1d7c4f
template <int MaxDepth>
constexpr auto cell_stack<MaxDepth>::truncate(int new_depth) -> status {
    if (new_depth < 0 || new_depth > depth_) {
        return foundation::parse_error{foundation::source_pos{},
                                       "stack truncate: depth out of range"};
    }
    depth_ = new_depth;
    return std::monostate{};
}
// 9c2f5a7d-4b8e-4d3a-9f6c-2e8b5a1d7c4f end

/// The Forth data stack (D7).
template <int MaxDepth>
using data_stack = cell_stack<MaxDepth>;

/// The Forth return stack (D7).
template <int MaxRDepth>
using return_stack = cell_stack<MaxRDepth>;

} // namespace smd::forth::machine

#endif
