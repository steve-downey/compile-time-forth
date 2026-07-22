// src/examples/godbolt_forth.cpp                                    -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// A single-file Godbolt extraction of smd::forth's public one-shot API
// (docs/forth-plan.md Step F15). This project's own house style requires
// every public header to be self-contained, and Step F15 made
// smd::forth::forth.hpp header-only (there is no more forth.cpp
// translation unit): pasting this file's own contents into a Godbolt
// "tree" alongside the handful of headers under src/smd/forth/ it
// transitively includes is therefore enough to reproduce this example --
// no other project source file is needed. Compare
// ~/src/compile-time-scheme/main/src/examples/godbolt_arithmetic.cpp,
// which this file follows as a structural pattern (one `compiled_*`
// namespace-scope constant, one `main` that inspects its result).
#include <smd/forth/forth.hpp>

#include <print>

namespace forth = smd::forth;

// 11e91ede-cd8d-4f11-ab27-3a7bf6d3f9ab
/// `SQUARED` doubled: compiled once, at compile time, then run twice at
/// ordinary runtime below to show both convenience accessors.
constexpr auto program = forth::compiled_forth<": SQUARED DUP * ;  6 SQUARED">;

auto main() -> int {
    auto const s = program.stack();
    if (s.size() != 1) {
        std::println("unexpected stack depth: {}", s.size());
        return 1;
    }
    std::println("SQUARED(6) = {}", s[0]);
    return s[0] == 36 ? 0 : 1;
}
// 11e91ede-cd8d-4f11-ab27-3a7bf6d3f9ab end
