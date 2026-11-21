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
set(sdkVersion 2.3.1)
set(toolchainVersion RISCV_PICO_2_3_1_0)
set(picotoolVersion 2.3.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================

set(PICO_PLATFORM rp2350-riscv)

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
    dhry_1.c 
    dhry_2.c 
)

# Clock and core voltage, stock RP2350 by default; overclock with e.g. -DSYS_CLOCK_KHZ=320000 -DCORE_VOLTAGE=VREG_VOLTAGE_1_30 or -DSYS_CLOCK_KHZ=540000 -DCORE_VOLTAGE=VREG_VOLTAGE_1_60
set(SYS_CLOCK_KHZ 150000 CACHE STRING "System clock in kHz")
set(CORE_VOLTAGE VREG_VOLTAGE_1_10 CACHE STRING "Core voltage (vreg_voltage enum name)")
target_compile_definitions(main PRIVATE
    SYS_CLOCK_KHZ=${SYS_CLOCK_KHZ}
    CORE_VOLTAGE=${CORE_VOLTAGE}
)

# Add specific optimization flags to your 'main' target
target_compile_options(main PRIVATE
    -O3
    -ffunction-sections 
    -fdata-sections
)

# pull in common dependencies
target_link_libraries(main 
    pico_stdlib
    pico_multicore
    hardware_dma
    hardware_adc
    #hardware_hazard3
)

pico_enable_stdio_uart(main 1)
# pico_enable_stdio_usb(main 1)

# create map/bin/hex file etc.
pico_add_extra_outputs(main)

# add url via pico_set_program_url
