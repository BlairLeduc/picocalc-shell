# shell — one source tree, two targets (design.md §6):
#   - BOOT2350.UF2, the shell as a no_flash RAM program (default), in the
#     flavour PICO_SHELL_FLAVOUR names: unix (default) or dos (§4.7)
#   - a host build of src/core/ and its tests with the system compiler and
#     no Pico SDK, behind -DPICO_SHELL_HOST=ON, run under CTest; it builds
#     both flavours.
#
# The core building clean under -Wall -Wextra -Werror on both toolchains
# is what keeps SDK dependencies out of it (EL §2.1).

cmake_minimum_required(VERSION 3.18)

option(PICO_SHELL_HOST "Build the portable core and its host tests, with no Pico SDK" OFF)

# Every fixed capacity lives in src/core/config.h; nothing here duplicates one.
# What both flavours share (design.md §4.7)...
set(SHELL_CORE_SOURCES
    src/core/bininfo.c
    src/core/editor_file.c
    src/core/editor_pattern.c
    src/core/files.c
    src/core/history.c
    src/core/input.c
    src/core/keys.c
    src/core/line.c
    src/core/out.c
    src/core/path.c
    src/core/pipe.c
    src/core/run.c
    src/core/shell.c
    src/core/system.c
    src/core/term.c
    src/core/text.c
    src/core/uf2.c
    src/core/vars.c
)
# ...the UNIX flavour's command language, vi and font...
set(SHELL_UNIX_SOURCES
    src/core/editor_lines.c
    src/core/editor_undo.c
    src/core/editor_vi.c
    src/core/expand.c
    src/core/font-master-512-8x10.c
    src/core/parse.c
    src/core/unix.c
    src/core/vi.c
)
# ...and the DOS flavour's.
set(SHELL_DOS_SOURCES
    src/core/dos.c
    src/core/dos_batch.c
    src/core/dos_files.c
    src/core/dos_parse.c
    src/core/dos_path.c
    src/core/dos_text.c
    src/core/edit.c
    src/core/editor_lines.c
    src/core/font-ibm-8x10.c
)

set(SHELL_WARNINGS -Wall -Wextra -Werror)

include(cmake/fatfs.cmake)

# The core in one flavour, unix or dos, as the library `name`. It uses
# FatFs's API for files (design.md §5); the disk under it is the port's
# (the SD card) or the host's (an image file).
function(shell_core_library name flavour)
    if (flavour STREQUAL "unix")
        add_library(${name} STATIC ${SHELL_CORE_SOURCES} ${SHELL_UNIX_SOURCES})
        target_compile_definitions(${name} PUBLIC SHELL_DOS=0)
    elseif (flavour STREQUAL "dos")
        add_library(${name} STATIC ${SHELL_CORE_SOURCES} ${SHELL_DOS_SOURCES})
        target_compile_definitions(${name} PUBLIC SHELL_DOS=1)
    else()
        message(FATAL_ERROR "shell: PICO_SHELL_FLAVOUR is unix or dos, not ${flavour}")
    endif()
    target_include_directories(${name} PUBLIC src/core)
    target_compile_options(${name} PRIVATE ${SHELL_WARNINGS})
    # Pico Logo's key layer cuts its Ctrl-G report to the status row on
    # purpose (its limits.h says so); GCC's truncation warning is off for
    # that file alone.
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        set_source_files_properties(src/core/editor_vi.c PROPERTIES
            COMPILE_OPTIONS -Wno-format-truncation)
    endif()
    target_link_libraries(${name} PUBLIC shell_fatfs)
endfunction()

if (PICO_SHELL_HOST)
    project(shell_host C)
    set(CMAKE_C_STANDARD 11)
    set(CMAKE_C_STANDARD_REQUIRED ON)
    set(CMAKE_C_EXTENSIONS OFF)
    # The FatFs/disk cycle below repeats two archives on the link line,
    # which GNU ld needs and Apple's linker warns about.
    if (APPLE)
        add_link_options(LINKER:-no_warn_duplicate_libraries)
    endif()

    shell_fatfs_library()
    shell_core_library(shell_core unix)
    shell_core_library(shell_core_dos dos)
    # Only the host formats disks: the tests' FAT16 and FAT32 images.
    target_compile_definitions(shell_fatfs PUBLIC SHELL_FATFS_MKFS=1)

    # The host's disk, an image file, and the host shell over it.
    add_library(shell_host_disk STATIC src/host/host_disk.c)
    target_include_directories(shell_host_disk PUBLIC src/host)
    target_compile_options(shell_host_disk PRIVATE ${SHELL_WARNINGS})
    target_link_libraries(shell_host_disk PUBLIC shell_fatfs)
    # FatFs calls the disk, and the disk FatFs's types: a cycle, which
    # CMake writes out so that GNU ld, which scans each archive once in
    # order, finds the disk for a program that names only the core.
    target_link_libraries(shell_fatfs PUBLIC shell_host_disk)

    add_executable(shell src/host/main.c)
    target_compile_options(shell PRIVATE ${SHELL_WARNINGS})
    target_link_libraries(shell PRIVATE shell_core shell_host_disk)
    add_executable(shell-dos src/host/main.c)
    target_compile_options(shell-dos PRIVATE ${SHELL_WARNINGS})
    target_link_libraries(shell-dos PRIVATE shell_core_dos shell_host_disk)

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

    # The flavour (design.md §4.7): each is its own BOOT2350.UF2, built in
    # its own directory.
    set(PICO_SHELL_FLAVOUR unix CACHE STRING "The command line's flavour: unix or dos")
    shell_fatfs_library()
    shell_core_library(shell_core ${PICO_SHELL_FLAVOUR})

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
        src/port/diskio.c
        src/port/kbd.c
        src/port/lcd.c
        src/port/loader.c
        src/port/main.c
        src/port/screen.c
        src/port/sd.c
        src/port/southbridge.c
        src/port/usb_descriptors.c
        src/port/usbdisk.c
    )
    # The loader's stage3 loads /BOOT2350.UF2 into SRAM and runs it
    # (design.md §3.2), as it does the loader's own menu.
    set_target_properties(shell PROPERTIES OUTPUT_NAME BOOT2350)
    pico_set_binary_type(shell no_flash)
    pico_set_program_name(shell "shell")
    if (PICO_SHELL_FLAVOUR STREQUAL "dos")
        pico_set_program_description(shell "DOS-like command line for the PicoCalc")
    else()
        pico_set_program_description(shell "UNIX-like command line for the PicoCalc")
    endif()

    # include/: the header programs use to return to the shell, which
    # loader.c checks against its own idea of the protocol. src/port has
    # tusb_config.h, which TinyUSB looks for on the include path.
    target_include_directories(shell PRIVATE src/port include ${SHELL_GENERATED})
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
        # board.c only reads the PSRAM's id, for info and MEM; nothing
        # sets it up or uses it (hardware-notes.md §2.4).
        PICO_RUNTIME_NO_INIT_PSRAM=1
    )
    target_link_libraries(shell
        shell_core
        pico_stdlib
        pico_unique_id
        hardware_clocks
        hardware_dma
        hardware_flash
        hardware_i2c
        hardware_psram
        hardware_spi
        hardware_vreg
        tinyusb_device
    )
    pico_enable_stdio_uart(shell ${PICO_SHELL_UART_ON})
    pico_enable_stdio_usb(shell 0)
    pico_add_extra_outputs(shell)

    # A flash program that returns to the shell when a key is pressed: the
    # board check of include/picocalc_shell.h (design.md §3.4). It shows
    # what it does with the shell's own panel, terminal and keyboard code.
    add_executable(exit-test
        test/pico/exit_test.c
        src/port/kbd.c
        src/port/lcd.c
        src/port/screen.c
        src/port/southbridge.c
    )
    target_include_directories(exit-test PRIVATE include src/port)
    target_compile_options(exit-test PRIVATE ${SHELL_WARNINGS})
    target_compile_definitions(exit-test PRIVATE
        PICO_SHELL_LCD_SPI_HZ=${PICO_SHELL_LCD_SPI_HZ}
        PICO_CLOCK_ADJUST_PERI_CLOCK_WITH_SYS_CLOCK=1
        PICO_DEFAULT_UART=1
        PICO_DEFAULT_UART_TX_PIN=4
        PICO_DEFAULT_UART_RX_PIN=5
    )
    target_link_libraries(exit-test shell_core pico_stdlib hardware_dma hardware_i2c hardware_spi)
    pico_enable_stdio_uart(exit-test 1)
    pico_enable_stdio_usb(exit-test 0)
    pico_add_extra_outputs(exit-test)

    # Memory use on every build (design.md §7, EL §2.1).
    target_link_options(shell PRIVATE -Wl,--print-memory-usage)
    add_custom_command(TARGET shell POST_BUILD
        COMMAND ${SHELL_SIZE} $<TARGET_FILE:shell>
        VERBATIM)
endif()
