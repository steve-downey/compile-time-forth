// src/smd/forth/test_neg_capacity_overflow.cpp                      -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Negative-compile test (step F36, docs/forth-plan-2.md: "negative-compile
// tests for ... capacity overflow ... extending the F15/F26
// infrastructure"). This translation unit is *expected* to fail to
// compile; it is never part of the `all` target (see the EXCLUDE_FROM_ALL
// `forth_test_neg_capacity_overflow` target in src/smd/forth/CMakeLists.txt,
// which follows test_neg_syntax_error.cpp's own pattern exactly) and is
// only ever built by its own dedicated CTest test, whose passing condition
// is that the build fails (CMake's WILL_FAIL test property).
//
// Every capacity smd::forth::compiled_forth carries is a template
// parameter with a project-standard default (D2); none of them grows past
// the caller's own declared ceiling. "1 2 3 4 5" pushes five cells onto the
// data stack while the top level runs it, immediately (there is no `:`
// ... `;` here, so nothing is compiled -- every number runs the moment the
// text interpreter meets it, D13). @p BuildDepth (compiled_forth's own
// eighth template parameter, the transient build-time forth_state's data
// stack capacity -- distinct from @p MaxStack, the *returned session's own*
// stack-snapshot capacity, seventh) is pinned to 4 below, one short of what
// five pushes need: the fifth push finds machine::cell_stack<4> already
// full and machine::cell_stack::push diagnoses "stack overflow" rather
// than writing past capacity (machine::stacks.hpp; every stack/data-space
// capacity in this project diagnoses exhaustion this way, never silently
// overflows -- see also machine::emit's own "compiled program exceeds
// MaxCode capacity" and machine::data_space's own "data space exhausted"
// for the code-space and data-space analogues this one program does not
// happen to exercise). compiled_forth<Source>'s own constexpr initializer
// then calls .value() on that failed foundation::result, which is not a
// core constant expression, so this translation unit fails to compile
// right here -- the same "a malformed program is a hard compile error"
// contract test_neg_syntax_error.cpp and test_neg_effect_mismatch.cpp
// exercise, extended here to a capacity, rather than a syntax or
// effect-lint, failure.
#include <smd/forth/forth.hpp>

namespace {
[[maybe_unused]] constexpr auto bad =
    smd::forth::compiled_forth<"1 2 3 4 5", 4096, 256, 1024, 4096, 32, 64, 4>;
} // namespace

auto main() -> int { return 0; }
