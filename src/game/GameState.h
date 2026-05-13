#pragma once
#include <cstdint>
#include <vector>
#include "tiles/Tile.h"

enum class GamePhase : uint8_t {
    DEALING,              // Distributing tiles from wall to players
    REPLACING_FLOWERS,    // Replacing bonus tiles with wall draws
    PLAYER_DRAW,          // Active player draws a tile
    PLAYER_TURN,          // Active player decides: discard, declare kong, or win (tsumo)
    DISCARD_ANIMATION,    // Brief pause for discard animation
    CLAIM_PHASE,          // Other players may claim the discard
    CLAIM_RESOLUTION,     // Determine highest-priority claim
    MELD_FORMATION,       // Form the claimed meld
    REPLACEMENT_DRAW,     // Draw replacement tile after kong or flower
    SCORING,              // Calculate faan, display result
    ROUND_END,            // Summary screen, transition to next round
    GAME_OVER             // Final scores
};

enum class ClaimType : uint8_t {
    None,
    Chow,   // 上
    Pung,   // 碰
    Kong,   // 槓
    Win     // 糊
};

struct ClaimOption {
    ClaimType type = ClaimType::None;
    int playerIndex = -1;
    // For chow, which tiles from hand are used (multiple possible chow combos)
    std::vector<Tile> meldTiles;
};
