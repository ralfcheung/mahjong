#include <jni.h>
#include <android/log.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>

#include "mahjong_api.h"
#include "GameController.h"
#include "render/Renderer.h"
#include "ai/InferenceEngine.h"
#include "player/AIPlayer.h"
#include "AndroidAssetResolver.h"
#include "InputEvent.h"
#include "platform/mobile/GestureRecognizer.h"
#include "raylib.h"

#include <memory>
#include <string>
#include <vector>

#define LOG_TAG "MahjongJNI"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// Global state (single-instance game)
static std::unique_ptr<GameController> g_controller;
static std::unique_ptr<AndroidAssetResolver> g_assets;
static std::unique_ptr<InferenceEngine> g_inference;
static Renderer g_renderer;
static GestureRecognizer g_gestures;

// Camera orbit state
static float g_cameraYaw = 0.0f;
static float g_cameraPitch = 55.0f;
static float g_cameraDistance = 26.0f;
static float g_cameraPanX = 0.0f;
static float g_cameraPanZ = 0.0f;

// Two-finger gesture state
static float g_prevPinchDist = 0.0f;
static float g_prevMidX = 0.0f, g_prevMidY = 0.0f;
static float g_prevAngle = 0.0f;
static bool g_twoFingerActive = false;

static void updateCameraFromOrbit() {
    float yawRad = g_cameraYaw * DEG2RAD;
    float pitchRad = g_cameraPitch * DEG2RAD;

    float camX = g_cameraDistance * cosf(pitchRad) * sinf(yawRad) + g_cameraPanX;
    float camY = g_cameraDistance * sinf(pitchRad);
    float camZ = g_cameraDistance * cosf(pitchRad) * cosf(yawRad) + g_cameraPanZ;

    g_renderer.camera().position = {camX, camY, camZ};
    g_renderer.camera().target = {g_cameraPanX, 0.0f, g_cameraPanZ};
}

static RenderState buildRenderState() {
    RenderState state;
    auto* tm = g_controller->turnManager();
    if (tm) {
        state.phase = tm->phase();
        state.activePlayerIndex = tm->activePlayer();
        state.wallRemaining = tm->wallRemaining();
        state.prevailingWind = g_controller->prevailingWind();

        for (int i = 0; i < 4; i++) {
            state.players[i].player = g_controller->getPlayer(i);
        }

        state.humanClaimOptions = g_controller->humanClaimOptions();
        state.canSelfKong = !g_controller->humanSelfKongOptions().empty();
        state.canSelfDrawWin = g_controller->canHumanSelfDrawWin();

        state.winnerIndex = tm->winnerIndex();

        if (tm->phase() == GamePhase::SCORING || tm->phase() == GamePhase::ROUND_END) {
            auto snap = g_controller->snapshot();
            state.scoringText = snap.scoringText;
        }

        if (tm->lastDiscard()) {
            state.lastDiscard = tm->lastDiscard();
        }
    }
    return state;
}

extern "C" {

JNIEXPORT void JNICALL
Java_com_mahjong_hk_NativeLib_init(JNIEnv* env, jobject /* this */,
                                    jint width, jint height,
                                    jobject assetManager,
                                    jstring internalPath) {
    LOGI("Initializing Mahjong HK (%dx%d)", width, height);

    // Raylib window init (Android uses NativeActivity, window already created)
    SetConfigFlags(FLAG_MSAA_4X_HINT);
    InitWindow(width, height, "Mahjong HK");
    SetTargetFPS(60);

    // Asset resolver
    const char* path = env->GetStringUTFChars(internalPath, nullptr);
    AAssetManager* mgr = AAssetManager_fromJava(env, assetManager);
    g_assets = std::make_unique<AndroidAssetResolver>(path, mgr);
    env->ReleaseStringUTFChars(internalPath, path);

    // Initialize renderer
    g_renderer.init(*g_assets, UIConfig::mobile());

    // Create game controller
    g_controller = std::make_unique<GameController>();

    // Load inference engine weights
    g_inference = std::make_unique<InferenceEngine>();
    g_inference->loadDiscardWeights(g_assets->resolve("model/discard_weights.bin"));
    g_inference->loadClaimWeights(g_assets->resolve("model/claim_weights.bin"));

    g_controller->startNewGame();

    // Wire inference engine to AI players
    for (int i = 0; i < 4; i++) {
        auto* player = g_controller->getPlayer(i);
        if (player && !player->isHuman()) {
            static_cast<AIPlayer*>(player)->setInferenceEngine(g_inference.get());
        }
    }

    LOGI("Mahjong HK initialized successfully");
}

JNIEXPORT void JNICALL
Java_com_mahjong_hk_NativeLib_shutdown(JNIEnv* /* env */, jobject /* this */) {
    g_renderer.shutdown();
    g_controller.reset();
    g_inference.reset();
    g_assets.reset();
    CloseWindow();
    LOGI("Mahjong HK shut down");
}

JNIEXPORT void JNICALL
Java_com_mahjong_hk_NativeLib_updateAndRender(JNIEnv* /* env */, jobject /* this */,
                                               jfloat dt) {
    if (!g_controller) return;

    g_controller->update(dt);
    updateCameraFromOrbit();

    RenderState state = buildRenderState();
    g_renderer.render(state);
}

JNIEXPORT void JNICALL
Java_com_mahjong_hk_NativeLib_handleTap(JNIEnv* /* env */, jobject /* this */,
                                         jfloat x, jfloat y) {
    if (!g_controller) return;
    auto* tm = g_controller->turnManager();
    if (!tm) return;

    int screenW = GetScreenWidth();
    int screenH = GetScreenHeight();
    GamePhase phase = tm->phase();

    // Button layout constants — must match UIConfig::mobile() in Renderer
    const float btnH = 70.0f, btnW = 140.0f, chowW = 180.0f, gap = 20.0f;
    const float btnY = screenH - btnH - 50.0f;

    // Claim buttons
    if (phase == GamePhase::CLAIM_PHASE) {
        auto options = g_controller->humanClaimOptions();
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
                        g_controller->humanClaim(options[i]);
                    } else {
                        g_controller->humanClaim(ClaimOption{});
                    }
                    return;
                }
                bx += widths[i] + gap;
            }
        }
    }

    // Self-draw win / self-kong buttons
    if (phase == GamePhase::PLAYER_TURN && tm->activePlayer() == 0) {
        bool canWin = g_controller->canHumanSelfDrawWin();
        auto kongOpts = g_controller->humanSelfKongOptions();

        if (canWin || !kongOpts.empty()) {
            int btnCount = (canWin ? 1 : 0) + (!kongOpts.empty() ? 1 : 0);
            float totalBtnW = btnCount * (btnW + gap) - gap;
            float startX = screenW * 0.5f - totalBtnW * 0.5f;
            int idx = 0;

            if (canWin) {
                float bx = startX + idx * (btnW + gap);
                if (x >= bx && x <= bx + btnW && y >= btnY && y <= btnY + btnH) {
                    g_controller->humanSelfDrawWin();
                    return;
                }
                idx++;
            }

            if (!kongOpts.empty()) {
                float bx = startX + idx * (btnW + gap);
                if (x >= bx && x <= bx + btnW && y >= btnY && y <= btnY + btnH) {
                    g_controller->humanSelfKong(kongOpts[0].suit, kongOpts[0].rank);
                    return;
                }
            }
        }
    }

    // Round end / scoring / game over
    if (phase == GamePhase::SCORING || phase == GamePhase::ROUND_END || phase == GamePhase::GAME_OVER) {
        g_controller->advanceRound();
        return;
    }

    // Tile tap for discard (single tap, same as desktop click)
    if (phase == GamePhase::PLAYER_TURN && tm->activePlayer() == 0) {
        const auto& tiles = g_controller->humanHandTiles();
        int total = (int)tiles.size();

        float closestDist = 9999.0f;
        int hitIndex = -1;
        for (int i = 0; i < total; i++) {
            BoundingBox bbox = g_renderer.getHandTileBBox(0, i, total);
            Vector3 center = {
                (bbox.min.x + bbox.max.x) * 0.5f,
                (bbox.min.y + bbox.max.y) * 0.5f,
                (bbox.min.z + bbox.max.z) * 0.5f
            };
            Vector2 screenPos = GetWorldToScreen(center, g_renderer.camera());
            float dx = x - screenPos.x;
            float dy = y - screenPos.y;
            float dist = sqrtf(dx * dx + dy * dy);
            if (dist < closestDist) {
                closestDist = dist;
                hitIndex = i;
            }
        }

        // Accept tap within reasonable pixel radius
        float maxTapDist = 80.0f;
        if (hitIndex >= 0 && closestDist < maxTapDist) {
            g_controller->humanDiscardTile(tiles[hitIndex].id);
        }
    }
}

JNIEXPORT void JNICALL
Java_com_mahjong_hk_NativeLib_handleTwoFingerGesture(JNIEnv* /* env */, jobject /* this */,
                                                      jfloat x1, jfloat y1,
                                                      jfloat x2, jfloat y2) {
    float dx = x2 - x1;
    float dy = y2 - y1;
    float dist = sqrtf(dx * dx + dy * dy);
    float midX = (x1 + x2) * 0.5f;
    float midY = (y1 + y2) * 0.5f;
    float angle = atan2f(dy, dx);

    if (!g_twoFingerActive) {
        g_twoFingerActive = true;
        g_prevPinchDist = dist;
        g_prevMidX = midX;
        g_prevMidY = midY;
        g_prevAngle = angle;
        return;
    }

    // Pinch → zoom
    float pinchDelta = dist - g_prevPinchDist;
    if (fabsf(pinchDelta) > 1.0f) {
        g_cameraDistance -= pinchDelta * 0.05f;
        if (g_cameraDistance < 10.0f) g_cameraDistance = 10.0f;
        if (g_cameraDistance > 40.0f) g_cameraDistance = 40.0f;
    }

    // Rotation
    float angleDelta = angle - g_prevAngle;
    while (angleDelta > M_PI) angleDelta -= 2.0f * M_PI;
    while (angleDelta < -M_PI) angleDelta += 2.0f * M_PI;
    g_cameraYaw += angleDelta * RAD2DEG * 0.5f;

    // Pan
    float panDX = midX - g_prevMidX;
    float panDY = midY - g_prevMidY;
    if (fabsf(panDX) > 1.0f || fabsf(panDY) > 1.0f) {
        float yawRad = g_cameraYaw * DEG2RAD;
        float panScale = g_cameraDistance * 0.002f;
        g_cameraPanX -= (panDX * cosf(yawRad) - panDY * sinf(yawRad)) * panScale;
        g_cameraPanZ -= (panDX * sinf(yawRad) + panDY * cosf(yawRad)) * panScale;
    }

    g_prevPinchDist = dist;
    g_prevMidX = midX;
    g_prevMidY = midY;
    g_prevAngle = angle;
}

JNIEXPORT void JNICALL
Java_com_mahjong_hk_NativeLib_endTwoFingerGesture(JNIEnv* /* env */, jobject /* this */) {
    g_twoFingerActive = false;
}

JNIEXPORT void JNICALL
Java_com_mahjong_hk_NativeLib_handleDoubleTap(JNIEnv* /* env */, jobject /* this */) {
    g_cameraYaw = 0.0f;
    g_cameraPitch = 55.0f;
    g_cameraDistance = 26.0f;
    g_cameraPanX = 0.0f;
    g_cameraPanZ = 0.0f;
}

JNIEXPORT jstring JNICALL
Java_com_mahjong_hk_NativeLib_snapshotJSON(JNIEnv* env, jobject /* this */) {
    if (!g_controller) return env->NewStringUTF("{}");
    MahjongGame game = reinterpret_cast<MahjongGame>(g_controller.get());
    char* json = mahjong_snapshot_json(game);
    jstring result = env->NewStringUTF(json);
    mahjong_free_snapshot(json);
    return result;
}

JNIEXPORT jint JNICALL
Java_com_mahjong_hk_NativeLib_currentPhase(JNIEnv* /* env */, jobject /* this */) {
    if (!g_controller || !g_controller->turnManager()) return 0;
    return static_cast<jint>(g_controller->turnManager()->phase());
}

JNIEXPORT jboolean JNICALL
Java_com_mahjong_hk_NativeLib_isGameOver(JNIEnv* /* env */, jobject /* this */) {
    return g_controller ? g_controller->isGameOver() : JNI_FALSE;
}

} // extern "C"
