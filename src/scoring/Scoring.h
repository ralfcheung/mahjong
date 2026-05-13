#pragma once
#include "WinDetector.h"
#include "player/Hand.h"
#include <vector>
#include <string>

struct FaanEntry {
    std::string nameCn;
    std::string nameEn;
    int faan;
};

struct FaanResult {
    int totalFaan = 0;
    std::vector<FaanEntry> breakdown;
    bool isLimit = false;
};

class Scoring {
public:
    static constexpr int LIMIT_FAAN = 13;
    static constexpr int MIN_FAAN = 3;  // HK standard minimum

    // Calculate faan for the best decomposition
    static FaanResult calculate(const Hand& hand, Tile winningTile,
                                const WinContext& context);

    // Calculate for a specific decomposition
    static FaanResult calculateForDecomposition(
        const WinDecomposition& decomp,
        const Hand& hand,
        Tile winningTile,
        const WinContext& context);

    // Convert faan to base payment points
    static int faanToPoints(int faan);
};
