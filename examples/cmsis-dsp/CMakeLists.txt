cmake_minimum_required(VERSION 3.14)

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

# Pull in Raspberry Pi Pico SDK
include(pico_sdk_import.cmake)

project(main C CXX ASM)

# Initialise Pico SDK
pico_sdk_init()

# ------------------------------------------------------------------------------------
# CMSIS-Core & CMSIS-DSP via CMake FetchContent
# ------------------------------------------------------------------------------------
include(FetchContent)

# 1. Fetch CMSIS-Core (Provides core_cm33.h and cmsis_compiler.h)
FetchContent_Declare(
    cmsis_core
    GIT_REPOSITORY https://github.com/ARM-software/CMSIS_5.git
    GIT_TAG        5.9.0
    SOURCE_SUBDIR  CMSIS/Core
)

# 2. Configure CMSIS-DSP compile definitions & features
set(CMSIS_DSP_FASTMATH OFF CACHE BOOL "" FORCE)
set(DISABLE_STDPERIPH_DRIVER ON CACHE BOOL "" FORCE)
set(CMSIS_DSP_PUBLISH_TARGETS OFF CACHE BOOL "" FORCE)

# Enable Cortex-M33 DSP hardware instructions
add_compile_definitions(
    ARM_MATH_CM33
    ARM_MATH_DSP
)

# 3. Fetch CMSIS-DSP repository
FetchContent_Declare(
    cmsis_dsp
    GIT_REPOSITORY https://github.com/ARM-software/CMSIS-DSP.git
    GIT_TAG        v1.15.0
    SOURCE_SUBDIR  Source
)

# Download and make targets available
FetchContent_MakeAvailable(cmsis_core cmsis_dsp)

# 4. Attach CMSIS-Core header path directly to the CMSISDSP library target
FetchContent_GetProperties(cmsis_core SOURCE_DIR cmsis_core_SOURCE_DIR)
target_include_directories(CMSISDSP PUBLIC 
    $<BUILD_INTERFACE:${cmsis_core_SOURCE_DIR}/CMSIS/Core/Include>
)
# ------------------------------------------------------------------------------------

# Add executable
add_executable(main main.c)

# Get the directory where the compiler executable lives
get_filename_component(TOOLCHAIN_BIN_DIR ${CMAKE_C_COMPILER} DIRECTORY)

# Locate arm-none-eabi-size inside that directory
find_program(CMAKE_SIZE_UTIL arm-none-eabi-size HINTS ${TOOLCHAIN_BIN_DIR})

# Use the resolved executable path
add_custom_command(TARGET main POST_BUILD
    COMMAND ${CMAKE_SIZE_UTIL} $<TARGET_FILE:main>
)

# Add CMSIS Core includes to the main target as well
target_include_directories(main PRIVATE 
    $<BUILD_INTERFACE:${cmsis_core_SOURCE_DIR}/CMSIS/Core/Include>
)

target_compile_options(main PRIVATE
    -O3
    -ffunction-sections 
    -fdata-sections
)

# Link Pico libraries and CMSIS-DSP
target_link_libraries(main 
    pico_stdlib
    pico_multicore
    hardware_dma
    hardware_adc
    CMSISDSP
)

pico_enable_stdio_uart(main 1)
pico_add_extra_outputs(main)