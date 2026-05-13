#pragma once
#include "GameSnapshot.h"
#include "game/GameState.h"
#include "tiles/Tile.h"
#include "tiles/TileEnums.h"
#include "player/Meld.h"
#include "scoring/Scoring.h"
#include <vector>
#include <string>
#include <array>
#include <functional>

class GameController;  // forward declare

class GameRecorder {
public:
    void attach(GameController& controller);
    void beginRound(const GameController& controller);
    void setClientId(const std::string& id);

    std::string toJson() const;
    void clear();
    bool hasData() const;
    int decisionCount() const;

private:
    void onDiscard(int playerIndex, Tile discarded, int turnCount, int wallRemaining);
    void onClaim(int playerIndex, ClaimType type, Tile claimedTile, int turnCount, int wallRemaining);
    void onSelfKong(int playerIndex, Suit suit, uint8_t rank, int turnCount, int wallRemaining);
    void onSelfDrawWin(int playerIndex, Tile winningTile, int turnCount, int wallRemaining);
    void onRoundEnd(int winnerIndex, bool selfDrawn, const FaanResult& result, bool isDraw);

    GameController* controller_ = nullptr;
    std::string clientId_;

    struct Decision {
        std::string type;  // "discard", "claim", "selfKong", "selfDrawWin"
        int turnCount = 0;
        int wallRemaining = 0;
        GameSnapshot snapshot;
        Tile discardedTile{};
        ClaimType claimType = ClaimType::None;
        Tile claimedTile{};
        Suit kongSuit{};
        uint8_t kongRank = 0;
        Tile winningTile{};
    };

    struct RoundResult {
        int winnerIndex = -1;
        bool selfDrawn = false;
        bool isDraw = false;
        FaanResult faanResult;
        std::array<int, 4> finalScores{};
    };

    struct RoundRecord {
        int roundNumber = 0;
        Wind prevailingWind = Wind::East;
        int dealerIndex = 0;
        std::vector<Decision> decisions;
        RoundResult result;
    };

    std::vector<RoundRecord> rounds_;
    RoundRecord currentRound_;
};
