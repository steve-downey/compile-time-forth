// src/smd/forth/machine/cell.hpp                                    -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#ifndef SRC_SMD_FORTH_MACHINE_CELL_HPP
#define SRC_SMD_FORTH_MACHINE_CELL_HPP

#include <smd/forth/foundation/result.hpp>

#include <cstdint>
#include <variant>

namespace smd::forth::machine {

/// The Forth machine's fundamental integer type (D7).
///
/// Every data-stack slot, return-stack slot, and data-space cell holds a
/// @c cell. A signed 64-bit width is generous for both compile-time and
/// runtime evaluation while still fitting the traditional single-cell
/// Forth model.
using cell = std::int64_t;

/// The Forth truth value pushed by comparison words: all bits set (D7).
inline constexpr cell flag_true = -1;

/// The Forth truth value pushed by comparison words: no bits set (D7).
inline constexpr cell flag_false = 0;

/// The result of an operation with no success payload.
///
/// @c foundation::result<T> has no @c T = @c void specialization (a
/// @c std::variant cannot hold @c void), so @c std::monostate stands in as
/// the empty success payload throughout the machine substrate wherever the
/// plan speaks of a conceptual @c result<void>.
using status = foundation::result<std::monostate>;

} // namespace smd::forth::machine

#endif
