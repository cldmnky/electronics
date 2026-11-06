# mnkybox Qt 6 QML touchscreen application (wave-2 UI team, `pi/ARCHITECTURE.md`).
#
# This directory is added by the root build through its documented wave-2 hook.
# The root finds Qt 6 (Core/Gui/Qml/Quick/Multimedia) and the pinned CLAP
# headers. A standalone configure is not supported: the app needs the mnky_*
# core libs and their include paths.
cmake_minimum_required(VERSION 3.24)

foreach(mnky_required_target mnky_link mnky_midi mnky_audio mnky_clap mnky_session
        Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Multimedia)
  if(NOT TARGET ${mnky_required_target})
    message(FATAL_ERROR
      "pi/app/CMakeLists.txt needs the root build (pi/CMakeLists.txt): "
      "${mnky_required_target} is not defined")
  endif()
endforeach()

set(MNKY_APP_SOURCES
  main.cpp
  app_controller.cpp
  app_state.cpp
  config_popup_model.cpp
  audio_glue.cpp
  emulator_synth.cpp
  eq_model.cpp
  eq_view_model.cpp
  fx_slot.cpp
  fx_view_model.cpp
  learn_view_model.cpp
  link_client.cpp
  logging.cpp
  logic_tests.cpp
  midi_glue.cpp
  midi_monitor_view_model.cpp
  mixer_model.cpp
  mixer_view_model.cpp
  performance_model.cpp
  pipe_params.cpp
  project_io.cpp
  project_logic.cpp
  projects_view_model.cpp
  recorder_view_model.cpp
  routing_view_model.cpp
  session_settings.cpp
  session_view_model.cpp
  system_view_model.cpp
  timeline_model.cpp
  ui_tap_runner.cpp
  timeline_view_model.cpp
  tools_view_model.cpp
  wav_info.cpp
  xy_view_model.cpp)

set(MNKY_APP_HEADERS
  app_context.hpp
  app_controller.hpp
  app_state.hpp
  config_popup_model.hpp
  audio_backend.hpp
  audio_glue.hpp
  emulator_synth.hpp
  eq_model.hpp
  eq_view_model.hpp
  fx_slot.hpp
  fx_view_model.hpp
  learn_view_model.hpp
  link_client.hpp
  logging.hpp
  logic.hpp
  midi_glue.hpp
  midi_monitor_view_model.hpp
  mixer_model.hpp
  mixer_view_model.hpp
  performance_model.hpp
  pipe_params.hpp
  project_io.hpp
  project_logic.hpp
  projects_view_model.hpp
  recorder_view_model.hpp
  routing_model.hpp
  routing_view_model.hpp
  session_settings.hpp
  session_view_model.hpp
  system_view_model.hpp
  text_buffer.hpp
  ui_tap_runner.hpp
  timeline_model.hpp
  timeline_view_model.hpp
  tools_view_model.hpp
  wav_info.hpp
  xy_view_model.hpp)

add_executable(mnkybox ${MNKY_APP_SOURCES} ${MNKY_APP_HEADERS})
set_target_properties(mnkybox PROPERTIES AUTOMOC ON)
target_compile_definitions(mnkybox PRIVATE MNKY_APP_VERSION="${PROJECT_VERSION}")
target_include_directories(mnkybox PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_features(mnkybox PRIVATE cxx_std_20)

# ARCHITECTURE.md acceptance: zero warnings on our sources. Qt's generated moc
# and resource sources are not in this list.
set_source_files_properties(${MNKY_APP_SOURCES} PROPERTIES COMPILE_OPTIONS
  "-Wall;-Wextra;-Werror")

target_link_libraries(mnkybox PRIVATE
  mnky_link
  mnky_midi
  mnky_audio
  mnky_clap
  mnky_session
  Qt6::Core
  Qt6::Gui
  Qt6::Qml
  Qt6::Quick
  Qt6::Multimedia)

# QML + the QML Style singleton are compiled into the binary. Style.qml is
# registered as a singleton type from main.cpp at :/qml/Style.qml.
qt_add_resources(mnkybox "qml"
  PREFIX "/"
  BASE "${CMAKE_CURRENT_SOURCE_DIR}"
  FILES
    qml/Main.qml
    qml/Style.qml
    qml/TransportStrip.qml
    qml/components/TouchButton.qml
    qml/components/TouchSlider.qml
    qml/components/MixerStrip.qml
    qml/components/MixerShiftStrip.qml
    qml/components/LearnPanel.qml
    qml/components/FxSlotPanel.qml
    qml/components/TouchComboBox.qml
    qml/components/DeviceComboBox.qml
    qml/components/ValuePopup.qml
    qml/components/TimelineWave.qml
    qml/components/PipeParamEditor.qml
    qml/screens/MixerScreen.qml
    qml/screens/RoutingScreen.qml
    qml/screens/FxScreen.qml
    qml/screens/RecorderScreen.qml
    qml/screens/SystemScreen.qml
    qml/screens/ProjectsScreen.qml
    qml/screens/MidiMonitorScreen.qml
    qml/screens/ToolsScreen.qml
    qml/screens/PunchScreen.qml
    qml/screens/MonitorScreen.qml
    qml/screens/ToolsStatusScreen.qml
    qml/screens/SystemShiftFace.qml
    qml/screens/EqScreen.qml
    qml/screens/XyScreen.qml
    qml/screens/TimelineScreen.qml
    qml/overlays/ConfigPopup.qml
    qml/overlays/Keypad.qml
    qml/overlays/ConfirmDialog.qml)

# mnky_clap links mnky_audio PUBLIC (clap::Host implements audio::IEffect), so
# libmnky_audio.a legitimately appears twice on the link line. Apple's ld calls
# that out as a warning; it is the same archive, so the note is noise.
if(APPLE)
  target_link_options(mnkybox PRIVATE "-Wl,-no_warn_duplicate_libraries")
endif()

# Default FX: the in-tree example plugin. The FX screen falls back to the
# default plugin path candidates when this compile-time path is stale.
if(TARGET mnkygain)
  add_dependencies(mnkybox mnkygain)
  target_compile_definitions(mnkybox PRIVATE
    MNKY_DEFAULT_PLUGIN="$<TARGET_FILE:mnkygain>")
  message(STATUS "mnkybox app: default FX plugin $<TARGET_FILE:mnkygain>")
else()
  message(STATUS "mnkybox app: no mnkygain target (needs the real CLAP headers); FX starts empty")
endif()

# --unittests runs the non-visual C++ assert checks; no window, audio device or
# MIDI port is opened.
if(BUILD_TESTING)
  add_test(NAME mnkybox_app_logic COMMAND mnkybox --unittests)
endif()
