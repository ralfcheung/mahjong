#pragma once
#include "player/Player.h"
#include "tiles/TileEnums.h"
#include <vector>

struct CooperativeTarget {
    int playerIndex = -1;       // -1 = no target / cooperation disabled
    int shanten = 99;
    float faanConfidence = 0.0f;
    bool neededTiles[34] = {};  // tile types that reduce target's shanten
};

class CooperativeStrategy {
public:
    // Elect the AI player (indices 1-3) closest to winning.
    // Returns playerIndex = -1 if cooperation is disabled (best shanten >= 4).
    static CooperativeTarget electTarget(
        const std::vector<const Player*>& allPlayers,
        Wind prevailingWind);

    // Check if the cooperator can feed a tile the target needs without
    // worsening own shanten by more than 1. Returns true and sets outTile
    // if a cooperative discard is found.
    static bool findCooperativeDiscard(
        const Hand& selfHand, Wind seatWind, Wind prevailingWind,
        const std::vector<const Player*>& allPlayers,
        const CooperativeTarget& target,
        Tile& outTile);

    // Check if a non-target AI should suppress its claim so the target
    // can claim the tile instead.
    static bool shouldSuppressClaim(
        Tile discardedTile, int discarderIndex,
        const CooperativeTarget& target,
        const std::vector<const Player*>& allPlayers);
};
