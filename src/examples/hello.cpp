// src/examples/hello.cpp                                            -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <smd/forth/forth.hpp>

#include <print>
#include <string_view>

namespace forth = smd::forth;

// 0f21c895-211f-4b4a-a830-bde8186d60bc
/// A small Forth program run entirely through the Step F15 public API:
/// `compiled_forth` compiles it once, at namespace-scope `constexpr`
/// initialization time; `.output()` runs it and returns what `.` printed.
constexpr auto program =
    forth::compiled_forth<": SQUARED DUP * ;  4 SQUARED  .">;

auto main() -> int {
    auto const out = program.output();
    std::println(
        "Hello, Forth! SQUARED(4) prints: {}",
        std::string_view{out.begin(), static_cast<std::size_t>(out.size())});
}
// 0f21c895-211f-4b4a-a830-bde8186d60bc end
