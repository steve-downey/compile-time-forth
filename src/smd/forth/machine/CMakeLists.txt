# src/smd/forth/machine/CMakeLists.txt                              -*-CMake-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

target_sources(
    compile-time-forth.forth
    PUBLIC
        FILE_SET forth_forth_headers
            TYPE HEADERS
            FILES
                cell.hpp
                codegen.hpp
                data_space.hpp
                dictionary.hpp
                emit.hpp
                eval_direct.hpp
                forth_state.hpp
                instruction.hpp
                stacks.hpp
                vm.hpp
)

if(FORTH_ENABLE_TESTING)
    add_executable(machine_test)

    target_sources(
        machine_test
        PRIVATE
            cell.test.cpp
            codegen.test.cpp
            data_space.test.cpp
            dictionary.test.cpp
            emit.test.cpp
            eval_direct.test.cpp
            forth_state.test.cpp
            instruction.test.cpp
            stacks.test.cpp
            vm.test.cpp
    )

    target_link_libraries(machine_test compile-time-forth.forth)
    target_link_libraries(machine_test Catch2::Catch2WithMain)

    include(Catch)
    catch_discover_tests(machine_test)
endif()
