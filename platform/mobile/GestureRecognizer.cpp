#include "GestureRecognizer.h"
#include "raylib.h"
#include <cmath>

std::vector<InputEvent> GestureRecognizer::update(float dt) {
    std::vector<InputEvent> events;
    totalTime_ += dt;

    int touchCount = GetTouchPointCount();

    // Update touch positions
    for (int i = 0; i < 2; i++) {
        touches_[i].prevX = touches_[i].x;
        touches_[i].prevY = touches_[i].y;
        if (i < touchCount) {
            Vector2 pos = GetTouchPosition(i);
            touches_[i].x = pos.x;
            touches_[i].y = pos.y;
            if (!touches_[i].active) {
                // New touch - initialize prev to current
                touches_[i].prevX = pos.x;
                touches_[i].prevY = pos.y;
            }
            touches_[i].active = true;
        } else {
            touches_[i].active = false;
        }
    }

    // --- Two-finger gestures ---
    if (touchCount == 2) {
        float dx0 = touches_[0].x - touches_[0].prevX;
        float dy0 = touches_[0].y - touches_[0].prevY;
        float dx1 = touches_[1].x - touches_[1].prevX;
        float dy1 = touches_[1].y - touches_[1].prevY;

        // Current and previous distance between fingers
        float curDist = sqrtf(
            (touches_[1].x - touches_[0].x) * (touches_[1].x - touches_[0].x) +
            (touches_[1].y - touches_[0].y) * (touches_[1].y - touches_[0].y));

        if (!pinchActive_) {
            prevPinchDist_ = curDist;
            pinchActive_ = true;
        }

        float pinchDelta = curDist - prevPinchDist_;
        prevPinchDist_ = curDist;

        // Pinch → zoom
        if (fabsf(pinchDelta) > 1.0f) {
            events.push_back(InputEvent::cameraZoom(pinchDelta * 0.05f));
        }

        // Average finger movement → pan or orbit
        float avgDx = (dx0 + dx1) * 0.5f;
        float avgDy = (dy0 + dy1) * 0.5f;

        // Cross product of finger movements: if fingers move in opposite rotational direction → orbit
        float cross = dx0 * dy1 - dy0 * dx1;
        if (fabsf(cross) > 5.0f) {
            // Two-finger rotate → orbit
            events.push_back(InputEvent::cameraOrbit(-avgDx * 0.3f, -avgDy * 0.3f));
        } else if (fabsf(avgDx) > 1.0f || fabsf(avgDy) > 1.0f) {
            // Two-finger drag → pan
            events.push_back(InputEvent::cameraPan(-avgDx * 0.02f, -avgDy * 0.02f));
        }
    } else {
        pinchActive_ = false;
    }

    // --- Single-finger tap detection ---
    if (touchCount == 0 && prevTouchCount_ == 1) {
        // Finger just lifted - this is a tap
        float tapX = touches_[0].prevX;
        float tapY = touches_[0].prevY;

        // Check for double-tap → camera reset
        if (lastTapTime_ > 0) {
            float elapsed = totalTime_ - lastTapTime_;
            float dist = sqrtf((tapX - lastTapX_) * (tapX - lastTapX_) +
                               (tapY - lastTapY_) * (tapY - lastTapY_));
            if (elapsed < DOUBLE_TAP_TIME && dist < DOUBLE_TAP_DIST) {
                events.push_back(InputEvent::cameraReset());
                lastTapTime_ = -1.0f;
                prevTouchCount_ = touchCount;
                return events;
            }
        }
        lastTapTime_ = totalTime_;
        lastTapX_ = tapX;
        lastTapY_ = tapY;

        // Two-tap discard flow
        if (tappedTileIndex_ >= 0) {
            if (tappedTileIndex_ == selectedTileIndex_) {
                // Second tap on same tile → confirm discard
                events.push_back(InputEvent::tileTap(tappedTileId_));
                selectedTileIndex_ = -1;
            } else {
                // Tap on different tile → change selection
                selectedTileIndex_ = tappedTileIndex_;
                selectedTileId_ = tappedTileId_;
            }
        } else {
            // Tapped empty space → clear selection
            selectedTileIndex_ = -1;
        }

        tappedTileIndex_ = -1;
        tappedTileId_ = 0;
    }

    prevTouchCount_ = touchCount;
    return events;
}
