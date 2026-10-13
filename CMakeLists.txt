# shell — one source tree, two targets (design.md §6):
#   - BOOT2350.UF2, the shell as a no_flash RAM program (default)
#   - a host build of src/core/ and its tests with the system compiler and
#     no Pico SDK, behind -DPICO_SHELL_HOST=ON, run under CTest.
#
# The core building clean under -Wall -Wextra -Werror on both toolchains
# is what keeps SDK dependencies out of it (EL §2.1).

cmake_minimum_required(VERSION 3.18)

option(PICO_SHELL_HOST "Build the portable core and its host tests, with no Pico SDK" OFF)

# Every fixed capacity lives in src/core/config.h; nothing here duplicates one.
set(SHELL_CORE_SOURCES
    src/core/displaytest.c
    src/core/font-5x10.c
    src/core/font-8x10.c
    src/core/keys.c
    src/core/out.c
    src/core/shell.c
    src/core/term.c
)

set(SHELL_WARNINGS -Wall -Wextra -Werror)

function(shell_core_library)
    add_library(shell_core STATIC ${SHELL_CORE_SOURCES})
    target_include_directories(shell_core PUBLIC src/core)
    target_compile_options(shell_core PRIVATE ${SHELL_WARNINGS})
endfunction()

if (PICO_SHELL_HOST)
    project(shell_host C)
    set(CMAKE_C_STANDARD 11)
    set(CMAKE_C_STANDARD_REQUIRED ON)
    set(CMAKE_C_EXTENSIONS OFF)

    shell_core_library()

    enable_testing()
    add_subdirectory(test/host)
else()
    # RP2350 only (design.md §1): one pico2 image for every RP2350 board.
    if (NOT DEFINED PICO_BOARD)
        set(PICO_BOARD pico2)
    endif()
    if (NOT DEFINED PICO_PLATFORM AND PICO_BOARD STREQUAL "pico2")
        set(PICO_PLATFORM rp2350)
    endif()

    include(cmake/pico_sdk_import.cmake)

    project(shell C CXX ASM)
    set(CMAKE_C_STANDARD 11)
    set(CMAKE_CXX_STANDARD 17)

    pico_sdk_init()

    if (NOT PICO_RP2350)
        message(FATAL_ERROR "shell: RP2350 boards only (design.md §1), not ${PICO_PLATFORM}")
    endif()

    shell_core_library()

    # The version for the banner, git describe --always --dirty, taken at
    # build time; the header only changes when the text does.
    set(SHELL_GENERATED ${CMAKE_BINARY_DIR}/generated)
    add_custom_target(shell_version ALL
        COMMAND ${CMAKE_COMMAND} -DSRC=${CMAKE_CURRENT_SOURCE_DIR}
                -DOUT=${SHELL_GENERATED}/shell_version.h
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.cmake
        BYPRODUCTS ${SHELL_GENERATED}/shell_version.h
        COMMENT "shell: version from git describe"
        VERBATIM)

    # PICO_SHELL_UART=OFF is the build that ships: nothing on UART1, so GP4
    # and GP5 are free. ON, the default, is the development build: the log,
    # the screen mirrored and keys typed over UART1 (design.md §6, EL §13.2).
    option(PICO_SHELL_UART "Log, mirror the screen and take keys over UART1 on GP4/GP5" ON)
    if (PICO_SHELL_UART)
        set(PICO_SHELL_UART_ON 1)
    else()
        set(PICO_SHELL_UART_ON 0)
    endif()

    # The panel's SPI rate: 75 MHz, the owner's decision after M0's
    # measurements (design.md §4.1; hardware-notes.md §4.2, §4.7).
    # 25000000 is the conservative fallback. A build directory configured
    # before keeps its cached value until this is passed once.
    set(PICO_SHELL_LCD_SPI_HZ 75000000 CACHE STRING "LCD SPI rate in Hz")

    get_filename_component(SHELL_TOOLCHAIN_BIN ${CMAKE_C_COMPILER} DIRECTORY)
    find_program(SHELL_SIZE arm-none-eabi-size HINTS ${SHELL_TOOLCHAIN_BIN} REQUIRED)

    add_executable(shell
        src/port/board.c
        src/port/kbd.c
        src/port/lcd.c
        src/port/main.c
        src/port/screen.c
        src/port/southbridge.c
    )
    # The loader's stage3 loads /BOOT2350.UF2 into SRAM and runs it
    # (design.md §3.2), as it does the loader's own menu.
    set_target_properties(shell PROPERTIES OUTPUT_NAME BOOT2350)
    pico_set_binary_type(shell no_flash)
    pico_set_program_name(shell "shell")
    pico_set_program_description(shell "UNIX-like command line for the PicoCalc")

    target_include_directories(shell PRIVATE src/port ${SHELL_GENERATED})
    target_compile_options(shell PRIVATE ${SHELL_WARNINGS})
    add_dependencies(shell shell_version)
    target_compile_definitions(shell PRIVATE
        PICO_SHELL_UART=${PICO_SHELL_UART_ON}
        PICO_SHELL_LCD_SPI_HZ=${PICO_SHELL_LCD_SPI_HZ}
        # clk_peri does not follow clk_sys by default (hardware-notes.md §3).
        PICO_CLOCK_ADJUST_PERI_CLOCK_WITH_SYS_CLOCK=1
        # UART1 at 115200 8-N-1, TX GP4 / RX GP5 (hardware-notes.md §2.7).
        PICO_DEFAULT_UART=1
        PICO_DEFAULT_UART_TX_PIN=4
        PICO_DEFAULT_UART_RX_PIN=5
    )
    target_link_libraries(shell
        shell_core
        pico_stdlib
        pico_unique_id
        hardware_clocks
        hardware_dma
        hardware_i2c
        hardware_spi
        hardware_vreg
    )
    pico_enable_stdio_uart(shell ${PICO_SHELL_UART_ON})
    pico_enable_stdio_usb(shell 0)
    pico_add_extra_outputs(shell)

    # Memory use on every build (design.md §7, EL §2.1).
    target_link_options(shell PRIVATE -Wl,--print-memory-usage)
    add_custom_command(TARGET shell POST_BUILD
        COMMAND ${SHELL_SIZE} $<TARGET_FILE:shell>
        VERBATIM)
endif()
