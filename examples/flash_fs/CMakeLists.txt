# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.3.1)
set(toolchainVersion 15_2_Rel1)
set(picotoolVersion 2.3.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()

set(PICO_PLATFORM rp2350)
set(PICO_BOARD_HEADER_DIRS "${CMAKE_SOURCE_DIR}/boards/include" CACHE STRING "Extra board header dirs")
set(PICO_BOARD sl_mcu_rp2354 CACHE STRING "Board type")

include(pico_sdk_import.cmake)

project(main C CXX ASM)

pico_sdk_init()

add_executable(main
    main.c
    lfs_port.c
    littlefs/lfs.c
    littlefs/lfs_util.c
)

get_filename_component(TOOLCHAIN_BIN_DIR ${CMAKE_C_COMPILER} DIRECTORY)
find_program(CMAKE_SIZE_UTIL arm-none-eabi-size HINTS ${TOOLCHAIN_BIN_DIR})

add_custom_command(TARGET main POST_BUILD
    COMMAND ${CMAKE_SIZE_UTIL} $<TARGET_FILE:main>
)

target_compile_options(main PRIVATE
    -O3
    -ffunction-sections 
    -fdata-sections
)

target_include_directories(main PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/littlefs
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(main 
    pico_stdlib
    pico_multicore
    pico_rand
    hardware_dma
    hardware_clocks
    hardware_flash
)

pico_enable_stdio_uart(main 1)

# ====================================================================
# BUILD LITTLEFS BINARY ONLY (NO EMBEDDING / NO LINKER MERGING)
# ====================================================================
find_program(MKLITTLEFS_EXEC mklittlefs)

if(MKLITTLEFS_EXEC)
    set(FS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/fs_root")
    set(FS_BIN "${CMAKE_CURRENT_BINARY_DIR}/littlefs.bin")

    file(GLOB_RECURSE FS_FILES "${FS_DIR}/*")

    add_custom_command(
        OUTPUT ${FS_BIN}
        COMMAND ${MKLITTLEFS_EXEC} -c ${FS_DIR} -b 4096 -p 256 -s 1048576 ${FS_BIN}
        DEPENDS ${FS_FILES}
        COMMENT "Generating standard LittleFS binary..."
    )

    add_custom_target(generate_littlefs ALL DEPENDS ${FS_BIN})
else()
    message(WARNING "mklittlefs executable not found.")
endif()

pico_add_extra_outputs(main)

# ====================================================================
# OPENOCD FLASH TARGETS (USING PICO SDK INSTALL)
# ====================================================================

# Resolve OpenOCD executable and scripts directory from Pico SDK
if(DEFINED ENV{PICO_OPENOCD_PATH})
    set(OPENOCD_BIN "$ENV{PICO_OPENOCD_PATH}/bin/openocd")
    set(OPENOCD_SCRIPTS "$ENV{PICO_OPENOCD_PATH}/scripts")
else()
    set(OPENOCD_BIN "${USERHOME}/.pico-sdk/openocd/0.12.0+dev/openocd")
    set(OPENOCD_SCRIPTS "${USERHOME}/.pico-sdk/openocd/0.12.0+pico/scripts")
endif()

# Fall back to system openocd if SDK paths don't exist
if(NOT EXISTS ${OPENOCD_BIN})
    find_program(OPENOCD_BIN openocd)
    set(OPENOCD_SCRIPTS "")
endif()

if(OPENOCD_SCRIPTS AND EXISTS ${OPENOCD_SCRIPTS})
    set(OPENOCD_SEARCH_ARGS -s ${OPENOCD_SCRIPTS})
else()
    set(OPENOCD_SEARCH_ARGS "")
endif()

# 1. Flash Filesystem Only via OpenOCD
add_custom_target(openocd_flash_fs
    COMMAND ${OPENOCD_BIN} ${OPENOCD_SEARCH_ARGS}
        -f interface/cmsis-dap.cfg
        -f target/rp2350-arm.cfg
        -c "program ${FS_BIN} 0x10100000 verify reset exit"
    DEPENDS generate_littlefs
    COMMENT "Flashing littlefs.bin to 0x10100000 via OpenOCD..."
)

# 2. Flash Main Firmware Only via OpenOCD
add_custom_target(openocd_flash_main
    COMMAND ${OPENOCD_BIN} ${OPENOCD_SEARCH_ARGS}
        -f interface/cmsis-dap.cfg
        -f target/rp2350-arm.cfg
        -c "program $<TARGET_FILE:main> verify reset exit"
    DEPENDS main
    COMMENT "Flashing main.elf via OpenOCD..."
)

# 3. Flash Everything (Main Firmware + LittleFS) via OpenOCD
add_custom_target(openocd_flash_all
    COMMAND ${OPENOCD_BIN} ${OPENOCD_SEARCH_ARGS}
        -f interface/cmsis-dap.cfgd
        -f target/rp2350-arm.cfg
        -c "program $<TARGET_FILE:main> verify"
        -c "program ${FS_BIN} 0x10100000 verify reset exit"
    DEPENDS main generate_littlefs
    COMMENT "Flashing main.elf + littlefs.bin via OpenOCD..."
)