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
set(sdkVersion 2.3.1)
set(toolchainVersion 15_2_Rel1)
set(picotoolVersion 2.3.1)
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

# ====================================================================
# AUTO-EMBED FOLDER INTO BINARY (Generates C headers from fs_root/)
# ====================================================================
file(GLOB FS_FILES "${CMAKE_CURRENT_SOURCE_DIR}/fs_root/*")
set(EMBEDDED_GEN_HEADERS "")

foreach(FILE_PATH ${FS_FILES})
    if(NOT IS_DIRECTORY ${FILE_PATH})
        get_filename_component(FILE_NAME ${FILE_PATH} NAME)
        string(MAKE_C_IDENTIFIER "${FILE_NAME}" CLEAN_NAME)
        set(HEADER_OUT "${CMAKE_CURRENT_BINARY_DIR}/gen_${CLEAN_NAME}.h")

        add_custom_command(
            OUTPUT ${HEADER_OUT}
            COMMAND ${CMAKE_COMMAND} -E echo "/* Auto-generated embedded file header */" > ${HEADER_OUT}
            COMMAND ${CMAKE_COMMAND} -E echo "const unsigned char file_${CLEAN_NAME}[] = {" >> ${HEADER_OUT}
            COMMAND xxd -i < ${FILE_PATH} >> ${HEADER_OUT}
            COMMAND ${CMAKE_COMMAND} -E echo "\;" >> ${HEADER_OUT}
            DEPENDS ${FILE_PATH}
            COMMENT "Packing ${FILE_NAME} into C array header"
        )
        list(APPEND EMBEDDED_GEN_HEADERS ${HEADER_OUT})
    endif()
endforeach()

add_custom_target(generate_embedded_files DEPENDS ${EMBEDDED_GEN_HEADERS})

# Add executable (Includes LittleFS source files and port)
add_executable(main
    main.c
    lfs_port.c
    littlefs/lfs.c
    littlefs/lfs_util.c
)

# Ensure embedded files are generated before building main
add_dependencies(main generate_embedded_files)

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

# LINK LIBRARIES (Include target_include_directories so the .h is found)
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
# pico_enable_stdio_usb(main 1)

# create map/bin/hex file etc.
pico_add_extra_outputs(main)