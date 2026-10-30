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

add_executable(main)

# -----------------------------------------------------------------------------
# W5500 ioLibrary Driver Sources & Headers
# -----------------------------------------------------------------------------
# Collect all .c source files inside Ethernet and Internet drivers
file(GLOB_RECURSE W5500_SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/w5500/Ethernet/*.c"
    "${CMAKE_CURRENT_LIST_DIR}/w5500/Internet/*.c"
)

# Explicitly pass W5500_SOURCES to target_sources so they get compiled and linked
target_sources(main PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/main.c
    ${CMAKE_CURRENT_LIST_DIR}/w5500/w5500_spi.c
    ${W5500_SOURCES}
)

# Add specific optimization flags to your 'main' target
target_compile_options(main PRIVATE
    -O0
    -ffunction-sections 
    -fdata-sections
)

# Add include directories so the compiler can find headers
target_include_directories(main PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/w5500
    ${CMAKE_CURRENT_LIST_DIR}/w5500/Ethernet
    ${CMAKE_CURRENT_LIST_DIR}/w5500/Ethernet/W5500
    ${CMAKE_CURRENT_LIST_DIR}/w5500/Internet/DHCP
    ${CMAKE_CURRENT_LIST_DIR}/w5500/Internet/DNS
)

# -----------------------------------------------------------------------------
# Compile Definitions & Hardware Configuration
# -----------------------------------------------------------------------------
# Set required WIZnet chip selector
target_compile_definitions(main PRIVATE
    _WIZCHIP_=5500
)

# pull in common dependencies
target_link_libraries(main 
    pico_stdlib
    hardware_spi        # Hardware SPI driver
    hardware_timer      # Repeating timer alarms
    hardware_clocks     # Clock frequency support
)

pico_enable_stdio_uart(main 1)

# create map/bin/hex file etc.
pico_add_extra_outputs(main)