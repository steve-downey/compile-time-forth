# example/CMakeLists.txt                                            -*-cmake-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

cmake_minimum_required(VERSION 4.0..4.2)

project(forth VERSION 0.0.0 LANGUAGES CXX)

# Instrumentation query
# find_package(Python COMPONENTS Interpreter)
cmake_instrumentation(
  API_VERSION 1
  DATA_VERSION 1
  HOOKS postGenerate preBuild postBuild preCMakeBuild postCMakeBuild postCMakeInstall postCTest
  OPTIONS trace
  CALLBACK ${CMAKE_SOURCE_DIR}/scripts/instrument.sh
)

# Includes
include(CTest)
include(FetchContent)

option(
    FORTH_ENABLE_TESTING
    "Enable building tests and test infrastructure"
    ${PROJECT_IS_TOP_LEVEL}
)

set(CMAKE_VERIFY_INTERFACE_HEADER_SETS ON)

# Vendor Beman Execution (git submodule at vendor/execution); no
# FetchContent, no vcpkg, no install-time discovery. Header-mode only: this
# project consumes it through the traditional <beman/execution26/...>
# headers, not C++ modules.
set(BEMAN_USE_MODULES OFF CACHE BOOL "Disable modules" FORCE)
add_subdirectory(vendor/execution)

# Create the library target and named header set for beman.optional
add_library(compile-time-forth.forth STATIC "")

# As of Step F15, every component under src/smd/forth/ is header-only (no
# component owns a .cpp of its own -- forth.cpp, the library's last
# remaining compiled source, was the F0-era forth::forth() placeholder,
# deleted outright per docs/forth-plan.md Step F15). A STATIC library
# target with zero compiled sources cannot infer its own linker language
# from its (nonexistent) sources; state it explicitly rather than
# reintroducing a dummy .cpp or reclassifying the target as INTERFACE
# (AGENTS.md's own CMake rules reserve INTERFACE libraries for "truly
# header-only code," but this target's whole point is to remain a stable,
# concrete home for a real compiled source the moment one exists again,
# e.g. F18's sender/receiver backend or F19's foreign-function support).
set_target_properties(compile-time-forth.forth PROPERTIES LINKER_LANGUAGE CXX)

target_sources(
    compile-time-forth.forth
    PUBLIC FILE_SET forth_forth_headers TYPE HEADERS BASE_DIRS src
)

target_link_libraries(compile-time-forth.forth PUBLIC beman::execution)

if(FORTH_ENABLE_TESTING)
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

add_subdirectory(src)

include(infra/cmake/beman-install-library.cmake)
beman_install_library(compile-time-forth.forth TARGETS compile-time-forth.forth NAMESPACE smd::)

# Coverage
configure_file("cmake/gcovr.cfg.in" gcovr.cfg @ONLY)

add_custom_target(
    process_coverage
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running gcovr to process coverage results"
    COMMAND mkdir -p coverage
    COMMAND gcovr --config gcovr.cfg .
)
