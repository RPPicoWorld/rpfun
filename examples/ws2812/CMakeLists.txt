# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.3.0)
set(toolchainVersion 15_2_Rel1)
set(picotoolVersion 2.3.0)
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

# Add executable
add_executable(main
    main.c
)

# Generate pio header
pico_generate_pio_header(main 
    ${CMAKE_CURRENT_LIST_DIR}/ws2812.pio
)

# Get the directory where the compiler executable lives
get_filename_component(TOOLCHAIN_BIN_DIR ${CMAKE_C_COMPILER} DIRECTORY)

# Locate arm-none-eabi-size inside that directory
find_program(CMAKE_SIZE_UTIL arm-none-eabi-size HINTS ${TOOLCHAIN_BIN_DIR})

# Use the resolved executable path
add_custom_command(TARGET main POST_BUILD
    COMMAND ${CMAKE_SIZE_UTIL} $<TARGET_FILE:main>
)

# Add specific optimization flags to your 'main' target
target_compile_options(main PRIVATE
    -O3
    -ffunction-sections 
    -fdata-sections
)

# 2. LINK LIBRARIES (Include target_include_directories so the .h is found)
target_include_directories(main PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(main 
    pico_stdlib
    pico_multicore
    hardware_dma
    hardware_pio
    hardware_clocks
)

pico_enable_stdio_uart(main 1)
# pico_enable_stdio_usb(main 1)

# create map/bin/hex file etc.
pico_add_extra_outputs(main)