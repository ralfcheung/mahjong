#pragma once
#include "GameSnapshot.h"
#include "game/TurnManager.h"
#include "player/Player.h"
#include "player/HumanPlayer.h"
#include "player/AIPlayer.h"
#include "ai/AdaptiveEngine.h"
#include "ai/HandSampler.h"
#include <array>
#include <memory>
#include <functional>
#include <vector>
#include <thread>
#include <atomic>

class GameController {
public:
    GameController();
    ~GameController();

    void startNewGame();
    void startSelfPlayGame();  // All 4 players are AIPlayer
    void update(float dt);

    // Human actions
    void humanDiscardTile(uint8_t tileId);
    void humanClaim(const ClaimOption& option);
    void humanSelfDrawWin();
    void humanSelfKong(Suit suit, uint8_t rank);
    void advanceRound();

    // State queries
    GameSnapshot snapshot() const;
    bool isGameOver() const;
    bool isRoundOver() const;
    GamePhase currentPhase() const;
    std::vector<ClaimOption> humanClaimOptions() const;
    bool canHumanSelfDrawWin() const;
    std::vector<TurnManager::SelfKongOption> humanSelfKongOptions() const;
    const std::vector<Tile>& humanHandTiles() const;

    // Round info
    Wind prevailingWind() const { return prevailingWind_; }
    int dealerIndex() const { return dealerIndex_; }
    int roundNumber() const { return roundNumber_; }

    // Per-round adaptation tier (0=skip, 1=low, 2=mid, 3=desktop)
    void setAdaptationTier(int tier);

    // Cooperative AI: 3 AIs coordinate against the human. Default on.
    void setCooperativeAI(bool enabled);

    // True while AI adaptation is running on a background thread
    bool isAIThinking() const { return aiAdapting_.load(std::memory_order_relaxed); }

    // Desktop bridge: direct Player access for existing Renderer
    const Player* getPlayer(int index) const;
    Player* getPlayer(int index);
    TurnManager* turnManager() { return turnManager_.get(); }
    const TurnManager* turnManager() const { return turnManager_.get(); }

    // Observer for round completion (used by desktop for training hooks)
    using RoundAdvanceCallback = std::function<void()>;
    void setRoundAdvanceCallback(RoundAdvanceCallback cb) { roundAdvanceCb_ = std::move(cb); }

    // Multi-observer API (multiple observers can coexist, persisted across rounds)
    void addDiscardObserver(TurnManager::DiscardObserver obs);
    void addClaimObserver(TurnManager::ClaimObserver obs);
    void addRoundEndObserver(TurnManager::RoundEndObserver obs);
    void addSelfKongObserver(TurnManager::SelfKongObserver obs);
    void addSelfDrawWinObserver(TurnManager::SelfDrawWinObserver obs);

    // Deprecated: single-observer setters (clear + add)
    void setDiscardObserver(TurnManager::DiscardObserver obs);
    void setClaimObserver(TurnManager::ClaimObserver obs);
    void setRoundEndObserver(TurnManager::RoundEndObserver obs);

    // Access players array (for training setup)
    std::array<std::unique_ptr<Player>, 4>& players() { return players_; }

private:
    void setupTurnManager();

    std::array<std::unique_ptr<Player>, 4> players_;
    std::unique_ptr<TurnManager> turnManager_;

    Wind prevailingWind_ = Wind::East;
    int dealerIndex_ = 0;
    int roundNumber_ = 0;
    bool gameOver_ = false;

    RoundAdvanceCallback roundAdvanceCb_;

    // Per-turn adaptation (runs on background thread for active AI)
    int lastAdaptedPlayer_ = -1;
    std::atomic<bool> aiAdapting_{false};
    std::thread adaptationThread_;
    void triggerAdaptationForPlayer(int playerIndex);
    void resetAdaptation();

    // Stored observers (re-wired to each new TurnManager)
    std::vector<TurnManager::DiscardObserver> discardObservers_;
    std::vector<TurnManager::ClaimObserver> claimObservers_;
    std::vector<TurnManager::RoundEndObserver> roundEndObservers_;
    std::vector<TurnManager::SelfKongObserver> selfKongObservers_;
    std::vector<TurnManager::SelfDrawWinObserver> selfDrawWinObservers_;
    void wireTurnManagerObservers();
};
