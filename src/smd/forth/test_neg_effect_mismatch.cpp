// src/smd/forth/test_neg_effect_mismatch.cpp                        -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Negative-compile test (step F36, docs/forth-plan-2.md: "negative-compile
// tests for ... declared-effect mismatch ... extending the F15/F26
// infrastructure"). This translation unit is *expected* to fail to
// compile; it is never part of the `all` target (see the EXCLUDE_FROM_ALL
// `forth_test_neg_effect_mismatch` target in src/smd/forth/CMakeLists.txt,
// which follows test_neg_syntax_error.cpp's own pattern exactly) and is
// only ever built by its own dedicated CTest test, whose passing condition
// is that the build fails (CMake's WILL_FAIL test property).
//
// ": BADDECL ( n -- n n ) DUP DROP ;" declares a net effect of +1 (one cell
// in, two out) but DUP DROP's own computed effect is +0 (one cell in, one
// out, net zero) -- interpreter::effect_lint (step F30, D20) checks a
// definition's declared comment against its own computed effect at ';' and
// promotes the disagreement to a hard gate exactly when a declared effect
// is present (interp.test.cpp's own DeclaredEffectMismatchDiagnosed is the
// runtime-checkable twin of this compile-time failure, and names the exact
// diagnostic text: "declared stack effect does not match computed stack
// effect"). compiled_forth<Source>'s own constexpr initializer then calls
// .value() on that failed foundation::result, which is not a core constant
// expression, so this translation unit fails to compile right here --
// exactly forth.hpp's own compiled_forth "a malformed program is a hard
// compile error" contract, extended from a syntax failure (D13's text
// interpreter, test_neg_syntax_error.cpp) to an effect-lint failure (D20).
#include <smd/forth/forth.hpp>

namespace {
[[maybe_unused]] constexpr auto bad =
    smd::forth::compiled_forth<": BADDECL ( n -- n n ) DUP DROP ;">;
} // namespace

auto main() -> int { return 0; }
