#include "Game.h"
#include "net/HttpUploader.h"
#include "player/AIPlayer.h"
#include "raylib.h"
#include "raymath.h"
#include <cmath>
#include <cstdlib>
#include <cstdio>

#ifdef HAS_TORCH
#include "ai/RLFeatures.h"
#include "scoring/Scoring.h"
#endif

Game::Game() = default;
Game::~Game() = default;

bool Game::init() {
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(1400, 900, "Hong Kong Mahjong 香港麻雀");
    SetTargetFPS(120);

    assets_ = std::make_unique<DesktopAssetResolver>(ASSETS_PATH);

    if (!renderer_.init(*assets_)) {
        return false;
    }

    controller_.startNewGame();

    // Load inference engine weights for adaptation (works even without libtorch)
    inferenceEngine_ = std::make_unique<InferenceEngine>();
    std::string modelDir = assets_->resolve("model");
    bool discOk = inferenceEngine_->loadDiscardWeights(modelDir + "/discard_weights.bin");
    bool claimOk = inferenceEngine_->loadClaimWeights(modelDir + "/claim_weights.bin");
    if (discOk || claimOk) {
        std::fprintf(stderr, "[Game] Loaded inference weights (discard=%d, claim=%d) — adaptation enabled\n",
                     discOk, claimOk);
        for (int i = 0; i < 4; i++) {
            auto* player = controller_.getPlayer(i);
            if (player && !player->isHuman()) {
                static_cast<AIPlayer*>(player)->setInferenceEngine(inferenceEngine_.get());
            }
        }
        controller_.setAdaptationTier(0);  // Adaptation disabled — base weights sufficient
    } else {
        std::fprintf(stderr, "[Game] No inference weights found — adaptation disabled\n");
    }

    // Wire game recorder (unconditional — records human decisions for all platforms)
    recorder_.setClientId("desktop");
    recorder_.attach(controller_);
    recorder_.beginRound(controller_);
    uploader_ = std::make_unique<HttpUploader>();

    // Round advance callback: upload replay data + begin new recording
    controller_.setRoundAdvanceCallback([this]() {
        uploadReplayData();
        recorder_.beginRound(controller_);
#ifdef HAS_TORCH
        onRoundComplete();
#endif
    });

#ifdef HAS_TORCH
    initNeuralAI();
    wireTrainerObservers();
#endif

    return true;
}

void Game::run() {
    while (!WindowShouldClose() && running_) {
        float dt = GetFrameTime();
        handleInput();
        update(dt);
        render();
    }
}

void Game::resetCamera() {
    camYaw_ = 0.0f;
    camPitch_ = 40.0f;
    camDist_ = 26.0f;
    camTarget_ = {0.0f, 0.0f, 0.0f};
}

// Collect platform-specific raw input into InputEvents
std::vector<InputEvent> Game::collectInputEvents() {
    std::vector<InputEvent> events;

    // --- Camera input ---

    // Scroll to zoom
    float wheel = GetMouseWheelMove();
    if (wheel != 0.0f) {
        events.push_back(InputEvent::cameraZoom(wheel * 2.0f));
    }

    // Right-drag to orbit
    if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
        Vector2 delta = GetMouseDelta();
        if (delta.x != 0 || delta.y != 0) {
            events.push_back(InputEvent::cameraOrbit(-delta.x * 0.3f, -delta.y * 0.3f));
        }
    }

    // Middle-drag to pan
    if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) {
        Vector2 delta = GetMouseDelta();
        if (delta.x != 0 || delta.y != 0) {
            float panSpeed = camDist_ * 0.003f;
            float yawRad = camYaw_ * DEG2RAD;
            float dx = (cosf(yawRad) * delta.x + sinf(yawRad) * delta.y) * panSpeed;
            float dz = (-sinf(yawRad) * delta.x + cosf(yawRad) * delta.y) * panSpeed;
            events.push_back(InputEvent::cameraPan(dx, dz));
        }
    }

    // WASD to pan camera
    float panSpeed = camDist_ * 0.02f * GetFrameTime() * 60.0f;
    float yawRadPan = camYaw_ * DEG2RAD;
    float fwdX = sinf(yawRadPan), fwdZ = cosf(yawRadPan);
    float rightX = cosf(yawRadPan), rightZ = -sinf(yawRadPan);

    float panDx = 0, panDz = 0;
    if (IsKeyDown(KEY_W)) { panDx -= fwdX * panSpeed; panDz -= fwdZ * panSpeed; }
    if (IsKeyDown(KEY_S)) { panDx += fwdX * panSpeed; panDz += fwdZ * panSpeed; }
    if (IsKeyDown(KEY_A)) { panDx -= rightX * panSpeed; panDz -= rightZ * panSpeed; }
    if (IsKeyDown(KEY_D)) { panDx += rightX * panSpeed; panDz += rightZ * panSpeed; }
    if (panDx != 0 || panDz != 0) {
        events.push_back(InputEvent::cameraPan(panDx, panDz));
    }

    // R key to reset camera
    if (IsKeyPressed(KEY_R)) {
        events.push_back(InputEvent::cameraReset());
    }

    // --- Game input ---
    auto phase = controller_.currentPhase();

    // Handle human tile selection and special actions during PLAYER_TURN
    if (phase == GamePhase::PLAYER_TURN && controller_.turnManager()->activePlayer() == 0) {
        bool canWin = controller_.canHumanSelfDrawWin();
        auto kongOpts = controller_.humanSelfKongOptions();

        // Button click detection for self-draw win and self-kong
        if (canWin || !kongOpts.empty()) {
            int screenW = GetScreenWidth();
            int screenH = GetScreenHeight();
            float btnW = 100, btnH = 45, gap = 15;
            int btnCount = (canWin ? 1 : 0) + (!kongOpts.empty() ? 1 : 0);
            float totalW = btnCount * (btnW + gap) - gap;
            float startX = screenW * 0.5f - totalW * 0.5f;
            float btnY = screenH - btnH - 50;
            int idx = 0;

            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                Vector2 mouse = GetMousePosition();

                if (canWin) {
                    Rectangle winRect = {startX + idx * (btnW + gap), btnY, btnW, btnH};
                    if (CheckCollisionPointRec(mouse, winRect)) {
                        events.push_back(InputEvent::selfDrawWin());
                    }
                    idx++;
                }
                if (!kongOpts.empty()) {
                    Rectangle kongRect = {startX + idx * (btnW + gap), btnY, btnW, btnH};
                    if (CheckCollisionPointRec(mouse, kongRect)) {
                        events.push_back(InputEvent::selfKong(kongOpts[0].suit, kongOpts[0].rank));
                    }
                }
            }

            // Keyboard shortcuts
            if (canWin && IsKeyPressed(KEY_H)) events.push_back(InputEvent::selfDrawWin());
            if (!kongOpts.empty() && IsKeyPressed(KEY_K))
                events.push_back(InputEvent::selfKong(kongOpts[0].suit, kongOpts[0].rank));
        }

        // Tile hover and click detection via 3D raycasting
        HumanPlayer* human = dynamic_cast<HumanPlayer*>(controller_.getPlayer(0));
        if (human && human->isWaitingForDiscard()) {
            const auto& tiles = human->hand().concealed();
            int totalTiles = (int)tiles.size();

            hoveredTileIndex_ = -1;
            Ray mouseRay = GetMouseRay(GetMousePosition(), renderer_.camera());
            float closestDist = 9999.0f;
            for (int i = 0; i < totalTiles; i++) {
                BoundingBox bbox = renderer_.getHandTileBBox(0, i, totalTiles);
                RayCollision coll = GetRayCollisionBox(mouseRay, bbox);
                if (coll.hit && coll.distance < closestDist) {
                    closestDist = coll.distance;
                    hoveredTileIndex_ = i;
                }
            }

            if (hoveredTileIndex_ >= 0 && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                events.push_back(InputEvent::tileTap(tiles[hoveredTileIndex_].id));
                hoveredTileIndex_ = -1;
            }
        }
    }

    // Claim button detection
    if (phase == GamePhase::CLAIM_PHASE) {
        auto options = controller_.humanClaimOptions();
        if (!options.empty()) {
            int screenW = GetScreenWidth();
            int screenH = GetScreenHeight();
            float btnH = 45, gap = 15;
            float btnY = screenH - btnH - 50;

            std::vector<float> btnWidths;
            for (const auto& opt : options) {
                btnWidths.push_back(opt.type == ClaimType::Chow ? 120.0f : 100.0f);
            }
            btnWidths.push_back(100.0f); // Pass button

            float totalW = 0;
            for (float w : btnWidths) totalW += w + gap;
            float startX = screenW * 0.5f - totalW * 0.5f;

            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                Vector2 mouse = GetMousePosition();
                float xPos = startX;
                for (int i = 0; i < (int)btnWidths.size(); i++) {
                    Rectangle rect = {xPos, btnY, btnWidths[i], btnH};
                    if (CheckCollisionPointRec(mouse, rect)) {
                        if (i < (int)options.size()) {
                            events.push_back(InputEvent::claimButton(options[i].type, i));
                        } else {
                            events.push_back(InputEvent::claimButton(ClaimType::None)); // Pass
                        }
                        break;
                    }
                    xPos += btnWidths[i] + gap;
                }
            }

            // Keyboard shortcuts
            if (IsKeyPressed(KEY_SPACE)) events.push_back(InputEvent::claimButton(ClaimType::None));
            for (int i = 0; i < (int)options.size(); i++) {
                if (options[i].type == ClaimType::Pung && IsKeyPressed(KEY_P))
                    events.push_back(InputEvent::claimButton(ClaimType::Pung, i));
                if (options[i].type == ClaimType::Kong && IsKeyPressed(KEY_K))
                    events.push_back(InputEvent::claimButton(ClaimType::Kong, i));
                if (options[i].type == ClaimType::Win && IsKeyPressed(KEY_H))
                    events.push_back(InputEvent::claimButton(ClaimType::Win, i));
            }
            if (IsKeyPressed(KEY_C)) {
                int chowCount = 0;
                int chowIdx = -1;
                for (int i = 0; i < (int)options.size(); i++) {
                    if (options[i].type == ClaimType::Chow) { chowCount++; chowIdx = i; }
                }
                if (chowCount == 1) {
                    events.push_back(InputEvent::claimButton(ClaimType::Chow, chowIdx));
                }
            }
        }
    }

    // Advance round (also check isRoundOver as fallback in case phase is stale)
    if (phase == GamePhase::SCORING || phase == GamePhase::ROUND_END || controller_.isRoundOver()) {
        if (IsKeyPressed(KEY_SPACE) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            events.push_back(InputEvent::advanceRound());
        }
    }

    return events;
}

// Process InputEvents through GameController and camera
void Game::processInputEvents(const std::vector<InputEvent>& events) {
    for (const auto& evt : events) {
        switch (evt.type) {
            case InputEventType::TileTap:
                controller_.humanDiscardTile(evt.tileId);
                break;

            case InputEventType::ClaimButton: {
                if (evt.claimType == ClaimType::None) {
                    controller_.humanClaim(ClaimOption{});
                } else {
                    auto options = controller_.humanClaimOptions();
                    if (evt.comboIndex >= 0 && evt.comboIndex < (int)options.size()) {
                        controller_.humanClaim(options[evt.comboIndex]);
                    }
                }
                break;
            }

            case InputEventType::SelfDrawWinButton:
                controller_.humanSelfDrawWin();
                break;

            case InputEventType::SelfKongButton:
                controller_.humanSelfKong(evt.kongSuit, evt.kongRank);
                break;

            case InputEventType::AdvanceRound:
                controller_.advanceRound();
                if (controller_.isGameOver()) {
                    running_ = false;
                }
                break;

            case InputEventType::CameraOrbit:
                camYaw_ += evt.deltaYaw;
                camPitch_ += evt.deltaPitch;
                if (camPitch_ < 10.0f) camPitch_ = 10.0f;
                if (camPitch_ > 89.0f) camPitch_ = 89.0f;
                break;

            case InputEventType::CameraZoom:
                camDist_ -= evt.deltaZoom;
                if (camDist_ < 8.0f) camDist_ = 8.0f;
                if (camDist_ > 50.0f) camDist_ = 50.0f;
                break;

            case InputEventType::CameraPan:
                camTarget_.x += evt.deltaPanX;
                camTarget_.z += evt.deltaPanZ;
                break;

            case InputEventType::CameraReset:
                resetCamera();
                break;
        }
    }

    // Update camera position from orbit parameters
    Camera3D& cam = renderer_.camera();
    float yawRad = camYaw_ * DEG2RAD;
    float pitchRad = camPitch_ * DEG2RAD;
    cam.position.x = camTarget_.x + camDist_ * cosf(pitchRad) * sinf(yawRad);
    cam.position.y = camTarget_.y + camDist_ * sinf(pitchRad);
    cam.position.z = camTarget_.z + camDist_ * cosf(pitchRad) * cosf(yawRad);
    cam.target = camTarget_;
}

void Game::handleInput() {
    auto events = collectInputEvents();
    processInputEvents(events);
}

void Game::update(float dt) {
    controller_.update(dt);
}

RenderState Game::buildRenderState() const {
    RenderState state;

    const TurnManager* tm = controller_.turnManager();
    if (tm) {
        state.phase = tm->phase();
        state.activePlayerIndex = tm->activePlayer();
        state.wallRemaining = tm->wallRemaining();
        state.lastDiscard = tm->lastDiscard();
        state.humanClaimOptions = tm->getHumanClaimOptionsWithCombos();
        state.canSelfKong = !tm->getHumanSelfKongOptions().empty();
        state.canSelfDrawWin = tm->canHumanSelfDrawWin();
        state.scoringText = tm->scoringText();
        state.winnerIndex = tm->winnerIndex();
    }
    state.prevailingWind = controller_.prevailingWind();
    state.aiThinking = controller_.isAIThinking();

    for (int i = 0; i < 4; i++) {
        state.players[i].player = controller_.getPlayer(i);
    }

    return state;
}

void Game::render() {
    RenderState state = buildRenderState();
    renderer_.render(state);
}

void Game::shutdown() {
    // Flush remaining replay data before shutdown
    uploadReplayData();

#ifdef HAS_TORCH
    if (trainer_) {
        trainer_->saveModels(modelDir_);
        std::fprintf(stderr, "Neural models saved.\n");
    }
#endif
    renderer_.shutdown();
    CloseWindow();
}

void Game::uploadReplayData() {
    if (!recorder_.hasData()) return;
    std::string json = recorder_.toJson();
    const char* url = std::getenv("MAHJONG_UPLOAD_URL");
    std::string uploadUrl = url ? url : "http://localhost:8080/api/games";
    uploader_->uploadJson(uploadUrl, json, [this](bool success, int statusCode, const std::string& response) {
        if (success && statusCode >= 200 && statusCode < 300) {
            std::fprintf(stderr, "[Replay] Upload successful (%d decisions)\n", recorder_.decisionCount());
            recorder_.clear();
        } else {
            std::fprintf(stderr, "[Replay] Upload failed (status %d), retrying next round\n", statusCode);
        }
    });
}

// --- Neural AI Integration ---
#ifdef HAS_TORCH

void Game::initNeuralAI() {
    modelDir_ = std::string(ASSETS_PATH) + "model";
    trainer_ = std::make_unique<DQNTrainer>();

    bool loaded = trainer_->loadModels(modelDir_);
    if (loaded) {
        std::fprintf(stderr, "Neural AI loaded from %s\n", modelDir_.c_str());
    } else {
        std::fprintf(stderr, "No trained model found. AI will use heuristic fallback.\n");
    }

    // Assign trainer to AI players
    for (int i = 1; i <= 3; i++) {
        AIPlayer* ai = dynamic_cast<AIPlayer*>(controller_.getPlayer(i));
        if (ai) {
            ai->setTrainer(trainer_.get());
        }
    }
}

void Game::wireTrainerObservers() {
    if (!trainer_) return;

    // Observe discards for training data (persisted across rounds via GameController)
    controller_.addDiscardObserver(
        [this](int playerIndex, Tile discarded, int turnCount, int wallRemaining) {
            if (!trainer_) return;

            auto& players = controller_.players();
            const Player* player = players[playerIndex].get();
            std::vector<const Player*> allPlayers;
            for (auto& p : players) allPlayers.push_back(p.get());

            RLGameContext ctx;
            ctx.turnCount = turnCount;
            ctx.wallRemaining = wallRemaining;
            ctx.seatWind = player->seatWind();
            ctx.prevailingWind = controller_.prevailingWind();
            ctx.playerIndex = playerIndex;
            ctx.allPlayers = &allPlayers;
            fillPlayerScores(ctx);

            auto features = extractDiscardFeatures(player->hand(), ctx);
            int actionIdx = tileTypeIndex(discarded);
            if (actionIdx < 0) return;

            if (player->isHuman()) {
                trainer_->recordHumanDiscard(features, actionIdx);
            }
        });

    // Observe claims for training data
    controller_.addClaimObserver(
        [this](int playerIndex, ClaimType type, Tile claimedTile, int turnCount, int wallRemaining) {
            if (!trainer_) return;

            auto& players = controller_.players();
            const Player* player = players[playerIndex].get();
            std::vector<const Player*> allPlayers;
            for (auto& p : players) allPlayers.push_back(p.get());

            RLGameContext ctx;
            ctx.turnCount = turnCount;
            ctx.wallRemaining = wallRemaining;
            ctx.seatWind = player->seatWind();
            ctx.prevailingWind = controller_.prevailingWind();
            ctx.playerIndex = playerIndex;
            ctx.allPlayers = &allPlayers;
            fillPlayerScores(ctx);

            int actionIdx = 3; // Pass
            if (type == ClaimType::Chow) actionIdx = 0;
            else if (type == ClaimType::Pung) actionIdx = 1;
            else if (type == ClaimType::Kong) actionIdx = 2;

            std::vector<ClaimType> available = {type};
            auto features = extractClaimFeatures(player->hand(), claimedTile, available, ctx);

            if (player->isHuman()) {
                trainer_->recordHumanClaim(features, actionIdx);
            }
        });

    // Observe round end for terminal rewards
    controller_.addRoundEndObserver(
        [this](int winnerIndex, bool selfDrawn, const FaanResult& result, bool isDraw) {
            if (!trainer_) return;

            float reward = 0.0f;
            if (isDraw) {
                reward = -1.0f;
            } else if (winnerIndex >= 0) {
                int pts = Scoring::faanToPoints(result.totalFaan);
                reward = 5.0f + static_cast<float>(pts);
            }

            trainer_->onRoundEnd(reward);
        });

    // Note: round advance callback is set in init() unconditionally
    // and calls onRoundComplete() when HAS_TORCH is enabled
}

void Game::onRoundComplete() {
    if (!trainer_) return;

    trainer_->saveModels(modelDir_);
    trainer_->runSelfPlay(20, controller_.prevailingWind(), controller_.dealerIndex());
    trainer_->saveModels(modelDir_);
}

#endif // HAS_TORCH
