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

add_executable(godbolt_forth)

# Add example source file.
target_sources(godbolt_forth PRIVATE godbolt_forth.cpp)

# Link example with the library.
target_link_libraries(godbolt_forth PRIVATE compile-time-forth.forth)

install(
    TARGETS godbolt_forth
    COMPONENT compile-time-forth.forth.examples
    DESTINATION ${CMAKE_INSTALL_BINDIR}
    EXCLUDE_FROM_ALL
)
