#import "RaylibBridge.h"

#include "raylib.h"
#include "GameController.h"
#include "GameRecorder.h"
#include "NSURLSessionUploader.h"
#include "AssetResolver.h"
#include "IOSAssetResolver.h"
#include "InputEvent.h"
#include "render/Renderer.h"
#include "ai/InferenceEngine.h"
#include "player/AIPlayer.h"
#include "platform/mobile/GestureRecognizer.h"
#include "mahjong_api.h"

#include <memory>
#include <string>

@implementation RaylibBridge {
    std::unique_ptr<GameController> _controller;
    std::unique_ptr<IOSAssetResolver> _assets;
    std::unique_ptr<InferenceEngine> _inferenceEngine;
    GameRecorder _recorder;
    std::unique_ptr<NSURLSessionUploader> _uploader;
    Renderer _renderer;
    GestureRecognizer _gestures;

    // Two-finger gesture tracking
    float _prevPinchDist;
    float _prevMidX, _prevMidY;
    float _prevAngle;
    BOOL _twoFingerActive;

    // Camera orbit state
    float _cameraYaw;
    float _cameraPitch;
    float _cameraDistance;
    float _cameraPanX;
    float _cameraPanZ;

    // Stored pixel dimensions (InitWindow might report points, not pixels)
    int _pixelWidth;
    int _pixelHeight;
}

- (void)initWithWidth:(int)width
               height:(int)height
          scaleFactor:(float)scaleFactor
           bundlePath:(NSString *)bundlePath
{
    // Store pixel dimensions for coordinate calculations
    _pixelWidth = width;
    _pixelHeight = height;

    // Initialize Raylib (on iOS, the GL context is already set up by the view)
    SetConfigFlags(FLAG_MSAA_4X_HINT);
    InitWindow(width, height, "Mahjong HK");
    SetTargetFPS(60);

    // Asset resolver
    std::string path = [bundlePath UTF8String];
    _assets = std::make_unique<IOSAssetResolver>(path);

    // Initialize renderer with mobile UI config
    _renderer.init(*_assets, UIConfig::mobile());

    // Camera defaults
    _cameraYaw = 0.0f;
    _cameraPitch = 55.0f;
    _cameraDistance = 26.0f;
    _cameraPanX = 0.0f;
    _cameraPanZ = 0.0f;
    _twoFingerActive = NO;
    _prevPinchDist = 0;

    // Create game controller
    _controller = std::make_unique<GameController>();

    // Load inference engine weights if available
    _inferenceEngine = std::make_unique<InferenceEngine>();
    std::string discardPath = _assets->resolve("model/discard_weights.bin");
    std::string claimPath = _assets->resolve("model/claim_weights.bin");
    _inferenceEngine->loadDiscardWeights(discardPath);
    _inferenceEngine->loadClaimWeights(claimPath);

    // Wire inference engine to AI players
    for (int i = 0; i < 4; i++) {
        auto* player = _controller->getPlayer(i);
        if (player && !player->isHuman()) {
            auto* ai = static_cast<AIPlayer*>(player);
            ai->setInferenceEngine(_inferenceEngine.get());
        }
    }

    _controller->startNewGame();

    // Re-wire inference engine after game start (players are recreated)
    for (int i = 0; i < 4; i++) {
        auto* player = _controller->getPlayer(i);
        if (player && !player->isHuman()) {
            auto* ai = static_cast<AIPlayer*>(player);
            ai->setInferenceEngine(_inferenceEngine.get());
        }
    }

    // Wire game recorder for replay upload
    _recorder.setClientId("ios");
    _recorder.attach(*_controller);
    _recorder.beginRound(*_controller);
    _uploader = std::make_unique<NSURLSessionUploader>();

    _controller->setRoundAdvanceCallback([this]() {
        if (_recorder.hasData()) {
            std::string json = _recorder.toJson();
            const char* envUrl = std::getenv("MAHJONG_UPLOAD_URL");
            std::string uploadUrl = envUrl ? envUrl : "http://localhost:8080/api/games";
            _uploader->uploadJson(uploadUrl, json,
                [this](bool ok, int status, const std::string&) {
                    if (ok && status >= 200 && status < 300) _recorder.clear();
                });
        }
        _recorder.beginRound(*_controller);
    });
}

- (void)shutdown {
    // Flush remaining replay data
    if (_recorder.hasData() && _uploader) {
        std::string json = _recorder.toJson();
        const char* envUrl = std::getenv("MAHJONG_UPLOAD_URL");
        std::string uploadUrl = envUrl ? envUrl : "http://localhost:8080/api/games";
        _uploader->uploadJson(uploadUrl, json,
            [this](bool ok, int status, const std::string&) {
                if (ok && status >= 200 && status < 300) _recorder.clear();
            });
    }

    _renderer.shutdown();
    _uploader.reset();
    _controller.reset();
    _inferenceEngine.reset();
    _assets.reset();
    CloseWindow();
}

- (void)updateCameraFromOrbit {
    float yawRad = _cameraYaw * DEG2RAD;
    float pitchRad = _cameraPitch * DEG2RAD;

    float camX = _cameraDistance * cosf(pitchRad) * sinf(yawRad) + _cameraPanX;
    float camY = _cameraDistance * sinf(pitchRad);
    float camZ = _cameraDistance * cosf(pitchRad) * cosf(yawRad) + _cameraPanZ;

    _renderer.camera().position = {camX, camY, camZ};
    _renderer.camera().target = {_cameraPanX, 0.0f, _cameraPanZ};
}

- (void)updateAndRender:(float)dt {
    if (!_controller) return;

    _controller->update(dt);

    [self updateCameraFromOrbit];

    // Build RenderState from controller
    RenderState state;
    auto* tm = _controller->turnManager();
    if (tm) {
        state.phase = tm->phase();
        state.activePlayerIndex = tm->activePlayer();
        state.wallRemaining = tm->wallRemaining();
        state.prevailingWind = _controller->prevailingWind();

        for (int i = 0; i < 4; i++) {
            state.players[i].player = _controller->getPlayer(i);
        }

        state.humanClaimOptions = _controller->humanClaimOptions();
        state.canSelfKong = !_controller->humanSelfKongOptions().empty();
        state.canSelfDrawWin = _controller->canHumanSelfDrawWin();

        state.winnerIndex = tm->winnerIndex();

        if (tm->phase() == GamePhase::SCORING || tm->phase() == GamePhase::ROUND_END) {
            auto snap = _controller->snapshot();
            state.scoringText = snap.scoringText;
        }

        if (tm->lastDiscard()) {
            state.lastDiscard = tm->lastDiscard();
        }
    }

    _renderer.render(state);
}

- (void)handleTap:(float)x y:(float)y {
    if (!_controller) return;

    auto* tm = _controller->turnManager();
    if (!tm) return;

    // Ensure camera is up to date before hit testing
    [self updateCameraFromOrbit];

    // Use stored pixel dimensions — GetScreenWidth/Height may return points on iOS
    int screenW = _pixelWidth;
    int screenH = _pixelHeight;

    // Check if tap is on a UI button
    GamePhase phase = tm->phase();

    // Button layout constants — must match UIConfig::mobile() in Renderer
    const float btnH = 70.0f, btnW = 140.0f, chowW = 180.0f, gap = 20.0f;
    const float btnY = screenH - btnH - 50.0f;

    // Claim buttons
    if (phase == GamePhase::CLAIM_PHASE) {
        auto options = _controller->humanClaimOptions();
        if (!options.empty()) {
            // Build widths matching renderer layout
            std::vector<float> widths;
            for (const auto& opt : options) {
                widths.push_back(opt.type == ClaimType::Chow ? chowW : btnW);
            }
            widths.push_back(btnW); // Pass button

            float totalW = 0;
            for (float w : widths) totalW += w + gap;
            float bx = screenW * 0.5f - totalW * 0.5f;

            for (int i = 0; i < (int)widths.size(); i++) {
                if (x >= bx && x <= bx + widths[i] && y >= btnY && y <= btnY + btnH) {
                    if (i < (int)options.size()) {
                        _controller->humanClaim(options[i]);
                    } else {
                        _controller->humanClaim(ClaimOption{}); // Pass
                    }
                    return;
                }
                bx += widths[i] + gap;
            }
        }
    }

    // Self-draw win / self-kong buttons
    if (phase == GamePhase::PLAYER_TURN && tm->activePlayer() == 0) {
        bool canWin = _controller->canHumanSelfDrawWin();
        auto kongOpts = _controller->humanSelfKongOptions();

        if (canWin || !kongOpts.empty()) {
            int btnCount = (canWin ? 1 : 0) + (!kongOpts.empty() ? 1 : 0);
            float totalW = btnCount * (btnW + gap) - gap;
            float startX = screenW * 0.5f - totalW * 0.5f;
            int idx = 0;

            if (canWin) {
                float bx = startX + idx * (btnW + gap);
                if (x >= bx && x <= bx + btnW && y >= btnY && y <= btnY + btnH) {
                    _controller->humanSelfDrawWin();
                    return;
                }
                idx++;
            }

            if (!kongOpts.empty()) {
                float bx = startX + idx * (btnW + gap);
                if (x >= bx && x <= bx + btnW && y >= btnY && y <= btnY + btnH) {
                    auto& k = kongOpts[0];
                    _controller->humanSelfKong(k.suit, k.rank);
                    return;
                }
            }
        }
    }

    // Round end / scoring / game over: advance
    if (phase == GamePhase::SCORING || phase == GamePhase::ROUND_END || phase == GamePhase::GAME_OVER) {
        _controller->advanceRound();
        return;
    }

    // Tile tap for discard — use 2D screen projection of bounding box centers
    // (more reliable than 3D raycasting on iOS where Raylib doesn't own the GL context)
    if (phase == GamePhase::PLAYER_TURN && tm->activePlayer() == 0) {
        const auto& tiles = _controller->humanHandTiles();
        int total = (int)tiles.size();

        NSLog(@"[MahjongTap] tap=(%.1f,%.1f) pixel=%dx%d raylib=%dx%d tiles=%d cam=(%.1f,%.1f,%.1f) fovy=%.1f",
              x, y, _pixelWidth, _pixelHeight, GetScreenWidth(), GetScreenHeight(), total,
              _renderer.camera().position.x,
              _renderer.camera().position.y,
              _renderer.camera().position.z,
              _renderer.camera().fovy);

        float closestDist = 9999.0f;
        int hitIndex = -1;
        for (int i = 0; i < total; i++) {
            BoundingBox bbox = _renderer.getHandTileBBox(0, i, total);
            Vector3 center = {
                (bbox.min.x + bbox.max.x) * 0.5f,
                (bbox.min.y + bbox.max.y) * 0.5f,
                (bbox.min.z + bbox.max.z) * 0.5f
            };
            // Use explicit pixel dimensions — GetWorldToScreen uses GetScreenWidth/Height
            // which may return points instead of pixels on iOS
            Vector2 screenPos = GetWorldToScreenEx(center, _renderer.camera(), _pixelWidth, _pixelHeight);
            float dx = x - screenPos.x;
            float dy = y - screenPos.y;
            float dist = sqrtf(dx * dx + dy * dy);
            if (i == 0 || i == total - 1) {
                NSLog(@"[MahjongTap] tile[%d] world=(%.2f,%.2f,%.2f) screen=(%.1f,%.1f) dist=%.1f",
                      i, center.x, center.y, center.z, screenPos.x, screenPos.y, dist);
            }
            if (dist < closestDist) {
                closestDist = dist;
                hitIndex = i;
            }
        }

        // Accept tap if within reasonable pixel radius of a tile center
        // (both tap coords and GetWorldToScreen output are in pixels)
        float maxTapDist = 80.0f;
        NSLog(@"[MahjongTap] closest: tile[%d] dist=%.1f maxTapDist=%.1f",
              hitIndex, closestDist, maxTapDist);
        if (hitIndex >= 0 && closestDist < maxTapDist) {
            NSLog(@"[MahjongTap] discarding tile id=%d", tiles[hitIndex].id);
            _controller->humanDiscardTile(tiles[hitIndex].id);
        }
    }
}

- (void)handleTwoFingerGesture:(float)x1
                             y1:(float)y1
                             x2:(float)x2
                             y2:(float)y2
{
    float dx = x2 - x1;
    float dy = y2 - y1;
    float dist = sqrtf(dx * dx + dy * dy);
    float midX = (x1 + x2) * 0.5f;
    float midY = (y1 + y2) * 0.5f;
    float angle = atan2f(dy, dx);

    if (!_twoFingerActive) {
        _twoFingerActive = YES;
        _prevPinchDist = dist;
        _prevMidX = midX;
        _prevMidY = midY;
        _prevAngle = angle;
        return;
    }

    // Pinch → zoom
    float pinchDelta = dist - _prevPinchDist;
    if (fabsf(pinchDelta) > 1.0f) {
        _cameraDistance -= pinchDelta * 0.05f;
        _cameraDistance = fmaxf(10.0f, fminf(40.0f, _cameraDistance));
    }

    // Rotation (angle change between frames)
    float angleDelta = angle - _prevAngle;
    // Normalize to [-PI, PI]
    while (angleDelta > M_PI) angleDelta -= 2.0f * M_PI;
    while (angleDelta < -M_PI) angleDelta += 2.0f * M_PI;
    _cameraYaw += angleDelta * RAD2DEG * 0.5f;

    // Pan (midpoint movement)
    float panDX = midX - _prevMidX;
    float panDY = midY - _prevMidY;
    if (fabsf(panDX) > 1.0f || fabsf(panDY) > 1.0f) {
        float yawRad = _cameraYaw * DEG2RAD;
        float panScale = _cameraDistance * 0.002f;
        _cameraPanX -= (panDX * cosf(yawRad) - panDY * sinf(yawRad)) * panScale;
        _cameraPanZ -= (panDX * sinf(yawRad) + panDY * cosf(yawRad)) * panScale;
    }

    _prevPinchDist = dist;
    _prevMidX = midX;
    _prevMidY = midY;
    _prevAngle = angle;
}

- (void)endTwoFingerGesture {
    _twoFingerActive = NO;
}

- (void)handleDoubleTap {
    _cameraYaw = 0.0f;
    _cameraPitch = 55.0f;
    _cameraDistance = 26.0f;
    _cameraPanX = 0.0f;
    _cameraPanZ = 0.0f;
}

- (int)currentPhase {
    if (!_controller || !_controller->turnManager()) return 0;
    return static_cast<int>(_controller->turnManager()->phase());
}

- (BOOL)isGameOver {
    return _controller ? _controller->isGameOver() : NO;
}

- (NSString *)snapshotJSON {
    if (!_controller) return @"{}";
    // Use C API for consistent JSON output
    MahjongGame game = reinterpret_cast<MahjongGame>(_controller.get());
    char* json = mahjong_snapshot_json(game);
    NSString* result = [NSString stringWithUTF8String:json];
    mahjong_free_snapshot(json);
    return result;
}

@end
