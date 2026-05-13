cmake_minimum_required(VERSION 3.14)
project(MahjongHK C CXX OBJCXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Build core library ---
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../core core_build)

# --- mahjong-ios: thin wrapper around mahjong-core ---
# Rendering is now done in Swift via Metal — no more Renderer.cpp or Raylib.
# Touch handling is in Swift — no more GestureRecognizer.
# GameBridge.mm, GameSnapshotBridge.mm, IOSAssetResolver.cpp,
# NSURLSessionUploader.mm, and all Swift sources are compiled by Xcode directly
# (via PBXFileSystemSynchronizedRootGroup).
#
# This CMake target just exposes mahjong-core with the right include paths
# so Xcode's ObjC++ files can #include the C++ headers.

add_library(mahjong-ios INTERFACE)

target_include_directories(mahjong-ios INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/MahjongHK
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../core
    ${CMAKE_CURRENT_SOURCE_DIR}/../platform
)

target_link_libraries(mahjong-ios INTERFACE mahjong-core)
