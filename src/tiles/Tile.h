#pragma once
#include "TileEnums.h"
#include "TileDefs.h"
#include <cstdint>
#include <string>

struct Tile {
    Suit suit;
    uint8_t rank;   // 1-9 for numbered suits, 1-4 for winds, 1-3 for dragons, 1-4 for flowers/seasons
    uint8_t id;     // Unique 0-143 for tracking individual tile instances

    bool isHonor() const { return isHonorSuit(suit); }
    bool isTerminal() const { return isNumberedSuit(suit) && (rank == 1 || rank == 9); }
    bool isTerminalOrHonor() const { return isHonor() || isTerminal(); }
    bool isSimple() const { return isNumberedSuit(suit) && rank >= 2 && rank <= 8; }
    bool isBonus() const { return isBonusSuit(suit); }

    bool sameAs(const Tile& other) const { return suit == other.suit && rank == other.rank; }

    bool operator<(const Tile& other) const {
        if (suit != other.suit) return suit < other.suit;
        return rank < other.rank;
    }

    bool operator==(const Tile& other) const {
        return id == other.id;
    }

    bool operator!=(const Tile& other) const {
        return id != other.id;
    }

    const char* chineseName() const {
        switch (suit) {
            case Suit::Bamboo:     return CHAR_BAMBOO_CN[rank - 1];
            case Suit::Characters: return CHAR_WAN_CN[rank - 1];
            case Suit::Dots:       return CHAR_DOTS_CN[rank - 1];
            case Suit::Wind:       return CHAR_WIND_CN[rank - 1];
            case Suit::Dragon:     return CHAR_DRAGON_CN[rank - 1];
            case Suit::Flower:     return CHAR_FLOWER_CN[rank - 1];
            case Suit::Season:     return CHAR_SEASON_CN[rank - 1];
        }
        return "?";
    }

    const char* englishName() const {
        switch (suit) {
            case Suit::Bamboo:     return CHAR_BAMBOO_EN[rank - 1];
            case Suit::Characters: return CHAR_WAN_EN[rank - 1];
            case Suit::Dots:       return CHAR_DOTS_EN[rank - 1];
            case Suit::Wind:       return CHAR_WIND_EN[rank - 1];
            case Suit::Dragon:     return CHAR_DRAGON_EN[rank - 1];
            case Suit::Flower:     return CHAR_FLOWER_EN[rank - 1];
            case Suit::Season:     return CHAR_SEASON_EN[rank - 1];
        }
        return "?";
    }

    std::string toString() const {
        return std::string(chineseName()) + " (" + englishName() + ")";
    }
};
