#include "AIPlayer.h"
#include "ai/ShantenCalculator.h"
#include "ai/HandEvaluator.h"
#include "ai/RLFeatures.h"
#include "ai/InferenceEngine.h"
#include "ai/AdaptiveEngine.h"
#include "ai/HandSampler.h"
#include "ai/RoundSimulator.h"
#include "ai/CooperativeStrategy.h"
#include <chrono>
#include <cstdio>

extern bool g_mahjong_verbose;

#ifdef HAS_TORCH
#include "ai/DQNTrainer.h"
#endif

// Default adaptation config (overridden by setAdaptationTier)
static AdaptationConfig s_defaultAdaptConfig = {
    0.001f,  // learningRate
    100,     // numSimulations
    15,      // maxSGDSteps
    32,      // batchSize
    0.95f,   // gamma
    1.0f     // gradClipNorm
};

// DQNTrainer methods need the full type (only available with HAS_TORCH)
static bool hasTrainedModels(DQNTrainer* trainer) {
#ifdef HAS_TORCH
    return trainer && trainer->hasTrainedModels();
#else
    (void)trainer;
    return false;
#endif
}

// Check if discarding into a suit an opponent is collecting (flush threat).
// Correct signal: opponent has 4+ numbered discards AND 0-1 discards of this suit AND melds in it.
// Returns -1 if no block needed, or the opponent player index that triggered the block.
static int flushThreatFrom(Suit discardSuit, int selfIdx,
                            const std::vector<const Player*>& allPlayers) {
    if (!isNumberedSuit(discardSuit)) return -1;

    // Check if AI is building its own flush (9+ tiles of any single suit)
    if (selfIdx >= 0 && selfIdx < static_cast<int>(allPlayers.size()) && allPlayers[selfIdx]) {
        const Player* self = allPlayers[selfIdx];
        int selfSuit[3] = {};
        for (const auto& t : self->hand().concealed()) {
            if (t.suit == Suit::Bamboo)          selfSuit[0]++;
            else if (t.suit == Suit::Characters) selfSuit[1]++;
            else if (t.suit == Suit::Dots)       selfSuit[2]++;
        }
        for (const auto& m : self->hand().melds()) {
            for (const auto& t : m.tiles) {
                if (t.suit == Suit::Bamboo)          selfSuit[0]++;
                else if (t.suit == Suit::Characters) selfSuit[1]++;
                else if (t.suit == Suit::Dots)       selfSuit[2]++;
            }
        }
        for (int s = 0; s < 3; s++) {
            if (selfSuit[s] >= 9) return -1;  // AI is flush-building, stay offensive
        }
    }

    int sIdx = static_cast<int>(discardSuit);
    for (int i = 0; i < static_cast<int>(allPlayers.size()); i++) {
        if (i == selfIdx || !allPlayers[i]) continue;
        const Player* opp = allPlayers[i];

        int suitDisc[3] = {};
        int totalNumbered = 0;
        for (const auto& d : opp->discards()) {
            if (d.suit == Suit::Bamboo)          { suitDisc[0]++; totalNumbered++; }
            else if (d.suit == Suit::Characters) { suitDisc[1]++; totalNumbered++; }
            else if (d.suit == Suit::Dots)       { suitDisc[2]++; totalNumbered++; }
        }

        int suitMeld = 0;
        for (const auto& m : opp->hand().melds()) {
            for (const auto& t : m.tiles) {
                if (static_cast<int>(t.suit) == sIdx) suitMeld++;
            }
        }

        // Strong signal: 2+ melds in one suit (6+ meld tiles) is always dangerous
        if (suitMeld >= 6) {
            // std::fprintf(stderr, "[AI] P%d flush-block (vs P%d) -- strong signal, 2+ melds number of melds %d\n", selfIdx, i, suitMeld);
            return i;
        }

        // Meld-count signal: 3+ total melds with 3+ tiles of this suit = close to winning + collecting
        int totalMelds = static_cast<int>(opp->hand().melds().size());
        if (totalMelds >= 3 && suitMeld >= 3) {
            // std::fprintf(stderr, "[AI] P%d flush-block (vs P%d) -- meld-count signal, %d melds, %d suit tiles\n", selfIdx, i, totalMelds, suitMeld);
            return i;
        }

        // Standard detection: discard pattern + meld presence
        // 1. opponent has discarded at least 10 numbered tiles total
        // 2. opponent has discarded 0-1 tiles of the suit you're about to discard (they're hoarding it)
        // 3. opponent has 3+ tiles of that suit in exposed melds (a Pung or Chow confirming they're collecting it)
        if (totalNumbered >= 10 && suitDisc[sIdx] <= 1 && suitMeld > 3) {
            // std::fprintf(stderr, "[AI] P%d flush-block (vs P%d) -- standard detection, number of melds %d\n", selfIdx, i, suitMeld);
            return i;
        }

    }
    return -1;
}

void AIPlayer::setGameContext(Wind prevailingWind,
                               const std::vector<const Player*>& allPlayers) {
    prevailingWind_ = prevailingWind;
    allPlayers_ = allPlayers;
}

void AIPlayer::setTurnInfo(int turnCount, int wallRemaining) {
    turnCount_ = turnCount;
    wallRemaining_ = wallRemaining;
}

void AIPlayer::requestDiscard(std::function<void(Tile)> callback) {
    auto& tiles = hand().concealed();
    if (tiles.empty()) return;

    // Cooperative check: feed tiles to the target AI if possible
    if (cooperativeMode_ && !allPlayers_.empty()) {
        auto target = CooperativeStrategy::electTarget(allPlayers_, prevailingWind_);
        if (target.playerIndex >= 0 && target.playerIndex != seatIndex()) {
            Tile coopTile;
            if (CooperativeStrategy::findCooperativeDiscard(
                    hand(), seatWind(), prevailingWind_, allPlayers_, target, coopTile)) {
                // Don't feed tiles that would help the human's flush
                if (flushThreatFrom(coopTile.suit, seatIndex(), allPlayers_) < 0) {
                    for (const auto& t : tiles) {
                        if (t.sameAs(coopTile)) {
                            // std::fprintf(stderr, "[AI] P%d discard: cooperative, tile=%s, helping P%d\n", seatIndex(), t.toString().c_str(), target.playerIndex);
                            callback(t); return;
                        }
                    }
                }
            }
        }
    }

#ifdef HAS_TORCH
    if (hasTrainedModels(trainer_) && !trainerClaimOnly_) {
        // Neural network discard via DQNTrainer (full libtorch)
        RLGameContext ctx;
        ctx.turnCount = turnCount_;
        ctx.wallRemaining = wallRemaining_;
        ctx.seatWind = seatWind();
        ctx.prevailingWind = prevailingWind_;
        ctx.playerIndex = seatIndex();
        ctx.allPlayers = &allPlayers_;
        fillPlayerScores(ctx);

        auto features = extractDiscardFeatures(hand(), ctx);

        std::vector<bool> validMask(NUM_TILE_TYPES, false);
        for (const auto& t : tiles) {
            int idx = tileTypeIndex(t);
            if (idx >= 0) validMask[idx] = true;
        }

        int actionIdx = trainer_->selectDiscardAction(features, validMask);

        Suit discardSuit;
        uint8_t discardRank;
        if (actionIdx < 9) {
            discardSuit = Suit::Bamboo; discardRank = actionIdx + 1;
        } else if (actionIdx < 18) {
            discardSuit = Suit::Characters; discardRank = actionIdx - 9 + 1;
        } else if (actionIdx < 27) {
            discardSuit = Suit::Dots; discardRank = actionIdx - 18 + 1;
        } else if (actionIdx < 31) {
            discardSuit = Suit::Wind; discardRank = actionIdx - 27 + 1;
        } else {
            discardSuit = Suit::Dragon; discardRank = actionIdx - 31 + 1;
        }

        int flushOpp = flushThreatFrom(discardSuit, seatIndex(), allPlayers_);
        if (flushOpp >= 0) {
            if (g_mahjong_verbose) std::fprintf(stderr, "[AI] P%d NN rejected: flush-block (vs P%d)\n", seatIndex(), flushOpp);
        } else {
            for (const auto& t : tiles) {
                if (t.suit == discardSuit && t.rank == discardRank) {
                    if (g_mahjong_verbose) std::fprintf(stderr, "[AI] P%d discard: NN\n", seatIndex());
                    callback(t);
                    return;
                }
            }
            if (g_mahjong_verbose) std::fprintf(stderr, "[AI] P%d NN rejected: tile-not-found\n", seatIndex());
        }
    }
#endif

    // Try adapted engine (per-round fine-tuned)
    if (adaptedDiscard(callback)) {
        if (g_mahjong_verbose) std::fprintf(stderr, "[AI] P%d discard: adapted\n", seatIndex());
        return;
    }

    // Try lightweight inference engine
    if (inferenceDiscard(callback)) {
        if (g_mahjong_verbose) std::fprintf(stderr, "[AI] P%d discard: inference\n", seatIndex());
        return;
    }

    if (g_mahjong_verbose) std::fprintf(stderr, "[AI] P%d discard: heuristic\n", seatIndex());
    heuristicDiscard(callback);
}

void AIPlayer::requestClaimDecision(
    Tile discardedTile,
    const std::vector<ClaimOption>& options,
    std::function<void(ClaimType)> callback) {

    // Always claim win
    for (const auto& opt : options) {
        if (opt.type == ClaimType::Win) {
            callback(ClaimType::Win);
            return;
        }
    }

    // Cooperative check: suppress claim if target needs this tile
    if (cooperativeMode_ && !allPlayers_.empty()) {
        auto target = CooperativeStrategy::electTarget(allPlayers_, prevailingWind_);
        if (target.playerIndex >= 0 && target.playerIndex != seatIndex()) {
            if (CooperativeStrategy::shouldSuppressClaim(
                    discardedTile, lastDiscarder_, target, allPlayers_)) {
                if (g_mahjong_verbose) std::fprintf(stderr, "[AI] P%d claim: cooperative-suppress\n", seatIndex());
                callback(ClaimType::None);
                return;
            }
        }
    }

#ifdef HAS_TORCH
    if (hasTrainedModels(trainer_)) {
        RLGameContext ctx;
        ctx.turnCount = turnCount_;
        ctx.wallRemaining = wallRemaining_;
        ctx.seatWind = seatWind();
        ctx.prevailingWind = prevailingWind_;
        ctx.playerIndex = seatIndex();
        ctx.allPlayers = &allPlayers_;
        fillPlayerScores(ctx);

        std::vector<ClaimType> availableClaims;
        for (const auto& opt : options) {
            if (opt.type != ClaimType::None && opt.type != ClaimType::Win) {
                availableClaims.push_back(opt.type);
            }
        }

        auto features = extractClaimFeatures(hand(), discardedTile, availableClaims, ctx);

        std::vector<bool> validMask(NUM_CLAIM_ACTIONS, false);
        validMask[3] = true; // Pass always valid
        for (auto ct : availableClaims) {
            if (ct == ClaimType::Chow) validMask[0] = true;
            else if (ct == ClaimType::Pung) validMask[1] = true;
            else if (ct == ClaimType::Kong) validMask[2] = true;
        }

        int actionIdx = trainer_->selectClaimAction(features, validMask);

        ClaimType chosen = ClaimType::None;
        switch (actionIdx) {
            case 0: chosen = ClaimType::Chow; break;
            case 1: chosen = ClaimType::Pung; break;
            case 2: chosen = ClaimType::Kong; break;
            default: chosen = ClaimType::None; break;
        }

        if (chosen != ClaimType::None) {
            bool valid = false;
            for (auto ct : availableClaims) {
                if (ct == chosen) { valid = true; break; }
            }
            if (!valid) chosen = ClaimType::None;
        }

        if (g_mahjong_verbose) std::fprintf(stderr, "[AI] P%d claim: NN\n", seatIndex());
        callback(chosen);
        return;
    }
#endif

    // Try adapted engine (per-round fine-tuned)
    if (adaptedClaim(discardedTile, options, callback)) {
        if (g_mahjong_verbose) std::fprintf(stderr, "[AI] P%d claim: adapted\n", seatIndex());
        return;
    }

    // Try lightweight inference engine
    if (inferenceClaim(discardedTile, options, callback)) {
        if (g_mahjong_verbose) std::fprintf(stderr, "[AI] P%d claim: inference\n", seatIndex());
        return;
    }

    if (g_mahjong_verbose) std::fprintf(stderr, "[AI] P%d claim: heuristic\n", seatIndex());
    heuristicClaim(discardedTile, options, callback);
}

bool AIPlayer::inferenceDiscard(std::function<void(Tile)> callback) {
    if (!inferenceEngine_ || !inferenceEngine_->hasDiscardWeights()) return false;

    auto& tiles = hand().concealed();

    RLGameContext ctx;
    ctx.turnCount = turnCount_;
    ctx.wallRemaining = wallRemaining_;
    ctx.seatWind = seatWind();
    ctx.prevailingWind = prevailingWind_;
    ctx.playerIndex = seatIndex();
    ctx.allPlayers = &allPlayers_;
    fillPlayerScores(ctx);

    auto features = extractDiscardFeatures(hand(), ctx);
    auto logits = inferenceEngine_->inferDiscard(features);
    if (logits.empty()) return false;

    std::vector<bool> validMask(NUM_TILE_TYPES, false);
    for (const auto& t : tiles) {
        int idx = tileTypeIndex(t);
        if (idx >= 0) validMask[idx] = true;
    }

    int actionIdx = InferenceEngine::selectBestAction(logits, validMask);
    if (actionIdx < 0) return false;

    // Decode action index to suit+rank
    Suit discardSuit;
    uint8_t discardRank;
    if (actionIdx < 9) {
        discardSuit = Suit::Bamboo; discardRank = actionIdx + 1;
    } else if (actionIdx < 18) {
        discardSuit = Suit::Characters; discardRank = actionIdx - 9 + 1;
    } else if (actionIdx < 27) {
        discardSuit = Suit::Dots; discardRank = actionIdx - 18 + 1;
    } else if (actionIdx < 31) {
        discardSuit = Suit::Wind; discardRank = actionIdx - 27 + 1;
    } else {
        discardSuit = Suit::Dragon; discardRank = actionIdx - 31 + 1;
    }

    int flushOpp = flushThreatFrom(discardSuit, seatIndex(), allPlayers_);
    if (flushOpp >= 0) {
        if (g_mahjong_verbose) std::fprintf(stderr, "[AI] P%d inference rejected: flush-block (vs P%d)\n", seatIndex(), flushOpp);
        return false;
    }

    for (const auto& t : tiles) {
        if (t.suit == discardSuit && t.rank == discardRank) {
            callback(t);
            return true;
        }
    }

    if (g_mahjong_verbose) std::fprintf(stderr, "[AI] P%d inference rejected: tile-not-found\n", seatIndex());
    return false;
}

bool AIPlayer::inferenceClaim(Tile discardedTile,
                               const std::vector<ClaimOption>& options,
                               std::function<void(ClaimType)> callback) {
    if (!inferenceEngine_ || !inferenceEngine_->hasClaimWeights()) return false;

    RLGameContext ctx;
    ctx.turnCount = turnCount_;
    ctx.wallRemaining = wallRemaining_;
    ctx.seatWind = seatWind();
    ctx.prevailingWind = prevailingWind_;
    ctx.playerIndex = seatIndex();
    ctx.allPlayers = &allPlayers_;
    fillPlayerScores(ctx);

    std::vector<ClaimType> availableClaims;
    for (const auto& opt : options) {
        if (opt.type != ClaimType::None && opt.type != ClaimType::Win) {
            availableClaims.push_back(opt.type);
        }
    }

    auto features = extractClaimFeatures(hand(), discardedTile, availableClaims, ctx);
    auto logits = inferenceEngine_->inferClaim(features);
    if (logits.empty()) return false;

    std::vector<bool> validMask(NUM_CLAIM_ACTIONS, false);
    validMask[3] = true; // Pass always valid
    for (auto ct : availableClaims) {
        if (ct == ClaimType::Chow) validMask[0] = true;
        else if (ct == ClaimType::Pung) validMask[1] = true;
        else if (ct == ClaimType::Kong) validMask[2] = true;
    }

    int actionIdx = InferenceEngine::selectBestAction(logits, validMask);

    ClaimType chosen = ClaimType::None;
    switch (actionIdx) {
        case 0: chosen = ClaimType::Chow; break;
        case 1: chosen = ClaimType::Pung; break;
        case 2: chosen = ClaimType::Kong; break;
        default: chosen = ClaimType::None; break;
    }

    if (chosen != ClaimType::None) {
        bool valid = false;
        for (auto ct : availableClaims) {
            if (ct == chosen) { valid = true; break; }
        }
        if (!valid) chosen = ClaimType::None;
    }

    callback(chosen);
    return true;
}

void AIPlayer::heuristicDiscard(std::function<void(Tile)> callback) {
    auto& tiles = hand().concealed();

    auto candidates = HandEvaluator::evaluateDiscards(
        hand(), seatWind(), prevailingWind_, allPlayers_);

    if (candidates.empty()) {
        callback(tiles[0]);
        return;
    }

    // Prefer candidates that don't feed an opponent's flush
    for (const auto& c : candidates) {
        if (flushThreatFrom(c.tile.suit, seatIndex(), allPlayers_) < 0) {
            for (const auto& t : tiles) {
                if (t.sameAs(c.tile)) {
                    callback(t);
                    return;
                }
            }
        }
    }

    // All candidates are in dangerous suits — pick the least dangerous
    {
        int bestIdx = 0;
        float leastDanger = 1e30f;
        for (int ci = 0; ci < static_cast<int>(candidates.size()); ci++) {
            float danger = 0.0f;
            if (isNumberedSuit(candidates[ci].tile.suit)) {
                int sIdx = static_cast<int>(candidates[ci].tile.suit);
                for (int p = 0; p < static_cast<int>(allPlayers_.size()); p++) {
                    if (p == seatIndex() || !allPlayers_[p]) continue;
                    for (const auto& m : allPlayers_[p]->hand().melds())
                        for (const auto& t : m.tiles)
                            if (static_cast<int>(t.suit) == sIdx) danger += 1.0f;
                }
            }
            if (danger < leastDanger) {
                leastDanger = danger;
                bestIdx = ci;
            }
        }
        Tile bestTile = candidates[bestIdx].tile;
        for (const auto& t : tiles) {
            if (t.sameAs(bestTile)) {
                callback(t);
                return;
            }
        }
    }

    callback(tiles[0]);
}

void AIPlayer::heuristicClaim(Tile discardedTile,
                               const std::vector<ClaimOption>& options,
                               std::function<void(ClaimType)> callback) {
    int currentShanten = ShantenCalculator::calculate(hand());

    ClaimType bestClaim = ClaimType::None;
    float bestScore = -1.0f;

    for (const auto& opt : options) {
        if (opt.type == ClaimType::None) continue;

        float score = HandEvaluator::evaluateClaim(
            hand(), discardedTile, opt.type,
            seatWind(), prevailingWind_, currentShanten,
            allPlayers_, seatIndex());

        if (score > bestScore) {
            bestScore = score;
            bestClaim = opt.type;
        }
    }

    callback(bestClaim);
}

// --- Per-round test-time adaptation ---

bool AIPlayer::inferenceEngineAvailable() const {
    return inferenceEngine_ && inferenceEngine_->hasDiscardWeights();
}

void AIPlayer::setAdaptationConfig(const AdaptationConfig& config) {
    s_defaultAdaptConfig = config;
}

void AIPlayer::adaptForRound(Wind prevailingWind, int dealerIndex,
                              const std::vector<const Player*>& allPlayers,
                              int wallRemaining) {
    ObservableState obs = HandSampler::buildObservable(seatIndex(), allPlayers);
    adaptForRound(prevailingWind, dealerIndex, obs, wallRemaining);
}

void AIPlayer::adaptForRound(Wind prevailingWind, int dealerIndex,
                              const ObservableState& obs, int wallRemaining) {
    if (!inferenceEngine_ || !inferenceEngine_->hasDiscardWeights()) return;

    auto startTime = std::chrono::steady_clock::now();

    // Clone base weights
    adaptedEngine_ = std::make_unique<AdaptiveEngine>();
    adaptedEngine_->cloneFrom(*inferenceEngine_);

    const AdaptationConfig& config = s_defaultAdaptConfig;

    // Run simulations and collect samples
    std::vector<AdaptationSample> allDiscardSamples;
    std::vector<AdaptationSample> allClaimSamples;

    std::mt19937 rng(static_cast<unsigned>(
        std::chrono::steady_clock::now().time_since_epoch().count()));

    for (int sim = 0; sim < config.numSimulations; sim++) {
        SampledWorld world = HandSampler::sampleWorld(obs, rng);

        SimResult simResult = RoundSimulator::simulate(
            world, seatIndex(), prevailingWind, dealerIndex,
            *adaptedEngine_, config, rng);

        allDiscardSamples.insert(allDiscardSamples.end(),
                                 simResult.discardSamples.begin(),
                                 simResult.discardSamples.end());
        allClaimSamples.insert(allClaimSamples.end(),
                               simResult.claimSamples.begin(),
                               simResult.claimSamples.end());
    }

    // SGD training on collected samples
    if (!allDiscardSamples.empty()) {
        for (int step = 0; step < config.maxSGDSteps; step++) {
            // Sample a mini-batch
            std::vector<AdaptationSample> batch;
            int batchSize = std::min(config.batchSize, (int)allDiscardSamples.size());

            if ((int)allDiscardSamples.size() <= batchSize) {
                batch = allDiscardSamples;
            } else {
                std::uniform_int_distribution<int> dist(0, (int)allDiscardSamples.size() - 1);
                for (int b = 0; b < batchSize; b++) {
                    batch.push_back(allDiscardSamples[dist(rng)]);
                }
            }

            adaptedEngine_->trainDiscardBatch(batch, config);
        }
    }

    if (!allClaimSamples.empty()) {
        for (int step = 0; step < config.maxSGDSteps; step++) {
            std::vector<AdaptationSample> batch;
            int batchSize = std::min(config.batchSize, (int)allClaimSamples.size());

            if ((int)allClaimSamples.size() <= batchSize) {
                batch = allClaimSamples;
            } else {
                std::uniform_int_distribution<int> dist(0, (int)allClaimSamples.size() - 1);
                for (int b = 0; b < batchSize; b++) {
                    batch.push_back(allClaimSamples[dist(rng)]);
                }
            }

            adaptedEngine_->trainClaimBatch(batch, config);
        }
    }

    auto endTime = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();

    // Print simulation summary
    std::fprintf(stderr, "[Adapt] P%d: %d sims, %d discard samples, %d claim samples, %lldms\n",
                 seatIndex(), config.numSimulations,
                 (int)allDiscardSamples.size(), (int)allClaimSamples.size(), (long long)ms);
    if (!allDiscardSamples.empty()) {
        float minTD = 1e30f, maxTD = -1e30f, sumTD = 0.0f;
        for (const auto& s : allDiscardSamples) {
            if (s.tdTarget < minTD) minTD = s.tdTarget;
            if (s.tdTarget > maxTD) maxTD = s.tdTarget;
            sumTD += s.tdTarget;
        }
        std::fprintf(stderr, "[Adapt] P%d discard TD-targets: min=%.3f max=%.3f avg=%.3f\n",
                     seatIndex(), minTD, maxTD, sumTD / (float)allDiscardSamples.size());
    }
    if (!allClaimSamples.empty()) {
        float minTD = 1e30f, maxTD = -1e30f, sumTD = 0.0f;
        for (const auto& s : allClaimSamples) {
            if (s.tdTarget < minTD) minTD = s.tdTarget;
            if (s.tdTarget > maxTD) maxTD = s.tdTarget;
            sumTD += s.tdTarget;
        }
        std::fprintf(stderr, "[Adapt] P%d claim TD-targets: min=%.3f max=%.3f avg=%.3f\n",
                     seatIndex(), minTD, maxTD, sumTD / (float)allClaimSamples.size());
    }
}

void AIPlayer::resetAdaptation() {
    adaptedEngine_.reset();
}

bool AIPlayer::adaptedDiscard(std::function<void(Tile)> callback) {
    if (!adaptedEngine_ || !adaptedEngine_->hasDiscardWeights()) return false;

    auto& tiles = hand().concealed();

    RLGameContext ctx;
    ctx.turnCount = turnCount_;
    ctx.wallRemaining = wallRemaining_;
    ctx.seatWind = seatWind();
    ctx.prevailingWind = prevailingWind_;
    ctx.playerIndex = seatIndex();
    ctx.allPlayers = &allPlayers_;
    fillPlayerScores(ctx);

    auto features = extractDiscardFeatures(hand(), ctx);
    auto logits = adaptedEngine_->inferDiscard(features);
    if (logits.empty()) return false;

    std::vector<bool> validMask(NUM_TILE_TYPES, false);
    for (const auto& t : tiles) {
        int idx = tileTypeIndex(t);
        if (idx >= 0) validMask[idx] = true;
    }

    int actionIdx = InferenceEngine::selectBestAction(logits, validMask);
    if (actionIdx < 0) return false;

    Suit discardSuit;
    uint8_t discardRank;
    if (actionIdx < 9) {
        discardSuit = Suit::Bamboo; discardRank = actionIdx + 1;
    } else if (actionIdx < 18) {
        discardSuit = Suit::Characters; discardRank = actionIdx - 9 + 1;
    } else if (actionIdx < 27) {
        discardSuit = Suit::Dots; discardRank = actionIdx - 18 + 1;
    } else if (actionIdx < 31) {
        discardSuit = Suit::Wind; discardRank = actionIdx - 27 + 1;
    } else {
        discardSuit = Suit::Dragon; discardRank = actionIdx - 31 + 1;
    }

    int flushOpp = flushThreatFrom(discardSuit, seatIndex(), allPlayers_);
    if (flushOpp >= 0) {
        if (g_mahjong_verbose) std::fprintf(stderr, "[AI] P%d adapted rejected: flush-block (vs P%d)\n", seatIndex(), flushOpp);
        return false;
    }

    for (const auto& t : tiles) {
        if (t.suit == discardSuit && t.rank == discardRank) {
            std::fprintf(stderr, "[AI] P%d adapted discard: %s (logit=%.3f)\n",
                         seatIndex(), t.toString().c_str(),
                         (actionIdx >= 0 && actionIdx < (int)logits.size()) ? logits[actionIdx] : 0.0f);
            callback(t);
            return true;
        }
    }

    if (g_mahjong_verbose) std::fprintf(stderr, "[AI] P%d adapted rejected: tile-not-found\n", seatIndex());
    return false;
}

bool AIPlayer::adaptedClaim(Tile discardedTile,
                             const std::vector<ClaimOption>& options,
                             std::function<void(ClaimType)> callback) {
    if (!adaptedEngine_ || !adaptedEngine_->hasClaimWeights()) return false;

    RLGameContext ctx;
    ctx.turnCount = turnCount_;
    ctx.wallRemaining = wallRemaining_;
    ctx.seatWind = seatWind();
    ctx.prevailingWind = prevailingWind_;
    ctx.playerIndex = seatIndex();
    ctx.allPlayers = &allPlayers_;
    fillPlayerScores(ctx);

    std::vector<ClaimType> availableClaims;
    for (const auto& opt : options) {
        if (opt.type != ClaimType::None && opt.type != ClaimType::Win) {
            availableClaims.push_back(opt.type);
        }
    }

    auto features = extractClaimFeatures(hand(), discardedTile, availableClaims, ctx);
    auto logits = adaptedEngine_->inferClaim(features);
    if (logits.empty()) return false;

    std::vector<bool> validMask(NUM_CLAIM_ACTIONS, false);
    validMask[3] = true;
    for (auto ct : availableClaims) {
        if (ct == ClaimType::Chow) validMask[0] = true;
        else if (ct == ClaimType::Pung) validMask[1] = true;
        else if (ct == ClaimType::Kong) validMask[2] = true;
    }

    int actionIdx = InferenceEngine::selectBestAction(logits, validMask);

    ClaimType chosen = ClaimType::None;
    switch (actionIdx) {
        case 0: chosen = ClaimType::Chow; break;
        case 1: chosen = ClaimType::Pung; break;
        case 2: chosen = ClaimType::Kong; break;
        default: chosen = ClaimType::None; break;
    }

    if (chosen != ClaimType::None) {
        bool valid = false;
        for (auto ct : availableClaims) {
            if (ct == chosen) { valid = true; break; }
        }
        if (!valid) chosen = ClaimType::None;
    }

    callback(chosen);
    return true;
}
