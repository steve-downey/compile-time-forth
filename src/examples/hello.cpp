// src/examples/hello.cpp                                             -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <smd/forth/forth.hpp>

#include <print>
#include <string_view>

// e8803c30-caf2-4383-a9af-1b1019234993
int main() {
    // Compiled entirely at compile time (docs/forth-plan.md Step F15):
    // SQUARED squares its argument, then the top-level program prints the
    // result of 6 SQUARED followed by a newline.
    constexpr auto program =
        smd::forth::compiled_forth<": SQUARED DUP * ;  6 SQUARED . CR">;

    auto result = program.run();
    if (!result.has_value()) {
        std::println("Hello, Forth! program failed: {}",
                     result.error().message);
        return 1;
    }

    auto const &out = result.value().output();
    std::string_view text{out.begin(), out.end()};
    std::println("Hello, compile-time Forth!");
    std::print("{}", text);
}
// e8803c30-caf2-4383-a9af-1b1019234993 end
