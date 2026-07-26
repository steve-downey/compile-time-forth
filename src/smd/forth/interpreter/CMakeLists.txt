# src/smd/forth/interpreter/CMakeLists.txt                          -*-CMake-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

target_sources(
    compile-time-forth.forth
    PUBLIC
        FILE_SET forth_forth_headers
            TYPE HEADERS
            FILES input_source.hpp interp.hpp
)

if(FORTH_ENABLE_TESTING)
    add_executable(interpreter_test)

    target_sources(
        interpreter_test
        PRIVATE input_source.test.cpp interp.test.cpp
    )

    target_link_libraries(interpreter_test compile-time-forth.forth)
    target_link_libraries(interpreter_test Catch2::Catch2WithMain)

    include(Catch)
    catch_discover_tests(interpreter_test)
endif()
