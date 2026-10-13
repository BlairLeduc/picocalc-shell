# Host tests (design.md §6). Every one builds src/core/ with the system
# compiler and no Pico SDK. One binary per area; exit 77 is a skip.

set(SHELL_HOST_TESTS
    test_displaytest
    test_keys
    test_shell
    test_skeleton
    test_term
)

foreach(t ${SHELL_HOST_TESTS})
    add_executable(${t} ${t}.c)
    target_link_libraries(${t} PRIVATE shell_core)
    target_include_directories(${t} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_options(${t} PRIVATE ${SHELL_WARNINGS})
    add_test(NAME ${t} COMMAND ${t})
    set_tests_properties(${t} PROPERTIES SKIP_RETURN_CODE 77)
endforeach()
