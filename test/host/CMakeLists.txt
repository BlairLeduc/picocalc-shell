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

# The port's renderer has no SDK header, so it is tested here on its own
# source, against a model of the panel in place of lcd.c.
add_executable(test_screen test_screen.c fake_lcd.c ${CMAKE_SOURCE_DIR}/src/port/screen.c)
target_link_libraries(test_screen PRIVATE shell_core)
target_include_directories(test_screen PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src/port)
target_compile_options(test_screen PRIVATE ${SHELL_WARNINGS})
add_test(NAME test_screen COMMAND test_screen)
set_tests_properties(test_screen PROPERTIES SKIP_RETURN_CODE 77)
