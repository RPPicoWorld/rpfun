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

# Fetch CMSIS-Core
FetchContent_Declare(
    cmsis_core
    GIT_REPOSITORY https://github.com/ARM-software/CMSIS_5.git
    GIT_TAG        5.9.0
    SOURCE_SUBDIR  CMSIS/Core
)

# Configure CMSIS-DSP compile options
set(CMSIS_DSP_FASTMATH ON CACHE BOOL "" FORCE)
set(DISABLE_STDPERIPH_DRIVER ON CACHE BOOL "" FORCE)
set(CMSIS_DSP_PUBLISH_TARGETS OFF CACHE BOOL "" FORCE)

# Preprocessor definitions to enable DSP features in headers
add_compile_definitions(
    ARM_MATH_CM33
    ARM_MATH_DSP
    ARM_MATH_LOOPUNROLL
    __FPU_PRESENT=1U
)

# Fetch CMSIS-DSP repository
FetchContent_Declare(
    cmsis_dsp
    GIT_REPOSITORY https://github.com/ARM-software/CMSIS-DSP.git
    GIT_TAG        v1.15.0
    SOURCE_SUBDIR  Source
)

FetchContent_MakeAvailable(cmsis_core cmsis_dsp)

# 4. Attach Core Includes & optimizations directly to CMSISDSP library
FetchContent_GetProperties(cmsis_core SOURCE_DIR cmsis_core_SOURCE_DIR)

target_include_directories(CMSISDSP PUBLIC 
    $<BUILD_INTERFACE:${cmsis_core_SOURCE_DIR}/CMSIS/Core/Include>
)

# Optimize CMSIS-DSP build without changing the ABI
target_compile_options(CMSISDSP PRIVATE
    -O3
    -ffunction-sections
    -fdata-sections
)
# ------------------------------------------------------------------------------------

# Executable setup
add_executable(main main.c)

# Add CMSIS Core includes to main
target_include_directories(main PRIVATE 
    $<BUILD_INTERFACE:${cmsis_core_SOURCE_DIR}/CMSIS/Core/Include>
)

# Optimizations for main executable
target_compile_options(main PRIVATE
    -O3
    -ffunction-sections 
    -fdata-sections
)

# Linker flags
target_link_options(main PRIVATE
    -Wl,--gc-sections
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