# mnkybox Pi DAW — root build (owned by the setup team).
#
# Configure/build from the repo root or from pi/:
#   cmake -S midibrain/pi -B /tmp/pi-build
#   cmake --build /tmp/pi-build -j
#   ctest --test-dir /tmp/pi-build --output-on-failure
#
# The core modules (link midi audio clap app emulator) are written by parallel
# teams, so every module subdirectory is optional: a missing directory skips its
# target instead of failing configure. Build files here are the contract, not a
# manufacturing or signal-integrity gate. See README.md and ARCHITECTURE.md.

cmake_minimum_required(VERSION 3.24)

# macOS dev-machine toolchain: JUCE 9.0.5 compiles its graphics/audio code against
# the macOS SDK frameworks (ApplicationServices, CoreMIDI, ...), which a non-Apple
# g++ on PATH does not have. So on Darwin default to Apple's clang unless the
# caller (or the CC/CXX environment) already picked a compiler explicitly. The
# Pi target keeps its normal system compiler.
if(APPLE AND NOT CMAKE_C_COMPILER AND NOT CMAKE_CXX_COMPILER
   AND NOT DEFINED ENV{CC} AND NOT DEFINED ENV{CXX})
  find_program(MNKY_DARWIN_C_COMPILER clang PATHS /usr/bin NO_DEFAULT_PATH)
  find_program(MNKY_DARWIN_CXX_COMPILER clang++ PATHS /usr/bin NO_DEFAULT_PATH)
  if(MNKY_DARWIN_C_COMPILER AND MNKY_DARWIN_CXX_COMPILER)
    set(CMAKE_C_COMPILER   "${MNKY_DARWIN_C_COMPILER}"   CACHE FILEPATH "mnkybox: macOS C compiler")
    set(CMAKE_CXX_COMPILER "${MNKY_DARWIN_CXX_COMPILER}" CACHE FILEPATH "mnkybox: macOS C++ compiler")
  endif()
endif()

project(mnkybox VERSION 0.1.0 LANGUAGES CXX)

# JUCE refuses to configure unless the top-level project also enables C (its
# bundled third-party code is C). The DAW's own code stays C++20-only.
enable_language(C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Upstream JUCE 9.0.5 only includes <cstring> in juce_StandardHeader.h for
# Linux/BSD, Android and MSVC; with current libc++ (and libstdc++) nothing pulls
# it in transitively, so juce_core's memset/memmove calls fail to compile.
# Force-include the header for C++/ObjC++ everywhere rather than patching the
# pinned third-party tree. Harmless for the JUCE-free core modules.
if(APPLE)
  add_compile_options("$<$<COMPILE_LANGUAGE:CXX,OBJCXX>:SHELL:-include cstring>")
endif()

# Default to a release-ish build; the DAW is a real-time audio app, but plain
# -On defaults are fine for the prototype. Override with -DCMAKE_BUILD_TYPE=...
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

# Core modules are held to the ARCHITECTURE.md standard: zero warnings, -Werror.
# Third-party code (RtMidi, JUCE, CLAP) is never compiled with these flags.
set(MNKY_WARNINGS -Wall -Wextra -Werror)

# Defines BUILD_TESTING (default ON) before any module subdirectory is added, so
# module CMakeLists can register their own CTest entries.
include(CTest)

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# --- Pinned third-party dependencies (see third_party/README.md) --------------
# Never float on a branch: both tags are pinned exactly and fetched shallow.
set(MNKY_JUCE_VERSION 9.0.5)
set(MNKY_CLAP_VERSION 1.2.10)

FetchContent_Declare(JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG        ${MNKY_JUCE_VERSION}
  GIT_SHALLOW    ON
  GIT_PROGRESS   ON)

# CLAP is consumed as headers only (`#include <clap/clap.h>`), so the upstream
# CMake project is deliberately not added: SOURCE_SUBDIR points at a directory
# without a CMakeLists.txt, which populates the source tree and stops there.
FetchContent_Declare(clap
  GIT_REPOSITORY https://github.com/free-audio/clap.git
  GIT_TAG        ${MNKY_CLAP_VERSION}
  GIT_SHALLOW    ON
  GIT_PROGRESS   ON
  SOURCE_SUBDIR  cmake/clap-headers-only)

# JUCE bootstraps its juceaide host tool with a nested CMake configure during the
# first configure of this project. That nested build inherits the environment but
# not our cache, so export the resolved compiler and the <cstring> workaround for
# its duration, then restore the caller's environment.
set(MNKY_SAVED_ENV_CC "$ENV{CC}")
set(MNKY_SAVED_ENV_CXX "$ENV{CXX}")
set(MNKY_SAVED_ENV_CXXFLAGS "$ENV{CXXFLAGS}")
set(MNKY_SAVED_ENV_OBJCXXFLAGS "$ENV{OBJCXXFLAGS}")

set(ENV{CC} "${CMAKE_C_COMPILER}")
set(ENV{CXX} "${CMAKE_CXX_COMPILER}")
if(APPLE)
  set(ENV{CXXFLAGS} "$ENV{CXXFLAGS} -include cstring")
  set(ENV{OBJCXXFLAGS} "$ENV{OBJCXXFLAGS} -include cstring")
endif()

FetchContent_MakeAvailable(JUCE clap)

set(ENV{CC} "${MNKY_SAVED_ENV_CC}")
set(ENV{CXX} "${MNKY_SAVED_ENV_CXX}")
set(ENV{CXXFLAGS} "${MNKY_SAVED_ENV_CXXFLAGS}")
set(ENV{OBJCXXFLAGS} "${MNKY_SAVED_ENV_OBJCXXFLAGS}")
unset(MNKY_SAVED_ENV_CC)
unset(MNKY_SAVED_ENV_CXX)
unset(MNKY_SAVED_ENV_CXXFLAGS)
unset(MNKY_SAVED_ENV_OBJCXXFLAGS)

add_library(mnky_clap_headers INTERFACE)
target_include_directories(mnky_clap_headers INTERFACE "${clap_SOURCE_DIR}/include")

# clap/CMakeLists.txt (CLAP team) accepts the pinned headers through this cache
# variable; set it before the module is added so the host core and the example
# mnkygain plugin build against CLAP 1.2.10 instead of the test-only ABI stub.
if(EXISTS "${clap_SOURCE_DIR}/include/clap/clap.h")
  set(MNKY_CLAP_HEADER_DIR "${clap_SOURCE_DIR}/include" CACHE PATH "Directory containing clap/clap.h (pinned ${MNKY_CLAP_VERSION})")
endif()

# --- RtMidi: compiled once, shared by every consumer --------------------------
# RtMidi.h changes class layout with the platform backend define, so the define
# must be PUBLIC: mnky_midi and the midi tests must see the same value that
# RtMidi.cpp was compiled with.
add_library(mnky_rtmidi STATIC third_party/rtmidi/RtMidi.cpp)
target_include_directories(mnky_rtmidi PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/third_party/rtmidi")
if(APPLE)
  target_compile_definitions(mnky_rtmidi PUBLIC __MACOSX_CORE__)
  target_link_libraries(mnky_rtmidi PUBLIC
    "-framework CoreMIDI"
    "-framework CoreAudio"
    "-framework CoreFoundation"
    "-framework CoreServices")
elseif(UNIX)
  # Raspberry Pi 5 target: ALSA sequencer backend.
  target_compile_definitions(mnky_rtmidi PUBLIC __LINUX_ALSA__)
  target_link_libraries(mnky_rtmidi PUBLIC asound)
else()
  message(STATUS "mnkybox: RtMidi has no configured backend on ${CMAKE_SYSTEM_NAME}; midi targets will not link")
endif()

# --- JACK: the Pi's routable audio path --------------------------------------
# Raspberry Pi OS runs PipeWire by default (jackd during bring-up) and both are
# reached through the JACK API, which JUCE exposes as its "JACK" device type.
# JUCE dlopen()s libjack at runtime but compiles juce_JackAudio.cpp against
# <jack/jack.h>, so the headers are a build dependency on Linux only. macOS has
# no JACK here: detection is skipped silently and the app keeps its
# ALSA/CoreAudio-only path (JUCE_JACK stays 0).
set(MNKY_JACK_FOUND FALSE)
if(UNIX AND NOT APPLE)
  find_package(PkgConfig QUIET)
  if(NOT PkgConfig_FOUND)
    message(FATAL_ERROR
      "mnkybox: the Linux/Pi build needs pkg-config to locate the JACK headers.\n"
      "Install both with: sudo apt-get install -y pkg-config libjack-jackd2-dev")
  endif()

  pkg_check_modules(MNKY_JACK IMPORTED_TARGET jack)
  if(NOT MNKY_JACK_FOUND)
    message(FATAL_ERROR
      "mnkybox: the Linux/Pi build needs the JACK headers (pkg-config module 'jack'),\n"
      "because JUCE's JACK device type is compiled in for the routable audio path.\n"
      "Install them with: sudo apt-get install -y libjack-jackd2-dev\n"
      "macOS builds skip JACK detection silently; no override flag is provided.")
  endif()

  # One interface target carries include dirs, libs and the two defines, so
  # pi/app/CMakeLists.txt only has to link it when it exists.
  add_library(mnky_jack INTERFACE)
  target_link_libraries(mnky_jack INTERFACE PkgConfig::MNKY_JACK)
  target_compile_definitions(mnky_jack INTERFACE MNKY_HAVE_JACK=1 JUCE_JACK=1)
  message(STATUS "mnkybox: JACK headers found (pkg-config jack) -> mnky_jack for the app target")
endif()

# --- Core module libraries ----------------------------------------------------
# Each module is JUCE-free and owned by one team. If the team also ships a
# CMakeLists.txt in its directory, that file defines the target; otherwise the
# root globs <dir>/*.cpp (excluding test_*.cpp) into the static library named
# here. Either way the target name is fixed by ARCHITECTURE.md.
function(mnky_core_module target dir)
  if(NOT IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${dir}")
    message(STATUS "mnkybox: pi/${dir}/ not present yet — skipping ${target}")
    return()
  endif()

  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${dir}/CMakeLists.txt")
    add_subdirectory("${dir}")
    return()
  endif()

  file(GLOB module_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${dir}/*.cpp")
  list(FILTER module_sources EXCLUDE REGEX "/test_[^/]*\\.cpp$")
  if(NOT module_sources)
    message(STATUS "mnkybox: pi/${dir}/ has no sources yet — skipping ${target}")
    return()
  endif()

  add_library(${target} STATIC ${module_sources})
  target_include_directories(${target} PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/${dir}")
  target_compile_options(${target} PRIVATE ${MNKY_WARNINGS})
endfunction()

mnky_core_module(mnky_link  link)
mnky_core_module(mnky_midi  midi)
mnky_core_module(mnky_audio audio)
mnky_core_module(mnky_clap  clap)
mnky_core_module(mnky_session session)

if(TARGET mnky_midi)
  target_link_libraries(mnky_midi PUBLIC mnky_rtmidi)
endif()

if(TARGET mnky_clap)
  target_link_libraries(mnky_clap PUBLIC mnky_clap_headers)
  if(TARGET mnky_audio)
    # clap::Host implements audio::IEffect.
    target_link_libraries(mnky_clap PUBLIC mnky_audio)
  endif()
  if(NOT APPLE)
    # dlopen/dlsym for the CLAP entry point.
    target_link_libraries(mnky_clap PUBLIC ${CMAKE_DL_LIBS})
  endif()
endif()

# --- Tests (CTest) ------------------------------------------------------------
# Module tests are assert-based `test_*.cpp` mains living next to the code. A
# module with no test file simply contributes nothing; configure must never fail
# because a parallel team has not landed its tests yet. A module that already
# defines its own `test_<module>` target (some module CMakeLists do) keeps it —
# the root only builds tests that nobody else owns.
function(mnky_module_tests module_target dir)
  if(NOT IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${dir}")
    return()
  endif()

  file(GLOB test_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${dir}/test_*.cpp")
  foreach(test_source IN LISTS test_sources)
    get_filename_component(test_name "${test_source}" NAME_WE)
    if(TARGET ${test_name})
      message(STATUS "mnkybox: pi/${dir}/ defines ${test_name} itself — not re-registering it")
      continue()
    endif()

    add_executable(${test_name} "${test_source}")
    target_include_directories(${test_name} PRIVATE
      "${CMAKE_CURRENT_SOURCE_DIR}"
      "${CMAKE_CURRENT_SOURCE_DIR}/${dir}")
    # The tests are assert-based, so the asserts must stay live in every build
    # type: -DNDEBUG from RelWithDebInfo/Release would silently disable every
    # check (and then fail -Werror on locals only used by asserts).
    target_compile_options(${test_name} PRIVATE ${MNKY_WARNINGS} -UNDEBUG)
    if(TARGET ${module_target})
      target_link_libraries(${test_name} PRIVATE ${module_target})
    endif()
    if(dir STREQUAL "midi")
      # The architecture's per-module build line compiles RtMidi into the test.
      target_link_libraries(${test_name} PRIVATE mnky_rtmidi)
    endif()
    add_test(NAME ${test_name} COMMAND ${test_name})
  endforeach()
endfunction()

mnky_module_tests(mnky_link  link)
mnky_module_tests(mnky_midi  midi)
mnky_module_tests(mnky_audio audio)
mnky_module_tests(mnky_clap  clap)
mnky_module_tests(mnky_session session)

# --- JUCE compile smoke -------------------------------------------------------
# The wave-2 app/ team owns the real juce_add_gui_app target. Until it lands this
# static library proves the pinned JUCE modules (gui_basics pulls graphics/
# events/audio_basics; audio_devices provides the Pi ALSA/CoreAudio backends)
# compile with this toolchain. It deliberately does not link or run.
add_library(mnky_juce_smoke STATIC cmake/juce_compile_smoke.cpp)
target_link_libraries(mnky_juce_smoke PRIVATE
  juce::juce_gui_basics
  juce::juce_audio_devices
  juce::juce_recommended_config_flags
  juce::juce_recommended_warning_flags)
target_compile_definitions(mnky_juce_smoke PRIVATE
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_APPLICATION_NAME_STRING="mnkybox"
  JUCE_APPLICATION_VERSION_STRING="${PROJECT_VERSION}")

# --- Optional wave-2 targets --------------------------------------------------
# app/ (JUCE touchscreen UI) and emulator/ (backends + headless selftest) own
# their own CMakeLists.txt so the GUI/emulator teams can extend the build
# without editing this file.
foreach(wave2_dir app emulator)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${wave2_dir}/CMakeLists.txt")
    add_subdirectory("${wave2_dir}")
  else()
    message(STATUS "mnkybox: pi/${wave2_dir}/CMakeLists.txt not present yet — skipped")
  endif()
endforeach()

message(STATUS "mnkybox: JUCE ${MNKY_JUCE_VERSION} (${juce_SOURCE_DIR}), CLAP ${MNKY_CLAP_VERSION} (headers) configured for ${CMAKE_SYSTEM_NAME} with ${CMAKE_CXX_COMPILER_ID}")
