# mnky_link: JUCE-free Pico SPI link core.
#
# Included by the root pi/CMakeLists.txt (which owns test targets and CTest
# registration for every module), or built standalone:
#   cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure
cmake_minimum_required(VERSION 3.20)

add_library(mnky_link STATIC link.cpp)
target_include_directories(mnky_link PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(mnky_link PUBLIC cxx_std_20)
target_compile_options(mnky_link PRIVATE -Wall -Wextra -Werror)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  # Standalone: no root build to own the test target, so provide it here. The
  # root build registers test_link itself (mnky_module_tests), so this block
  # must stay inside the guard to avoid a duplicate target.
  project(mnky_link CXX)
  enable_testing()
  add_executable(test_link test_link.cpp)
  target_link_libraries(test_link PRIVATE mnky_link)
  target_compile_options(test_link PRIVATE -Wall -Wextra -Werror)
  add_test(NAME link COMMAND test_link)
endif()
