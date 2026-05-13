#pragma once
#include "AdaptiveEngine.h"
#include "HandSampler.h"
#include "ai/RLFeatures.h"
#include <vector>
#include <random>

struct SimResult {
    std::vector<AdaptationSample> discardSamples;
    std::vector<AdaptationSample> claimSamples;
};

class RoundSimulator {
public:
    // Run a headless game simulation from a sampled world.
    // Collects TD-learning samples only for adaptPlayerIndex.
    static SimResult simulate(const SampledWorld& world,
                              int adaptPlayerIndex,
                              Wind prevailingWind,
                              int dealerIndex,
                              AdaptiveEngine& engine,
                              const AdaptationConfig& config,
                              std::mt19937& rng);

private:
    // Decode action index to suit+rank
    static void decodeAction(int actionIdx, Suit& suit, uint8_t& rank);

    // Try to remove a tile of given suit+rank from hand, return the removed tile
    static bool removeTileBySuitRank(Hand& hand, Suit suit, uint8_t rank, Tile& removed);
};
