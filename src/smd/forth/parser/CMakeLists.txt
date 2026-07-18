# src/smd/forth/parser/CMakeLists.txt                                -*-cmake-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

target_sources(
    compile-time-forth.forth
    PUBLIC
        FILE_SET forth_forth_headers
            TYPE HEADERS
            FILES alt.hpp cursor.hpp parser.hpp parser_ops.hpp
)

if(FORTH_ENABLE_TESTING)
    add_executable(parser_test)

    target_sources(
        parser_test
        PRIVATE alt.test.cpp cursor.test.cpp parser.test.cpp parser_ops.test.cpp
    )

    target_link_libraries(parser_test compile-time-forth.forth)
    target_link_libraries(parser_test Catch2::Catch2WithMain)

    include(Catch)
    catch_discover_tests(parser_test)
endif()
