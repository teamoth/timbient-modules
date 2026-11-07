cmake_minimum_required(VERSION 3.20)
if(APPLE)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Oldest macOS this runs on")
endif()
project(TimbientModules VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
  add_compile_options(/utf-8)
  # one C runtime, built in, for everything (SDL included): nothing extra to install on Windows
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()
option(TIMBIENT_APP "Build the app (needs SDL3 and miniaudio, fetched automatically)" ON)
option(TIMBIENT_TESTS "Build the offline test programs" OFF)

# the engine and interface: plain C++, no libraries needed
set(ENGINE_SOURCES
  src/engine/engine.cpp
  src/engine/modules_trig.cpp
  src/engine/modules_voice.cpp
  src/engine/modules_voice2.cpp
  src/engine/modules_fx2.cpp
  src/engine/modules_trig2.cpp
  src/engine/modules_audio.cpp
  src/engine/modules_midi.cpp
  src/engine/modules_fx.cpp)
set(UI_SOURCES src/ui/ui.cpp src/ui/session.cpp src/ui/browser.cpp)

# Mutable Instruments' Plaits (MIT licence, see third_party/mutable/LICENSE): the MI VOICES module
file(GLOB_RECURSE MUTABLE_SOURCES third_party/mutable/plaits/*.cc third_party/mutable/stmlib/*.cc)
add_library(mutable STATIC ${MUTABLE_SOURCES})
target_include_directories(mutable PUBLIC third_party/mutable)
target_compile_definitions(mutable PRIVATE TEST)   # Plaits' "computer, not its own chip" build
if(MSVC)
  target_compile_options(mutable PRIVATE /W0 "/FI${CMAKE_SOURCE_DIR}/third_party/mutable/msvc_compat.h")
else()
  target_compile_options(mutable PRIVATE -w)
endif()
list(APPEND ENGINE_SOURCES src/engine_mi/modules_mi.cpp)

if(TIMBIENT_TESTS)
  add_executable(timbient_render tests/render.cpp ${ENGINE_SOURCES})
  add_executable(timbient_shot tests/shot.cpp ${ENGINE_SOURCES} ${UI_SOURCES})
  add_executable(timbient_allmods tests/allmods.cpp ${ENGINE_SOURCES} ${UI_SOURCES})
  add_executable(timbient_micost tests/micost.cpp ${ENGINE_SOURCES} ${UI_SOURCES})
  foreach(t timbient_render timbient_shot timbient_allmods timbient_micost)
    target_link_libraries(${t} PRIVATE mutable)
  endforeach()
endif()

if(TIMBIENT_APP)
  include(FetchContent)
  # SDL3: the window, drawing and input
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  set(SDL_AUDIO OFF CACHE BOOL "" FORCE)   # sound goes through miniaudio instead
  FetchContent_Declare(SDL3 URL https://github.com/libsdl-org/SDL/releases/download/release-3.4.2/SDL3-3.4.2.tar.gz)
  FetchContent_MakeAvailable(SDL3)
  # miniaudio: one header, straight to CoreAudio / WASAPI / ALSA / PulseAudio
  FetchContent_Declare(miniaudio GIT_REPOSITORY https://github.com/mackron/miniaudio.git GIT_TAG 0.11.22 GIT_SHALLOW TRUE SOURCE_SUBDIR not-a-cmake-project)
  FetchContent_MakeAvailable(miniaudio)
  # RtMidi: MIDI devices through CoreMIDI / WinMM / ALSA (built straight into the app)
  FetchContent_Declare(rtmidi GIT_REPOSITORY https://github.com/thestk/rtmidi.git GIT_TAG 6.0.0 GIT_SHALLOW TRUE SOURCE_SUBDIR not-a-cmake-project)
  FetchContent_MakeAvailable(rtmidi)

  set(APP_NAME "Timbient Modules")
  if(APPLE)
    set(MACOSX_BUNDLE_ICON_FILE timbient.icns)
    set_source_files_properties(assets/timbient.icns PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    add_executable(timbient MACOSX_BUNDLE src/app/main.cpp src/app/audio_files.cpp src/app/midi.cpp ${rtmidi_SOURCE_DIR}/RtMidi.cpp ${ENGINE_SOURCES} ${UI_SOURCES} assets/timbient.icns)
    target_link_libraries(timbient PRIVATE mutable)
    configure_file(assets/Info.plist.in ${CMAKE_BINARY_DIR}/Info.plist)
    set_target_properties(timbient PROPERTIES OUTPUT_NAME "${APP_NAME}" MACOSX_BUNDLE_INFO_PLIST ${CMAKE_BINARY_DIR}/Info.plist)
    target_link_libraries(timbient PRIVATE "-framework CoreAudio" "-framework AudioToolbox" "-framework AudioUnit" "-framework CoreFoundation" "-framework CoreMIDI" "-framework CoreServices")
    target_compile_definitions(timbient PRIVATE __MACOSX_CORE__)
    target_compile_definitions(timbient PRIVATE MA_NO_RUNTIME_LINKING)
  elseif(WIN32)
    add_executable(timbient WIN32 src/app/main.cpp src/app/audio_files.cpp src/app/midi.cpp ${rtmidi_SOURCE_DIR}/RtMidi.cpp ${ENGINE_SOURCES} ${UI_SOURCES} assets/timbient.rc)
    target_link_libraries(timbient PRIVATE mutable)
    set_target_properties(timbient PROPERTIES OUTPUT_NAME "Timbient Modules")
    target_compile_definitions(timbient PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN __WINDOWS_MM__)   # NOMINMAX: stop Windows headers redefining min and max
    target_link_libraries(timbient PRIVATE winmm)
  else()
    add_executable(timbient src/app/main.cpp src/app/audio_files.cpp src/app/midi.cpp ${rtmidi_SOURCE_DIR}/RtMidi.cpp ${ENGINE_SOURCES} ${UI_SOURCES})
    target_link_libraries(timbient PRIVATE mutable)
    set_target_properties(timbient PROPERTIES OUTPUT_NAME "timbient-modules")
    target_link_libraries(timbient PRIVATE pthread dl m asound)
    target_compile_definitions(timbient PRIVATE __LINUX_ALSA__)
  endif()
  target_include_directories(timbient PRIVATE ${miniaudio_SOURCE_DIR} ${rtmidi_SOURCE_DIR})
  target_link_libraries(timbient PRIVATE SDL3::SDL3-static)
endif()
