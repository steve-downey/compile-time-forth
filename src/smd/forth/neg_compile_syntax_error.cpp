// src/smd/forth/neg_compile_syntax_error.cpp                        -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Step F15 negative-compile test (docs/forth-plan.md): proves a Forth
// syntax error is a HARD COMPILE ERROR through compiled_forth, not merely a
// runtime failure discovered later by .run(). This file is deliberately
// never part of the ordinary build graph -- it is not listed in this
// component's own CMakeLists.txt target_sources() call for anything that
// gets built normally. Instead, CMakeLists.txt drives it through a
// configure-time try_compile() that asserts it FAILS to compile, and fails
// the whole configure step (message(FATAL_ERROR ...)) if it ever succeeds.
//
// ": SQUARED DUP *" is missing its closing ';' -- read_program.hpp's own
// parse_colon_def/parse_body_until diagnoses this as
// "unterminated definition (no ;)". compiled_forth<...> propagates that
// foundation::parse_error through .value() at the read_program stage of its
// own pipeline (forth.hpp's detail::compile_program), and .value() on a
// held error throws (std::get on the wrong std::variant alternative) --
// which is not a core constant expression, so the constexpr initializer
// below cannot compile.
#include <smd/forth/forth.hpp>

namespace {
constexpr auto bad_program =
    smd::forth::compiled_forth<": SQUARED DUP *">.stack();
}

int main() { return static_cast<int>(bad_program.size()); }
