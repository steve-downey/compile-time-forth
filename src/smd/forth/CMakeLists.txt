include(GNUInstallDirs)

target_sources(
    compile-time-forth.forth
    PUBLIC FILE_SET forth_forth_headers TYPE HEADERS FILES forth.hpp
    PRIVATE forth.cpp
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

add_subdirectory(foundation)
