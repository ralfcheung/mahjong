#pragma once

#ifdef HAS_TORCH

#include "MahjongNet.h"
#include "RLFeatures.h"
#include "player/Player.h"
#include "tiles/TileEnums.h"
#include <torch/torch.h>
#include <vector>
#include <string>
#include <random>
#include <deque>

struct Transition {
    torch::Tensor state;
    int action;
    float reward;
    torch::Tensor nextState;
    bool done;
};

struct TrainingStats {
    int gamesPlayed = 0;
    float epsilon = 1.0f;
    float learningRate = 0.001f;
    float avgLoss = 0.0f;
    int totalTransitions = 0;
};

class DQNTrainer {
public:
    DQNTrainer();

    // Model management
    bool loadModels(const std::string& modelDir);
    void saveModels(const std::string& modelDir) const;
    bool hasTrainedModels() const { return modelsLoaded_; }

    // Get networks for inference
    DiscardNet& discardNet() { return discardNet_; }
    ClaimNet& claimNet() { return claimNet_; }

    // Record transitions
    void recordDiscardTransition(const std::vector<float>& state, int action,
                                  float reward, const std::vector<float>& nextState, bool done);
    void recordClaimTransition(const std::vector<float>& state, int action,
                                float reward, const std::vector<float>& nextState, bool done);

    // Record human observation (supervised learning)
    void recordHumanDiscard(const std::vector<float>& state, int action);
    void recordHumanClaim(const std::vector<float>& state, int action);

    // Training
    void trainStep();
    void onRoundEnd(float roundReward);

    // Self-play between rounds
    void runSelfPlay(int numGames, Wind prevailingWind, int dealerIndex);

    // Target network sync
    void syncTargetNetworks();

    // Exploration
    float getEpsilon() const { return stats_.epsilon; }
    int selectDiscardAction(const std::vector<float>& state, const std::vector<bool>& validMask);
    int selectClaimAction(const std::vector<float>& state, const std::vector<bool>& validMask);

    const TrainingStats& stats() const { return stats_; }

private:
    void trainDiscardBatch();
    void trainClaimBatch();
    void trainSupervisedDiscard();
    void trainSupervisedClaim();
    void decayEpsilon();

    // Replay buffer helpers
    std::vector<Transition> sampleBatch(const std::deque<Transition>& buffer, int batchSize);

    // Save/load replay buffer
    void saveReplayBuffer(const std::string& path) const;
    bool loadReplayBuffer(const std::string& path);
    void saveStats(const std::string& path) const;
    bool loadStats(const std::string& path);

    // Networks
    DiscardNet discardNet_{nullptr};
    DiscardNet discardTargetNet_{nullptr};
    ClaimNet claimNet_{nullptr};
    ClaimNet claimTargetNet_{nullptr};

    // Optimizers
    std::unique_ptr<torch::optim::Adam> discardOptimizer_;
    std::unique_ptr<torch::optim::Adam> claimOptimizer_;

    // Replay buffers
    static constexpr int MAX_BUFFER_SIZE = 50000;
    static constexpr int BATCH_SIZE = 64;
    static constexpr float GAMMA = 0.95f;
    static constexpr int TARGET_SYNC_INTERVAL = 100;

    std::deque<Transition> discardBuffer_;
    std::deque<Transition> claimBuffer_;

    // Supervised learning buffers (human observations)
    std::vector<std::pair<torch::Tensor, int>> humanDiscardSamples_;
    std::vector<std::pair<torch::Tensor, int>> humanClaimSamples_;

    TrainingStats stats_;
    bool modelsLoaded_ = false;
    std::mt19937 rng_{std::random_device{}()};
};

#endif // HAS_TORCH
