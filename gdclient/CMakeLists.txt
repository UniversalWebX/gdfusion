cmake_minimum_required(VERSION 3.20)
project(GDClient LANGUAGES C CXX)

if(NOT WIN32)
  message(FATAL_ERROR "GDClient is a Windows-only injected DLL.")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "Configure an x64 build.")
endif()

option(GDCLIENT_ENABLE_GAME_HOOKS "Enable strictly version-guarded native Geometry Dash hooks" ON)

add_library(GDClient SHARED
  src/GDClient.cpp
  src/native_game_hooks.cpp
)
target_compile_features(GDClient PRIVATE cxx_std_17)
target_compile_definitions(GDClient PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
target_link_libraries(GDClient PRIVATE gdi32 gdiplus winmm user32 shell32)

if(GDCLIENT_ENABLE_GAME_HOOKS)
  include(FetchContent)
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build MinHook as a static library" FORCE)
  FetchContent_Declare(
    minhook
    URL https://github.com/TsudaKageyu/minhook/archive/refs/tags/v1.3.4.zip
  )
  FetchContent_MakeAvailable(minhook)
  target_compile_definitions(GDClient PRIVATE GDCLIENT_ENABLE_GAME_HOOKS=1)
  target_link_libraries(GDClient PRIVATE minhook)
else()
  target_compile_definitions(GDClient PRIVATE GDCLIENT_ENABLE_GAME_HOOKS=0)
endif()

if(MSVC)
  target_compile_options(GDClient PRIVATE /W4 /permissive- /EHsc /utf-8)
  target_link_options(GDClient PRIVATE /INCREMENTAL:NO)
endif()
set_target_properties(GDClient PROPERTIES OUTPUT_NAME "GDClient")
