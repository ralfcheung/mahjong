#include "WinDetector.h"
#include <algorithm>
#include <set>

std::vector<WinDecomposition> WinDetector::findWins(const Hand& hand, Tile winningTile) {
    std::vector<WinDecomposition> results;

    // Combine concealed tiles + winning tile
    std::vector<Tile> tiles = hand.concealed();
    tiles.push_back(winningTile);
    std::sort(tiles.begin(), tiles.end());

    // Check special hands first
    if (hand.melds().empty()) {
        if (isThirteenOrphans(tiles)) {
            WinDecomposition wd;
            wd.specialType = WinDecomposition::SpecialType::ThirteenOrphans;
            results.push_back(wd);
        }
    }

    // Standard decomposition: existing melds from hand + decompose concealed
    int meldsNeeded = 4 - (int)hand.melds().size();
    std::vector<Meld> currentMelds;

    // We need to decompose the concealed tiles into (meldsNeeded) melds + 1 pair
    decompose(tiles, currentMelds, false, results, hand.melds());

    return results;
}

void WinDetector::decompose(std::vector<Tile> tiles, std::vector<Meld>& currentMelds,
                             bool hasPair, std::vector<WinDecomposition>& results,
                             const std::vector<Meld>& existingMelds) {
    if (tiles.empty()) {
        if (hasPair) {
            int totalMelds = (int)existingMelds.size() + (int)currentMelds.size();
            // Count: melds that are not pairs should be exactly 4, and we need 1 pair
            int pairCount = 0;
            int meldCount = 0;
            for (const auto& m : currentMelds) {
                if (m.type == MeldType::Pair) pairCount++;
                else meldCount++;
            }
            meldCount += (int)existingMelds.size();
            if (meldCount == 4 && pairCount == 1) {
                WinDecomposition wd;
                for (const auto& m : currentMelds) {
                    if (m.type == MeldType::Pair) wd.eyes = m;
                    else wd.melds.push_back(m);
                }
                // Add existing exposed melds
                for (const auto& m : existingMelds) {
                    wd.melds.push_back(m);
                }
                results.push_back(wd);
            }
        }
        return;
    }

    // Try pair with first tile
    if (!hasPair && tiles.size() >= 2) {
        if (tiles[0].sameAs(tiles[1])) {
            Meld pair;
            pair.type = MeldType::Pair;
            pair.tiles = {tiles[0], tiles[1]};
            currentMelds.push_back(pair);

            std::vector<Tile> remaining(tiles.begin() + 2, tiles.end());
            decompose(remaining, currentMelds, true, results, existingMelds);

            currentMelds.pop_back();
        }
    }

    // Try pung with first tile
    if (tiles.size() >= 3) {
        if (tiles[0].sameAs(tiles[1]) && tiles[1].sameAs(tiles[2])) {
            Meld pung;
            pung.type = MeldType::Pung;
            pung.tiles = {tiles[0], tiles[1], tiles[2]};
            currentMelds.push_back(pung);

            std::vector<Tile> remaining(tiles.begin() + 3, tiles.end());
            decompose(remaining, currentMelds, hasPair, results, existingMelds);

            currentMelds.pop_back();
        }
    }

    // Try chow with first tile (only for numbered suits)
    if (tiles.size() >= 3 && isNumberedSuit(tiles[0].suit)) {
        Suit s = tiles[0].suit;
        uint8_t r = tiles[0].rank;

        // Find tiles with rank r+1 and r+2 of same suit
        auto findTile = [&tiles](Suit s, uint8_t r, int startFrom) -> int {
            for (int i = startFrom; i < (int)tiles.size(); i++) {
                if (tiles[i].suit == s && tiles[i].rank == r) return i;
            }
            return -1;
        };

        int idx1 = findTile(s, r + 1, 1);
        int idx2 = (idx1 >= 0) ? findTile(s, r + 2, idx1 + 1) : -1;

        if (idx1 >= 0 && idx2 >= 0) {
            Meld chow;
            chow.type = MeldType::Chow;
            chow.tiles = {tiles[0], tiles[idx1], tiles[idx2]};
            currentMelds.push_back(chow);

            std::vector<Tile> remaining;
            for (int i = 0; i < (int)tiles.size(); i++) {
                if (i != 0 && i != idx1 && i != idx2) {
                    remaining.push_back(tiles[i]);
                }
            }
            decompose(remaining, currentMelds, hasPair, results, existingMelds);

            currentMelds.pop_back();
        }
    }
}

bool WinDetector::isThirteenOrphans(const std::vector<Tile>& tiles) {
    if (tiles.size() != 14) return false;

    // Need one each of: 1,9 of each suit, all 4 winds, all 3 dragons + 1 duplicate
    struct TileSpec { Suit suit; uint8_t rank; };
    TileSpec needed[] = {
        {Suit::Bamboo, 1}, {Suit::Bamboo, 9},
        {Suit::Characters, 1}, {Suit::Characters, 9},
        {Suit::Dots, 1}, {Suit::Dots, 9},
        {Suit::Wind, 1}, {Suit::Wind, 2}, {Suit::Wind, 3}, {Suit::Wind, 4},
        {Suit::Dragon, 1}, {Suit::Dragon, 2}, {Suit::Dragon, 3}
    };

    int counts[13] = {};
    for (const auto& t : tiles) {
        for (int i = 0; i < 13; i++) {
            if (t.suit == needed[i].suit && t.rank == needed[i].rank) {
                counts[i]++;
                break;
            }
        }
    }

    int total = 0;
    bool hasPair = false;
    for (int i = 0; i < 13; i++) {
        if (counts[i] == 0) return false;
        if (counts[i] == 2) hasPair = true;
        total += counts[i];
    }

    return total == 14 && hasPair;
}

bool WinDetector::isTenpai(const Hand& hand) {
    // Try every possible tile as the winning tile
    for (Suit suit : {Suit::Bamboo, Suit::Characters, Suit::Dots}) {
        for (uint8_t rank = 1; rank <= 9; rank++) {
            Tile test{suit, rank, 255};
            auto wins = findWins(hand, test);
            if (!wins.empty()) return true;
        }
    }
    for (uint8_t rank = 1; rank <= 4; rank++) {
        Tile test{Suit::Wind, rank, 255};
        auto wins = findWins(hand, test);
        if (!wins.empty()) return true;
    }
    for (uint8_t rank = 1; rank <= 3; rank++) {
        Tile test{Suit::Dragon, rank, 255};
        auto wins = findWins(hand, test);
        if (!wins.empty()) return true;
    }
    return false;
}

std::vector<Tile> WinDetector::waitingTiles(const Hand& hand) {
    std::vector<Tile> waits;
    for (Suit suit : {Suit::Bamboo, Suit::Characters, Suit::Dots}) {
        for (uint8_t rank = 1; rank <= 9; rank++) {
            Tile test{suit, rank, 255};
            auto wins = findWins(hand, test);
            if (!wins.empty()) waits.push_back(test);
        }
    }
    for (uint8_t rank = 1; rank <= 4; rank++) {
        Tile test{Suit::Wind, rank, 255};
        auto wins = findWins(hand, test);
        if (!wins.empty()) waits.push_back(test);
    }
    for (uint8_t rank = 1; rank <= 3; rank++) {
        Tile test{Suit::Dragon, rank, 255};
        auto wins = findWins(hand, test);
        if (!wins.empty()) waits.push_back(test);
    }
    return waits;
}
