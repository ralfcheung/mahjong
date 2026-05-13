#include "Wall.h"
#include <algorithm>
#include <chrono>

Wall::Wall() : rng_(static_cast<unsigned>(std::chrono::steady_clock::now().time_since_epoch().count())) {
    buildFullSet();
}

void Wall::buildFullSet() {
    tiles_.clear();
    tiles_.reserve(144);
    uint8_t id = 0;

    // Numbered suits: Bamboo, Characters, Dots - each rank 1-9, 4 copies
    for (Suit suit : {Suit::Bamboo, Suit::Characters, Suit::Dots}) {
        for (uint8_t rank = 1; rank <= 9; rank++) {
            for (int copy = 0; copy < 4; copy++) {
                tiles_.push_back({suit, rank, id++});
            }
        }
    }

    // Wind tiles: East(1), South(2), West(3), North(4) - 4 copies each
    for (uint8_t rank = 1; rank <= 4; rank++) {
        for (int copy = 0; copy < 4; copy++) {
            tiles_.push_back({Suit::Wind, rank, id++});
        }
    }

    // Dragon tiles: Red(1), Green(2), White(3) - 4 copies each
    for (uint8_t rank = 1; rank <= 3; rank++) {
        for (int copy = 0; copy < 4; copy++) {
            tiles_.push_back({Suit::Dragon, rank, id++});
        }
    }

    // Flower tiles: 1-4, one copy each
    for (uint8_t rank = 1; rank <= 4; rank++) {
        tiles_.push_back({Suit::Flower, rank, id++});
    }

    // Season tiles: 1-4, one copy each
    for (uint8_t rank = 1; rank <= 4; rank++) {
        tiles_.push_back({Suit::Season, rank, id++});
    }
}

void Wall::shuffle() {
    std::shuffle(tiles_.begin(), tiles_.end(), rng_);
    frontIndex_ = 0;
    backIndex_ = static_cast<int>(tiles_.size()) - 1;
}

Tile Wall::draw() {
    return tiles_[frontIndex_++];
}

Tile Wall::drawReplacement() {
    return tiles_[backIndex_--];
}

bool Wall::isEmpty() const {
    return frontIndex_ > backIndex_;
}

int Wall::remaining() const {
    return backIndex_ - frontIndex_ + 1;
}
