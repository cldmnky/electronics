# CLAP host core + example gain plugin (JUCE-free, portable C++20).
#
# Standalone:   cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure
# As a module:  add_subdirectory(clap) from pi/CMakeLists.txt (setup team owns the root)
#
# CLAP headers are supplied by the setup team's FetchContent (free-audio/clap
# 1.2.10). Until that lands, pin the include directory with
# -DMNKY_CLAP_HEADER_DIR=<dir containing clap/clap.h>; without it the host core
# and its test build against the test-only ABI stand-in in test/clap_stub.

cmake_minimum_required(VERSION 3.20)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(mnky_clap LANGUAGES CXX)
  enable_testing()
endif()

set(MNKY_CLAP_HEADER_DIR "" CACHE PATH "Directory containing clap/clap.h (CLAP 1.2.x)")

set(MNKY_CLAP_INCLUDE_DIR "")
if(MNKY_CLAP_HEADER_DIR AND EXISTS "${MNKY_CLAP_HEADER_DIR}/clap/clap.h")
  set(MNKY_CLAP_INCLUDE_DIR "${MNKY_CLAP_HEADER_DIR}")
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/clap/include/clap/clap.h")
  set(MNKY_CLAP_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/clap/include")
endif()

add_library(mnky_clap STATIC host.cpp)
target_include_directories(mnky_clap PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(mnky_clap PUBLIC cxx_std_20)
target_link_libraries(mnky_clap PUBLIC ${CMAKE_DL_LIBS})

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(mnky_clap PRIVATE -Wall -Wextra -Werror)
endif()

if(MNKY_CLAP_INCLUDE_DIR)
  target_include_directories(mnky_clap PUBLIC "${MNKY_CLAP_INCLUDE_DIR}")
  message(STATUS "midibrain/clap: using CLAP headers from ${MNKY_CLAP_INCLUDE_DIR}")
else()
  target_include_directories(mnky_clap PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/test/clap_stub")
  target_compile_definitions(mnky_clap PUBLIC CLAP_ABI_STUB=1)
  message(STATUS "midibrain/clap: no CLAP headers found, host core + test build against the TEST-ONLY "
                 "ABI stub in test/clap_stub; set -DMNKY_CLAP_HEADER_DIR=<dir with clap/clap.h> to use "
                 "the real headers")
endif()

option(MNKY_CLAP_BUILD_TESTS "Build the CLAP host core test" ON)
if(MNKY_CLAP_BUILD_TESTS)
  add_executable(test_clap test_clap.cpp)
  target_link_libraries(test_clap PRIVATE mnky_clap)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(test_clap PRIVATE -Wall -Wextra -Werror)
  endif()
  if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR OR (DEFINED BUILD_TESTING AND BUILD_TESTING))
    add_test(NAME clap_host_core COMMAND test_clap)
  endif()
endif()

# Best effort: the example plugin needs the real headers (it must not be built
# against the test-only stand-in). Bundle assembly is documented in
# effects/mnkygain/README.md.
if(MNKY_CLAP_INCLUDE_DIR)
  add_library(mnkygain MODULE effects/mnkygain/mnkygain.cpp)
  target_include_directories(mnkygain PRIVATE "${MNKY_CLAP_INCLUDE_DIR}")
  set_target_properties(mnkygain PROPERTIES PREFIX "" OUTPUT_NAME "mnkygain" CXX_VISIBILITY_PRESET hidden)
  if(APPLE)
    # Same artifact name as the documented direct build; wrap it in a .clap bundle (README).
    set_target_properties(mnkygain PROPERTIES SUFFIX ".dylib")
  endif()
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(mnkygain PRIVATE -Wall -Wextra -Werror)
  endif()
else()
  message(STATUS "midibrain/clap: mnkygain plugin target skipped (needs real CLAP headers); "
                 "effects/mnkygain/README.md has a direct c++ build line")
endif()
