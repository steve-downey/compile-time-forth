// src/smd/forth/test_neg_syntax_error.cpp                           -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Negative-compile test (docs/forth-plan.md Step F15's own merge
// criterion: "a negative-compile test proves a syntax error fails
// compilation"). This translation unit is *expected* to fail to compile;
// it is never part of the `all` target (see the EXCLUDE_FROM_ALL
// `forth_test_neg_syntax_error` target in src/smd/forth/CMakeLists.txt)
// and is only ever built by its own dedicated CTest test, whose passing
// condition is that the build fails (CMake's WILL_FAIL test property).
//
// ": UNCLOSED DUP *" never closes its colon definition with a terminating
// ";", so smd::forth::interpreter::interpret diagnoses it as "unterminated
// colon definition" once its source runs out while STATE is still nonzero
// (interp.hpp's own interpret loop; step F26 retargeted this test onto the
// text interpreter after the R1 reader::read_program that used to diagnose
// this same shape of malformed program was deleted -- see DIV-0014).
// compiled_forth<Source>'s own constexpr initializer then calls .value() on
// that failed foundation::result, which is not a core constant expression
// (std::get on the wrong std::variant alternative throws), so this
// translation unit fails to compile right here -- exactly the "hard compile
// error" contract forth.hpp's own compiled_forth documents.
#include <smd/forth/forth.hpp>

namespace {
[[maybe_unused]] constexpr auto bad =
    smd::forth::compiled_forth<": UNCLOSED DUP *">;
} // namespace

auto main() -> int { return 0; }
