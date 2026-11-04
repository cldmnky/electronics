# mnkybox JUCE touchscreen application (wave-2 GUI team, `pi/ARCHITECTURE.md`).
#
# This directory is added by the root build through its documented wave-2 hook
# (pi/CMakeLists.txt: "app/ (JUCE touchscreen UI) and emulator/ ... own their own
# CMakeLists.txt"), so nothing outside this directory has to change for the app
# target to appear. A standalone configure is not supported: the app needs the
# root build's pinned JUCE, the pinned CLAP headers and the mnky_* core libs.
cmake_minimum_required(VERSION 3.24)

foreach(mnky_required_target juce::juce_gui_basics mnky_link mnky_midi mnky_audio mnky_clap)
  if(NOT TARGET ${mnky_required_target})
    message(FATAL_ERROR
      "pi/app/CMakeLists.txt needs the root build (pi/CMakeLists.txt): "
      "${mnky_required_target} is not defined")
  endif()
endforeach()

juce_add_gui_app(mnkybox
  PRODUCT_NAME    "mnkybox"
  COMPANY_NAME    "mnkybox"
  BUNDLE_ID       "com.cldmnky.mnkybox"
  VERSION         "${PROJECT_VERSION}"
  NEEDS_CURL      FALSE
  NEEDS_WEB_BROWSER FALSE)

set(MNKY_APP_SOURCES
  main.cpp
  theme.cpp
  MainComponent.cpp
  app_state.cpp
  logging.cpp
  mixer_model.cpp
  emulator_synth.cpp
  audio_glue.cpp
  fx_slot.cpp
  midi_glue.cpp
  link_client.cpp
  logic_tests.cpp
  screens/MixerScreen.cpp
  screens/RoutingScreen.cpp
  screens/FxScreen.cpp
  screens/RecorderScreen.cpp
  screens/SystemScreen.cpp)
target_sources(mnkybox PRIVATE ${MNKY_APP_SOURCES})

# Headers are listed for IDE/index completeness only; JUCE's unity build picks
# the sources up regardless.
set(MNKY_APP_HEADERS
  logic.hpp
  logging.hpp
  app_state.hpp
  app_context.hpp
  mixer_model.hpp
  emulator_synth.hpp
  audio_glue.hpp
  fx_slot.hpp
  midi_glue.hpp
  link_client.hpp
  MainComponent.hpp
  theme.hpp
  ui_style.hpp
  screens/Screen.hpp
  screens/MixerScreen.hpp
  screens/RoutingScreen.hpp
  screens/routing_model.hpp
  screens/FxScreen.hpp
  screens/RecorderScreen.hpp
  screens/SystemScreen.hpp)
target_sources(mnkybox PRIVATE ${MNKY_APP_HEADERS})
source_group("app" FILES ${MNKY_APP_HEADERS})

target_include_directories(mnkybox PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/..")

target_compile_features(mnkybox PRIVATE cxx_std_20)

# The ARCHITECTURE.md acceptance standard: zero warnings on our sources. JUCE
# compiles its module implementation (incl. vendored Opus/HarfBuzz, which trip
# GCC -Werror) inside this target, so the warning set is scoped per-source
# instead of target-wide.
set_source_files_properties(${MNKY_APP_SOURCES} PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Werror")

target_compile_definitions(mnkybox PRIVATE
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:mnkybox,JUCE_PRODUCT_NAME>"
  JUCE_APPLICATION_VERSION_STRING="${PROJECT_VERSION}")

# mnky_clap links mnky_audio PUBLIC (clap::Host implements audio::IEffect), so
# libmnky_audio.a legitimately appears twice on the link line. Apple's ld calls
# that out as a warning; it is the same archive, so the note is noise.
target_link_libraries(mnkybox PRIVATE
  mnky_link
  mnky_midi
  mnky_audio
  mnky_clap
  juce::juce_gui_basics
  juce::juce_audio_devices
  juce::juce_audio_formats
  juce::juce_recommended_config_flags)

if(APPLE)
  target_link_options(mnkybox PRIVATE "-Wl,-no_warn_duplicate_libraries")
endif()

# Default FX: the in-tree example plugin. The FX screen falls back to a file
# picker when the compile-time path is stale (e.g. a cleaned build directory).
if(TARGET mnkygain)
  add_dependencies(mnkybox mnkygain)
  target_compile_definitions(mnkybox PRIVATE MNKY_DEFAULT_PLUGIN="$<TARGET_FILE:mnkygain>")
  message(STATUS "mnkybox app: default FX plugin $<TARGET_FILE:mnkygain>")
else()
  message(STATUS "mnkybox app: no mnkygain target (needs the real CLAP headers); FX starts empty")
endif()

# --unittests runs the non-visual panel/value logic through juce::UnitTestRunner.
# The target is a macOS .app / Linux ELF bundle; CTest invokes it directly.
if(BUILD_TESTING)
  add_test(NAME mnkybox_app_logic COMMAND mnkybox --unittests)
endif()
