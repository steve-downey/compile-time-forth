# src/smd/forth/foundation/CMakeLists.txt                            -*-cmake-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

target_sources(
    compile-time-forth.forth
    PUBLIC
        FILE_SET forth_forth_headers
            TYPE HEADERS
            FILES
                alternative.hpp
                applicative.hpp
                arena_box.hpp
                functor.hpp
                parse_error.hpp
                result.hpp
                source_pos.hpp
                source_span.hpp
                static_vector.hpp
)

if(FORTH_ENABLE_TESTING)
    add_executable(foundation_test)

    target_sources(
        foundation_test
        PRIVATE
            alternative.test.cpp
            applicative.test.cpp
            arena_box.test.cpp
            functor.test.cpp
            parse_error.test.cpp
            result.test.cpp
            source_pos.test.cpp
            source_span.test.cpp
            static_vector.test.cpp
    )

    target_link_libraries(foundation_test compile-time-forth.forth)
    target_link_libraries(foundation_test Catch2::Catch2WithMain)

    include(Catch)
    catch_discover_tests(foundation_test)
endif()
