#pragma once
#include "tiles/Tile.h"
#include "Meld.h"
#include <vector>
#include <algorithm>

class Hand {
public:
    void addTile(Tile tile);
    void removeTileById(uint8_t tileId);
    void removeTileBySuitRank(Suit suit, uint8_t rank);
    void sortTiles();

    const std::vector<Tile>& concealed() const { return concealed_; }
    const std::vector<Meld>& melds() const { return melds_; }
    const std::vector<Tile>& flowers() const { return flowers_; }

    std::vector<Tile>& concealedMut() { return concealed_; }

    void addMeld(Meld meld);
    void addFlower(Tile flower);

    // Promote an exposed pung to a kong by adding the 4th tile
    bool promoteToKong(Suit suit, uint8_t rank, Tile fourthTile);

    int concealedCount() const { return static_cast<int>(concealed_.size()); }
    bool hasTile(Suit suit, uint8_t rank) const;
    int countTile(Suit suit, uint8_t rank) const;

    // Get all tiles (concealed + meld tiles) for scoring analysis
    std::vector<Tile> allTiles() const;

    void clear();

private:
    std::vector<Tile> concealed_;
    std::vector<Meld> melds_;
    std::vector<Tile> flowers_;
};
