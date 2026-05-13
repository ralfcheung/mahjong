#pragma once
#include "render/Renderer.h"
#include "GameController.h"
#include "GameRecorder.h"
#include "DataUploader.h"
#include "InputEvent.h"
#include "AssetResolver.h"
#include "ai/InferenceEngine.h"
#include <vector>
#include <memory>

#ifdef HAS_TORCH
#include "ai/DQNTrainer.h"
#endif

class Game {
public:
    Game();
    ~Game();

    bool init();
    void run();
    void shutdown();

private:
    void update(float dt);
    void render();

    // Input: collect platform events, then process them
    void handleInput();
    std::vector<InputEvent> collectInputEvents();
    void processInputEvents(const std::vector<InputEvent>& events);

    void resetCamera();

    RenderState buildRenderState() const;

    GameController controller_;
    Renderer renderer_;
    std::unique_ptr<AssetResolver> assets_;

    bool running_ = true;
    int hoveredTileIndex_ = -1;

    // Orbit camera state
    float camYaw_ = 0.0f;       // Horizontal angle (degrees)
    float camPitch_ = 40.0f;    // Vertical angle (degrees, from horizon)
    float camDist_ = 26.0f;     // Distance from target
    Vector3 camTarget_ = {0.0f, 0.0f, 0.0f};

    // Game replay recording and upload
    GameRecorder recorder_;
    std::unique_ptr<DataUploader> uploader_;
    void uploadReplayData();

    // Lightweight inference engine for adaptation (always loaded if weights exist)
    std::unique_ptr<InferenceEngine> inferenceEngine_;

#ifdef HAS_TORCH
    std::unique_ptr<DQNTrainer> trainer_;
    std::string modelDir_;

    void initNeuralAI();
    void wireTrainerObservers();
    void onRoundComplete();
#endif
};
