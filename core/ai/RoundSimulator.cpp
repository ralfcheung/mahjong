#include "RoundSimulator.h"
#include "scoring/WinDetector.h"
#include "scoring/Scoring.h"
#include "ai/ShantenCalculator.h"

void RoundSimulator::decodeAction(int actionIdx, Suit& suit, uint8_t& rank) {
    if (actionIdx < 9) {
        suit = Suit::Bamboo; rank = actionIdx + 1;
    } else if (actionIdx < 18) {
        suit = Suit::Characters; rank = actionIdx - 9 + 1;
    } else if (actionIdx < 27) {
        suit = Suit::Dots; rank = actionIdx - 18 + 1;
    } else if (actionIdx < 31) {
        suit = Suit::Wind; rank = actionIdx - 27 + 1;
    } else {
        suit = Suit::Dragon; rank = actionIdx - 31 + 1;
    }
}

bool RoundSimulator::removeTileBySuitRank(Hand& hand, Suit suit, uint8_t rank, Tile& removed) {
    auto& tiles = hand.concealedMut();
    for (auto it = tiles.begin(); it != tiles.end(); ++it) {
        if (it->suit == suit && it->rank == rank) {
            removed = *it;
            tiles.erase(it);
            return true;
        }
    }
    return false;
}

SimResult RoundSimulator::simulate(const SampledWorld& world,
                                    int adaptPlayerIndex,
                                    Wind prevailingWind,
                                    int dealerIndex,
                                    AdaptiveEngine& engine,
                                    const AdaptationConfig& config,
                                    std::mt19937& rng) {
    SimResult result;

    // Copy world state so we can mutate it
    std::array<Hand, 4> hands = world.hands;
    std::array<std::vector<Tile>, 4> discardPiles;
    std::vector<Tile> wall = world.wall;
    int wallIdx = 0;

    auto wallEmpty = [&]() { return wallIdx >= (int)wall.size(); };
    auto wallDraw = [&]() -> Tile { return wall[wallIdx++]; };

    // Seat winds
    std::array<Wind, 4> seatWinds;
    for (int i = 0; i < 4; i++) {
        seatWinds[i] = static_cast<Wind>((static_cast<int>(prevailingWind) + i) % 4);
    }

    // Sort initial hands
    for (int i = 0; i < 4; i++) {
        hands[i].sortTiles();
    }

    int activePlayer = dealerIndex;
    int turnCount = 0;
    bool roundDone = false;

    // Build a lightweight Player-like context for feature extraction
    // We use a simple struct that tracks what features need
    struct SimPlayerState {
        int seatIndex;
        Wind seatWind;
        int score = 0;
    };
    std::array<SimPlayerState, 4> playerStates;
    for (int i = 0; i < 4; i++) {
        playerStates[i] = {i, seatWinds[i], 0};
    }

    // Helper: build RLGameContext for a player
    auto buildCtx = [&](int pIdx) -> RLGameContext {
        RLGameContext ctx;
        ctx.turnCount = turnCount;
        ctx.wallRemaining = (int)wall.size() - wallIdx;
        ctx.seatWind = seatWinds[pIdx];
        ctx.prevailingWind = prevailingWind;
        ctx.playerIndex = pIdx;
        ctx.allPlayers = nullptr;  // We don't have Player objects
        ctx.lightweight = true;   // Skip expensive acceptance calculations
        for (int i = 0; i < 4; i++) {
            ctx.playerScores[i] = playerStates[i].score;
        }
        return ctx;
    };

    // We can't use extractDiscardFeatures with allPlayers=nullptr fully,
    // but the core features (hand counts, shanten, etc.) still work.
    // Opponent meld/discard features won't be populated - this is acceptable
    // for short adaptation simulations.

    // Heuristic discard: pick tile whose removal minimizes shanten
    auto heuristicDiscard = [](Hand& hand) -> int {
        auto& tiles = hand.concealed();
        int bestIdx = tileTypeIndex(tiles[0]);
        int bestShanten = 99;
        bool bestIsTerminal = false;

        for (size_t i = 0; i < tiles.size(); i++) {
            Tile t = tiles[i];
            int idx = tileTypeIndex(t);
            if (idx < 0) continue;

            // Temporarily remove tile and compute shanten
            hand.concealedMut().erase(hand.concealedMut().begin() + i);
            int sh = ShantenCalculator::calculate(hand);
            hand.concealedMut().insert(hand.concealedMut().begin() + i, t);

            bool isTerminal = t.isTerminalOrHonor();
            if (sh < bestShanten || (sh == bestShanten && isTerminal && !bestIsTerminal)) {
                bestShanten = sh;
                bestIdx = idx;
                bestIsTerminal = isTerminal;
            }
        }
        return bestIdx;
    };

    // Heuristic claim: claim if it reduces shanten OR provides faan value
    auto heuristicClaim = [&seatWinds, prevailingWind](
            const Hand& hand, Tile discardedTile,
            const std::vector<ClaimType>& available, int candidate) -> int {
        int curShanten = ShantenCalculator::calculate(hand);

        // Try pung/kong first (higher priority)
        for (auto ct : available) {
            if (ct == ClaimType::Kong || ct == ClaimType::Pung) {
                int afterShanten = ShantenCalculator::calculateAfterPung(
                    hand, discardedTile.suit, discardedTile.rank);
                if (afterShanten < curShanten) {
                    return (ct == ClaimType::Kong) ? 2 : 1;
                }
                // Also claim if shanten stays equal but tile has faan value:
                // dragons, seat wind, prevailing wind
                if (afterShanten == curShanten) {
                    bool faanValuable = (discardedTile.suit == Suit::Dragon) ||
                        (discardedTile.suit == Suit::Wind &&
                         (static_cast<int>(discardedTile.rank) == static_cast<int>(seatWinds[candidate]) + 1 ||
                          static_cast<int>(discardedTile.rank) == static_cast<int>(prevailingWind) + 1));
                    if (faanValuable) {
                        return (ct == ClaimType::Kong) ? 2 : 1;
                    }
                }
            }
        }

        // Try chow
        for (auto ct : available) {
            if (ct == ClaimType::Chow && isNumberedSuit(discardedTile.suit)) {
                uint8_t r = discardedTile.rank;
                Suit s = discardedTile.suit;
                int best = curShanten;
                if (r >= 3 && hand.hasTile(s, r-2) && hand.hasTile(s, r-1))
                    best = std::min(best, ShantenCalculator::calculateAfterChow(hand, s, r, r-2, r-1));
                if (r >= 2 && r <= 8 && hand.hasTile(s, r-1) && hand.hasTile(s, r+1))
                    best = std::min(best, ShantenCalculator::calculateAfterChow(hand, s, r, r-1, r+1));
                if (r <= 7 && hand.hasTile(s, r+1) && hand.hasTile(s, r+2))
                    best = std::min(best, ShantenCalculator::calculateAfterChow(hand, s, r, r+1, r+2));
                if (best < curShanten) return 0;
            }
        }

        return 3;  // Pass
    };

    bool claimerNeedsDiscard = false;  // After a claim, skip draw phase

    while (!roundDone && !wallEmpty() && turnCount < 80) {
        if (!claimerNeedsDiscard) {
            // Draw phase
            Tile drawn = wallDraw();
            if (drawn.isBonus()) {
                hands[activePlayer].addFlower(drawn);
                if (!wallEmpty()) {
                    drawn = wallDraw();  // Simplified: draw from front instead of back
                    hands[activePlayer].addTile(drawn);
                } else {
                    break;
                }
            } else {
                hands[activePlayer].addTile(drawn);
            }
            hands[activePlayer].sortTiles();

            // Check self-draw win
            auto wins = WinDetector::findWins(hands[activePlayer], drawn);
            if (!wins.empty()) {
                WinContext wctx;
                wctx.selfDrawn = true;
                wctx.seatWind = seatWinds[activePlayer];
                wctx.prevailingWind = prevailingWind;
                wctx.isDealer = (activePlayer == dealerIndex);
                wctx.turnCount = turnCount;
                FaanResult fr = Scoring::calculate(hands[activePlayer], drawn, wctx);
                if (fr.totalFaan >= Scoring::MIN_FAAN) {
                    roundDone = true;
                    break;
                }
            }
        }
        claimerNeedsDiscard = false;

        // Discard phase — heuristic for decision, NN only for adapt player TD targets
        auto& concealed = hands[activePlayer].concealed();
        if (concealed.empty()) break;

        int actionIdx = heuristicDiscard(hands[activePlayer]);

        // Remove tile
        Suit discardSuit;
        uint8_t discardRank;
        decodeAction(actionIdx, discardSuit, discardRank);

        Tile discardedTile;
        if (!removeTileBySuitRank(hands[activePlayer], discardSuit, discardRank, discardedTile)) {
            auto& cm = hands[activePlayer].concealedMut();
            if (cm.empty()) break;
            discardedTile = cm.front();
            cm.erase(cm.begin());
            actionIdx = tileTypeIndex(discardedTile);
        }
        discardPiles[activePlayer].push_back(discardedTile);
        hands[activePlayer].sortTiles();

        // Collect discard sample for adapt player (NN inference only here)
        if (activePlayer == adaptPlayerIndex) {
            RLGameContext ctx = buildCtx(adaptPlayerIndex);
            auto state = extractDiscardFeatures(hands[adaptPlayerIndex], ctx);

            // TD target: gamma * max Q(s', a')
            auto nextLogits = engine.inferDiscard(state);
            float maxNextQ = 0.0f;
            if (!nextLogits.empty()) {
                for (float q : nextLogits) {
                    if (q > maxNextQ) maxNextQ = q;
                }
            }

            float tdTarget = config.gamma * maxNextQ;
            result.discardSamples.push_back({state, actionIdx, tdTarget});
        }

        // --- Claim phase (heuristic for all, NN only for adapt player samples) ---
        int bestClaimer = -1;
        int bestClaimAction = 3;  // Pass
        int bestPriority = 0;

        for (int ci = 1; ci <= 3; ci++) {
            int candidate = (activePlayer + ci) % 4;
            const auto& chand = hands[candidate];

            // Win check
            {
                Hand tempHand = chand;
                tempHand.addTile(discardedTile);
                auto wins = WinDetector::findWins(tempHand, discardedTile);
                if (!wins.empty()) {
                    WinContext wctx;
                    wctx.selfDrawn = false;
                    wctx.seatWind = seatWinds[candidate];
                    wctx.prevailingWind = prevailingWind;
                    wctx.isDealer = (candidate == dealerIndex);
                    wctx.turnCount = turnCount;
                    FaanResult fr = Scoring::calculate(chand, discardedTile, wctx);
                    if (fr.totalFaan >= Scoring::MIN_FAAN) {
                        roundDone = true;
                        break;
                    }
                }
            }

            // Build available claims
            std::vector<ClaimType> available;
            int count = chand.countTile(discardedTile.suit, discardedTile.rank);
            if (count >= 3) available.push_back(ClaimType::Kong);
            if (count >= 2) available.push_back(ClaimType::Pung);

            if (ci == 1 && isNumberedSuit(discardedTile.suit)) {
                uint8_t r = discardedTile.rank;
                if ((r >= 3 && chand.hasTile(discardedTile.suit, r-2) && chand.hasTile(discardedTile.suit, r-1)) ||
                    (r >= 2 && r <= 8 && chand.hasTile(discardedTile.suit, r-1) && chand.hasTile(discardedTile.suit, r+1)) ||
                    (r <= 7 && chand.hasTile(discardedTile.suit, r+1) && chand.hasTile(discardedTile.suit, r+2))) {
                    available.push_back(ClaimType::Chow);
                }
            }

            if (available.empty()) continue;

            // Heuristic claim decision
            int claimAction = heuristicClaim(chand, discardedTile, available, candidate);

            // Collect claim sample for adapt player
            // TD target uses discard Q-value on post-claim state (player discards after claiming)
            if (candidate == adaptPlayerIndex) {
                RLGameContext claimCtx = buildCtx(candidate);
                auto claimState = extractClaimFeatures(chand, discardedTile, available, claimCtx);

                // Estimate value of post-claim state via discard network
                auto postClaimLogits = engine.inferDiscard(claimState);
                float maxNextQ = -1e30f;
                if (!postClaimLogits.empty()) {
                    for (float q : postClaimLogits) {
                        if (q > maxNextQ) maxNextQ = q;
                    }
                }
                if (maxNextQ < -1e29f) maxNextQ = 0.0f;

                float tdTarget = config.gamma * maxNextQ;
                result.claimSamples.push_back({claimState, claimAction, tdTarget});
            }

            // Priority: Kong/Pung = 2, Chow = 1
            int claimPriority = 0;
            if (claimAction == 0) claimPriority = 1;       // Chow
            else if (claimAction == 1) claimPriority = 2;   // Pung
            else if (claimAction == 2) claimPriority = 2;   // Kong

            if (claimPriority > bestPriority) {
                bestPriority = claimPriority;
                bestClaimer = candidate;
                bestClaimAction = claimAction;
            }
        }

        if (roundDone) break;

        // Resolve claim
        if (bestClaimer >= 0 && bestClaimAction != 3) {
            discardPiles[activePlayer].pop_back();
            hands[bestClaimer].addTile(discardedTile);

            if (bestClaimAction == 1) {  // Pung
                Meld meld;
                meld.type = MeldType::Pung;
                meld.exposed = true;
                meld.tiles.push_back(discardedTile);
                for (int i = 0; i < 2; i++) {
                    Tile t;
                    removeTileBySuitRank(hands[bestClaimer], discardedTile.suit, discardedTile.rank, t);
                    meld.tiles.push_back(t);
                }
                Tile extra;
                removeTileBySuitRank(hands[bestClaimer], discardedTile.suit, discardedTile.rank, extra);
                hands[bestClaimer].addMeld(meld);
            } else if (bestClaimAction == 2) {  // Kong
                Meld meld;
                meld.type = MeldType::Kong;
                meld.exposed = true;
                meld.tiles.push_back(discardedTile);
                for (int i = 0; i < 3; i++) {
                    Tile t;
                    removeTileBySuitRank(hands[bestClaimer], discardedTile.suit, discardedTile.rank, t);
                    meld.tiles.push_back(t);
                }
                Tile extra;
                removeTileBySuitRank(hands[bestClaimer], discardedTile.suit, discardedTile.rank, extra);
                hands[bestClaimer].addMeld(meld);
            } else if (bestClaimAction == 0) {  // Chow
                Tile claimedBack;
                removeTileBySuitRank(hands[bestClaimer], discardedTile.suit, discardedTile.rank, claimedBack);
            }

            hands[bestClaimer].sortTiles();
            activePlayer = bestClaimer;
            claimerNeedsDiscard = true;
        } else {
            activePlayer = (activePlayer + 1) % 4;
        }

        turnCount++;
    }

    return result;
}
