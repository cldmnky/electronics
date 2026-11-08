# mnkybox midi core (JUCE-free): router, dynamic USB port registry, USB MIDI,
# clock discipline, buttons, MIDI learn.
#
# Two supported uses:
#  * root build (pi/CMakeLists.txt, setup team): its mnky_core_module() adds this
#    directory, expects the fixed target name `mnky_midi`, provides `mnky_rtmidi`
#    (RtMidi compiled once, platform define PUBLIC) and registers `test_midi`
#    through mnky_module_tests().
#  * standalone:  cmake -S . -B build && cmake --build build && ctest --test-dir build
#    Here this file compiles RtMidi itself under the same target name and adds
#    the test, so the module can be verified without the JUCE root build.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  cmake_minimum_required(VERSION 3.21)
  project(mnky_midi LANGUAGES CXX)
  enable_testing()
  set(MB_MIDI_STANDALONE ON)
else()
  set(MB_MIDI_STANDALONE OFF)
endif()

set(MB_RTMIDI_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/rtmidi/RtMidi.cpp)

if(NOT TARGET mnky_rtmidi)
  add_library(mnky_rtmidi STATIC ${MB_RTMIDI_SOURCE})
  target_include_directories(mnky_rtmidi PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/rtmidi)
  if(APPLE)
    if(MB_MIDI_STANDALONE AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
      # RtMidi's CoreMIDI backend includes Apple framework headers that GCC
      # cannot parse under -Werror; the architecture's build line assumes Apple
      # clang as well.
      message(FATAL_ERROR
        "mnky_midi on macOS needs Apple clang: configure with "
        "-DCMAKE_CXX_COMPILER=/usr/bin/c++ (found ${CMAKE_CXX_COMPILER})")
    endif()
    target_compile_definitions(mnky_rtmidi PUBLIC __MACOSX_CORE__)
    target_link_libraries(mnky_rtmidi PUBLIC
      "-framework CoreMIDI"
      "-framework CoreAudio"
      "-framework CoreFoundation"
      "-framework CoreServices"
    )
  elseif(UNIX)
    # Pi 5 target: ALSA backend (the macOS run covers the CoreMIDI backend).
    target_compile_definitions(mnky_rtmidi PUBLIC __LINUX_ALSA__)
    target_link_libraries(mnky_rtmidi PUBLIC asound)
  endif()
endif()

add_library(mnky_midi STATIC
  router.cpp
  port_registry.cpp
  midifx.cpp
  clock.cpp
  usb.cpp
  learn.cpp
)
target_include_directories(mnky_midi PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/..
  # usb.cpp includes <rtmidi/RtMidi.h>, the same spelling as the architecture's
  # build line (-I../third_party).
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party
)
target_compile_features(mnky_midi PUBLIC cxx_std_20)
target_compile_options(mnky_midi PRIVATE -Wall -Wextra -Werror)
target_link_libraries(mnky_midi PUBLIC mnky_rtmidi)

if(MB_MIDI_STANDALONE)
  add_executable(test_midi test_midi.cpp)
  target_link_libraries(test_midi PRIVATE mnky_midi)
  target_compile_options(test_midi PRIVATE -Wall -Wextra -Werror)
  add_test(NAME test_midi COMMAND test_midi)

  add_executable(test_midifx test_midifx.cpp)
  target_link_libraries(test_midifx PRIVATE mnky_midi)
  target_compile_options(test_midifx PRIVATE -Wall -Wextra -Werror)
  add_test(NAME test_midifx COMMAND test_midifx)

  add_executable(test_learn test_learn.cpp)
  target_link_libraries(test_learn PRIVATE mnky_midi)
  target_compile_options(test_learn PRIVATE -Wall -Wextra -Werror)
  add_test(NAME test_learn COMMAND test_learn)

  # Dynamic USB port registry + session binding rules. Includes the session
  # header for the RouterState shape but calls no session.cpp symbol, so the
  # standalone midi build does not need the audio engine.
  add_executable(test_registry test_registry.cpp)
  target_link_libraries(test_registry PRIVATE mnky_midi)
  target_compile_options(test_registry PRIVATE -Wall -Wextra -Werror)
  # Standalone only: compile the real session reader/writer (and the audio TUs
  # session.cpp links against) to prove a dynamic-id snapshot round-trips. The
  # root build's module test links mnky_midi alone and skips this section.
  target_sources(test_registry PRIVATE
    ../session/session.cpp
    ../audio/engine.cpp
    ../audio/take_player.cpp
    ../audio/click.cpp)
  target_include_directories(test_registry PRIVATE ../session ../audio)
  target_compile_definitions(test_registry PRIVATE MNKY_MIDI_SESSION_TEST=1)
  add_test(NAME test_registry COMMAND test_registry)
endif()
