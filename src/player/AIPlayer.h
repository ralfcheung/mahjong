#pragma once
#include "Player.h"
#include <vector>
#include <memory>

class DQNTrainer;
class InferenceEngine;
class AdaptiveEngine;
struct AdaptationConfig;
struct ObservableState;

class AIPlayer : public Player {
public:
    using Player::Player;

    bool isHuman() const override { return false; }

    void requestDiscard(std::function<void(Tile)> callback) override;
    void requestClaimDecision(
        Tile discardedTile,
        const std::vector<ClaimOption>& options,
        std::function<void(ClaimType)> callback) override;

    // Called by TurnManager to provide game context before decisions
    void setGameContext(Wind prevailingWind,
                        const std::vector<const Player*>& allPlayers);

    // Turn info for feature extraction
    void setTurnInfo(int turnCount, int wallRemaining);

    // Lightweight inference engine (no libtorch dependency)
    void setInferenceEngine(InferenceEngine* engine) { inferenceEngine_ = engine; }

    void setTrainer(DQNTrainer* trainer) { trainer_ = trainer; }
    void setTrainerClaimOnly(bool claimOnly) { trainerClaimOnly_ = claimOnly; }

    // Cooperative AI: 3 AIs coordinate against the human player
    void setCooperativeMode(bool enabled) { cooperativeMode_ = enabled; }
    void setLastDiscarder(int idx) { lastDiscarder_ = idx; }

    // Per-round test-time adaptation
    void adaptForRound(Wind prevailingWind, int dealerIndex,
                       const std::vector<const Player*>& allPlayers,
                       int wallRemaining);
    // Overload with pre-built observable state (for thread safety)
    void adaptForRound(Wind prevailingWind, int dealerIndex,
                       const ObservableState& obs, int wallRemaining);
    void resetAdaptation();

    // Configuration for adaptation (device-tier aware)
    void setAdaptationConfig(const AdaptationConfig& config);

    bool inferenceEngineAvailable() const;

private:
    Wind prevailingWind_ = Wind::East;
    std::vector<const Player*> allPlayers_;
    int turnCount_ = 0;
    int wallRemaining_ = 70;

    InferenceEngine* inferenceEngine_ = nullptr;
    DQNTrainer* trainer_ = nullptr;
    bool trainerClaimOnly_ = false;

    bool cooperativeMode_ = true;
    int lastDiscarder_ = -1;

    // Adapted engine for current round (null when not adapted)
    std::unique_ptr<AdaptiveEngine> adaptedEngine_;

    // Inference using adapted engine
    bool adaptedDiscard(std::function<void(Tile)> callback);
    bool adaptedClaim(Tile discardedTile,
                      const std::vector<ClaimOption>& options,
                      std::function<void(ClaimType)> callback);

    // Inference engine discard/claim (lightweight, no torch)
    bool inferenceDiscard(std::function<void(Tile)> callback);
    bool inferenceClaim(Tile discardedTile,
                        const std::vector<ClaimOption>& options,
                        std::function<void(ClaimType)> callback);

    // Heuristic fallback
    void heuristicDiscard(std::function<void(Tile)> callback);
    void heuristicClaim(Tile discardedTile,
                        const std::vector<ClaimOption>& options,
                        std::function<void(ClaimType)> callback);
};
