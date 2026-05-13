cmake_minimum_required(VERSION 3.14)
project(mahjong-native CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Root of the mahjong project
set(MAHJONG_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../../..)

# --- Build core library ---
add_subdirectory(${MAHJONG_ROOT}/core ${CMAKE_CURRENT_BINARY_DIR}/core_build)

# --- Fetch and build raylib for Android ---
include(FetchContent)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
set(OPENGL_VERSION "ES 2.0" CACHE STRING "" FORCE)

FetchContent_Declare(
    raylib
    GIT_REPOSITORY "https://github.com/raysan5/raylib.git"
    GIT_TAG "5.5"
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(raylib)

# --- JNI native library ---
add_library(mahjong-native SHARED
    mahjong_jni.cpp
    AndroidAssetResolver.cpp
    # Renderer sources (shared with desktop)
    ${MAHJONG_ROOT}/src/render/Renderer.cpp
    ${MAHJONG_ROOT}/src/render/TileRenderer.cpp
    ${MAHJONG_ROOT}/src/render/TileTextureAtlas.cpp
    # GestureRecognizer
    ${MAHJONG_ROOT}/platform/mobile/GestureRecognizer.cpp
)

target_include_directories(mahjong-native PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${MAHJONG_ROOT}/src
    ${MAHJONG_ROOT}/core
    ${MAHJONG_ROOT}/platform
)

target_link_libraries(mahjong-native
    mahjong-core
    raylib
    android
    log
    EGL
    GLESv2
)
