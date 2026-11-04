add_library(sysinfo STATIC
    sysinfo.c
)

target_include_directories(sysinfo PUBLIC .)

target_link_libraries(sysinfo PUBLIC
    pico_stdlib
    pico_unique_id
    hardware_clocks
    hardware_resets
    hardware_structs
)