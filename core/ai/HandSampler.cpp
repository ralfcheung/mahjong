#include "HandSampler.h"
#include <algorithm>

ObservableState HandSampler::buildObservable(int playerIndex,
                                              const std::vector<const Player*>& allPlayers) {
    ObservableState obs;
    obs.ownPlayerIndex = playerIndex;

    if (playerIndex >= 0 && playerIndex < (int)allPlayers.size() && allPlayers[playerIndex]) {
        obs.ownHand = &allPlayers[playerIndex]->hand();
    }

    // Collect discards from all players
    for (int i = 0; i < 4 && i < (int)allPlayers.size(); i++) {
        if (allPlayers[i]) {
            obs.allDiscards[i] = allPlayers[i]->discards();
        }
    }

    // Collect opponent info
    int oppSlot = 0;
    for (int i = 0; i < 4 && i < (int)allPlayers.size(); i++) {
        if (i == playerIndex) continue;
        if (oppSlot >= 3) break;
        const Player* opp = allPlayers[i];
        if (opp) {
            obs.opponentMelds[oppSlot] = opp->hand().melds();
            obs.opponentFlowers[oppSlot] = opp->hand().flowers();
            obs.opponentConcealedCount[oppSlot] = opp->hand().concealedCount();
        }
        oppSlot++;
    }

    return obs;
}

std::vector<Tile> HandSampler::buildFullTileSet() {
    std::vector<Tile> tiles;
    tiles.reserve(144);
    uint8_t id = 0;

    // Numbered suits: Bamboo, Characters, Dots - each rank 1-9, 4 copies
    for (Suit suit : {Suit::Bamboo, Suit::Characters, Suit::Dots}) {
        for (uint8_t rank = 1; rank <= 9; rank++) {
            for (int copy = 0; copy < 4; copy++) {
                tiles.push_back({suit, rank, id++});
            }
        }
    }

    // Wind tiles: East(1), South(2), West(3), North(4) - 4 copies each
    for (uint8_t rank = 1; rank <= 4; rank++) {
        for (int copy = 0; copy < 4; copy++) {
            tiles.push_back({Suit::Wind, rank, id++});
        }
    }

    // Dragon tiles: Red(1), Green(2), White(3) - 4 copies each
    for (uint8_t rank = 1; rank <= 3; rank++) {
        for (int copy = 0; copy < 4; copy++) {
            tiles.push_back({Suit::Dragon, rank, id++});
        }
    }

    // Flower tiles: 1-4, one copy each
    for (uint8_t rank = 1; rank <= 4; rank++) {
        tiles.push_back({Suit::Flower, rank, id++});
    }

    // Season tiles: 1-4, one copy each
    for (uint8_t rank = 1; rank <= 4; rank++) {
        tiles.push_back({Suit::Season, rank, id++});
    }

    return tiles;
}

SampledWorld HandSampler::sampleWorld(const ObservableState& obs, std::mt19937& rng) {
    SampledWorld world;

    // Mark all visible tile IDs
    std::vector<bool> visible(144, false);

    // Own hand: concealed tiles
    if (obs.ownHand) {
        for (const auto& t : obs.ownHand->concealed()) {
            if (t.id < 144) visible[t.id] = true;
        }
        // Own melds
        for (const auto& m : obs.ownHand->melds()) {
            for (const auto& t : m.tiles) {
                if (t.id < 144) visible[t.id] = true;
            }
        }
        // Own flowers
        for (const auto& t : obs.ownHand->flowers()) {
            if (t.id < 144) visible[t.id] = true;
        }
    }

    // All discards
    for (int i = 0; i < 4; i++) {
        for (const auto& t : obs.allDiscards[i]) {
            if (t.id < 144) visible[t.id] = true;
        }
    }

    // Opponent melds and flowers
    for (int opp = 0; opp < 3; opp++) {
        for (const auto& m : obs.opponentMelds[opp]) {
            for (const auto& t : m.tiles) {
                if (t.id < 144) visible[t.id] = true;
            }
        }
        for (const auto& t : obs.opponentFlowers[opp]) {
            if (t.id < 144) visible[t.id] = true;
        }
    }

    // Collect unseen tiles
    auto allTiles = buildFullTileSet();
    std::vector<Tile> unseen;
    for (const auto& t : allTiles) {
        if (!visible[t.id]) {
            unseen.push_back(t);
        }
    }

    // Shuffle unseen tiles
    std::shuffle(unseen.begin(), unseen.end(), rng);

    // Copy own hand as-is
    if (obs.ownHand) {
        world.hands[obs.ownPlayerIndex] = *obs.ownHand;
    }

    // Deal concealed tiles to opponents
    int unseenIdx = 0;
    int oppSlot = 0;
    for (int i = 0; i < 4; i++) {
        if (i == obs.ownPlayerIndex) continue;
        if (oppSlot >= 3) break;

        Hand& h = world.hands[i];

        // Copy their known melds and flowers
        for (const auto& m : obs.opponentMelds[oppSlot]) {
            h.addMeld(m);
        }
        for (const auto& t : obs.opponentFlowers[oppSlot]) {
            h.addFlower(t);
        }

        // Deal concealed tiles from unseen pool
        int needed = obs.opponentConcealedCount[oppSlot];
        for (int c = 0; c < needed && unseenIdx < (int)unseen.size(); c++) {
            h.addTile(unseen[unseenIdx++]);
        }

        oppSlot++;
    }

    // Remaining unseen tiles form the simulated wall
    for (int i = unseenIdx; i < (int)unseen.size(); i++) {
        world.wall.push_back(unseen[i]);
    }

    return world;
}
