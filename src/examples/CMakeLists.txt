include(GNUInstallDirs)

add_executable(hello)

# Add example source file.
target_sources(hello PRIVATE hello.cpp)

# Link example with the library.
target_link_libraries(hello PRIVATE compile-time-forth.forth)

install(
    TARGETS hello
    COMPONENT compile-time-forth.forth.examples
    DESTINATION ${CMAKE_INSTALL_BINDIR}
    EXCLUDE_FROM_ALL
)

# Godbolt single-file extraction example (docs/forth-plan.md Step F15).
add_executable(godbolt_forth)

target_sources(godbolt_forth PRIVATE godbolt_forth.cpp)

target_link_libraries(godbolt_forth PRIVATE compile-time-forth.forth)

install(
    TARGETS godbolt_forth
    COMPONENT compile-time-forth.forth.examples
    DESTINATION ${CMAKE_INSTALL_BINDIR}
    EXCLUDE_FROM_ALL
)

# Step F34 (docs/forth-plan-2.md): the foreign function interface example --
# Forth calling C++ at compile time, a runtime-only foreign word guarded by
# std::is_constant_evaluated, and C++ calling back into a compiled session.
add_executable(ffi_gcd)

target_sources(ffi_gcd PRIVATE ffi_gcd.cpp)

target_link_libraries(ffi_gcd PRIVATE compile-time-forth.forth)

install(
    TARGETS ffi_gcd
    COMPONENT compile-time-forth.forth.examples
    DESTINATION ${CMAKE_INSTALL_BINDIR}
    EXCLUDE_FROM_ALL
)

if(FORTH_ENABLE_TESTING)
    add_test(NAME examples_godbolt_forth COMMAND godbolt_forth)
    set_tests_properties(
        examples_godbolt_forth
        PROPERTIES PASS_REGULAR_EXPRESSION "SQUARED\\(6\\) = 36"
    )

    add_test(NAME examples_ffi_gcd COMMAND ffi_gcd)
    set_tests_properties(
        examples_ffi_gcd
        PROPERTIES
            PASS_REGULAR_EXPRESSION
                "\\[trace\\] top of stack = 54.*computed at runtime"
    )
endif()
