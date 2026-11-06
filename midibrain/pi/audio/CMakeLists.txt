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

# Native audio I/O: libpipewire is the product path, snd_pcm the fallback. Both
# are detected through pkg-config and compiled out (with a clear error at
# runtime) when the headers are absent, so the core still builds on macOS.
find_package(PkgConfig QUIET)
set(MNKY_HAVE_PIPEWIRE 0)
set(MNKY_HAVE_ALSA 0)
if(PkgConfig_FOUND)
  pkg_check_modules(MNKY_PIPEWIRE QUIET IMPORTED_TARGET libpipewire-0.3)
  pkg_check_modules(MNKY_ALSA QUIET IMPORTED_TARGET alsa)
  if(MNKY_PIPEWIRE_FOUND)
    set(MNKY_HAVE_PIPEWIRE 1)
  endif()
  if(MNKY_ALSA_FOUND)
    set(MNKY_HAVE_ALSA 1)
  endif()
endif()

add_library(mnky_audio STATIC
  engine.cpp recorder.cpp click.cpp take_player.cpp peaks.cpp
  device_backend.cpp pipewire_backend.cpp alsa_backend.cpp
  fx/comp.cpp fx/delay.cpp fx/effect.cpp fx/eq.cpp fx/params.cpp fx/registry.cpp fx/reverb.cpp)
target_include_directories(mnky_audio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(mnky_audio PUBLIC cxx_std_20)
target_link_libraries(mnky_audio PUBLIC Threads::Threads)
target_compile_definitions(mnky_audio PRIVATE
  MNKY_HAVE_PIPEWIRE=${MNKY_HAVE_PIPEWIRE}
  MNKY_HAVE_ALSA=${MNKY_HAVE_ALSA})
if(MNKY_HAVE_PIPEWIRE)
  target_link_libraries(mnky_audio PUBLIC PkgConfig::MNKY_PIPEWIRE)
else()
  message(STATUS "mnkybox audio: libpipewire-0.3 not found - PipeWire backend compiled out")
endif()
if(MNKY_HAVE_ALSA)
  target_link_libraries(mnky_audio PUBLIC PkgConfig::MNKY_ALSA)
else()
  message(STATUS "mnkybox audio: alsa not found - snd_pcm backend compiled out")
endif()

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

# DAW engine wave: REC SOURCE PRE/POST/POSTFX tap matrix, RENDER REGION TO
# TRACK, the SONG clip scheduler (boundary declick, loop-in-place, muted,
# edits while running) and RENDER SONG, plus a clip-voice CPU probe.
add_executable(test_daw_song test_daw_song.cpp)
target_compile_options(test_daw_song PRIVATE ${MNKY_WARNINGS} -UNDEBUG)
target_link_libraries(test_daw_song PRIVATE mnky_audio)
add_test(NAME test_daw_song COMMAND test_daw_song)

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

# Per-track FX rack (engine wave 11): two series slots per track (FX1 -> FX2),
# each a built-in delay/reverb instance with its own settings and dry/wet mix;
# the post-EQ/pre-fader placement, hard bypass, hot parameter application, the
# empty-slot zero-cost path, the per-slot external (CLAP) seam, the legacy
# single-insert wrapper and the per-block CPU probe. Also proves the retired
# global FX send buses are inert.
add_executable(test_audio_insert test_audio_insert.cpp)
target_compile_options(test_audio_insert PRIVATE ${MNKY_WARNINGS} -UNDEBUG)
target_link_libraries(test_audio_insert PRIVATE mnky_audio)
add_test(NAME test_audio_insert COMMAND test_audio_insert)

# Native device layer: exact sample-format conversion (S32/S24_3/S24_32/S16/
# F32), the 18-in -> strips -> 20-out deviceOut mapping, the capture pump, the
# buffer-size chunking and a fake period-based PCM device.
add_executable(test_device_backend test_device_backend.cpp)
target_compile_options(test_device_backend PRIVATE ${MNKY_WARNINGS} -UNDEBUG)
target_link_libraries(test_device_backend PRIVATE mnky_audio)
add_test(NAME test_device_backend COMMAND test_device_backend)

# Native-backend evidence tool for the Pi (UMC1820): device enumeration with
# real node/card names, an 18-channel take through the same Engine/Recorder
# path, and per-channel peak/fundamental/THD analysis. Built only where a real
# backend exists (Linux); the Pi runs it, macOS never does.
if(MNKY_HAVE_PIPEWIRE OR MNKY_HAVE_ALSA)
  add_executable(mnky_audio_probe backend_probe.cpp)
  target_compile_options(mnky_audio_probe PRIVATE ${MNKY_WARNINGS})
  target_link_libraries(mnky_audio_probe PRIVATE mnky_audio)
endif()

# Engine capacity/real-time budget benchmark (audio wave 11): 64 stereo tracks
# through gain/pan/4-band EQ/sends, the skip-inactive path and the per-track FX
# rack (FX1/FX2 delay and reverb counts), at 64- and 128-frame blocks. A
# measurement tool, not a CTest gate: it prints a table and exits 0. Run it on
# the studio Pi before quoting any number.
add_executable(mnky_audio_bench engine_bench.cpp)
target_compile_options(mnky_audio_bench PRIVATE ${MNKY_WARNINGS})
target_link_libraries(mnky_audio_bench PRIVATE mnky_audio)

# pi/CMakeLists.txt already builds test_audio from test_audio.cpp through
# mnky_module_tests(), so only the standalone build creates the target here.
if(MNKY_AUDIO_STANDALONE)
  add_executable(test_audio test_audio.cpp)
  target_compile_options(test_audio PRIVATE ${MNKY_WARNINGS})
  target_link_libraries(test_audio PRIVATE mnky_audio)
  add_test(NAME mnky_audio COMMAND test_audio)
endif()
