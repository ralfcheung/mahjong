#pragma once
#include "Tile.h"
#include <vector>
#include <random>

class Wall {
public:
    Wall();

    void shuffle();
    Tile draw();                  // Draw from front (live wall)
    Tile drawReplacement();       // Draw from back (dead wall, for kong/flower)
    bool isEmpty() const;
    int remaining() const;

    const std::vector<Tile>& allTiles() const { return tiles_; }

private:
    void buildFullSet();

    std::vector<Tile> tiles_;
    int frontIndex_ = 0;
    int backIndex_ = 0;
    std::mt19937 rng_;
};
