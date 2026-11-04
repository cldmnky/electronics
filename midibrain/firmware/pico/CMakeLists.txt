# mnkybox Pico firmware (RP2350 / Pico 2). Configure with PICO_SDK_PATH set:
#   export PICO_SDK_PATH=$HOME/pico/pico-sdk
#   export PATH=$HOME/pico/arm-gnu-toolchain-*/bin:$PATH
#   cmake -S . -B build -DPICO_BOARD=pico2 -DCMAKE_BUILD_TYPE=MinSizeRel
#   cmake --build build -j
# Output is build/midibrain.uf2 (plus .elf/.hex/.bin). See ../README.md.
cmake_minimum_required(VERSION 3.13)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
include(pico_sdk_import.cmake)
project(midibrain C CXX ASM)
pico_sdk_init()
add_executable(midibrain main.cpp)
target_include_directories(midibrain PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(midibrain
  pico_stdlib
  hardware_uart
  hardware_spi
  hardware_gpio
  hardware_timer
  hardware_watchdog)
# Console goes to USB (debug logs); both hardware UARTs carry MIDI, so the
# UART console stays off. Forgetting this breaks MIDI IN 1 (UART0).
pico_enable_stdio_usb(midibrain 1)
pico_enable_stdio_uart(midibrain 0)
pico_add_extra_outputs(midibrain)
