#pragma once
#include "player/Hand.h"
#include "player/Player.h"
#include "tiles/TileEnums.h"
#include "game/GameState.h"
#include <vector>

struct DiscardCandidate {
    Tile tile;
    int shantenAfterDiscard;
    int acceptCount;       // tiles that improve shanten after this discard
    float strategicScore;  // faan compatibility + defensive safety
    float dangerLevel = 0.0f;  // how dangerous this tile is against threatening opponents
};

struct FaanPlan {
    bool aimHalfFlush = false;
    bool aimFullFlush = false;
    Suit flushSuit = Suit::Bamboo;
    bool hasDragonPungPotential = false;
    bool hasWindPungPotential = false;
    float faanConfidence = 0.0f;  // 0-1: likelihood of reaching 3 faan
    bool aimThirteenOrphans = false;
    int thirteenOrphansShanten = 99;
    bool aimAllPungs = false;
};

class HandEvaluator {
public:
    // Determine the best discard. Returns candidates sorted best-first.
    static std::vector<DiscardCandidate> evaluateDiscards(
        const Hand& hand, Wind seatWind, Wind prevailingWind,
        const std::vector<const Player*>& allPlayers);

    // Analyze what faan patterns the hand is heading toward.
    static FaanPlan analyzeFaanPotential(
        const Hand& hand, Wind seatWind, Wind prevailingWind);

    // Evaluate whether claiming a meld is beneficial.
    // Returns positive = should claim, negative = should pass.
    static float evaluateClaim(
        const Hand& hand, Tile claimedTile, ClaimType claimType,
        Wind seatWind, Wind prevailingWind, int currentShanten,
        const std::vector<const Player*>& allPlayers = {},
        int selfIndex = -1);

private:
    static float defensiveScore(const Tile& candidate,
                                const std::vector<const Player*>& allPlayers,
                                int selfIndex);
};
