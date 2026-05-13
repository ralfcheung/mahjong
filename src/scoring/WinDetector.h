#pragma once
#include "tiles/Tile.h"
#include "player/Meld.h"
#include "player/Hand.h"
#include <vector>

struct WinDecomposition {
    std::vector<Meld> melds;  // 4 melds
    Meld eyes;                // 1 pair

    enum class SpecialType {
        None,
        ThirteenOrphans
    };
    SpecialType specialType = SpecialType::None;
};

struct WinContext {
    bool selfDrawn = false;
    bool lastWallTile = false;
    bool kongReplacement = false;
    bool robbedKong = false;
    Wind seatWind = Wind::East;
    Wind prevailingWind = Wind::East;
    bool isDealer = false;
    bool heavenlyHand = false;
    bool earthlyHand = false;
    bool humanHand = false;           // 人糊: non-dealer first-turn self-draw
    bool allEightFlowers = false;     // 大花糊: all 8 flowers collected
    int consecutiveKongs = 0;         // 連槓開花: consecutive kongs before win
    int turnCount = 0;
};

class WinDetector {
public:
    // Find all valid winning decompositions for the hand
    // winningTile is the 14th tile (either drawn or claimed)
    static std::vector<WinDecomposition> findWins(const Hand& hand, Tile winningTile);

    // Quick check: is the hand one tile away from winning?
    static bool isTenpai(const Hand& hand);

    // Which tiles would complete this hand?
    static std::vector<Tile> waitingTiles(const Hand& hand);

private:
    // Recursive decomposition of concealed tiles into melds + pair
    static void decompose(std::vector<Tile> tiles, std::vector<Meld>& currentMelds,
                          bool hasPair, std::vector<WinDecomposition>& results,
                          const std::vector<Meld>& existingMelds);

    // Check special hands
    static bool isThirteenOrphans(const std::vector<Tile>& tiles);
};
