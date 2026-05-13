#pragma once
#include "tiles/Tile.h"
#include <vector>

enum class MeldType : uint8_t {
    Chow,           // 3 consecutive same-suit (上)
    Pung,           // 3 identical (碰)
    Kong,           // 4 identical exposed (明槓)
    ConcealedKong,  // 4 identical concealed (暗槓)
    Pair            // 2 identical (眼) - only in win decomposition
};

struct Meld {
    MeldType type;
    std::vector<Tile> tiles;
    bool exposed = false;  // true if formed by claiming a discard

    Suit suit() const { return tiles.empty() ? Suit::Bamboo : tiles[0].suit; }
    uint8_t rank() const { return tiles.empty() ? 0 : tiles[0].rank; }

    bool isKong() const { return type == MeldType::Kong || type == MeldType::ConcealedKong; }
    bool isPung() const { return type == MeldType::Pung; }
    bool isChow() const { return type == MeldType::Chow; }
    bool isPungOrKong() const { return isPung() || isKong(); }

    int tileCount() const { return static_cast<int>(tiles.size()); }
};
