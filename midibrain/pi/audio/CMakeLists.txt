# mnkybox audio core: JUCE-free engine + multitrack float32 recorder.
#
# Included by pi/CMakeLists.txt through mnky_core_module(mnky_audio audio), which
# owns the CTest registration for the audio tests in that build. Standalone:
#   cmake -S midibrain/pi/audio -B /tmp/mnky_audio_build
#   cmake --build /tmp/mnky_audio_build && ctest --test-dir /tmp/mnky_audio_build
cmake_minimum_required(VERSION 3.20)

set(MNKY_AUDIO_STANDALONE OFF)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(MNKY_AUDIO_STANDALONE ON)
  project(mnky_audio LANGUAGES CXX)
  enable_testing()
endif()

find_package(Threads REQUIRED)

add_library(mnky_audio STATIC
  engine.cpp recorder.cpp
  fx/comp.cpp fx/delay.cpp fx/effect.cpp fx/eq.cpp fx/params.cpp fx/registry.cpp fx/reverb.cpp)
target_include_directories(mnky_audio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(mnky_audio PUBLIC cxx_std_20)
target_link_libraries(mnky_audio PUBLIC Threads::Threads)

# The root build defines MNKY_WARNINGS; a standalone build uses the same set.
if(NOT MNKY_WARNINGS)
  set(MNKY_WARNINGS -Wall -Wextra -Werror)
endif()
target_compile_options(mnky_audio PRIVATE ${MNKY_WARNINGS})

# The FX suite has its own assert-main; the root build's mnky_module_tests()
# skips it because this file owns the target.
add_executable(test_fx test_fx.cpp)
target_compile_options(test_fx PRIVATE ${MNKY_WARNINGS} -UNDEBUG)
target_link_libraries(test_fx PRIVATE mnky_audio)
add_test(NAME test_fx COMMAND test_fx)

# pi/CMakeLists.txt already builds test_audio from test_audio.cpp through
# mnky_module_tests(), so only the standalone build creates the target here.
if(MNKY_AUDIO_STANDALONE)
  add_executable(test_audio test_audio.cpp)
  target_compile_options(test_audio PRIVATE ${MNKY_WARNINGS})
  target_link_libraries(test_audio PRIVATE mnky_audio)
  add_test(NAME mnky_audio COMMAND test_audio)
endif()
