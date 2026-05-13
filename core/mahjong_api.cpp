#include "mahjong_api.h"
#include "GameController.h"
#include "JsonHelpers.h"
#include "ai/InferenceEngine.h"
#include "player/AIPlayer.h"
#include <array>
#include <cstring>
#include <sstream>

#ifdef HAS_TORCH
#include "ai/DQNTrainer.h"
#include "ai/RLFeatures.h"
#include "scoring/Scoring.h"
#include <memory>
#endif

bool g_mahjong_verbose = true;

void mahjong_set_verbose(int enabled) {
    g_mahjong_verbose = (enabled != 0);
}

struct GameInstance {
    GameController controller;
#ifdef HAS_TORCH
    std::unique_ptr<DQNTrainer> trainer;
    std::string modelDir;
#endif
};

static GameInstance* toInstance(MahjongGame game) {
    return reinterpret_cast<GameInstance*>(game);
}

MahjongGame mahjong_create(void) {
    auto* inst = new GameInstance();
    return reinterpret_cast<MahjongGame>(inst);
}

void mahjong_destroy(MahjongGame game) {
    delete toInstance(game);
}

void mahjong_start(MahjongGame game) {
    toInstance(game)->controller.startNewGame();
}

void mahjong_update(MahjongGame game, float dt) {
    toInstance(game)->controller.update(dt);
}

void mahjong_discard_tile(MahjongGame game, uint8_t tileId) {
    auto* inst = toInstance(game);
    // Ensure handlePlayerTurn() has run and registered the discard callback.
    // A zero-dt update triggers the callback setup without advancing timers.
    inst->controller.update(0.0f);
    inst->controller.humanDiscardTile(tileId);
}

void mahjong_claim_pass(MahjongGame game) {
    toInstance(game)->controller.humanClaim(ClaimOption{});
}

void mahjong_claim_pung(MahjongGame game) {
    auto* inst = toInstance(game);
    auto options = inst->controller.humanClaimOptions();
    for (const auto& opt : options) {
        if (opt.type == ClaimType::Pung) {
            inst->controller.humanClaim(opt);
            return;
        }
    }
}

void mahjong_claim_kong(MahjongGame game) {
    auto* inst = toInstance(game);
    auto options = inst->controller.humanClaimOptions();
    for (const auto& opt : options) {
        if (opt.type == ClaimType::Kong) {
            inst->controller.humanClaim(opt);
            return;
        }
    }
}

void mahjong_claim_chow(MahjongGame game, uint8_t handRank1, uint8_t handRank2) {
    auto* inst = toInstance(game);
    auto options = inst->controller.humanClaimOptions();
    for (const auto& opt : options) {
        if (opt.type == ClaimType::Chow && opt.meldTiles.size() == 2) {
            if (opt.meldTiles[0].rank == handRank1 && opt.meldTiles[1].rank == handRank2) {
                inst->controller.humanClaim(opt);
                return;
            }
        }
    }
}

void mahjong_claim_win(MahjongGame game) {
    auto* inst = toInstance(game);
    auto options = inst->controller.humanClaimOptions();
    for (const auto& opt : options) {
        if (opt.type == ClaimType::Win) {
            inst->controller.humanClaim(opt);
            return;
        }
    }
}

void mahjong_self_draw_win(MahjongGame game) {
    toInstance(game)->controller.humanSelfDrawWin();
}

void mahjong_self_kong(MahjongGame game, uint8_t suit, uint8_t rank) {
    toInstance(game)->controller.humanSelfKong(static_cast<Suit>(suit), rank);
}

void mahjong_claim_by_index(MahjongGame game, int index) {
    auto* inst = toInstance(game);
    if (index < 0) {
        inst->controller.humanClaim(ClaimOption{});
        return;
    }
    auto options = inst->controller.humanClaimOptions();
    if (index < static_cast<int>(options.size())) {
        inst->controller.humanClaim(options[index]);
    }
}

void mahjong_advance_round(MahjongGame game) {
    toInstance(game)->controller.advanceRound();
}

int mahjong_current_phase(MahjongGame game) {
    return static_cast<int>(toInstance(game)->controller.currentPhase());
}

int mahjong_is_game_over(MahjongGame game) {
    return toInstance(game)->controller.isGameOver() ? 1 : 0;
}

int mahjong_wall_remaining(MahjongGame game) {
    auto* tm = toInstance(game)->controller.turnManager();
    return tm ? tm->wallRemaining() : 0;
}

int mahjong_active_player(MahjongGame game) {
    auto* tm = toInstance(game)->controller.turnManager();
    return tm ? tm->activePlayer() : 0;
}

int mahjong_can_self_draw_win(MahjongGame game) {
    return toInstance(game)->controller.canHumanSelfDrawWin() ? 1 : 0;
}

int mahjong_human_claim_count(MahjongGame game) {
    return static_cast<int>(toInstance(game)->controller.humanClaimOptions().size());
}

// JSON serialization uses shared helpers from JsonHelpers.h

char* mahjong_snapshot_json(MahjongGame game) {
    auto* inst = toInstance(game);
    GameSnapshot snap = inst->controller.snapshot();

    std::ostringstream os;
    os << "{\"phase\":" << static_cast<int>(snap.phase)
       << ",\"activePlayerIndex\":" << snap.activePlayerIndex
       << ",\"prevailingWind\":" << static_cast<int>(snap.prevailingWind)
       << ",\"wallRemaining\":" << snap.wallRemaining
       << ",\"canSelfKong\":" << (snap.canSelfKong ? "true" : "false")
       << ",\"canSelfDrawWin\":" << (snap.canSelfDrawWin ? "true" : "false")
       << ",\"scoringText\":\"" << json::escape(snap.scoringText) << "\""
       << ",\"hasLastDiscard\":" << (snap.hasLastDiscard ? "true" : "false");

    if (snap.hasLastDiscard) {
        os << ",\"lastDiscard\":";
        json::appendTile(os,snap.lastDiscard);
    }

    // Human claim options
    os << ",\"humanClaimOptions\":[";
    for (size_t i = 0; i < snap.humanClaimOptions.size(); i++) {
        if (i > 0) os << ",";
        json::appendClaimOption(os, snap.humanClaimOptions[i], static_cast<int>(i));
    }
    os << "]";

    // Self kong options
    os << ",\"selfKongOptions\":[";
    for (size_t i = 0; i < snap.selfKongOptions.size(); i++) {
        if (i > 0) os << ",";
        json::appendSelfKongEntry(os, snap.selfKongOptions[i]);
    }
    os << "]";

    // Scoring
    os << ",\"scoring\":";
    json::appendScoringSnapshot(os, snap.scoring);

    os << ",\"players\":[";
    for (int i = 0; i < 4; i++) {
        if (i > 0) os << ",";
        const auto& p = snap.players[i];
        os << "{\"name\":\"" << json::escape(p.name) << "\""
           << ",\"seatWind\":" << static_cast<int>(p.seatWind)
           << ",\"score\":" << p.score
           << ",\"isHuman\":" << (p.isHuman ? "true" : "false")
           << ",\"concealedCount\":" << p.concealedCount;

        os << ",\"concealed\":[";
        for (size_t j = 0; j < p.concealed.size(); j++) {
            if (j > 0) os << ",";
            json::appendTile(os,p.concealed[j]);
        }
        os << "]";

        os << ",\"melds\":[";
        for (size_t j = 0; j < p.melds.size(); j++) {
            if (j > 0) os << ",";
            json::appendMeld(os,p.melds[j]);
        }
        os << "]";

        os << ",\"flowers\":[";
        for (size_t j = 0; j < p.flowers.size(); j++) {
            if (j > 0) os << ",";
            json::appendTile(os,p.flowers[j]);
        }
        os << "]";

        os << ",\"discards\":[";
        for (size_t j = 0; j < p.discards.size(); j++) {
            if (j > 0) os << ",";
            json::appendTile(os,p.discards[j]);
        }
        os << "]}";
    }
    os << "]";

    // Training stats (when neural AI is active)
#ifdef HAS_TORCH
    if (inst->trainer) {
        const auto& stats = inst->trainer->stats();
        os << ",\"training\":{"
           << "\"epsilon\":" << stats.epsilon
           << ",\"gamesPlayed\":" << stats.gamesPlayed
           << ",\"avgLoss\":" << stats.avgLoss
           << ",\"totalTransitions\":" << stats.totalTransitions
           << "}";
    }
#endif

    os << "}";

    std::string jsonStr = os.str();
    char* result = new char[jsonStr.size() + 1];
    std::memcpy(result, jsonStr.c_str(), jsonStr.size() + 1);
    return result;
}

void mahjong_free_snapshot(char* json) {
    delete[] json;
}

// --- Neural AI ---

void mahjong_init_neural_ai(MahjongGame game, const char* modelDir) {
#ifdef HAS_TORCH
    auto* inst = toInstance(game);
    inst->modelDir = modelDir;
    inst->trainer = std::make_unique<DQNTrainer>();

    bool loaded = inst->trainer->loadModels(inst->modelDir);
    if (loaded) {
        std::fprintf(stderr, "[NN] Neural AI loaded from %s\n", modelDir);
    } else {
        std::fprintf(stderr, "[NN] No trained model found. AI will use heuristic fallback.\n");
    }

    // Assign trainer to AI players (players 1-3)
    for (int i = 1; i <= 3; i++) {
        AIPlayer* ai = dynamic_cast<AIPlayer*>(inst->controller.getPlayer(i));
        if (ai) {
            ai->setTrainer(inst->trainer.get());
        }
    }

    // Wire discard observer for training data
    inst->controller.addDiscardObserver(
        [inst](int playerIndex, Tile discarded, int turnCount, int wallRemaining) {
            if (!inst->trainer) return;

            auto& players = inst->controller.players();
            const Player* player = players[playerIndex].get();
            std::vector<const Player*> allPlayers;
            for (auto& p : players) allPlayers.push_back(p.get());

            RLGameContext ctx;
            ctx.turnCount = turnCount;
            ctx.wallRemaining = wallRemaining;
            ctx.seatWind = player->seatWind();
            ctx.prevailingWind = inst->controller.prevailingWind();
            ctx.playerIndex = playerIndex;
            ctx.allPlayers = &allPlayers;
            fillPlayerScores(ctx);

            auto features = extractDiscardFeatures(player->hand(), ctx);
            int actionIdx = tileTypeIndex(discarded);
            if (actionIdx < 0) return;

            if (player->isHuman()) {
                inst->trainer->recordHumanDiscard(features, actionIdx);
            }
        });

    // Wire claim observer for training data
    inst->controller.addClaimObserver(
        [inst](int playerIndex, ClaimType type, Tile claimedTile, int turnCount, int wallRemaining) {
            if (!inst->trainer) return;

            auto& players = inst->controller.players();
            const Player* player = players[playerIndex].get();
            std::vector<const Player*> allPlayers;
            for (auto& p : players) allPlayers.push_back(p.get());

            RLGameContext ctx;
            ctx.turnCount = turnCount;
            ctx.wallRemaining = wallRemaining;
            ctx.seatWind = player->seatWind();
            ctx.prevailingWind = inst->controller.prevailingWind();
            ctx.playerIndex = playerIndex;
            ctx.allPlayers = &allPlayers;
            fillPlayerScores(ctx);

            int actionIdx = 3; // Pass
            if (type == ClaimType::Chow) actionIdx = 0;
            else if (type == ClaimType::Pung) actionIdx = 1;
            else if (type == ClaimType::Kong) actionIdx = 2;

            std::vector<ClaimType> available = {type};
            auto features = extractClaimFeatures(player->hand(), claimedTile, available, ctx);

            if (player->isHuman()) {
                inst->trainer->recordHumanClaim(features, actionIdx);
            }
        });

    // Wire round end observer for terminal rewards
    inst->controller.addRoundEndObserver(
        [inst](int winnerIndex, bool selfDrawn, const FaanResult& result, bool isDraw) {
            if (!inst->trainer) return;

            float reward = 0.0f;
            if (isDraw) {
                reward = -1.0f;
            } else if (winnerIndex >= 0) {
                int pts = Scoring::faanToPoints(result.totalFaan);
                // Faan-scaled reward: higher faan = much bigger reward
                reward = 1.0f + static_cast<float>(result.totalFaan) * 1.0f +
                         static_cast<float>(pts);
            }

            inst->trainer->onRoundEnd(reward);
        });

    // Wire round advance callback for self-play + model save
    inst->controller.setRoundAdvanceCallback([inst]() {
        if (!inst->trainer) return;

        inst->trainer->saveModels(inst->modelDir);
        inst->trainer->runSelfPlay(20, inst->controller.prevailingWind(),
                                   inst->controller.dealerIndex());
        inst->trainer->saveModels(inst->modelDir);
    });
#else
    (void)game;
    (void)modelDir;
    std::fprintf(stderr, "[NN] Neural AI not available (built without HAS_TORCH)\n");
#endif
}

void mahjong_run_self_play(MahjongGame game, int numGames) {
#ifdef HAS_TORCH
    auto* inst = toInstance(game);
    if (!inst->trainer) {
        std::fprintf(stderr, "[NN] No trainer initialized, call mahjong_init_neural_ai first\n");
        return;
    }

    std::fprintf(stderr, "[NN] Starting full self-play: %d games\n", numGames);
    bool wasVerbose = g_mahjong_verbose;
    g_mahjong_verbose = false;
    auto totalWins = std::make_shared<int>(0);
    auto totalDraws = std::make_shared<int>(0);
    auto totalRounds = std::make_shared<int>(0);
    auto humanWins = std::make_shared<int>(0);    // player 0 (independent)
    auto aiTeamWins = std::make_shared<int>(0);   // players 1-3 (cooperative)

    for (int g = 0; g < numGames; g++) {
        GameController ctrl;
        ctrl.startSelfPlayGame();

        ctrl.setAdaptationTier(0);  // disable adaptation for speed

        // Warm-up schedule: pure heuristic until replay buffer has enough data,
        // then NN handles claims only (heuristic keeps driving discards)
        const int WARMUP_ROUNDS = 320;
        bool nnActive = inst->trainer->stats().gamesPlayed >= WARMUP_ROUNDS;
        if (nnActive) {
            for (int i = 1; i <= 3; i++) {
                AIPlayer* ai = dynamic_cast<AIPlayer*>(ctrl.getPlayer(i));
                if (ai) {
                    ai->setTrainer(inst->trainer.get());
                    ai->setTrainerClaimOnly(true);
                }
            }
        }

        // Per-player state tracking for proper (prev_state, action, reward, cur_state) transitions
        struct DiscardTracker {
            std::vector<float> features;
            int action = -1;
            int shanten = 8;
            bool hasPending = false;
        };
        auto trackers = std::make_shared<std::array<DiscardTracker, 4>>();
        auto lastDiscarder = std::make_shared<int>(-1);

        // Wire discard observer for training data
        ctrl.addDiscardObserver(
            [&ctrl, inst, trackers, lastDiscarder](int playerIndex, Tile discarded, int turnCount, int wallRemaining) {
                if (!inst->trainer) return;

                *lastDiscarder = playerIndex;

                auto& players = ctrl.players();
                const Player* player = players[playerIndex].get();
                std::vector<const Player*> allPlayers;
                for (auto& p : players) allPlayers.push_back(p.get());

                RLGameContext ctx;
                ctx.turnCount = turnCount;
                ctx.wallRemaining = wallRemaining;
                ctx.seatWind = player->seatWind();
                ctx.prevailingWind = ctrl.prevailingWind();
                ctx.playerIndex = playerIndex;
                ctx.allPlayers = &allPlayers;
                fillPlayerScores(ctx);

                auto features = extractDiscardFeatures(player->hand(), ctx);
                int actionIdx = tileTypeIndex(discarded);
                if (actionIdx < 0) return;

                int curShanten = ShantenCalculator::calculate(player->hand());

                auto& tracker = (*trackers)[playerIndex];
                if (tracker.hasPending) {
                    // Record transition: prev_state -> cur_state with shanten reward
                    float reward = 0.0f;
                    if (curShanten < tracker.shanten) reward = 0.5f;
                    else if (curShanten > tracker.shanten) reward = -0.3f;

                    // Safe discard bonus: genbutsu (tile already discarded by an opponent)
                    bool genbutsu = false;
                    for (int j = 0; j < (int)allPlayers.size(); j++) {
                        if (j == playerIndex || !allPlayers[j]) continue;
                        for (const auto& d : allPlayers[j]->discards()) {
                            if (d.suit == discarded.suit && d.rank == discarded.rank) {
                                genbutsu = true;
                                break;
                            }
                        }
                        if (genbutsu) break;
                    }
                    if (genbutsu) reward += 0.1f;

                    inst->trainer->recordDiscardTransition(
                        tracker.features, tracker.action, reward, features, false);
                }

                tracker.features = features;
                tracker.action = actionIdx;
                tracker.shanten = curShanten;
                tracker.hasPending = true;
            });

        // Wire claim observer for training data
        ctrl.addClaimObserver(
            [&ctrl, inst](int playerIndex, ClaimType type, Tile claimedTile, int turnCount, int wallRemaining) {
                if (!inst->trainer) return;
                if (type == ClaimType::Win) return; // Win is always taken, not a decision

                auto& players = ctrl.players();
                const Player* player = players[playerIndex].get();
                std::vector<const Player*> allPlayers;
                for (auto& p : players) allPlayers.push_back(p.get());

                RLGameContext ctx;
                ctx.turnCount = turnCount;
                ctx.wallRemaining = wallRemaining;
                ctx.seatWind = player->seatWind();
                ctx.prevailingWind = ctrl.prevailingWind();
                ctx.playerIndex = playerIndex;
                ctx.allPlayers = &allPlayers;
                fillPlayerScores(ctx);

                int actionIdx = 3; // Pass
                if (type == ClaimType::Chow) actionIdx = 0;
                else if (type == ClaimType::Pung) actionIdx = 1;
                else if (type == ClaimType::Kong) actionIdx = 2;

                // Build available claims list (we know at least 'type' was available)
                std::vector<ClaimType> available = {type};
                auto features = extractClaimFeatures(player->hand(), claimedTile, available, ctx);

                // Reward: shanten improvement from claiming
                float reward = 0.0f;
                if (actionIdx != 3) {
                    int shantenBefore = ShantenCalculator::calculate(player->hand());
                    Hand simHand = player->hand();
                    simHand.addTile(claimedTile);
                    int shantenAfter = ShantenCalculator::calculate(simHand);
                    if (shantenAfter < shantenBefore) reward = 1.0f;
                    else if (shantenAfter > shantenBefore) reward = -0.5f;
                }

                // Next state is the same features (post-claim state isn't easily available here)
                auto nextState = features;
                inst->trainer->recordClaimTransition(features, actionIdx, reward, nextState, false);
            });

        // Wire round end observer for terminal rewards
        ctrl.addRoundEndObserver(
            [inst, trackers, lastDiscarder, totalWins, totalDraws, totalRounds, humanWins, aiTeamWins](int winnerIndex, bool selfDrawn, const FaanResult& result, bool isDraw) {
                (*totalRounds)++;
                if (!inst->trainer) return;

                int faan = result.totalFaan;
                int pts = Scoring::faanToPoints(faan);

                if (!isDraw && winnerIndex >= 0) {
                    (*totalWins)++;
                    if (winnerIndex == 0) (*humanWins)++;
                    else (*aiTeamWins)++;
                    std::fprintf(stderr, "[NN] Player %d wins (%s, %d faan):",
                                 winnerIndex, selfDrawn ? "self-draw" : "discard", faan);
                    for (const auto& entry : result.breakdown) {
                        std::fprintf(stderr, " %s(%d)", entry.nameCn.c_str(), entry.faan);
                    }
                    std::fprintf(stderr, "\n");
                } else if (isDraw) {
                    (*totalDraws)++;
                }

                // Record terminal transition for each player's last pending discard
                for (int i = 0; i < 4; i++) {
                    auto& tracker = (*trackers)[i];
                    if (!tracker.hasPending) continue;

                    float reward = 0.0f;
                    if (isDraw) {
                        reward = -1.0f;
                    } else if (i == winnerIndex) {
                        // Faan-scaled reward: higher faan = much bigger reward
                        reward = 1.0f + static_cast<float>(faan) * 1.0f +
                                 static_cast<float>(pts);
                    } else if (!selfDrawn && i == *lastDiscarder) {
                        // Dealing-in penalty: scaled by opponent's hand value
                        reward = -1.0f - static_cast<float>(faan) * 0.5f -
                                 static_cast<float>(pts) * 0.5f;
                    } else {
                        // Bystander loser: lighter penalty
                        reward = -1.5f;
                    }

                    std::vector<float> terminal(DISCARD_FEATURE_SIZE, 0.0f);
                    inst->trainer->recordDiscardTransition(
                        tracker.features, tracker.action, reward, terminal, true);
                    tracker.hasPending = false;
                }

                // Train and update stats
                float roundReward = isDraw ? -1.0f :
                    (winnerIndex >= 0 ? 1.0f + static_cast<float>(faan) * 1.0f +
                                        static_cast<float>(pts) : 0.0f);
                inst->trainer->onRoundEnd(roundReward);
            });

        // Run all rounds to completion
        // dt=0.04f: small enough for claim phase first-tick eval (<0.05f),
        // passes AI delay threshold (0.3f) after 8 calls
        const float dt = 0.04f;
        const int maxIterations = 100000;

        while (!ctrl.isGameOver()) {
            int iterations = 0;
            while (!ctrl.isRoundOver() && !ctrl.isGameOver() && iterations < maxIterations) {
                ctrl.update(dt);
                iterations++;
            }
            if (ctrl.isGameOver()) break;
            ctrl.advanceRound();
        }

        if ((g + 1) % 10 == 0 || g == 0) {
            bool nowActive = inst->trainer->stats().gamesPlayed >= WARMUP_ROUNDS;
            std::fprintf(stderr, "[NN] Self-play game %d/%d | epsilon=%.3f | gamesPlayed=%d | %s\n",
                g + 1, numGames, inst->trainer->stats().epsilon,
                inst->trainer->stats().gamesPlayed,
                nowActive ? "NN active" : "warm-up (heuristic)");
        }
    }

    g_mahjong_verbose = wasVerbose;
    inst->trainer->saveModels(inst->modelDir);
    int tw = *totalWins, td = *totalDraws, tr = *totalRounds;
    int hw = *humanWins, aw = *aiTeamWins;
    float aiRate = tr > 0 ? 100.0f * aw / tr : 0.0f;
    float humanRate = tr > 0 ? 100.0f * hw / tr : 0.0f;
    std::fprintf(stderr, "[NN] Full self-play complete: %d games, %d rounds\n", numGames, tr);
    std::fprintf(stderr, "[NN] AI team: %d wins (%.1f%%) | Human: %d wins (%.1f%%) | Draws: %d\n",
        aw, aiRate, hw, humanRate, td);
    std::fprintf(stderr, "[NN] gamesPlayed=%d | epsilon=%.3f\n",
        inst->trainer->stats().gamesPlayed, inst->trainer->stats().epsilon);
#else
    (void)game;
    (void)numGames;
    std::fprintf(stderr, "[NN] Neural AI not available (built without HAS_TORCH)\n");
#endif
}

char* mahjong_training_stats_json(MahjongGame game) {
#ifdef HAS_TORCH
    auto* inst = toInstance(game);
    if (!inst->trainer) {
        const char* empty = "{\"active\":false}";
        char* result = new char[strlen(empty) + 1];
        std::memcpy(result, empty, strlen(empty) + 1);
        return result;
    }

    const auto& stats = inst->trainer->stats();
    std::ostringstream os;
    os << "{\"active\":true"
       << ",\"epsilon\":" << stats.epsilon
       << ",\"gamesPlayed\":" << stats.gamesPlayed
       << ",\"avgLoss\":" << stats.avgLoss
       << ",\"totalTransitions\":" << stats.totalTransitions
       << ",\"learningRate\":" << stats.learningRate
       << ",\"hasTrainedModels\":" << (inst->trainer->hasTrainedModels() ? "true" : "false")
       << "}";

    std::string jsonStr = os.str();
    char* result = new char[jsonStr.size() + 1];
    std::memcpy(result, jsonStr.c_str(), jsonStr.size() + 1);
    return result;
#else
    (void)game;
    const char* empty = "{\"active\":false}";
    char* result = new char[strlen(empty) + 1];
    std::memcpy(result, empty, strlen(empty) + 1);
    return result;
#endif
}

void mahjong_free_training_stats(char* json) {
    delete[] json;
}

char* mahjong_run_benchmark(int numGames, int adaptationTier, const char* weightsDir) {
    std::fprintf(stderr, "[Benchmark] Running %d games, adaptation tier %d\n", numGames, adaptationTier);
    bool wasVerbose = g_mahjong_verbose;
    g_mahjong_verbose = false;

    // Load inference engine weights if provided (needed for adaptation tiers > 0)
    InferenceEngine engine;
    bool hasWeights = false;
    if (weightsDir && weightsDir[0] != '\0') {
        std::string dir(weightsDir);
        hasWeights = engine.loadDiscardWeights(dir + "/discard_weights.bin");
        engine.loadClaimWeights(dir + "/claim_weights.bin");
        if (hasWeights) {
            std::fprintf(stderr, "[Benchmark] Loaded inference weights from %s\n", weightsDir);
        } else {
            std::fprintf(stderr, "[Benchmark] No weights found in %s — adaptation disabled\n", weightsDir);
        }
    }

    int aiTeamWins = 0, humanWins = 0, totalDraws = 0, totalRounds = 0;
    std::array<int, 14> aiFaanDist{};  // index 0-12 = that faan, 13 = limit

    for (int g = 0; g < numGames; g++) {
        GameController ctrl;
        ctrl.startSelfPlayGame();
        ctrl.setAdaptationTier(adaptationTier);

        // Set inference engine on all players (needed for adaptation)
        if (hasWeights) {
            for (int i = 0; i < 4; i++) {
                AIPlayer* ai = dynamic_cast<AIPlayer*>(ctrl.getPlayer(i));
                if (ai) ai->setInferenceEngine(&engine);
            }
        }

        // Wire round-end observer to count wins
        bool gameOver = false;
        ctrl.addRoundEndObserver(
            [&](int winner, bool selfDrawn, const FaanResult& result, bool isDraw) {
                (void)selfDrawn;
                totalRounds++;
                if (!isDraw && winner >= 0) {
                    if (winner == 0) humanWins++;
                    else {
                        aiTeamWins++;
                        int fi = std::min(result.totalFaan, 13);
                        aiFaanDist[fi]++;
                    }
                    std::fprintf(stderr, "[Bench] Player %d wins (%s, %d faan):",
                                 winner, selfDrawn ? "self-draw" : "discard", result.totalFaan);
                    for (const auto& entry : result.breakdown) {
                        std::fprintf(stderr, " %s(%d)", entry.nameCn.c_str(), entry.faan);
                    }
                    std::fprintf(stderr, "\n");
                } else {
                    totalDraws++;
                }
            });

        const float dt = 0.04f;
        const int maxIterations = 100000;
        while (!ctrl.isGameOver()) {
            int iterations = 0;
            while (!ctrl.isRoundOver() && !ctrl.isGameOver() && iterations < maxIterations) {
                ctrl.update(dt);
                iterations++;
            }
            if (ctrl.isGameOver()) break;
            ctrl.advanceRound();
        }

        if ((g + 1) % 10 == 0 || g == 0) {
            std::fprintf(stderr, "[Benchmark] Game %d/%d complete\n", g + 1, numGames);
        }
    }

    g_mahjong_verbose = wasVerbose;

    float aiRate = totalRounds > 0 ? 100.0f * aiTeamWins / totalRounds : 0.0f;
    float humanRate = totalRounds > 0 ? 100.0f * humanWins / totalRounds : 0.0f;
    float drawRate = totalRounds > 0 ? 100.0f * totalDraws / totalRounds : 0.0f;
    std::fprintf(stderr, "[Benchmark] Complete: %d games, %d rounds, tier %d\n",
                 numGames, totalRounds, adaptationTier);
    std::fprintf(stderr, "[Benchmark] AI team: %d (%.1f%%) | Human: %d (%.1f%%) | Draws: %d (%.1f%%)\n",
                 aiTeamWins, aiRate, humanWins, humanRate, totalDraws, drawRate);

    // Print faan distribution
    if (aiTeamWins > 0) {
        std::fprintf(stderr, "[Benchmark] AI faan distribution:");
        for (int f = 3; f <= 13; f++) {
            if (aiFaanDist[f] > 0) {
                std::fprintf(stderr, " %d-faan:%d", f, aiFaanDist[f]);
            }
        }
        std::fprintf(stderr, "\n");
    }

    // Return results as JSON
    std::ostringstream os;
    os << "{\"games\":" << numGames
       << ",\"rounds\":" << totalRounds
       << ",\"adaptationTier\":" << adaptationTier
       << ",\"aiTeamWins\":" << aiTeamWins
       << ",\"humanWins\":" << humanWins
       << ",\"draws\":" << totalDraws
       << ",\"aiWinRate\":" << aiRate
       << ",\"humanWinRate\":" << humanRate
       << ",\"drawRate\":" << drawRate
       << ",\"aiFaanDist\":{";
    bool first = true;
    for (int f = 0; f <= 13; f++) {
        if (aiFaanDist[f] > 0) {
            if (!first) os << ",";
            os << "\"" << f << "\":" << aiFaanDist[f];
            first = false;
        }
    }
    os << "}}";
    std::string jsonStr = os.str();
    char* result = new char[jsonStr.size() + 1];
    std::memcpy(result, jsonStr.c_str(), jsonStr.size() + 1);
    return result;
}

void mahjong_free_benchmark_result(char* json) {
    delete[] json;
}
