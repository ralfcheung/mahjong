cmake_minimum_required(VERSION 3.14)
project(mahjong CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- libtorch (optional, must run before add_subdirectory(core)) ---
# Set default CMAKE_PREFIX_PATH to third_party/libtorch if it exists
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libtorch")
    list(APPEND CMAKE_PREFIX_PATH "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libtorch")
endif()

find_package(Torch QUIET)
if(Torch_FOUND)
    message(STATUS "libtorch found: ${TORCH_LIBRARIES}")
    set(HAS_TORCH ON)
else()
    message(STATUS "libtorch not found - building without neural network AI")
    message(STATUS "Run ./scripts/download_libtorch.sh to enable neural network features")
    set(HAS_TORCH OFF)
endif()

# --- Build core library (pure C++, no external deps for static; torch for shared) ---
add_subdirectory(core)

# --- Fetch raylib ---
include(FetchContent)
set(FETCHCONTENT_QUIET FALSE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES    OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    raylib
    GIT_REPOSITORY "https://github.com/raysan5/raylib.git"
    GIT_TAG "5.5"
    GIT_PROGRESS TRUE
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(raylib)

# --- Desktop app sources (Raylib-dependent only) ---
set(DESKTOP_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/game/Game.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/render/Renderer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/render/TileRenderer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/render/TileTextureAtlas.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/net/HttpUploader.cpp
)

# Add libtorch-dependent sources only when available
if(HAS_TORCH)
    list(APPEND DESKTOP_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/src/ai/MahjongNet.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/ai/DQNTrainer.cpp
    )
endif()

# --- Executable ---
add_executable(${PROJECT_NAME} ${DESKTOP_SOURCES})
target_include_directories(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(${PROJECT_NAME} PRIVATE mahjong-core raylib)

if(HAS_TORCH)
    target_link_libraries(${PROJECT_NAME} PRIVATE "${TORCH_LIBRARIES}")
    target_include_directories(${PROJECT_NAME} PRIVATE "${TORCH_INCLUDE_DIRS}")
    set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 17)
endif()

# --- Assemble tile assets (pick newest version per image into live/) ---
add_custom_target(assemble-tiles
    COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/scripts/assemble_tile_assets.sh"
                 "${CMAKE_CURRENT_SOURCE_DIR}/assets/tiles"
    COMMENT "Assembling tile assets from version folders into live/"
)
add_dependencies(${PROJECT_NAME} assemble-tiles)

# --- Assets path macro ---
target_compile_definitions(${PROJECT_NAME} PRIVATE
    ASSETS_PATH="${CMAKE_CURRENT_SOURCE_DIR}/assets/"
)

if(HAS_TORCH)
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAS_TORCH=1)
endif()

# --- Test: atlas tile mapping ---
add_executable(test_atlas_tiles
    tests/test_atlas_tiles.cpp
    src/render/TileTextureAtlas.cpp
)
target_include_directories(test_atlas_tiles PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/core"
)
target_link_libraries(test_atlas_tiles PRIVATE raylib)
target_compile_definitions(test_atlas_tiles PRIVATE
    ASSETS_PATH="${CMAKE_CURRENT_SOURCE_DIR}/assets/"
)
add_dependencies(test_atlas_tiles assemble-tiles)

# --- Test: scoring ---
add_executable(test_scoring tests/test_scoring.cpp)
target_include_directories(test_scoring PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/core"
)
target_link_libraries(test_scoring PRIVATE mahjong-core)
