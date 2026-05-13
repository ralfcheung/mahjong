#pragma once
#include "game/GameState.h"
#include "tiles/Tile.h"
#include "tiles/TileEnums.h"
#include "player/Meld.h"
#include <vector>
#include <array>
#include <string>

struct ScoringEntry {
    std::string nameCn;
    std::string nameEn;
    int faan = 0;
};

struct ScoringSnapshot {
    int winnerIndex = -1;
    bool selfDrawn = false;
    bool isDraw = false;
    int totalFaan = 0;
    bool isLimit = false;
    int basePoints = 0;
    std::vector<ScoringEntry> breakdown;
};

struct PlayerSnapshot {
    std::vector<Tile> concealed;   // Full tiles for human player
    int concealedCount = 0;        // Tile count (for AI players, hides actual tiles)
    std::vector<Meld> melds;
    std::vector<Tile> flowers;
    std::vector<Tile> discards;
    std::string name;
    Wind seatWind = Wind::East;
    int score = 0;
    bool isHuman = false;
};

struct SelfKongEntry {
    uint8_t suit;
    uint8_t rank;
    bool isPromote;
};

struct GameSnapshot {
    GamePhase phase = GamePhase::DEALING;
    int activePlayerIndex = 0;
    Wind prevailingWind = Wind::East;
    int wallRemaining = 0;

    std::array<PlayerSnapshot, 4> players;

    std::vector<ClaimOption> humanClaimOptions;
    std::vector<SelfKongEntry> selfKongOptions;
    bool canSelfKong = false;
    bool canSelfDrawWin = false;

    std::string scoringText;
    ScoringSnapshot scoring;

    bool hasLastDiscard = false;
    Tile lastDiscard{};

    bool aiThinking = false;
};
