cmake_minimum_required(VERSION 3.20)
project(GDClientCatalog LANGUAGES C CXX)

if(NOT WIN32)
  message(FATAL_ERROR "The standalone Cocos catalog app currently targets Windows.")
endif()

include(FetchContent)
FetchContent_Declare(
  cocos2dx
  GIT_REPOSITORY https://github.com/cocos2d/cocos2d-x.git
  GIT_TAG v4
  GIT_SHALLOW TRUE
  GIT_SUBMODULES ""
)
FetchContent_GetProperties(cocos2dx)
if(NOT cocos2dx_POPULATED)
  FetchContent_Populate(cocos2dx)
endif()

set(COCOS2DX_ROOT_PATH "${cocos2dx_SOURCE_DIR}")
if(NOT EXISTS "${COCOS2DX_ROOT_PATH}/external/CMakeLists.txt")
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${COCOS2DX_ROOT_PATH}/download-deps.py" -r no
    WORKING_DIRECTORY "${COCOS2DX_ROOT_PATH}"
    RESULT_VARIABLE COCOS_DOWNLOAD_RESULT
    OUTPUT_VARIABLE COCOS_DOWNLOAD_OUTPUT
    ERROR_VARIABLE COCOS_DOWNLOAD_ERROR
  )
  if(NOT COCOS_DOWNLOAD_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to download the Cocos third-party libraries.\n${COCOS_DOWNLOAD_OUTPUT}\n${COCOS_DOWNLOAD_ERROR}")
  endif()
endif()

if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(WARNING "The bundled Cocos third-party libraries in this workspace are Win32-oriented. Build with -A Win32 for a reliable standalone app build.")
endif()

set(CMAKE_MODULE_PATH "${COCOS2DX_ROOT_PATH}/cmake/Modules")
set(ENGINE_BINARY_PATH "${CMAKE_CURRENT_BINARY_DIR}/engine")
set(BUILD_LUA_LIBS OFF CACHE BOOL "Disable unused Cocos Lua bindings" FORCE)
set(BUILD_EDITOR_COCOSTUDIO OFF CACHE BOOL "Disable unused Cocos Studio support" FORCE)
set(BUILD_EDITOR_SPINE OFF CACHE BOOL "Disable unused Cocos Spine support" FORCE)
set(BUILD_EXTENSIONS OFF CACHE BOOL "Disable unused Cocos extensions" FORCE)

include(CocosBuildSet)
add_subdirectory("${COCOS2DX_ROOT_PATH}/cocos" "${ENGINE_BINARY_PATH}/cocos/core")
if(MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 8)
  target_compile_options(cocos2d PRIVATE
    "/FI${CMAKE_CURRENT_SOURCE_DIR}/app/win32_compat.h"
  )
endif()

add_executable(GDClientCatalog WIN32
  app/main.cpp
  app/CatalogScene.cpp
  app/CatalogScene.h
)
target_compile_features(GDClientCatalog PRIVATE cxx_std_17)
target_include_directories(GDClientCatalog PRIVATE
  "${COCOS2DX_ROOT_PATH}/external/json"
)
target_link_libraries(GDClientCatalog PRIVATE cocos2d)

if(MSVC)
  target_compile_options(GDClientCatalog PRIVATE /W4 /permissive- /EHsc /utf-8)
endif()

set(COCOS_RUNTIME_DLLS
  "${COCOS2DX_ROOT_PATH}/external/win32-specific/gles/prebuilt/glew32.dll"
  "${COCOS2DX_ROOT_PATH}/external/win32-specific/icon/prebuilt/iconv.dll"
  "${COCOS2DX_ROOT_PATH}/external/win32-specific/OpenalSoft/prebuilt/OpenAL32.dll"
  "${COCOS2DX_ROOT_PATH}/external/win32-specific/MP3Decoder/prebuilt/libmpg123.dll"
  "${COCOS2DX_ROOT_PATH}/external/win32-specific/OggDecoder/prebuilt/libogg.dll"
  "${COCOS2DX_ROOT_PATH}/external/win32-specific/OggDecoder/prebuilt/libvorbis.dll"
  "${COCOS2DX_ROOT_PATH}/external/win32-specific/OggDecoder/prebuilt/libvorbisfile.dll"
  "${COCOS2DX_ROOT_PATH}/external/win32-specific/OggDecoder/prebuilt/ogg.dll"
  "${COCOS2DX_ROOT_PATH}/external/win32-specific/OggDecoder/prebuilt/vorbis.dll"
  "${COCOS2DX_ROOT_PATH}/external/win32-specific/OggDecoder/prebuilt/vorbisfile.dll"
  "${COCOS2DX_ROOT_PATH}/external/openssl/prebuilt/win32/libcrypto-1_1.dll"
  "${COCOS2DX_ROOT_PATH}/external/openssl/prebuilt/win32/libssl-1_1.dll"
  "${COCOS2DX_ROOT_PATH}/external/curl/prebuilt/win32/libcurl.dll"
  "${COCOS2DX_ROOT_PATH}/external/uv/prebuilt/win32/uv.dll"
  "${COCOS2DX_ROOT_PATH}/external/websockets/prebuilt/win32/websockets.dll"
  "${COCOS2DX_ROOT_PATH}/external/zlib/prebuilt/win32/zlib1.dll"
)

add_custom_command(TARGET GDClientCatalog POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/../data/features.json"
    "$<TARGET_FILE_DIR:GDClientCatalog>/features.json"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${COCOS2DX_ROOT_PATH}/external/win32-specific/gles/prebuilt/glew32.dll"
    "$<TARGET_FILE_DIR:GDClientCatalog>/glew32.dll"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${COCOS2DX_ROOT_PATH}/external/win32-specific/icon/prebuilt/iconv.dll"
    "$<TARGET_FILE_DIR:GDClientCatalog>/iconv.dll"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${COCOS2DX_ROOT_PATH}/external/win32-specific/OpenalSoft/prebuilt/OpenAL32.dll"
    "$<TARGET_FILE_DIR:GDClientCatalog>/OpenAL32.dll"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${COCOS2DX_ROOT_PATH}/external/win32-specific/MP3Decoder/prebuilt/libmpg123.dll"
    "$<TARGET_FILE_DIR:GDClientCatalog>/libmpg123.dll"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${COCOS2DX_ROOT_PATH}/external/win32-specific/OggDecoder/prebuilt/libogg.dll"
    "$<TARGET_FILE_DIR:GDClientCatalog>/libogg.dll"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${COCOS2DX_ROOT_PATH}/external/win32-specific/OggDecoder/prebuilt/libvorbis.dll"
    "$<TARGET_FILE_DIR:GDClientCatalog>/libvorbis.dll"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${COCOS2DX_ROOT_PATH}/external/win32-specific/OggDecoder/prebuilt/libvorbisfile.dll"
    "$<TARGET_FILE_DIR:GDClientCatalog>/libvorbisfile.dll"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${COCOS2DX_ROOT_PATH}/external/win32-specific/OggDecoder/prebuilt/ogg.dll"
    "$<TARGET_FILE_DIR:GDClientCatalog>/ogg.dll"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${COCOS2DX_ROOT_PATH}/external/win32-specific/OggDecoder/prebuilt/vorbis.dll"
    "$<TARGET_FILE_DIR:GDClientCatalog>/vorbis.dll"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${COCOS2DX_ROOT_PATH}/external/win32-specific/OggDecoder/prebuilt/vorbisfile.dll"
    "$<TARGET_FILE_DIR:GDClientCatalog>/vorbisfile.dll"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${COCOS2DX_ROOT_PATH}/external/openssl/prebuilt/win32/libcrypto-1_1.dll"
    "$<TARGET_FILE_DIR:GDClientCatalog>/libcrypto-1_1.dll"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${COCOS2DX_ROOT_PATH}/external/openssl/prebuilt/win32/libssl-1_1.dll"
    "$<TARGET_FILE_DIR:GDClientCatalog>/libssl-1_1.dll"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${COCOS2DX_ROOT_PATH}/external/curl/prebuilt/win32/libcurl.dll"
    "$<TARGET_FILE_DIR:GDClientCatalog>/libcurl.dll"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${COCOS2DX_ROOT_PATH}/external/uv/prebuilt/win32/uv.dll"
    "$<TARGET_FILE_DIR:GDClientCatalog>/uv.dll"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${COCOS2DX_ROOT_PATH}/external/websockets/prebuilt/win32/websockets.dll"
    "$<TARGET_FILE_DIR:GDClientCatalog>/websockets.dll"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${COCOS2DX_ROOT_PATH}/external/zlib/prebuilt/win32/zlib1.dll"
    "$<TARGET_FILE_DIR:GDClientCatalog>/zlib1.dll"
)
