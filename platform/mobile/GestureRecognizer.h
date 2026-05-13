#pragma once
#include "InputEvent.h"
#include <vector>
#include <cstdint>

// Multi-touch gesture detection for mobile platforms.
// Uses Raylib's touch API (GetTouchPointCount, GetTouchPosition).
// Produces InputEvents for camera control and tile interaction.
//
// Gesture mapping (mobile):
//   One finger tap          → TileTap / ClaimButton / AdvanceRound
//   Two-finger rotate       → CameraOrbit
//   Pinch                   → CameraZoom
//   Two-finger drag         → CameraPan
//   Double-tap              → CameraReset
//
// Tile discard uses two-tap confirmation:
//   First tap  → selects/highlights tile
//   Second tap → confirms discard (same tile)
//   Tap elsewhere → changes selection

class GestureRecognizer {
public:
    // Call once per frame with current touch state.
    // Returns list of InputEvents detected this frame.
    std::vector<InputEvent> update(float dt);

    // Currently selected tile index (-1 = none).
    // Used by renderer to highlight the selected tile.
    int selectedTileIndex() const { return selectedTileIndex_; }
    void clearSelection() { selectedTileIndex_ = -1; }

    // Set the tile index that a screen tap resolved to (via raycasting).
    // Called by the platform layer after hit testing.
    void setTappedTileIndex(int index) { tappedTileIndex_ = index; }
    void setTappedTileId(uint8_t id) { tappedTileId_ = id; }

    // Directly update the selected tile (for platform layers that bypass update()).
    void selectTile(int index, uint8_t id) { selectedTileIndex_ = index; selectedTileId_ = id; }

private:
    // Touch tracking
    struct TouchPoint {
        float x = 0, y = 0;
        float prevX = 0, prevY = 0;
        bool active = false;
    };
    TouchPoint touches_[2];  // Track up to 2 simultaneous touches
    int prevTouchCount_ = 0;

    // Two-tap discard state
    int selectedTileIndex_ = -1;
    uint8_t selectedTileId_ = 0;
    int tappedTileIndex_ = -1;
    uint8_t tappedTileId_ = 0;

    // Double-tap detection
    float lastTapTime_ = -1.0f;
    float lastTapX_ = 0, lastTapY_ = 0;
    static constexpr float DOUBLE_TAP_TIME = 0.35f;   // seconds
    static constexpr float DOUBLE_TAP_DIST = 30.0f;    // pixels

    // Pinch state
    float prevPinchDist_ = 0.0f;
    bool pinchActive_ = false;

    float totalTime_ = 0.0f;
};
