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
  engine.cpp recorder.cpp click.cpp take_player.cpp peaks.cpp
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

# Engine tap provenance / recorder master arming and the click source. Owned
# here (like test_fx) so the standalone audio build runs them too; the root
# build's mnky_module_tests() skips targets that already exist.
add_executable(test_audio_features test_audio_features.cpp)
target_compile_options(test_audio_features PRIVATE ${MNKY_WARNINGS} -UNDEBUG)
target_link_libraries(test_audio_features PRIVATE mnky_audio)
add_test(NAME test_audio_features COMMAND test_audio_features)

add_executable(test_click test_click.cpp)
target_compile_options(test_click PRIVATE ${MNKY_WARNINGS} -UNDEBUG)
target_link_libraries(test_click PRIVATE mnky_audio)
add_test(NAME test_click COMMAND test_click)

# Take playback: selection, newest-wins layering, the 48 kHz refusal path and
# per-track take looping (beat-mapped range and whole-take loops).
add_executable(test_audio_playback test_audio_playback.cpp)
target_compile_options(test_audio_playback PRIVATE ${MNKY_WARNINGS} -UNDEBUG)
target_link_libraries(test_audio_playback PRIVATE mnky_audio)
add_test(NAME test_audio_playback COMMAND test_audio_playback)

# Non-destructive region edits in the take voice: beat-mapped offsets/lengths,
# per-region gain and fade ramps, normalize, clamp reasons, and looping the
# edited stream instead of the raw take.
add_executable(test_take_edit test_take_edit.cpp)
target_compile_options(test_take_edit PRIVATE ${MNKY_WARNINGS} -UNDEBUG)
target_link_libraries(test_take_edit PRIVATE mnky_audio)
add_test(NAME test_take_edit COMMAND test_take_edit)

# Background peak scanner: 256-frame min/max buckets, the coarse fold, the
# `.peaks` sidecar round-trip, mtime/size invalidation and worker-thread progress.
add_executable(test_peaks test_peaks.cpp)
target_compile_options(test_peaks PRIVATE ${MNKY_WARNINGS} -UNDEBUG)
target_link_libraries(test_peaks PRIVATE mnky_audio)
add_test(NAME test_peaks COMMAND test_peaks)

# Per-strip input source (identity, remap, silence, stereo pair) and output
# destination (Main / OUT3&4 / Phones=CUE / None).
add_executable(test_audio_routing test_audio_routing.cpp)
target_compile_options(test_audio_routing PRIVATE ${MNKY_WARNINGS} -UNDEBUG)
target_link_libraries(test_audio_routing PRIVATE mnky_audio)
add_test(NAME test_audio_routing COMMAND test_audio_routing)

# Four-band EQ inserts: the shipped biquad shapes, type-None transparency, the
# per-strip send matrix and the post-compressor main placement.
add_executable(test_audio_eq test_audio_eq.cpp)
target_compile_options(test_audio_eq PRIVATE ${MNKY_WARNINGS} -UNDEBUG)
target_link_libraries(test_audio_eq PRIVATE mnky_audio)
add_test(NAME test_audio_eq COMMAND test_audio_eq)

# pi/CMakeLists.txt already builds test_audio from test_audio.cpp through
# mnky_module_tests(), so only the standalone build creates the target here.
if(MNKY_AUDIO_STANDALONE)
  add_executable(test_audio test_audio.cpp)
  target_compile_options(test_audio PRIVATE ${MNKY_WARNINGS})
  target_link_libraries(test_audio PRIVATE mnky_audio)
  add_test(NAME mnky_audio COMMAND test_audio)
endif()
