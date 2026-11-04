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
  src/engine/modules_fx.cpp)
set(UI_SOURCES src/ui/ui.cpp src/ui/session.cpp)

if(TIMBIENT_TESTS)
  add_executable(timbient_render tests/render.cpp ${ENGINE_SOURCES})
  add_executable(timbient_shot tests/shot.cpp ${ENGINE_SOURCES} ${UI_SOURCES})
  add_executable(timbient_allmods tests/allmods.cpp ${ENGINE_SOURCES} ${UI_SOURCES})
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

  set(APP_NAME "Timbient Modules")
  if(APPLE)
    set(MACOSX_BUNDLE_ICON_FILE timbient.icns)
    set_source_files_properties(assets/timbient.icns PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    add_executable(timbient MACOSX_BUNDLE src/app/main.cpp ${ENGINE_SOURCES} ${UI_SOURCES} assets/timbient.icns)
    configure_file(assets/Info.plist.in ${CMAKE_BINARY_DIR}/Info.plist)
    set_target_properties(timbient PROPERTIES OUTPUT_NAME "${APP_NAME}" MACOSX_BUNDLE_INFO_PLIST ${CMAKE_BINARY_DIR}/Info.plist)
    target_link_libraries(timbient PRIVATE "-framework CoreAudio" "-framework AudioToolbox" "-framework AudioUnit" "-framework CoreFoundation")
    target_compile_definitions(timbient PRIVATE MA_NO_RUNTIME_LINKING)
  elseif(WIN32)
    add_executable(timbient WIN32 src/app/main.cpp ${ENGINE_SOURCES} ${UI_SOURCES} assets/timbient.rc)
    set_target_properties(timbient PROPERTIES OUTPUT_NAME "Timbient Modules")
    target_compile_definitions(timbient PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)   # stop Windows headers redefining min and max
  else()
    add_executable(timbient src/app/main.cpp ${ENGINE_SOURCES} ${UI_SOURCES})
    set_target_properties(timbient PROPERTIES OUTPUT_NAME "timbient-modules")
    target_link_libraries(timbient PRIVATE pthread dl m)
  endif()
  target_include_directories(timbient PRIVATE ${miniaudio_SOURCE_DIR})
  target_link_libraries(timbient PRIVATE SDL3::SDL3-static)
endif()
