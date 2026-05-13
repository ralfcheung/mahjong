#include "Hand.h"

void Hand::addTile(Tile tile) {
    concealed_.push_back(tile);
}

void Hand::removeTileById(uint8_t tileId) {
    auto it = std::find_if(concealed_.begin(), concealed_.end(),
        [tileId](const Tile& t) { return t.id == tileId; });
    if (it != concealed_.end()) {
        concealed_.erase(it);
    }
}

void Hand::removeTileBySuitRank(Suit suit, uint8_t rank) {
    auto it = std::find_if(concealed_.begin(), concealed_.end(),
        [suit, rank](const Tile& t) { return t.suit == suit && t.rank == rank; });
    if (it != concealed_.end()) {
        concealed_.erase(it);
    }
}

void Hand::sortTiles() {
    std::sort(concealed_.begin(), concealed_.end());
}

void Hand::addMeld(Meld meld) {
    melds_.push_back(std::move(meld));
}

void Hand::addFlower(Tile flower) {
    flowers_.push_back(flower);
}

bool Hand::promoteToKong(Suit suit, uint8_t rank, Tile fourthTile) {
    for (auto& meld : melds_) {
        if (meld.type == MeldType::Pung && meld.suit() == suit && meld.rank() == rank) {
            meld.type = MeldType::Kong;
            meld.tiles.push_back(fourthTile);
            return true;
        }
    }
    return false;
}

bool Hand::hasTile(Suit suit, uint8_t rank) const {
    return std::any_of(concealed_.begin(), concealed_.end(),
        [suit, rank](const Tile& t) { return t.suit == suit && t.rank == rank; });
}

int Hand::countTile(Suit suit, uint8_t rank) const {
    return static_cast<int>(std::count_if(concealed_.begin(), concealed_.end(),
        [suit, rank](const Tile& t) { return t.suit == suit && t.rank == rank; }));
}

std::vector<Tile> Hand::allTiles() const {
    std::vector<Tile> all = concealed_;
    for (const auto& meld : melds_) {
        for (const auto& t : meld.tiles) {
            all.push_back(t);
        }
    }
    return all;
}

void Hand::clear() {
    concealed_.clear();
    melds_.clear();
    flowers_.clear();
}
