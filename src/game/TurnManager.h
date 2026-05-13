#pragma once
#include "GameState.h"
#include "tiles/Wall.h"
#include "player/Player.h"
#include "scoring/Scoring.h"
#include "scoring/PaymentCalculator.h"
#include <array>
#include <memory>
#include <functional>
#include <vector>
#include <string>

class TurnManager {
public:
    TurnManager(std::array<std::unique_ptr<Player>, 4>& players,
                Wind prevailingWind, int dealerIndex);

    void startRound();
    void update(float dt);

    GamePhase phase() const { return phase_; }
    int activePlayer() const { return activePlayer_; }
    bool isRoundOver() const { return roundOver_; }
    int wallRemaining() const { return wall_.remaining(); }
    const Tile* lastDiscard() const { return lastDiscard_ ? &lastDiscardTile_ : nullptr; }
    Wall& wall() { return wall_; }

    // For checking which claims are available for the human player
    std::vector<ClaimType> getHumanClaimOptions() const;

    // Returns full claim options with chow combos expanded (each valid chow sequence is separate)
    std::vector<ClaimOption> getHumanClaimOptionsWithCombos() const;

    // Self-kong options during player's turn (concealed kong or promoted kong)
    // Returns list of tiles that can be konged (suit+rank identifier tiles)
    struct SelfKongOption {
        Suit suit;
        uint8_t rank;
        bool isPromote; // true = add to existing pung, false = concealed kong
    };
    std::vector<SelfKongOption> getHumanSelfKongOptions() const;

    // Check if human can declare self-draw win
    bool canHumanSelfDrawWin() const { return humanCanSelfDraw_; }

    // Called by input handler when human declares self-draw win
    void onHumanSelfDrawWin();

    // Called by input handler when human selects a self-kong
    void onHumanSelfKong(Suit suit, uint8_t rank);

    // Called by input handler when human selects a claim
    void onHumanClaim(const ClaimOption& option);

    // Turn info accessors
    int turnCount() const { return turnCount_; }

    // Observer callbacks for neural network training and game recording
    using DiscardObserver = std::function<void(int playerIndex, Tile discarded, int turnCount, int wallRemaining)>;
    using ClaimObserver = std::function<void(int playerIndex, ClaimType type, Tile claimedTile, int turnCount, int wallRemaining)>;
    using RoundEndObserver = std::function<void(int winnerIndex, bool selfDrawn, const FaanResult& result, bool isDraw)>;
    using SelfKongObserver = std::function<void(int playerIndex, Suit suit, uint8_t rank, int turnCount, int wallRemaining)>;
    using SelfDrawWinObserver = std::function<void(int playerIndex, Tile winningTile, int turnCount, int wallRemaining)>;

    // Multi-observer API (multiple observers can coexist)
    void addDiscardObserver(DiscardObserver observer);
    void addClaimObserver(ClaimObserver observer);
    void addRoundEndObserver(RoundEndObserver observer);
    void addSelfKongObserver(SelfKongObserver observer);
    void addSelfDrawWinObserver(SelfDrawWinObserver observer);
    void clearObservers();

    // Deprecated: single-observer setters (clear + add)
    void setDiscardObserver(DiscardObserver observer);
    void setClaimObserver(ClaimObserver observer);
    void setRoundEndObserver(RoundEndObserver observer);

    // Result
    int winnerIndex() const { return winnerIndex_; }
    bool isSelfDrawn() const { return selfDrawn_; }
    const FaanResult& scoringResult() const { return scoringResult_; }
    std::string scoringText() const { return scoringText_; }

private:
    void enterPhase(GamePhase newPhase);
    void deal();
    void replaceFlowers();
    void playerDraw();
    void handlePlayerTurn(float dt);
    void handleClaimPhase(float dt);
    void advanceToNextPlayer();

    // Claim helpers
    std::vector<ClaimOption> computeClaimOptions(Tile discard);
    void resolveClaim(ClaimType type, int claimingPlayer, const std::vector<Tile>& meldTiles = {});

    // Check if a player can win with a tile
    bool canWinWith(int playerIndex, Tile tile) const;

    std::array<std::unique_ptr<Player>, 4>& players_;
    Wind prevailingWind_;
    int dealerIndex_;
    Wall wall_;

    GamePhase phase_ = GamePhase::DEALING;
    int activePlayer_ = 0;
    float phaseTimer_ = 0.0f;
    bool roundOver_ = false;

    Tile lastDiscardTile_{};
    bool lastDiscard_ = false;
    int lastDiscardPlayer_ = -1;

    int winnerIndex_ = -1;
    bool selfDrawn_ = false;

    // Claim phase
    std::vector<ClaimOption> pendingClaims_;
    bool humanClaimPending_ = false;
    ClaimOption humanClaimChoice_;
    bool humanClaimResolved_ = false;
    float claimTimer_ = 0.0f;
    static constexpr float CLAIM_TIME_LIMIT = 60.0f;
    int aiClaimPlayer_ = -1;
    ClaimType aiClaimType_ = ClaimType::None;

    bool waitingForHumanDiscard_ = false;
    bool humanCanSelfDraw_ = false;
    bool firstTurn_ = true;
    int turnCount_ = 0;
    bool isKongReplacement_ = false;
    int consecutiveKongs_ = 0;

    // Self-kong options the human has declined (won't be offered again)
    std::vector<std::pair<Suit, uint8_t>> declinedSelfKongs_;

    Tile lastDrawnTile_{};
    bool hasLastDrawn_ = false;

    FaanResult scoringResult_;
    std::string scoringText_;

    void checkSelfDrawWin();
    void handleScoring(int winner, bool selfDraw, Tile winTile);
    void resolveSelfKong(int playerIdx, Suit suit, uint8_t rank);

    // Observers (multi-observer vectors)
    std::vector<DiscardObserver> discardObservers_;
    std::vector<ClaimObserver> claimObservers_;
    std::vector<RoundEndObserver> roundEndObservers_;
    std::vector<SelfKongObserver> selfKongObservers_;
    std::vector<SelfDrawWinObserver> selfDrawWinObservers_;
};
