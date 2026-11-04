# mnkybox session core: versioned JSON save/load (JUCE-free, portable C++20).
#
# Standalone build:
#   cmake -S midibrain/pi/session -B /tmp/mnky_session_build
#   cmake --build /tmp/mnky_session_build && ctest --test-dir /tmp/mnky_session_build
#
# In the root pi build this directory only defines mnky_session; pi/CMakeLists.txt
# owns test registration through mnky_module_tests() when it adds the module.
cmake_minimum_required(VERSION 3.20)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(mnky_session LANGUAGES CXX)
  enable_testing()
  set(MNKY_SESSION_STANDALONE ON)
else()
  set(MNKY_SESSION_STANDALONE OFF)
endif()

# The root build defines MNKY_WARNINGS; a standalone build uses the same set.
if(NOT MNKY_WARNINGS)
  set(MNKY_WARNINGS -Wall -Wextra -Werror)
endif()

add_library(mnky_session STATIC session.cpp)
target_include_directories(mnky_session PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(mnky_session PUBLIC cxx_std_20)
target_compile_options(mnky_session PRIVATE ${MNKY_WARNINGS})

# Root build: link the core libraries it already provides. Standalone: compile
# only the dependency translation units the session format reads.
if(TARGET mnky_audio)
  set(MNKY_SESSION_AUDIO mnky_audio)
else()
  # engine.cpp references TakePlayer/ClickSource: keep every audio TU the
  # engine links against in this fallback list.
  add_library(mnky_session_audio STATIC ../audio/engine.cpp
                  ../audio/take_player.cpp ../audio/click.cpp)
  target_include_directories(mnky_session_audio PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../audio)
  target_compile_features(mnky_session_audio PUBLIC cxx_std_20)
  target_compile_options(mnky_session_audio PRIVATE ${MNKY_WARNINGS})
  set(MNKY_SESSION_AUDIO mnky_session_audio)
endif()

if(TARGET mnky_midi)
  set(MNKY_SESSION_MIDI mnky_midi)
else()
  add_library(mnky_session_midi STATIC ../midi/router.cpp)
  target_include_directories(mnky_session_midi PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../midi)
  target_compile_features(mnky_session_midi PUBLIC cxx_std_20)
  target_compile_options(mnky_session_midi PRIVATE ${MNKY_WARNINGS})
  set(MNKY_SESSION_MIDI mnky_session_midi)
endif()

target_link_libraries(mnky_session PUBLIC
  ${MNKY_SESSION_AUDIO}
  ${MNKY_SESSION_MIDI})

if(MNKY_SESSION_STANDALONE)
  add_executable(test_session test_session.cpp)
  # Asserts must stay live even if the caller configures a Release build.
  target_compile_options(test_session PRIVATE ${MNKY_WARNINGS} -UNDEBUG)
  target_link_libraries(test_session PRIVATE mnky_session)
  add_test(NAME test_session COMMAND test_session)
endif()
