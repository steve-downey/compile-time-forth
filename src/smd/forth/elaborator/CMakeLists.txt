# src/smd/forth/elaborator/CMakeLists.txt                            -*-CMake-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

target_sources(
    compile-time-forth.forth
    PUBLIC
        FILE_SET forth_forth_headers
            TYPE HEADERS
            FILES elaborate.hpp elaborated_core.hpp stack_effect.hpp
)

if(FORTH_ENABLE_TESTING)
    add_executable(elaborator_test)

    target_sources(
        elaborator_test
        PRIVATE
            elaborate.test.cpp
            elaborated_core.test.cpp
            stack_effect.test.cpp
    )

    target_link_libraries(elaborator_test compile-time-forth.forth)
    target_link_libraries(elaborator_test Catch2::Catch2WithMain)

    include(Catch)
    catch_discover_tests(elaborator_test)
endif()
