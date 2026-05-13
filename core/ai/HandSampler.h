#pragma once
#include "tiles/Tile.h"
#include "player/Hand.h"
#include "player/Meld.h"
#include "player/Player.h"
#include <vector>
#include <array>
#include <random>

struct ObservableState {
    const Hand* ownHand = nullptr;
    int ownPlayerIndex = 0;
    std::vector<Tile> allDiscards[4];       // discards from all 4 players
    std::vector<Meld> opponentMelds[3];     // exposed melds of 3 opponents
    std::vector<Tile> opponentFlowers[3];   // flower tiles of 3 opponents
    int opponentConcealedCount[3] = {};     // known concealed tile counts
};

struct SampledWorld {
    std::array<Hand, 4> hands;
    std::vector<Tile> wall;
};

class HandSampler {
public:
    // Build an ObservableState from the current game state
    static ObservableState buildObservable(int playerIndex,
                                           const std::vector<const Player*>& allPlayers);

    // Sample a plausible world consistent with what we can observe
    static SampledWorld sampleWorld(const ObservableState& obs, std::mt19937& rng);

private:
    // Build the full 144-tile set (same ID scheme as Wall::buildFullSet)
    static std::vector<Tile> buildFullTileSet();
};
