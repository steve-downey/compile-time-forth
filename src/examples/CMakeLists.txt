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

if(FORTH_ENABLE_TESTING)
    add_test(NAME examples_godbolt_forth COMMAND godbolt_forth)
    set_tests_properties(
        examples_godbolt_forth
        PROPERTIES PASS_REGULAR_EXPRESSION "SQUARED\\(6\\) = 36"
    )
endif()
