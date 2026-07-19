// src/smd/forth/machine/data_space.hpp                               -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#ifndef SRC_SMD_FORTH_MACHINE_DATA_SPACE_HPP
#define SRC_SMD_FORTH_MACHINE_DATA_SPACE_HPP

#include <smd/forth/foundation/parse_error.hpp>
#include <smd/forth/foundation/result.hpp>
#include <smd/forth/foundation/source_pos.hpp>
#include <smd/forth/foundation/static_vector.hpp>
#include <smd/forth/machine/cell.hpp>

#include <variant>

namespace smd::forth::machine {

/// A typed index into a @ref data_space (D10).
///
/// Distinct from @ref cell so an address cannot be mistaken for an
/// ordinary arithmetic value at the type level, while still being
/// explicitly convertible to and from @ref cell -- Forth code needs to
/// carry an address on the data stack (`VARIABLE X` leaves an address that
/// a later `X @` fetches through), and the data stack only ever holds
/// @c cell.
class addr {
  public:
    constexpr addr() = default;

    /// Constructs an address from a raw cell index. Explicit: an address
    /// is never implicitly manufactured from an arbitrary cell.
    constexpr explicit addr(cell index);

    /// Converts back to the raw cell index. Explicit for the same reason.
    [[nodiscard]] constexpr explicit operator cell() const;

    // HIDDEN FRIEND
    friend constexpr auto operator==(addr const &, addr const &)
        -> bool = default;

  private:
    cell index_{};
};

constexpr addr::addr(cell index) : index_(index) {}

constexpr addr::operator cell() const { return index_; }

/// A bump-allocated arena of @ref cell values (D10).
///
/// Backs `VARIABLE`, `CONSTANT`, and `CREATE`/`ALLOT` (F16): @ref allot
/// reserves a run of cells and returns the @ref addr of the first one;
/// @ref fetch and @ref store read/write a single cell at an @ref addr.
/// Every operation is bounds-checked against the region actually allotted
/// so far (the arena's "HERE" boundary) -- fetching or storing through an
/// @ref addr that @ref allot never handed out is a diagnosed error, never
/// undefined behavior.
///
/// The backing @c foundation::static_vector is filled to @p MaxData once,
/// at construction (mirroring @ref cell_stack in `stacks.hpp`), so its own
/// precondition-checked @c operator[] is always exercised in-bounds; this
/// type tracks the allotted high-water mark itself.
///
/// @tparam MaxData Maximum number of cells the arena may hold.
template <int MaxData = 1024>
class data_space {
  public:
    constexpr data_space();

    /// Reserves @p count contiguous, zero-initialized cells and returns
    /// the address of the first.
    /// Diagnoses arena exhaustion if fewer than @p count cells remain, or
    /// if @p count is negative.
    constexpr auto allot(int count) -> foundation::result<addr>;

    /// Reads the cell at @p address.
    /// Diagnoses an out-of-bounds address: negative, or not yet allotted.
    [[nodiscard]] constexpr auto fetch(addr address) const
        -> foundation::result<cell>;

    /// Writes @p value to the cell at @p address.
    /// Diagnoses an out-of-bounds address: negative, or not yet allotted.
    constexpr auto store(addr address, cell value) -> status;

    /// The number of cells allotted so far.
    [[nodiscard]] constexpr auto size() const -> int;

    /// The address one past the last allotted cell -- Forth's `HERE`.
    [[nodiscard]] constexpr auto here() const -> addr;

  private:
    foundation::static_vector<cell, MaxData> storage_{};
    int high_{};
};

template <int MaxData>
constexpr data_space<MaxData>::data_space() {
    for (int i = 0; i < MaxData; ++i) {
        storage_.push_back(cell{0});
    }
}

template <int MaxData>
constexpr auto data_space<MaxData>::allot(int count)
    -> foundation::result<addr> {
    if (count < 0 || count > MaxData - high_) {
        return foundation::parse_error{foundation::source_pos{},
                                       "data space exhausted"};
    }
    auto const base = high_;
    high_ += count;
    return addr{static_cast<cell>(base)};
}

template <int MaxData>
constexpr auto data_space<MaxData>::fetch(addr address) const
    -> foundation::result<cell> {
    auto const index = static_cast<cell>(address);
    if (index < 0 || index >= static_cast<cell>(high_)) {
        return foundation::parse_error{foundation::source_pos{},
                                       "data space address out of bounds"};
    }
    return storage_[static_cast<int>(index)];
}

template <int MaxData>
constexpr auto data_space<MaxData>::store(addr address, cell value)
    -> status {
    auto const index = static_cast<cell>(address);
    if (index < 0 || index >= static_cast<cell>(high_)) {
        return foundation::parse_error{foundation::source_pos{},
                                       "data space address out of bounds"};
    }
    storage_[static_cast<int>(index)] = value;
    return std::monostate{};
}

template <int MaxData>
constexpr auto data_space<MaxData>::size() const -> int {
    return high_;
}

template <int MaxData>
constexpr auto data_space<MaxData>::here() const -> addr {
    return addr{static_cast<cell>(high_)};
}

} // namespace smd::forth::machine

#endif
