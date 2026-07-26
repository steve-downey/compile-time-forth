// src/smd/forth/conformance/gforth_diff.hpp                          -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#ifndef SRC_SMD_FORTH_CONFORMANCE_GFORTH_DIFF_HPP
#define SRC_SMD_FORTH_CONFORMANCE_GFORTH_DIFF_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Step F32 (docs/forth-plan-2.md), D14/D23: "the gforth differential harness
// (runtime CI job: same programs through the image and gforth, diff
// stacks/output)." This is deliberately *not* `constexpr` machinery: D3's own
// "heap-backed fix/Box types are barred from the compiled pipeline" is about
// the session image this project builds (code space, dictionary, data
// space), not an ordinary runtime test utility that shells out to a
// subprocess -- `std::vector`/`std::string` here are plain, ordinary runtime
// code, the same way `main()`/CMake/the build itself is.

namespace smd::forth::conformance {

/// The integer type gforth's own `.s` output cells parse into -- this
/// project's own @ref machine::cell is also a signed 64-bit integer (D7), so
/// both sides of a diff share one representation.
using gforth_cell = std::int64_t;

/// Parses one line of gforth's own `.s` output (`"<n> v1 v2 ... vn"`, bottom
/// cell first, exactly the format @ref machine::primitive::dot_s also
/// prints, `machine/forth_state.hpp`) into @c n cell values, bottom-to-top.
///
/// Returns @c std::nullopt if @p text does not begin with `<`, a decimal
/// digit sequence, and `>` (a malformed or unexpected line, rather than a
/// stack snapshot) -- this project's own "diagnosed error, never UB" (D7)
/// applies to this harness's own parsing too, not only to the compiled
/// pipeline it is testing.
auto parse_dot_s_output(std::string_view text)
    -> std::optional<std::vector<gforth_cell>>;

/// Runs `gforth --version` and returns its first line trimmed of the
/// trailing newline, or @c std::nullopt if gforth could not be invoked at
/// all. Call once per harness run and record the result: "a reported
/// divergence is attributable to a specific oracle build" (this step's own
/// brief) since gforth may change under this project mid-flight.
auto gforth_version() -> std::optional<std::string>;

/// One gforth run's own observable result: whatever ordinary text @p program
/// printed (`.`/`."`/`EMIT`/`CR`/`TYPE`, in gforth's own output), and the
/// final data stack @ref run_via_gforth's own trailing `.s` captured,
/// bottom-to-top -- the two things D23's own "diff stacks/output" names.
struct gforth_result {
    std::string output;
    std::vector<gforth_cell> stack;
};

/// Runs @p program through `gforth -e '<program> .s bye'` as a subprocess,
/// splitting its own captured stdout at the last `<...>` marker (`.s`'s own
/// output, always the last thing printed since it runs immediately before
/// `bye`) into @p program's own ordinary output and its final stack.
///
/// Returns @c std::nullopt if gforth could not be invoked, exited
/// abnormally, or its own output did not parse as ending in a `.s` line
/// (including the case where @p program itself diagnosed an error inside
/// gforth -- gforth writes diagnostics to stderr, not into the captured
/// stdout stream this function reads, so a diagnosed @p program simply
/// yields no parseable trailing `.s` line here rather than a misleading
/// empty stack).
///
/// @p program is wrapped in single quotes for the shell (`-e '...'`); any
/// single quote already inside @p program is escaped (`'\''`) so this
/// remains correct for a program that itself contains one, though none of
/// this project's own battery (`gforth_diff.test.cpp`) currently does.
auto run_via_gforth(std::string_view program) -> std::optional<gforth_result>;

} // namespace smd::forth::conformance

#endif
