include(GNUInstallDirs)

target_sources(
    compile-time-forth.forth
    PUBLIC FILE_SET forth_forth_headers TYPE HEADERS FILES forth.hpp
)

## Tests
if(FORTH_ENABLE_TESTING AND NOT Catch2_FOUND)
    find_package(Catch2 3 QUIET)
    if(Catch2_FOUND)
        include(Catch)
    else()
        message(
            WARNING
            "
No provider for Catch2. Unable to build tests.
Consider using CMAKE_PROJECT_TOP_LEVEL_INCLUDES=./cmake/use-fetch-content.cmake as documented in the README.md
"
        )
    endif()
endif()

if(FORTH_ENABLE_TESTING)
    add_executable(forth_test)

    target_sources(forth_test PRIVATE forth.test.cpp)

    target_link_libraries(forth_test compile-time-forth.forth)
    target_link_libraries(forth_test Catch2::Catch2WithMain)

    include(Catch)
    catch_discover_tests(forth_test)
endif()

add_subdirectory(elaborator)
add_subdirectory(foundation)
add_subdirectory(machine)
add_subdirectory(parser)
add_subdirectory(reader)
add_subdirectory(sender)

## Negative-compile test (Step F15, docs/forth-plan.md): a Forth program
## with a genuine syntax error must be a hard compile error through
## compiled_forth (see neg_compile_syntax_error.cpp), not merely a runtime
## failure. This project has no prior negative-compile-test harness (F21 is
## where a general one is planned); this is a narrowly scoped configure-time
## check for this one contract, using try_compile()'s own LINK_LIBRARIES
## target support so the probe compile picks up compile-time-forth.forth's
## own include directories and required C++ standard automatically. Runs
## every time CMake reconfigures (i.e. whenever this file or
## neg_compile_syntax_error.cpp changes), which `make compile` triggers
## through the generated build system's own rerun-cmake rule.
if(FORTH_ENABLE_TESTING)
    try_compile(
        FORTH_NEG_COMPILE_SYNTAX_ERROR_SUCCEEDED
        SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/neg_compile_syntax_error.cpp
        LINK_LIBRARIES compile-time-forth.forth
        CXX_STANDARD 26
        CXX_STANDARD_REQUIRED ON
        OUTPUT_VARIABLE FORTH_NEG_COMPILE_SYNTAX_ERROR_OUTPUT
    )
    if(FORTH_NEG_COMPILE_SYNTAX_ERROR_SUCCEEDED)
        message(
            FATAL_ERROR
            "Step F15 negative-compile test (neg_compile_syntax_error.cpp) "
            "was expected to FAIL to compile -- a Forth syntax error must "
            "be a hard compile error through compiled_forth -- but it "
            "compiled successfully. Compiler output:\n"
            "${FORTH_NEG_COMPILE_SYNTAX_ERROR_OUTPUT}"
        )
    endif()
endif()
