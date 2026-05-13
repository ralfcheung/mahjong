#ifdef HAS_TORCH

#include "DQNTrainer.h"
#include "GameController.h"
#include "player/AIPlayer.h"
#include "tiles/Wall.h"
#include "scoring/WinDetector.h"
#include "scoring/Scoring.h"
#include <fstream>
#include <filesystem>
#include <cstdio>

extern bool g_mahjong_verbose;

namespace fs = std::filesystem;

DQNTrainer::DQNTrainer() {
    discardNet_ = DiscardNet();
    discardTargetNet_ = DiscardNet();
    claimNet_ = ClaimNet();
    claimTargetNet_ = ClaimNet();

    discardOptimizer_ = std::make_unique<torch::optim::Adam>(
        discardNet_->parameters(), torch::optim::AdamOptions(stats_.learningRate));
    claimOptimizer_ = std::make_unique<torch::optim::Adam>(
        claimNet_->parameters(), torch::optim::AdamOptions(stats_.learningRate));

    syncTargetNetworks();
}

bool DQNTrainer::loadModels(const std::string& modelDir) {
    std::string discardPath = modelDir + "/discard_net.pt";
    std::string claimPath = modelDir + "/claim_net.pt";
    std::string bufferPath = modelDir + "/replay_buffer.bin";
    std::string statsPath = modelDir + "/training_stats.json";

    bool loaded = false;

    try {
        if (fs::exists(discardPath)) {
            torch::load(discardNet_, discardPath);
            loaded = true;
        }
        if (fs::exists(claimPath)) {
            torch::load(claimNet_, claimPath);
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Warning: Failed to load neural models: %s\n", e.what());
        // Reinitialize on failure
        discardNet_ = DiscardNet();
        claimNet_ = ClaimNet();
        loaded = false;
    }

    if (loaded) {
        syncTargetNetworks();
        loadReplayBuffer(bufferPath);
        loadStats(statsPath);

        // Recreate optimizers with loaded learning rate
        discardOptimizer_ = std::make_unique<torch::optim::Adam>(
            discardNet_->parameters(), torch::optim::AdamOptions(stats_.learningRate));
        claimOptimizer_ = std::make_unique<torch::optim::Adam>(
            claimNet_->parameters(), torch::optim::AdamOptions(stats_.learningRate));

        modelsLoaded_ = true;
        std::fprintf(stderr, "Neural models loaded. Games played: %d, epsilon: %.3f\n",
                     stats_.gamesPlayed, stats_.epsilon);
    }

    return loaded;
}

void DQNTrainer::saveModels(const std::string& modelDir) const {
    std::fprintf(stderr, "[NN] Saving models to %s (games=%d, epsilon=%.3f, loss=%.4f)\n",
                modelDir.c_str(), stats_.gamesPlayed, stats_.epsilon, stats_.avgLoss);
    try {
        fs::create_directories(modelDir);
        torch::save(discardNet_, modelDir + "/discard_net.pt");
        torch::save(claimNet_, modelDir + "/claim_net.pt");
        saveReplayBuffer(modelDir + "/replay_buffer.bin");
        saveStats(modelDir + "/training_stats.json");
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Warning: Failed to save models: %s\n", e.what());
    }
}

void DQNTrainer::recordDiscardTransition(const std::vector<float>& state, int action,
                                           float reward, const std::vector<float>& nextState, bool done) {
    Transition t;
    t.state = torch::tensor(state);
    t.action = action;
    t.reward = reward;
    t.nextState = torch::tensor(nextState);
    t.done = done;

    discardBuffer_.push_back(std::move(t));
    if ((int)discardBuffer_.size() > MAX_BUFFER_SIZE) {
        discardBuffer_.pop_front();
    }
    stats_.totalTransitions++;
}

void DQNTrainer::recordClaimTransition(const std::vector<float>& state, int action,
                                         float reward, const std::vector<float>& nextState, bool done) {
    Transition t;
    t.state = torch::tensor(state);
    t.action = action;
    t.reward = reward;
    t.nextState = torch::tensor(nextState);
    t.done = done;

    claimBuffer_.push_back(std::move(t));
    if ((int)claimBuffer_.size() > MAX_BUFFER_SIZE) {
        claimBuffer_.pop_front();
    }
    stats_.totalTransitions++;
}

void DQNTrainer::recordHumanDiscard(const std::vector<float>& state, int action) {
    humanDiscardSamples_.push_back({torch::tensor(state), action});
    if ((int)humanDiscardSamples_.size() > 1000) {
        humanDiscardSamples_.erase(humanDiscardSamples_.begin());
    }
}

void DQNTrainer::recordHumanClaim(const std::vector<float>& state, int action) {
    humanClaimSamples_.push_back({torch::tensor(state), action});
    if ((int)humanClaimSamples_.size() > 1000) {
        humanClaimSamples_.erase(humanClaimSamples_.begin());
    }
}

std::vector<Transition> DQNTrainer::sampleBatch(const std::deque<Transition>& buffer, int batchSize) {
    std::vector<Transition> batch;
    int n = static_cast<int>(buffer.size());
    if (n == 0) return batch;

    std::uniform_int_distribution<int> dist(0, n - 1);
    for (int i = 0; i < batchSize && i < n; i++) {
        batch.push_back(buffer[dist(rng_)]);
    }
    return batch;
}

void DQNTrainer::trainDiscardBatch() {
    if ((int)discardBuffer_.size() < BATCH_SIZE) return;

    auto batch = sampleBatch(discardBuffer_, BATCH_SIZE);
    int n = static_cast<int>(batch.size());

    std::vector<torch::Tensor> states, nextStates;
    std::vector<int64_t> actions;
    std::vector<float> rewards;
    std::vector<float> dones;

    for (auto& t : batch) {
        states.push_back(t.state);
        nextStates.push_back(t.nextState);
        actions.push_back(t.action);
        rewards.push_back(t.reward);
        dones.push_back(t.done ? 1.0f : 0.0f);
    }

    auto stateBatch = torch::stack(states);
    auto nextStateBatch = torch::stack(nextStates);
    auto actionBatch = torch::tensor(actions).unsqueeze(1);
    auto rewardBatch = torch::tensor(rewards);
    auto doneBatch = torch::tensor(dones);

    // Current Q values
    discardNet_->train();
    auto qValues = discardNet_->forward(stateBatch).gather(1, actionBatch).squeeze(1);

    // Double Q-learning: use main net to select action, target net to evaluate
    torch::Tensor target;
    {
        torch::NoGradGuard noGrad;
        auto nextActions = discardNet_->forward(nextStateBatch).argmax(1).unsqueeze(1);
        auto nextQTarget = discardTargetNet_->forward(nextStateBatch).gather(1, nextActions).squeeze(1);
        target = rewardBatch + GAMMA * nextQTarget * (1.0f - doneBatch);
    }

    // Huber loss
    auto loss = torch::smooth_l1_loss(qValues, target.detach());

    discardOptimizer_->zero_grad();
    loss.backward();
    torch::nn::utils::clip_grad_norm_(discardNet_->parameters(), 1.0);
    discardOptimizer_->step();

    stats_.avgLoss = 0.9f * stats_.avgLoss + 0.1f * loss.item<float>();
}

void DQNTrainer::trainClaimBatch() {
    if ((int)claimBuffer_.size() < BATCH_SIZE) return;

    auto batch = sampleBatch(claimBuffer_, BATCH_SIZE);

    std::vector<torch::Tensor> states, nextStates;
    std::vector<int64_t> actions;
    std::vector<float> rewards;
    std::vector<float> dones;

    for (auto& t : batch) {
        states.push_back(t.state);
        nextStates.push_back(t.nextState);
        actions.push_back(t.action);
        rewards.push_back(t.reward);
        dones.push_back(t.done ? 1.0f : 0.0f);
    }

    auto stateBatch = torch::stack(states);
    auto nextStateBatch = torch::stack(nextStates);
    auto actionBatch = torch::tensor(actions).unsqueeze(1);
    auto rewardBatch = torch::tensor(rewards);
    auto doneBatch = torch::tensor(dones);

    claimNet_->train();
    auto qValues = claimNet_->forward(stateBatch).gather(1, actionBatch).squeeze(1);

    torch::Tensor target;
    {
        torch::NoGradGuard noGrad;
        auto nextActions = claimNet_->forward(nextStateBatch).argmax(1).unsqueeze(1);
        auto nextQTarget = claimTargetNet_->forward(nextStateBatch).gather(1, nextActions).squeeze(1);
        target = rewardBatch + GAMMA * nextQTarget * (1.0f - doneBatch);
    }

    auto loss = torch::smooth_l1_loss(qValues, target.detach());

    claimOptimizer_->zero_grad();
    loss.backward();
    torch::nn::utils::clip_grad_norm_(claimNet_->parameters(), 1.0);
    claimOptimizer_->step();
}

void DQNTrainer::trainSupervisedDiscard() {
    if (humanDiscardSamples_.size() < 16) return;

    // Train on a mini-batch of human observations
    int batchSize = std::min(16, (int)humanDiscardSamples_.size());
    std::vector<torch::Tensor> states;
    std::vector<int64_t> targets;

    std::uniform_int_distribution<int> dist(0, (int)humanDiscardSamples_.size() - 1);
    for (int i = 0; i < batchSize; i++) {
        int idx = dist(rng_);
        states.push_back(humanDiscardSamples_[idx].first);
        targets.push_back(humanDiscardSamples_[idx].second);
    }

    auto stateBatch = torch::stack(states);
    auto targetBatch = torch::tensor(targets);

    discardNet_->train();
    auto logits = discardNet_->forward(stateBatch);
    auto loss = torch::cross_entropy_loss(logits, targetBatch);

    discardOptimizer_->zero_grad();
    loss.backward();
    torch::nn::utils::clip_grad_norm_(discardNet_->parameters(), 1.0);
    discardOptimizer_->step();
}

void DQNTrainer::trainSupervisedClaim() {
    if (humanClaimSamples_.size() < 16) return;

    int batchSize = std::min(16, (int)humanClaimSamples_.size());
    std::vector<torch::Tensor> states;
    std::vector<int64_t> targets;

    std::uniform_int_distribution<int> dist(0, (int)humanClaimSamples_.size() - 1);
    for (int i = 0; i < batchSize; i++) {
        int idx = dist(rng_);
        states.push_back(humanClaimSamples_[idx].first);
        targets.push_back(humanClaimSamples_[idx].second);
    }

    auto stateBatch = torch::stack(states);
    auto targetBatch = torch::tensor(targets);

    claimNet_->train();
    auto logits = claimNet_->forward(stateBatch);
    auto loss = torch::cross_entropy_loss(logits, targetBatch);

    claimOptimizer_->zero_grad();
    loss.backward();
    torch::nn::utils::clip_grad_norm_(claimNet_->parameters(), 1.0);
    claimOptimizer_->step();
}

void DQNTrainer::trainStep() {
    trainDiscardBatch();
    trainClaimBatch();
    trainSupervisedDiscard();
    trainSupervisedClaim();
}

void DQNTrainer::onRoundEnd(float roundReward) {
    stats_.gamesPlayed++;
    decayEpsilon();

    if (g_mahjong_verbose) std::fprintf(stderr, "[NN] Round %d ended | reward=%.1f | epsilon=%.3f | "
                "discard_buf=%d | claim_buf=%d | human_samples=%d\n",
                stats_.gamesPlayed, roundReward, stats_.epsilon,
                (int)discardBuffer_.size(), (int)claimBuffer_.size(),
                (int)humanDiscardSamples_.size());

    // Train multiple steps at round end
    for (int i = 0; i < 4; i++) {
        trainStep();
    }

    if (g_mahjong_verbose && stats_.avgLoss > 0.0f) {
        std::fprintf(stderr, "[NN] Training loss=%.4f\n", stats_.avgLoss);
    }

    // Sync target networks periodically
    if (stats_.gamesPlayed % TARGET_SYNC_INTERVAL == 0) {
        syncTargetNetworks();
        if (g_mahjong_verbose) std::fprintf(stderr, "[NN] Target networks synced (every %d games)\n", TARGET_SYNC_INTERVAL);
    }

    modelsLoaded_ = true;
}

void DQNTrainer::syncTargetNetworks() {
    torch::NoGradGuard noGrad;

    auto discardParams = discardNet_->named_parameters();
    auto targetParams = discardTargetNet_->named_parameters();
    for (auto& param : discardParams) {
        auto target = targetParams.find(param.key());
        if (target != nullptr) {
            target->copy_(param.value());
        }
    }

    auto claimParams = claimNet_->named_parameters();
    auto claimTargetParams = claimTargetNet_->named_parameters();
    for (auto& param : claimParams) {
        auto target = claimTargetParams.find(param.key());
        if (target != nullptr) {
            target->copy_(param.value());
        }
    }
}

void DQNTrainer::decayEpsilon() {
    // Decay from 1.0 to 0.10 over 1500 rounds
    // Slower decay gives NN more exploration time after warm-up
    float decayRate = (1.0f - 0.10f) / 1500.0f;
    stats_.epsilon = std::max(0.10f, 1.0f - stats_.gamesPlayed * decayRate);
}

int DQNTrainer::selectDiscardAction(const std::vector<float>& state,
                                      const std::vector<bool>& validMask) {
    // Epsilon-greedy
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    if (dist(rng_) < stats_.epsilon) {
        // Random valid action
        std::vector<int> validActions;
        for (int i = 0; i < (int)validMask.size(); i++) {
            if (validMask[i]) validActions.push_back(i);
        }
        if (validActions.empty()) return 0;
        std::uniform_int_distribution<int> actionDist(0, (int)validActions.size() - 1);
        return validActions[actionDist(rng_)];
    }

    // Greedy: forward pass
    discardNet_->eval();
    torch::NoGradGuard noGrad;
    auto input = torch::tensor(state).unsqueeze(0);
    auto qValues = discardNet_->forward(input).squeeze(0);

    // Mask invalid actions
    for (int i = 0; i < (int)validMask.size() && i < NUM_TILE_TYPES; i++) {
        if (!validMask[i]) {
            qValues[i] = -1e9f;
        }
    }

    return qValues.argmax().item<int>();
}

int DQNTrainer::selectClaimAction(const std::vector<float>& state,
                                    const std::vector<bool>& validMask) {
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    if (dist(rng_) < stats_.epsilon) {
        std::vector<int> validActions;
        for (int i = 0; i < (int)validMask.size(); i++) {
            if (validMask[i]) validActions.push_back(i);
        }
        if (validActions.empty()) return NUM_CLAIM_ACTIONS - 1; // Pass
        std::uniform_int_distribution<int> actionDist(0, (int)validActions.size() - 1);
        return validActions[actionDist(rng_)];
    }

    claimNet_->eval();
    torch::NoGradGuard noGrad;
    auto input = torch::tensor(state).unsqueeze(0);
    auto qValues = claimNet_->forward(input).squeeze(0);

    for (int i = 0; i < (int)validMask.size() && i < NUM_CLAIM_ACTIONS; i++) {
        if (!validMask[i]) {
            qValues[i] = -1e9f;
        }
    }

    return qValues.argmax().item<int>();
}

// Self-play simulation: full AIPlayer pipeline via GameController
void DQNTrainer::runSelfPlay(int numGames, Wind prevailingWind, int dealerIndex) {
    std::fprintf(stderr, "[NN] Self-play: running %d games...\n", numGames);
    int aiTeamWins = 0;   // players 1-3
    int humanWins = 0;    // player 0
    int selfPlayDraws = 0;
    std::array<int, 14> aiFaanDist{};  // faan distribution for AI wins (index 0-12, 13=limit)

    for (int game = 0; game < numGames; game++) {
        // Create local GameController with 4 AI players
        GameController controller;
        controller.startSelfPlayGame();   // player 0 independent, 1-3 cooperative
        controller.setAdaptationTier(0);  // disable adaptation for speed

        // Warm-up schedule: pure heuristic for first 320 rounds,
        // then NN handles claims only (heuristic keeps driving discards)
        const int WARMUP_ROUNDS = 320;
        if (stats_.gamesPlayed >= WARMUP_ROUNDS) {
            for (int i = 1; i <= 3; i++) {
                AIPlayer* ai = dynamic_cast<AIPlayer*>(controller.getPlayer(i));
                if (ai) {
                    ai->setTrainer(this);
                    ai->setTrainerClaimOnly(true);
                }
            }
        }

        // Track state for reward computation
        int lastDiscarder = -1;
        std::array<int, 4> prevShanten;
        for (int i = 0; i < 4; i++) prevShanten[i] = 8;
        bool roundDone = false;

        // Build allPlayers vector for feature extraction
        std::vector<const Player*> allPlayers;
        for (int i = 0; i < 4; i++)
            allPlayers.push_back(controller.getPlayer(i));

        // Wire discard observer
        controller.addDiscardObserver(
            [&](int pIdx, Tile disc, int turn, int wallRem) {
                lastDiscarder = pIdx;

                // Only record transitions for NN players 1-3
                if (pIdx == 0) return;

                const Player* player = controller.getPlayer(pIdx);

                // Reconstruct pre-discard hand
                Hand preHand = player->hand();
                preHand.addTile(disc);

                RLGameContext ctx;
                ctx.turnCount = turn;
                ctx.wallRemaining = wallRem;
                ctx.seatWind = player->seatWind();
                ctx.prevailingWind = controller.prevailingWind();
                ctx.playerIndex = pIdx;
                ctx.allPlayers = &allPlayers;
                fillPlayerScores(ctx);

                auto state = extractDiscardFeatures(preHand, ctx);
                auto nextState = extractDiscardFeatures(player->hand(), ctx);
                int actionIdx = tileTypeIndex(disc);
                if (actionIdx < 0) return;

                // Shanten-based reward shaping
                int newShanten = ShantenCalculator::calculate(player->hand());
                float reward = 0.0f;
                if (newShanten < prevShanten[pIdx]) reward = 0.5f;
                else if (newShanten > prevShanten[pIdx]) reward = -0.3f;
                prevShanten[pIdx] = newShanten;

                recordDiscardTransition(state, actionIdx, reward, nextState, false);
            });

        // Wire claim observer
        controller.addClaimObserver(
            [&](int pIdx, ClaimType type, Tile claimed, int turn, int wallRem) {
                if (pIdx == 0) return;

                const Player* player = controller.getPlayer(pIdx);

                RLGameContext ctx;
                ctx.turnCount = turn;
                ctx.wallRemaining = wallRem;
                ctx.seatWind = player->seatWind();
                ctx.prevailingWind = controller.prevailingWind();
                ctx.playerIndex = pIdx;
                ctx.allPlayers = &allPlayers;
                fillPlayerScores(ctx);

                int actionIdx = 3; // Pass
                if (type == ClaimType::Chow) actionIdx = 0;
                else if (type == ClaimType::Pung) actionIdx = 1;
                else if (type == ClaimType::Kong) actionIdx = 2;

                auto state = extractClaimFeatures(player->hand(), claimed, {type}, ctx);

                int newShanten = ShantenCalculator::calculate(player->hand());
                float reward = (newShanten < prevShanten[pIdx]) ? 1.0f : 0.0f;
                prevShanten[pIdx] = newShanten;

                recordClaimTransition(state, actionIdx, reward, state, false);
            });

        // Wire round-end observer
        controller.addRoundEndObserver(
            [&](int winner, bool selfDrawn, const FaanResult& result, bool isDraw) {
                std::vector<float> dummyNext(DISCARD_FEATURE_SIZE, 0.0f);

                if (!isDraw && winner >= 0) {
                    if (winner == 0) humanWins++;
                    else {
                        aiTeamWins++;
                        int fi = std::min(result.totalFaan, 13);
                        aiFaanDist[fi]++;
                    }

                    // Winner reward (only for NN players 1-3)
                    if (winner != 0) {
                        float winReward = 5.0f + static_cast<float>(Scoring::faanToPoints(result.totalFaan));
                        RLGameContext ctx;
                        ctx.seatWind = controller.getPlayer(winner)->seatWind();
                        ctx.prevailingWind = controller.prevailingWind();
                        ctx.playerIndex = winner;
                        ctx.allPlayers = &allPlayers;
                        fillPlayerScores(ctx);
                        auto winState = extractDiscardFeatures(controller.getPlayer(winner)->hand(), ctx);
                        recordDiscardTransition(winState, 0, winReward, dummyNext, true);
                    }

                    // Deal-in penalty (only for NN players 1-3)
                    if (!selfDrawn && lastDiscarder > 0) {
                        float penalty = -(2.0f + static_cast<float>(result.totalFaan));
                        RLGameContext ctx;
                        ctx.seatWind = controller.getPlayer(lastDiscarder)->seatWind();
                        ctx.prevailingWind = controller.prevailingWind();
                        ctx.playerIndex = lastDiscarder;
                        ctx.allPlayers = &allPlayers;
                        fillPlayerScores(ctx);
                        auto diState = extractDiscardFeatures(controller.getPlayer(lastDiscarder)->hand(), ctx);
                        recordDiscardTransition(diState, 0, penalty, dummyNext, true);
                    }
                } else {
                    selfPlayDraws++;
                    // Draw penalty for NN players
                    for (int i = 1; i <= 3; i++) {
                        RLGameContext ctx;
                        ctx.seatWind = controller.getPlayer(i)->seatWind();
                        ctx.prevailingWind = controller.prevailingWind();
                        ctx.playerIndex = i;
                        ctx.allPlayers = &allPlayers;
                        fillPlayerScores(ctx);
                        auto state = extractDiscardFeatures(controller.getPlayer(i)->hand(), ctx);
                        recordDiscardTransition(state, 0, -1.0f, dummyNext, true);
                    }
                }
                roundDone = true;
            });

        // Run tight loop until round ends
        // dt=0.04f: small enough for claim phase first-tick eval (<0.05f threshold)
        const float dt = 0.04f;
        const int maxIterations = 10000;
        for (int iter = 0; iter < maxIterations && !roundDone; iter++) {
            controller.update(dt);
        }

        // Train step + bookkeeping
        stats_.gamesPlayed++;
        decayEpsilon();
        trainStep();
    }

    int totalGames = aiTeamWins + humanWins + selfPlayDraws;
    float aiWinRate = totalGames > 0 ? 100.0f * aiTeamWins / totalGames : 0.0f;
    float humanWinRate = totalGames > 0 ? 100.0f * humanWins / totalGames : 0.0f;
    std::fprintf(stderr, "[NN] Self-play done: AI team %d wins (%.1f%%) | "
                "Human %d wins (%.1f%%) | %d draws | "
                "discard_buf=%d | loss=%.4f\n",
                aiTeamWins, aiWinRate, humanWins, humanWinRate, selfPlayDraws,
                (int)discardBuffer_.size(), stats_.avgLoss);

    // Print AI faan distribution
    if (aiTeamWins > 0) {
        std::fprintf(stderr, "[NN] AI faan distribution:");
        for (int f = 3; f <= 13; f++) {
            if (aiFaanDist[f] > 0) {
                std::fprintf(stderr, " %d-faan:%d", f, aiFaanDist[f]);
            }
        }
        std::fprintf(stderr, "\n");
    }
}

void DQNTrainer::saveReplayBuffer(const std::string& path) const {
    try {
        std::ofstream file(path, std::ios::binary);
        if (!file) return;

        // Save discard buffer
        int32_t discardSize = static_cast<int32_t>(discardBuffer_.size());
        file.write(reinterpret_cast<const char*>(&discardSize), sizeof(discardSize));
        for (const auto& t : discardBuffer_) {
            auto stateData = t.state.contiguous().data_ptr<float>();
            auto nextData = t.nextState.contiguous().data_ptr<float>();
            int32_t stateSize = static_cast<int32_t>(t.state.numel());
            int32_t nextSize = static_cast<int32_t>(t.nextState.numel());

            file.write(reinterpret_cast<const char*>(&stateSize), sizeof(stateSize));
            file.write(reinterpret_cast<const char*>(stateData), stateSize * sizeof(float));
            file.write(reinterpret_cast<const char*>(&t.action), sizeof(t.action));
            file.write(reinterpret_cast<const char*>(&t.reward), sizeof(t.reward));
            file.write(reinterpret_cast<const char*>(&nextSize), sizeof(nextSize));
            file.write(reinterpret_cast<const char*>(nextData), nextSize * sizeof(float));
            uint8_t done = t.done ? 1 : 0;
            file.write(reinterpret_cast<const char*>(&done), sizeof(done));
        }

        // Save claim buffer
        int32_t claimSize = static_cast<int32_t>(claimBuffer_.size());
        file.write(reinterpret_cast<const char*>(&claimSize), sizeof(claimSize));
        for (const auto& t : claimBuffer_) {
            auto stateData = t.state.contiguous().data_ptr<float>();
            auto nextData = t.nextState.contiguous().data_ptr<float>();
            int32_t stateSize = static_cast<int32_t>(t.state.numel());
            int32_t nextSize = static_cast<int32_t>(t.nextState.numel());

            file.write(reinterpret_cast<const char*>(&stateSize), sizeof(stateSize));
            file.write(reinterpret_cast<const char*>(stateData), stateSize * sizeof(float));
            file.write(reinterpret_cast<const char*>(&t.action), sizeof(t.action));
            file.write(reinterpret_cast<const char*>(&t.reward), sizeof(t.reward));
            file.write(reinterpret_cast<const char*>(&nextSize), sizeof(nextSize));
            file.write(reinterpret_cast<const char*>(nextData), nextSize * sizeof(float));
            uint8_t done = t.done ? 1 : 0;
            file.write(reinterpret_cast<const char*>(&done), sizeof(done));
        }
    } catch (...) {
        // Silently fail on save errors
    }
}

bool DQNTrainer::loadReplayBuffer(const std::string& path) {
    try {
        std::ifstream file(path, std::ios::binary);
        if (!file) return false;

        auto readTransitions = [&](std::deque<Transition>& buffer) {
            int32_t count;
            file.read(reinterpret_cast<char*>(&count), sizeof(count));
            for (int32_t i = 0; i < count; i++) {
                Transition t;
                int32_t stateSize;
                file.read(reinterpret_cast<char*>(&stateSize), sizeof(stateSize));
                std::vector<float> stateData(stateSize);
                file.read(reinterpret_cast<char*>(stateData.data()), stateSize * sizeof(float));
                t.state = torch::tensor(stateData);

                file.read(reinterpret_cast<char*>(&t.action), sizeof(t.action));
                file.read(reinterpret_cast<char*>(&t.reward), sizeof(t.reward));

                int32_t nextSize;
                file.read(reinterpret_cast<char*>(&nextSize), sizeof(nextSize));
                std::vector<float> nextData(nextSize);
                file.read(reinterpret_cast<char*>(nextData.data()), nextSize * sizeof(float));
                t.nextState = torch::tensor(nextData);

                uint8_t done;
                file.read(reinterpret_cast<char*>(&done), sizeof(done));
                t.done = (done != 0);

                buffer.push_back(std::move(t));
            }
        };

        readTransitions(discardBuffer_);
        readTransitions(claimBuffer_);
        return true;
    } catch (...) {
        return false;
    }
}

void DQNTrainer::saveStats(const std::string& path) const {
    try {
        std::ofstream file(path);
        if (!file) return;
        char buf[512];
        std::snprintf(buf, sizeof(buf),
            "{\n"
            "  \"gamesPlayed\": %d,\n"
            "  \"epsilon\": %.6f,\n"
            "  \"learningRate\": %.6f,\n"
            "  \"avgLoss\": %.6f,\n"
            "  \"totalTransitions\": %d,\n"
            "  \"discardBufferSize\": %d,\n"
            "  \"claimBufferSize\": %d\n"
            "}\n",
            stats_.gamesPlayed, stats_.epsilon, stats_.learningRate,
            stats_.avgLoss, stats_.totalTransitions,
            (int)discardBuffer_.size(), (int)claimBuffer_.size());
        file << buf;
    } catch (...) {}
}

bool DQNTrainer::loadStats(const std::string& path) {
    try {
        std::ifstream file(path);
        if (!file) return false;
        std::string content((std::istreambuf_iterator<char>(file)),
                             std::istreambuf_iterator<char>());

        // Simple JSON parsing for our known fields
        auto extractInt = [&](const std::string& key) -> int {
            auto pos = content.find("\"" + key + "\"");
            if (pos == std::string::npos) return 0;
            pos = content.find(':', pos);
            if (pos == std::string::npos) return 0;
            return std::atoi(content.c_str() + pos + 1);
        };
        auto extractFloat = [&](const std::string& key) -> float {
            auto pos = content.find("\"" + key + "\"");
            if (pos == std::string::npos) return 0.0f;
            pos = content.find(':', pos);
            if (pos == std::string::npos) return 0.0f;
            return static_cast<float>(std::atof(content.c_str() + pos + 1));
        };

        stats_.gamesPlayed = extractInt("gamesPlayed");
        stats_.epsilon = extractFloat("epsilon");
        stats_.learningRate = extractFloat("learningRate");
        stats_.avgLoss = extractFloat("avgLoss");
        stats_.totalTransitions = extractInt("totalTransitions");
        return true;
    } catch (...) {
        return false;
    }
}

#endif // HAS_TORCH
