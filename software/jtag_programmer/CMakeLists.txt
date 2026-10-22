cmake_minimum_required(VERSION 3.13)

if(NOT DEFINED PICO_SDK_PATH)
    if(DEFINED ENV{PICO_SDK_PATH})
        set(PICO_SDK_PATH "$ENV{PICO_SDK_PATH}")
    else()
        message(FATAL_ERROR "Set PICO_SDK_PATH to the Raspberry Pi Pico SDK directory.")
    endif()
endif()

set(PICO_BOARD pico CACHE STRING "Pico board definition for the RP2040")
include("${PICO_SDK_PATH}/external/pico_sdk_import.cmake")

project(dirtyjtag_gatemate C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

pico_sdk_init()

add_executable(dirtyjtag_gatemate
    dirtyjtag.c
    jtag.c
    usb_descriptors.c
)

target_include_directories(dirtyjtag_gatemate PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(dirtyjtag_gatemate PRIVATE
    pico_stdlib
    hardware_clocks
    hardware_gpio
    tinyusb_device
)

pico_enable_stdio_uart(dirtyjtag_gatemate 0)
pico_enable_stdio_usb(dirtyjtag_gatemate 0)
pico_add_extra_outputs(dirtyjtag_gatemate)