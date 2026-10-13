# fatfs.cmake — FatFs R0.15 for both builds (design.md §4.2, THIRD-PARTY.md).
#
# FatFs is not in the tree. The Pico SDK carries a copy for TinyUSB, and
# ff.h includes "ffconf.h" from its own directory, so the files are copied
# into the build tree beside src/fatfs/ffconf.h rather than compiled in
# place (which would find TinyUSB's config), as the emulators do. The host
# build needs only these files from the SDK, never its headers: it looks
# for the SDK as tools/build.sh does, PICO_SDK_PATH if that exists, else
# the newest under ~/.pico-sdk/sdk.

function(shell_fatfs_library)
    set(sdk "${PICO_SDK_PATH}")
    if (NOT sdk AND DEFINED ENV{PICO_SDK_PATH} AND EXISTS "$ENV{PICO_SDK_PATH}")
        set(sdk "$ENV{PICO_SDK_PATH}")
    endif()
    if (NOT sdk)
        file(GLOB versions LIST_DIRECTORIES true "$ENV{HOME}/.pico-sdk/sdk/*")
        if (versions)
            list(SORT versions COMPARE NATURAL ORDER DESCENDING)
            list(GET versions 0 sdk)
        endif()
    endif()

    set(src ${sdk}/lib/tinyusb/lib/fatfs/source)
    set(dir ${CMAKE_BINARY_DIR}/fatfs)
    foreach(f ff.c ff.h ffunicode.c diskio.h)
        if (NOT EXISTS ${src}/${f})
            message(FATAL_ERROR "shell: FatFs ${f} not found in ${src}; set PICO_SDK_PATH")
        endif()
        configure_file(${src}/${f} ${dir}/${f} COPYONLY)
    endforeach()
    configure_file(${CMAKE_SOURCE_DIR}/src/fatfs/ffconf.h ${dir}/ffconf.h COPYONLY)

    add_library(shell_fatfs STATIC ${dir}/ff.c ${dir}/ffunicode.c)
    target_include_directories(shell_fatfs PUBLIC ${dir})
    # Third-party code: built as it comes, without our -Werror.
endfunction()
