cmake_minimum_required(VERSION 3.13)

# Pico 2 W (RP2350 + CYW43439). Must be set before the SDK/board is resolved.
set(PICO_BOARD pico2_w CACHE STRING "Board type")

include(pico_sdk_import.cmake)
project(pico_ball_gts C CXX ASM)
pico_sdk_init()

add_executable(pico_ball_gts
        main.c
        wfc_http.c
        gts.c
        dhcpserver.c
        dnsserver.c
        )

# lwipopts.h lives next to these sources.
target_include_directories(pico_ball_gts PRIVATE ${CMAKE_CURRENT_LIST_DIR})

target_link_libraries(pico_ball_gts
        pico_stdlib
        pico_cyw43_arch_lwip_threadsafe_background
        )

# USB serial for capture dumps + status; no UART.
pico_enable_stdio_usb(pico_ball_gts 1)
pico_enable_stdio_uart(pico_ball_gts 0)

pico_add_extra_outputs(pico_ball_gts)
