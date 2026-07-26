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

    # Negative-compile test (docs/forth-plan.md Step F15's own merge
    # criterion): test_neg_syntax_error.cpp is *expected* to fail to
    # compile, because its own compiled_forth<"..."> instantiation names a
    # syntactically invalid Forth program. EXCLUDE_FROM_ALL keeps it out of
    # `make compile`/`make all`'s own `all` target -- it is never built
    # except by the dedicated CTest test below, whose WILL_FAIL property
    # inverts the usual pass/fail sense: the test *passes* exactly when the
    # build *fails*. This is new infrastructure introduced ahead of its
    # originally-planned home (docs/forth-plan.md Step F21 names the same
    # `test_neg_*.cpp` pattern, imported from compile-time-scheme's
    # `test_neg_*.cpp` files per section 5's import table) because F15's
    # own merge criterion needs a working "this must not compile" mechanism
    # now; compile-time-scheme's own `test_neg_*.cpp` files are unwired
    # snippets (not valid standalone translation units, never attached to
    # any CMake target), so there was no working mechanism to import
    # directly -- see handoff.md's Step F15 section for the full record.
    add_executable(forth_test_neg_syntax_error EXCLUDE_FROM_ALL)
    target_sources(
        forth_test_neg_syntax_error
        PRIVATE test_neg_syntax_error.cpp
    )
    target_link_libraries(
        forth_test_neg_syntax_error
        PRIVATE compile-time-forth.forth
    )

    add_test(
        NAME forth.negative_compile_syntax_error
        COMMAND
            ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target
            forth_test_neg_syntax_error --config $<CONFIG>
    )
    set_tests_properties(
        forth.negative_compile_syntax_error
        PROPERTIES WILL_FAIL TRUE
    )
endif()

add_subdirectory(elaborator)
add_subdirectory(foundation)
add_subdirectory(interpreter)
add_subdirectory(machine)
add_subdirectory(parser)
add_subdirectory(reader)
add_subdirectory(sender)
