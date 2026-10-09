add_library(m24cxx STATIC
    m24cxx.c
)

target_include_directories(m24cxx PUBLIC .)

target_link_libraries(m24cxx PUBLIC
    pico_stdlib
    hardware_i2c
)