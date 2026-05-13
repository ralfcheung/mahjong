#pragma once
#include "player/Hand.h"

class ShantenCalculator {
public:
    // Calculate minimum shanten number for the hand.
    // -1 = complete (already a winning hand)
    //  0 = tenpai (one tile away from winning)
    //  1 = iishanten (two tiles away)
    //  etc.
    static int calculate(const Hand& hand);

    // Shanten after removing a specific tile type from concealed hand (for discard eval).
    static int calculateAfterDiscard(const Hand& hand, Suit suit, uint8_t rank);

    // Shanten after claiming a pung (remove 2 from concealed, add pung meld).
    static int calculateAfterPung(const Hand& hand, Suit suit, uint8_t rank);

    // Shanten after claiming a chow (remove 2 from concealed, add chow meld).
    // claimedRank is the discarded tile's rank; handRank1/handRank2 are the two from hand.
    static int calculateAfterChow(const Hand& hand, Suit claimedSuit,
                                   uint8_t claimedRank, uint8_t handRank1, uint8_t handRank2);

    // Count how many of the 34 tile types would reduce shanten after discarding a tile.
    // Returns acceptance count (0-34). Higher = more flexible/efficient hand after discard.
    static int countAcceptanceAfterDiscard(const Hand& hand, Suit discardSuit, uint8_t discardRank);

    // Count acceptance for the current hand (without discarding first).
    static int countAcceptance(const Hand& hand);

private:
    // Compact tile count representation
    // Groups: 0=Bamboo, 1=Characters, 2=Dots, 3=Honors (winds 0-3, dragons 4-6)
    struct Counts {
        int c[4][9] = {};
        int existingMelds = 0;
    };

    static Counts buildCounts(const Hand& hand);
    static int calcFromCounts(const Counts& counts);
    static int calcSpecialShanten(const Counts& counts);
};
