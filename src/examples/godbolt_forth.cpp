// src/examples/godbolt_forth.cpp                                     -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// A single self-contained translation unit demonstrating the public one-shot
// API (docs/forth-plan.md Step F15): paste this file into Godbolt's
// "compiler explorer" (a plain single-file session, not a CMake tree) after
// the project's own header search path is available (this project's
// self-contained public headers under src/, matching the sibling
// compile-time-scheme project's scripts/deploy_godbolt_tree.py convention:
// the example file plus every project header, packaged as a Godbolt tree
// session), then compile with -std=c++26. Everything below runs at compile
// time; main() only formats the already-computed result for display.

#include <smd/forth/forth.hpp>

#include <print>
#include <string_view>

// abc0ca9b-ab0d-4228-8d40-4e22ee7b61de
namespace forth = smd::forth;

// FACTORIAL, computed entirely at compile time via RECURSE (F11), then run
// for 6! -- exercises a colon word calling itself, not just straight-line
// arithmetic.
constexpr auto program = forth::compiled_forth<
    ": FACTORIAL DUP 1 > IF DUP 1- RECURSE * THEN ;  6 FACTORIAL">;

// The stack result is itself available at compile time: no runtime
// computation is required to know 6! == 720.
static_assert(program.stack().size() == 1);
static_assert(program.stack()[0] == 720);

int main() {
    auto const &top = program.stack();
    std::println("6! = {}", top[0]);
}
// abc0ca9b-ab0d-4228-8d40-4e22ee7b61de end
