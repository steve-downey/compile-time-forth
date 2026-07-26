# src/smd/forth/conformance/CMakeLists.txt                          -*-CMake-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

target_sources(
    compile-time-forth.forth
    PRIVATE gforth_diff.cpp
    PUBLIC
        FILE_SET forth_conformance_headers
            TYPE HEADERS
            FILES gforth_diff.hpp ttester_corpus.hpp
)

# gforth found on PATH at configure time gates the differential-harness test
# below (D23's own runtime CI job). Per this project's own "silent skipping
# is the failure mode to avoid": if gforth is not found, this is a loud
# CMake STATUS message, not a quietly-vanished test.
find_program(FORTH_GFORTH_EXECUTABLE gforth)
if(FORTH_GFORTH_EXECUTABLE)
    message(
        STATUS
        "gforth found at ${FORTH_GFORTH_EXECUTABLE}: differential harness test enabled"
    )
else()
    message(
        STATUS
        "gforth NOT found on PATH: GforthDiffTest's own live-gforth battery will report itself skipped at test time (gforth_version() returns nullopt), not silently vanish"
    )
endif()

if(FORTH_ENABLE_TESTING)
    add_executable(conformance_test)

    target_sources(
        conformance_test
        PRIVATE
            core_suite_arithmetic.test.cpp
            core_suite_control.test.cpp
            core_suite_defining.test.cpp
            core_suite_memory.test.cpp
            core_suite_stack.test.cpp
            core_suite_strings.test.cpp
            gforth_diff.test.cpp
            ttester_corpus.test.cpp
    )

    target_link_libraries(conformance_test compile-time-forth.forth)
    target_link_libraries(conformance_test Catch2::Catch2WithMain)

    # D22 ("budgets are architecture"): each shard's own `compiled_forth<...>`
    # runs the adapted Hayes ttester (ttester_corpus.hpp) plus a dozen-odd
    # T{ ... -> ... }T assertions through this project's own text
    # interpreter, entirely inside one constexpr evaluation -- real
    # interpretation work, not merely deep recursion, so it costs far more
    # per source character than the smaller compiled_forth<Source> programs
    # elsewhere in this project. GCC's/Clang's own default constexpr op/step
    # ceiling (2^25 ops for GCC) exists to catch runaway template
    # metaprogramming, not to bound a real interpreter loop this project
    # already bounds with its own Fuel template parameter (interpreter.hpp's
    # own consume_interp_fuel) -- raising the compiler's own ceiling here is
    # widening the *compiler's* unrelated safety net to fit a real, already-
    # bounded workload, not disabling a check this project depends on.
    target_compile_options(
        conformance_test
        PRIVATE
            $<$<CXX_COMPILER_ID:GNU>:-fconstexpr-ops-limit=4294967295>
            $<$<CXX_COMPILER_ID:Clang>:-fconstexpr-steps=134217728>
    )

    include(Catch)
    catch_discover_tests(conformance_test)
endif()
