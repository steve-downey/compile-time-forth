// testinstall/test.cpp                                               -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <iostream>
#include <smd/forth/forth.hpp>

int main() {
    std::cout << "forth: |" << forth::forth() << '|' << '\n';
    return 0;
}
