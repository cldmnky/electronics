# mnkybox emulator: headless selftest binary (no display, no JUCE).
#
# Included from pi/CMakeLists.txt when this directory has a CMakeLists.txt; the
# root build then provides the core targets (mnky_link mnky_midi mnky_audio
# mnky_clap) and their include paths. Standalone:
#   cmake -S midibrain/pi/emulator -B /tmp/mnky_emulator_build \
#         -DMNKY_CLAP_HEADER_DIR=<dir containing clap/clap.h>
#   cmake --build /tmp/mnky_emulator_build
#   ctest --test-dir /tmp/mnky_emulator_build --output-on-failure
cmake_minimum_required(VERSION 3.20)

set(MNKY_EMULATOR_STANDALONE OFF)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(MNKY_EMULATOR_STANDALONE ON)
  project(mnky_emulator LANGUAGES CXX)
  enable_testing()
endif()

# The root build defines the module warning set; standalone uses the same one.
if(NOT MNKY_WARNINGS)
  set(MNKY_WARNINGS -Wall -Wextra -Werror)
endif()

find_package(Threads REQUIRED)

add_executable(mnky_selftest selftest.cpp scenario.cpp synth.cpp clock_sim.cpp)
target_include_directories(mnky_selftest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(mnky_selftest PRIVATE cxx_std_20)
target_compile_options(mnky_selftest PRIVATE ${MNKY_WARNINGS})
target_link_libraries(mnky_selftest PRIVATE Threads::Threads)

if(TARGET mnky_link AND TARGET mnky_midi AND TARGET mnky_audio AND TARGET mnky_clap)
  # Root build: the module targets own their sources and include paths.
  # mnky_clap links mnky_audio PUBLIC (Host implements audio::IEffect), so
  # libmnky_audio.a legitimately appears twice on the link line. Apple's ld
  # calls that out as a warning; it is the same archive, so the note is noise.
  target_link_libraries(mnky_selftest PRIVATE mnky_link mnky_midi mnky_audio mnky_clap)
  if(APPLE)
    target_link_options(mnky_selftest PRIVATE "-Wl,-no_warn_duplicate_libraries")
  endif()
else()
  # Standalone: compile exactly the core sources the scenario drives. The
  # scenario uses Router and Engine but no RtMidi, so the USB MIDI wrapper and
  # its platform backend stay out of this build.
  target_sources(mnky_selftest PRIVATE
    ../link/link.cpp
    ../midi/router.cpp
    ../audio/engine.cpp
    ../audio/recorder.cpp
    ../clap/host.cpp)
  target_include_directories(mnky_selftest PRIVATE ../link ../midi ../audio ../clap)
  target_link_libraries(mnky_selftest PRIVATE ${CMAKE_DL_LIBS})

  # clap/host.hpp includes <clap/clap.h>; the FX stage dlopen()s a real module,
  # so the test-only ABI stub in clap/test/clap_stub is not an option.
  set(MNKY_CLAP_HEADER_DIR "" CACHE PATH "Directory containing clap/clap.h (CLAP 1.2.x)")
  if(MNKY_CLAP_HEADER_DIR AND EXISTS "${MNKY_CLAP_HEADER_DIR}/clap/clap.h")
    target_include_directories(mnky_selftest PRIVATE "${MNKY_CLAP_HEADER_DIR}")
  elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/clap/include/clap/clap.h")
    target_include_directories(mnky_selftest PRIVATE ../third_party/clap/include)
  else()
    message(FATAL_ERROR
      "mnkybox emulator selftest: CLAP headers not found; pass "
      "-DMNKY_CLAP_HEADER_DIR=<directory containing clap/clap.h>")
  endif()
endif()

# Optional CLAP module for the FX stage. Without it the stage reports SKIP and
# runs the local stub gain, so CTest stays green on a machine with no plugin
# built. Set it at configure time, e.g.
#   -DMNKY_SELFTEST_CLAP_PLUGIN=/tmp/pi-build/clap/mnkygain.dylib
set(MNKY_SELFTEST_CLAP_PLUGIN "" CACHE FILEPATH
    "CLAP effect module passed to mnky_selftest (e.g. mnkygain.dylib)")

if(MNKY_SELFTEST_CLAP_PLUGIN)
  add_test(NAME emulator_selftest COMMAND mnky_selftest "${MNKY_SELFTEST_CLAP_PLUGIN}")
else()
  add_test(NAME emulator_selftest COMMAND mnky_selftest)
endif()

# MIDI-clock emulation harness: pure host code, no display/JUCE/CLAP, virtual
# time. Prints the review's acceptance matrix; phase 2 swaps the firmware
# adapters in behind the seam in clock_sim.hpp.
add_executable(mnky_clock_sim_test clock_sim_test.cpp clock_sim.cpp)
target_include_directories(mnky_clock_sim_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(mnky_clock_sim_test PRIVATE cxx_std_20)
target_compile_options(mnky_clock_sim_test PRIVATE ${MNKY_WARNINGS})
add_test(NAME mnky_clock_sim_test COMMAND mnky_clock_sim_test)
