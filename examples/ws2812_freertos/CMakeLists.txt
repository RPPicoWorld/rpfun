# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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

# Custom board headers
set(PICO_BOARD_HEADER_DIRS "${CMAKE_SOURCE_DIR}/boards/include" CACHE STRING "Extra board header dirs")
set(PICO_BOARD sl_mcu_rp2354 CACHE STRING "Board type")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Pull in Raspberry Pi Pico SDK
include(pico_sdk_import.cmake)

# MUST be called first to declare languages (including ASM)
project(main C CXX ASM)

# MUST be initialized to set up the toolchain rules for ASM
pico_sdk_init()

# 4. Now import FreeRTOS AFTER toolchain & ASM are initialized
set(FREERTOS_KERNEL_PATH "${CMAKE_CURRENT_LIST_DIR}/FreeRTOS-Kernel" CACHE PATH "Path to FreeRTOS Kernel")
include(${FREERTOS_KERNEL_PATH}/portable/ThirdParty/GCC/RP2040/FreeRTOS_Kernel_import.cmake)

# Add executable
add_executable(main
    main.c
    stats.c
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

target_include_directories(main PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)

target_compile_options(main PRIVATE
    -O3
    -ffunction-sections 
    -fdata-sections
)

# Link dependencies and FreeRTOS SMP
target_link_libraries(main 
    pico_stdlib
    pico_multicore
    hardware_dma
    hardware_adc
    hardware_pio
    FreeRTOS-Kernel
    FreeRTOS-Kernel-Heap4
    m 
)

pico_enable_stdio_uart(main 1)

pico_add_extra_outputs(main)