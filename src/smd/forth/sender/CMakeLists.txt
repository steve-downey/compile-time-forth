# src/smd/forth/sender/CMakeLists.txt                               -*-CMake-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

target_sources(
    compile-time-forth.forth
    PUBLIC FILE_SET forth_forth_headers TYPE HEADERS FILES vocab.hpp
)

## Tests
if(FORTH_ENABLE_TESTING)
    add_executable(sender_test)

    target_sources(sender_test PRIVATE vocab.test.cpp)

    target_link_libraries(sender_test compile-time-forth.forth)
    target_link_libraries(sender_test Catch2::Catch2WithMain)

    include(Catch)
    catch_discover_tests(sender_test)
endif()
