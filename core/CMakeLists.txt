cmake_minimum_required(VERSION 3.14)

# libmahjong-core: pure C++17 game logic, zero external dependencies
# No Raylib, no libtorch

set(CORE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/GameController.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/GameRecorder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/mahjong_api.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/game/TurnManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/tiles/Wall.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/player/Hand.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/player/HumanPlayer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/player/AIPlayer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/scoring/Scoring.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/scoring/WinDetector.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/scoring/PaymentCalculator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ai/ShantenCalculator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ai/HandEvaluator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ai/CooperativeStrategy.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ai/InferenceEngine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ai/AdaptiveEngine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ai/HandSampler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ai/RoundSimulator.cpp
)

set(CORE_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/GameController.h
    ${CMAKE_CURRENT_SOURCE_DIR}/GameSnapshot.h
    ${CMAKE_CURRENT_SOURCE_DIR}/mahjong_api.h
)

add_library(mahjong-core STATIC ${CORE_SOURCES} ${CORE_HEADERS})

target_include_directories(mahjong-core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

target_compile_features(mahjong-core PUBLIC cxx_std_17)

if(HAS_TORCH)
    target_link_libraries(mahjong-core PRIVATE "${TORCH_LIBRARIES}")
    target_include_directories(mahjong-core PRIVATE "${TORCH_INCLUDE_DIRS}")
    target_compile_definitions(mahjong-core PRIVATE HAS_TORCH=1)
endif()

# Shared library for MCP server / ctypes binding
set(SHARED_SOURCES ${CORE_SOURCES})
if(HAS_TORCH)
    list(APPEND SHARED_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/ai/MahjongNet.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/ai/DQNTrainer.cpp
    )
endif()

add_library(mahjong-shared SHARED ${SHARED_SOURCES} ${CORE_HEADERS})

target_include_directories(mahjong-shared PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

target_compile_features(mahjong-shared PUBLIC cxx_std_17)
set_target_properties(mahjong-shared PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(HAS_TORCH)
    target_link_libraries(mahjong-shared PRIVATE "${TORCH_LIBRARIES}")
    target_include_directories(mahjong-shared PRIVATE "${TORCH_INCLUDE_DIRS}")
    target_compile_definitions(mahjong-shared PRIVATE HAS_TORCH=1)
endif()
