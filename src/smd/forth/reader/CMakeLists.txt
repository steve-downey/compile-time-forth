# src/smd/forth/reader/CMakeLists.txt                                 -*-cmake-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

target_sources(
    compile-time-forth.forth
    PUBLIC FILE_SET forth_forth_headers TYPE HEADERS FILES syntax_tree.hpp
)

if(FORTH_ENABLE_TESTING)
    add_executable(reader_test)

    target_sources(reader_test PRIVATE syntax_tree.test.cpp)

    target_link_libraries(reader_test compile-time-forth.forth)
    target_link_libraries(reader_test Catch2::Catch2WithMain)

    include(Catch)
    catch_discover_tests(reader_test)
endif()
