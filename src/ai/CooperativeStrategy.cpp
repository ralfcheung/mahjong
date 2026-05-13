#include "CooperativeStrategy.h"
#include "ai/ShantenCalculator.h"
#include "ai/HandEvaluator.h"
#include "ai/RLFeatures.h"
#include <algorithm>

// Decode tile type index (0-33) back to Suit
static Suit suitFromTileType(int tileType) {
    if (tileType < 9)  return Suit::Bamboo;
    if (tileType < 18) return Suit::Characters;
    if (tileType < 27) return Suit::Dots;
    if (tileType < 31) return Suit::Wind;
    return Suit::Dragon;
}

CooperativeTarget CooperativeStrategy::electTarget(
    const std::vector<const Player*>& allPlayers,
    Wind prevailingWind) {

    CooperativeTarget result;

    if (allPlayers.size() < 4 || !allPlayers[0]) return result;

    // Cooperation activates when human looks close to winning (observable info only).
    // 2+ exposed melds means the human is progressing fast.
    int humanMeldCount = static_cast<int>(allPlayers[0]->hand().melds().size());
    if (humanMeldCount < 2) return result;

    // Find AI closest to winning based on observable info (melds + discards).
    // More melds = closer to winning. Tie-break by suit focus (flush potential).
    int bestIdx = -1;
    int bestMelds = 0;
    float bestFocus = -1.0f;

    for (int i = 1; i <= 3; i++) {
        if (i >= static_cast<int>(allPlayers.size()) || !allPlayers[i]) continue;

        const Player* ai = allPlayers[i];
        int meldCount = static_cast<int>(ai->hand().melds().size());

        // Suit focus: fraction of melds in the most common suit
        float focus = 0.0f;
        if (meldCount > 0) {
            int suitCounts[7] = {};
            for (const auto& m : ai->hand().melds()) {
                if (!m.tiles.empty()) {
                    suitCounts[static_cast<int>(m.tiles[0].suit)]++;
                }
            }
            int maxSuit = *std::max_element(suitCounts, suitCounts + 7);
            focus = static_cast<float>(maxSuit) / static_cast<float>(meldCount);
        }

        if (meldCount > bestMelds ||
            (meldCount == bestMelds && focus > bestFocus)) {
            bestMelds = meldCount;
            bestFocus = focus;
            bestIdx = i;
        }
    }

    // Target must have at least 1 exposed meld to be viable
    if (bestIdx < 0 || bestMelds < 1) return result;

    result.playerIndex = bestIdx;
    result.shanten = 4 - bestMelds; // rough observable estimate
    result.faanConfidence = bestFocus;

    // Compute needed tiles from target's melds and discards only.
    const Player* target = allPlayers[bestIdx];

    // Identify suits present in target's exposed melds
    bool suitInMelds[7] = {};
    for (const auto& m : target->hand().melds()) {
        for (const auto& t : m.tiles) {
            suitInMelds[static_cast<int>(t.suit)] = true;
        }
    }

    // Tile types the target has discarded (they clearly don't want these)
    bool discardedByTarget[34] = {};
    for (const auto& d : target->discards()) {
        int idx = tileTypeIndex(d);
        if (idx >= 0 && idx < 34) discardedByTarget[idx] = true;
    }

    // Needed tiles: in suit(s) matching target's melds, not discarded by target
    for (int tt = 0; tt < 34; tt++) {
        Suit s = suitFromTileType(tt);
        if (suitInMelds[static_cast<int>(s)] && !discardedByTarget[tt]) {
            result.neededTiles[tt] = true;
        }
    }

    return result;
}

bool CooperativeStrategy::findCooperativeDiscard(
    const Hand& selfHand, Wind seatWind, Wind prevailingWind,
    const std::vector<const Player*>& allPlayers,
    const CooperativeTarget& target,
    Tile& outTile) {

    if (target.playerIndex < 0) return false;

    int selfShanten = ShantenCalculator::calculate(selfHand);

    // Get discard candidates sorted best-first (uses cooperator's own hand — always known)
    auto candidates = HandEvaluator::evaluateDiscards(
        selfHand, seatWind, prevailingWind, allPlayers);

    // Find a tile the target needs that the cooperator can afford to give up
    // (discarding it doesn't worsen own shanten by more than 1)
    for (const auto& c : candidates) {
        int idx = tileTypeIndex(c.tile);
        if (idx < 0 || idx >= 34) continue;

        if (target.neededTiles[idx] && c.shantenAfterDiscard <= selfShanten + 1) {
            outTile = c.tile;
            return true;
        }
    }

    return false;
}

bool CooperativeStrategy::shouldSuppressClaim(
    Tile discardedTile, int discarderIndex,
    const CooperativeTarget& target,
    const std::vector<const Player*>& allPlayers) {

    (void)discarderIndex;
    (void)allPlayers;

    if (target.playerIndex < 0) return false;

    int idx = tileTypeIndex(discardedTile);
    if (idx < 0 || idx >= 34) return false;

    // If the target needs this tile type, suppress our claim so the target
    // has a chance to claim it instead.
    return target.neededTiles[idx];
}
