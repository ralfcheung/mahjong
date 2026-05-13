#pragma once
#include "game/GameState.h"
#include "tiles/TileEnums.h"
#include <cstdint>

enum class InputEventType : uint8_t {
    // Game actions
    TileTap,              // Human tapped a tile to discard
    ClaimButton,          // Human pressed a claim button
    SelfDrawWinButton,    // Human pressed self-draw win
    SelfKongButton,       // Human pressed self-kong
    AdvanceRound,         // Human pressed to advance to next round

    // Camera control
    CameraOrbit,          // Rotate camera (yaw/pitch delta)
    CameraZoom,           // Zoom camera (distance delta)
    CameraPan,            // Pan camera (XZ delta)
    CameraReset           // Reset camera to default position
};

struct InputEvent {
    InputEventType type;

    // TileTap: which tile was selected (resolved by platform hit testing)
    uint8_t tileId = 0;

    // ClaimButton: which claim action
    ClaimType claimType = ClaimType::None;
    int comboIndex = -1;  // For chow combos

    // SelfKong: which tile to kong
    Suit kongSuit = Suit::Bamboo;
    uint8_t kongRank = 0;

    // Camera deltas
    float deltaYaw = 0.0f;
    float deltaPitch = 0.0f;
    float deltaZoom = 0.0f;
    float deltaPanX = 0.0f;
    float deltaPanZ = 0.0f;

    // Factory methods for convenience
    static InputEvent tileTap(uint8_t id) {
        InputEvent e{};
        e.type = InputEventType::TileTap;
        e.tileId = id;
        return e;
    }

    static InputEvent claimButton(ClaimType ct, int combo = -1) {
        InputEvent e{};
        e.type = InputEventType::ClaimButton;
        e.claimType = ct;
        e.comboIndex = combo;
        return e;
    }

    static InputEvent selfDrawWin() {
        InputEvent e{};
        e.type = InputEventType::SelfDrawWinButton;
        return e;
    }

    static InputEvent selfKong(Suit s, uint8_t r) {
        InputEvent e{};
        e.type = InputEventType::SelfKongButton;
        e.kongSuit = s;
        e.kongRank = r;
        return e;
    }

    static InputEvent advanceRound() {
        InputEvent e{};
        e.type = InputEventType::AdvanceRound;
        return e;
    }

    static InputEvent cameraOrbit(float dYaw, float dPitch) {
        InputEvent e{};
        e.type = InputEventType::CameraOrbit;
        e.deltaYaw = dYaw;
        e.deltaPitch = dPitch;
        return e;
    }

    static InputEvent cameraZoom(float dZoom) {
        InputEvent e{};
        e.type = InputEventType::CameraZoom;
        e.deltaZoom = dZoom;
        return e;
    }

    static InputEvent cameraPan(float dx, float dz) {
        InputEvent e{};
        e.type = InputEventType::CameraPan;
        e.deltaPanX = dx;
        e.deltaPanZ = dz;
        return e;
    }

    static InputEvent cameraReset() {
        InputEvent e{};
        e.type = InputEventType::CameraReset;
        return e;
    }
};
