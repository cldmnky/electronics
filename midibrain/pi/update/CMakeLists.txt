# mnky_update: RP2350 BOOTSEL firmware-update core (UF2 validation, drive
# identification, copy/verify) and the manual `pico-update` CLI.
#
# Included by the root pi/CMakeLists.txt (which owns test targets and CTest
# registration), or built standalone:
#   cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure
#
# This directory is macOS-safe: no <linux/...> headers. The production scan
# reads sysfs/mounts paths; tests point it at a fake tree.
cmake_minimum_required(VERSION 3.20)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(mnky_update CXX)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
endif()

# The orchestrator API takes a mnky::link::PicoLink, so the link core is a hard
# dependency. Standalone builds add it; the root build already defined it.
if(NOT TARGET mnky_link)
  add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../link" "${CMAKE_CURRENT_BINARY_DIR}/link")
endif()

add_library(mnky_update STATIC
  uf2.cpp
  bootsel.cpp
  flash.cpp
  update.cpp)
target_include_directories(mnky_update PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_features(mnky_update PUBLIC cxx_std_20)
target_compile_options(mnky_update PRIVATE -Wall -Wextra -Werror)
target_link_libraries(mnky_update PUBLIC mnky_link)

add_executable(pico-update pico_update_main.cpp)
target_link_libraries(pico-update PRIVATE mnky_update)
target_compile_options(pico-update PRIVATE -Wall -Wextra -Werror)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  # No root build to own the test target, so provide it here. The root registers
  # test_update itself (mnky_module_tests), so this block stays inside the guard
  # to avoid a duplicate target. -UNDEBUG keeps assert checks live in every
  # build type, matching the other modules.
  enable_testing()
  add_executable(test_update test_update.cpp)
  target_link_libraries(test_update PRIVATE mnky_update)
  target_compile_options(test_update PRIVATE -Wall -Wextra -Werror -UNDEBUG)
  add_test(NAME update COMMAND test_update)
endif()
