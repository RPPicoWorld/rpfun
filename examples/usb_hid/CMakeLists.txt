# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================

set(PICO_PLATFORM rp2350)

# custom board headers
set(PICO_BOARD_HEADER_DIRS "${CMAKE_SOURCE_DIR}/boards/include" CACHE STRING "Extra board header dirs")

set(PICO_BOARD sl_mcu_rp2354 CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(main C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1

add_executable(main
    main.c
)

target_sources(main PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/main.c
    ${CMAKE_CURRENT_LIST_DIR}/usb_descriptors.c
)

# Add specific optimization flags to your 'main' target
target_compile_options(main PRIVATE
    -O3
    -ffunction-sections 
    -fdata-sections
)

# Add include directories so the compiler can find tusb_config.h
target_include_directories(main PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}        # If tusb_config.h is in the root directory
)

# pull in common dependencies
target_link_libraries(main 
    pico_stdlib
    tinyusb_device 
    tinyusb_board
)

pico_enable_stdio_uart(main 1)

# create map/bin/hex file etc.
pico_add_extra_outputs(main)

# add url via pico_set_program_url
