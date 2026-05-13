#pragma once
#include <cstdint>
#include <string>

enum class Suit : uint8_t {
    Bamboo,      // 索/條
    Characters,  // 萬
    Dots,        // 筒
    Wind,        // 風
    Dragon,      // 三元
    Flower,      // 花
    Season       // 季
};

enum class Wind : uint8_t {
    East = 0,   // 東
    South = 1,  // 南
    West = 2,   // 西
    North = 3   // 北
};

inline Wind nextWind(Wind w) {
    return static_cast<Wind>((static_cast<int>(w) + 1) % 4);
}

inline bool isNumberedSuit(Suit s) {
    return s == Suit::Bamboo || s == Suit::Characters || s == Suit::Dots;
}

inline bool isHonorSuit(Suit s) {
    return s == Suit::Wind || s == Suit::Dragon;
}

inline bool isBonusSuit(Suit s) {
    return s == Suit::Flower || s == Suit::Season;
}

inline const char* suitToString(Suit s) {
    switch (s) {
        case Suit::Bamboo:     return "Bamboo";
        case Suit::Characters: return "Characters";
        case Suit::Dots:       return "Dots";
        case Suit::Wind:       return "Wind";
        case Suit::Dragon:     return "Dragon";
        case Suit::Flower:     return "Flower";
        case Suit::Season:     return "Season";
    }
    return "?";
}

inline const char* windToString(Wind w) {
    switch (w) {
        case Wind::East:  return "East";
        case Wind::South: return "South";
        case Wind::West:  return "West";
        case Wind::North: return "North";
    }
    return "?";
}
