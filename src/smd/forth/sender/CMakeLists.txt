# src/smd/forth/sender/CMakeLists.txt                               -*-CMake-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

target_sources(
    compile-time-forth.forth
    PUBLIC
        FILE_SET forth_forth_headers
            TYPE HEADERS
            FILES lower.hpp run_and_compare.hpp vocab.hpp
)

## Tests
if(FORTH_ENABLE_TESTING)
    add_executable(sender_test)

    # Step F33 (docs/forth-plan-2.md), D22 ("budgets are architecture"),
    # DIV-0026: each lower_*.test.cpp shard is its own translation unit,
    # deliberately kept to one or two corpus programs sharing the same
    # forth_state/capacity combination -- instantiating this component's own
    # sender machinery costs several hundred MB of peak compiler memory per
    # distinct combination even after DIV-0026's own type-erasure fix (an
    # earlier, receiver-templated draft was unboundedly worse: a single
    # translation unit exercising the full battery reached a 42 GB OOM
    # kill). See docs/compiler_architecture.org's own Phase 16 section for
    # the measured per-shard wall-clock/peak-RSS table this baseline
    # establishes.
    target_sources(
        sender_test
        PRIVATE
            lower.test.cpp
            lower_catch_fallback.test.cpp
            lower_catch_native.test.cpp
            lower_defining.test.cpp
            lower_exit.test.cpp
            lower_fuel.test.cpp
            lower_if.test.cpp
            lower_leave.test.cpp
            lower_loops1.test.cpp
            lower_loops2.test.cpp
            vocab.test.cpp
    )

    target_link_libraries(sender_test compile-time-forth.forth)
    target_link_libraries(sender_test Catch2::Catch2WithMain)

    include(Catch)
    catch_discover_tests(sender_test)
endif()
