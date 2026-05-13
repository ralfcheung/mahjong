#include "GameController.h"
#include "scoring/Scoring.h"
#include <cstdio>

extern bool g_mahjong_verbose;

GameController::GameController() = default;
GameController::~GameController() {
    if (adaptationThread_.joinable()) {
        adaptationThread_.join();
    }
}

void GameController::startNewGame() {
    prevailingWind_ = Wind::East;
    dealerIndex_ = 0;
    roundNumber_ = 0;
    gameOver_ = false;

    // Create players: human (seat 0) + 3 AI
    players_[0] = std::make_unique<HumanPlayer>(0, Wind::East, "You");
    players_[1] = std::make_unique<AIPlayer>(1, Wind::South, "AI East");
    players_[2] = std::make_unique<AIPlayer>(2, Wind::West, "AI North");
    players_[3] = std::make_unique<AIPlayer>(3, Wind::North, "AI West");

    setupTurnManager();
}

void GameController::startSelfPlayGame() {
    prevailingWind_ = Wind::East;
    dealerIndex_ = 0;
    roundNumber_ = 0;
    gameOver_ = false;

    // Create 4 AI players for self-play
    // Player 0 acts as the "human opponent" (plays independently)
    // Players 1-3 cooperate against player 0
    players_[0] = std::make_unique<AIPlayer>(0, Wind::East, "AI East");
    players_[1] = std::make_unique<AIPlayer>(1, Wind::South, "AI South");
    players_[2] = std::make_unique<AIPlayer>(2, Wind::West, "AI West");
    players_[3] = std::make_unique<AIPlayer>(3, Wind::North, "AI North");

    setupTurnManager();

    // Player 0 plays independently; players 1-3 cooperate against it
    auto* p0 = dynamic_cast<AIPlayer*>(players_[0].get());
    if (p0) p0->setCooperativeMode(false);
}

void GameController::setupTurnManager() {
    turnManager_ = std::make_unique<TurnManager>(players_, prevailingWind_, dealerIndex_);
    wireTurnManagerObservers();
    turnManager_->startRound();
    lastAdaptedPlayer_ = -1;
    if (g_mahjong_verbose) std::fprintf(stderr, "[GameController] setupTurnManager: round=%d, phase=%d\n",
                 roundNumber_, static_cast<int>(turnManager_->phase()));
}

void GameController::wireTurnManagerObservers() {
    if (!turnManager_) return;
    for (auto& obs : discardObservers_) turnManager_->addDiscardObserver(obs);
    for (auto& obs : claimObservers_) turnManager_->addClaimObserver(obs);
    for (auto& obs : roundEndObservers_) turnManager_->addRoundEndObserver(obs);
    for (auto& obs : selfKongObservers_) turnManager_->addSelfKongObserver(obs);
    for (auto& obs : selfDrawWinObservers_) turnManager_->addSelfDrawWinObserver(obs);
}

void GameController::addDiscardObserver(TurnManager::DiscardObserver obs) {
    discardObservers_.push_back(obs);
    if (turnManager_) turnManager_->addDiscardObserver(obs);
}

void GameController::addClaimObserver(TurnManager::ClaimObserver obs) {
    claimObservers_.push_back(obs);
    if (turnManager_) turnManager_->addClaimObserver(obs);
}

void GameController::addRoundEndObserver(TurnManager::RoundEndObserver obs) {
    roundEndObservers_.push_back(obs);
    if (turnManager_) turnManager_->addRoundEndObserver(obs);
}

void GameController::addSelfKongObserver(TurnManager::SelfKongObserver obs) {
    selfKongObservers_.push_back(obs);
    if (turnManager_) turnManager_->addSelfKongObserver(obs);
}

void GameController::addSelfDrawWinObserver(TurnManager::SelfDrawWinObserver obs) {
    selfDrawWinObservers_.push_back(obs);
    if (turnManager_) turnManager_->addSelfDrawWinObserver(obs);
}

void GameController::setDiscardObserver(TurnManager::DiscardObserver obs) {
    discardObservers_.clear();
    addDiscardObserver(std::move(obs));
}

void GameController::setClaimObserver(TurnManager::ClaimObserver obs) {
    claimObservers_.clear();
    addClaimObserver(std::move(obs));
}

void GameController::setRoundEndObserver(TurnManager::RoundEndObserver obs) {
    roundEndObservers_.clear();
    addRoundEndObserver(std::move(obs));
}

void GameController::update(float dt) {
    if (turnManager_) {
        // Trigger per-turn adaptation when an AI player is about to draw
        if (turnManager_->phase() == GamePhase::PLAYER_DRAW) {
            int active = turnManager_->activePlayer();
            if (active != lastAdaptedPlayer_) {
                AIPlayer* ai = dynamic_cast<AIPlayer*>(players_[active].get());
                if (ai && ai->inferenceEngineAvailable()) {
                    if (g_mahjong_verbose) std::fprintf(stderr, "[GameController] Triggering adaptation for player %d, round %d\n",
                                 active, roundNumber_);
                    triggerAdaptationForPlayer(active);
                }
                lastAdaptedPlayer_ = active;
            }
        } else {
            // Reset tracking when we leave PLAYER_DRAW so the next draw triggers adaptation
            lastAdaptedPlayer_ = -1;
        }

        // Pause game logic while AI is thinking (adaptation in progress)
        if (!aiAdapting_.load(std::memory_order_relaxed)) {
            turnManager_->update(dt);
        }
    }
}

void GameController::triggerAdaptationForPlayer(int playerIndex) {
    // Always join previous thread before starting a new one
    if (adaptationThread_.joinable()) {
        adaptationThread_.join();
    }

    AIPlayer* ai = dynamic_cast<AIPlayer*>(players_[playerIndex].get());
    if (!ai || !ai->inferenceEngineAvailable()) return;

    // Reset this player's engine to base weights before re-adapting
    ai->resetAdaptation();

    // Snapshot data the background thread needs
    Wind pw = prevailingWind_;
    int di = dealerIndex_;
    int wallRem = turnManager_ ? turnManager_->wallRemaining() : 70;

    std::vector<const Player*> allPlayers;
    for (int i = 0; i < 4; i++) {
        allPlayers.push_back(players_[i].get());
    }
    ObservableState obs = HandSampler::buildObservable(playerIndex, allPlayers);

    aiAdapting_.store(true, std::memory_order_relaxed);
    int roundNum = roundNumber_;
    adaptationThread_ = std::thread([ai, pw, di, obs = std::move(obs), wallRem, roundNum, playerIndex, this]() {
        ai->adaptForRound(pw, di, obs, wallRem);
        aiAdapting_.store(false, std::memory_order_relaxed);
        if (g_mahjong_verbose) std::fprintf(stderr, "[GameController] Adaptation complete for player %d, round %d\n",
                     playerIndex, roundNum);
    });
}

void GameController::resetAdaptation() {
    // Wait for background adaptation to finish before resetting
    if (adaptationThread_.joinable()) {
        adaptationThread_.join();
    }
    for (int i = 0; i < 4; i++) {
        AIPlayer* ai = dynamic_cast<AIPlayer*>(players_[i].get());
        if (ai) {
            ai->resetAdaptation();
        }
    }
}

void GameController::setCooperativeAI(bool enabled) {
    for (int i = 0; i < 4; i++) {
        AIPlayer* ai = dynamic_cast<AIPlayer*>(players_[i].get());
        if (ai) {
            ai->setCooperativeMode(enabled);
        }
    }
}

void GameController::setAdaptationTier(int tier) {
    AdaptationConfig config;
    switch (tier) {
        case 0:  // Skip adaptation entirely
            config.numSimulations = 0;
            config.maxSGDSteps = 0;
            config.batchSize = 1;
            break;
        case 1:  // Mid mobile (A14)
            config.numSimulations = 80;
            config.maxSGDSteps = 10;
            config.batchSize = 32;
            break;
        case 2:  // High mobile (A15+)
            config.numSimulations = 200;
            config.maxSGDSteps = 15;
            config.batchSize = 32;
            break;
        case 3:  // Desktop
        default:
            config.numSimulations = 300;
            config.maxSGDSteps = 20;
            config.batchSize = 32;
            break;
    }

    for (int i = 0; i < 4; i++) {
        AIPlayer* ai = dynamic_cast<AIPlayer*>(players_[i].get());
        if (ai) {
            ai->setAdaptationConfig(config);
        }
    }
}

void GameController::humanDiscardTile(uint8_t tileId) {
    if (!turnManager_) return;
    HumanPlayer* human = dynamic_cast<HumanPlayer*>(players_[0].get());
    if (human && human->isWaitingForDiscard()) {
        human->onTileSelected(tileId);
    }
}

void GameController::humanClaim(const ClaimOption& option) {
    if (!turnManager_) return;
    turnManager_->onHumanClaim(option);
}

void GameController::humanSelfDrawWin() {
    if (!turnManager_) return;
    turnManager_->onHumanSelfDrawWin();
}

void GameController::humanSelfKong(Suit suit, uint8_t rank) {
    if (!turnManager_) return;
    turnManager_->onHumanSelfKong(suit, rank);
}

void GameController::advanceRound() {
    if (!turnManager_) return;
    auto phase = turnManager_->phase();
    if (g_mahjong_verbose) std::fprintf(stderr, "[GameController] advanceRound called, phase=%d, round=%d\n",
                 static_cast<int>(phase), roundNumber_);
    if (phase != GamePhase::SCORING && phase != GamePhase::ROUND_END
        && !turnManager_->isRoundOver()) return;

    if (roundAdvanceCb_) {
        roundAdvanceCb_();
    }

    // Reset per-round adaptation before starting next round
    resetAdaptation();

    dealerIndex_ = (dealerIndex_ + 1) % 4;
    roundNumber_++;
    if (roundNumber_ >= 16) {
        gameOver_ = true;
        return;
    }

    if (roundNumber_ % 4 == 0) {
        prevailingWind_ = nextWind(prevailingWind_);
    }

    setupTurnManager();
}

GameSnapshot GameController::snapshot() const {
    GameSnapshot snap;

    if (turnManager_) {
        snap.phase = turnManager_->phase();
        snap.activePlayerIndex = turnManager_->activePlayer();
        snap.wallRemaining = turnManager_->wallRemaining();
        snap.humanClaimOptions = turnManager_->getHumanClaimOptionsWithCombos();
        auto selfKongOpts = turnManager_->getHumanSelfKongOptions();
        snap.canSelfKong = !selfKongOpts.empty();
        for (const auto& opt : selfKongOpts) {
            snap.selfKongOptions.push_back({
                static_cast<uint8_t>(opt.suit), opt.rank, opt.isPromote
            });
        }
        snap.canSelfDrawWin = turnManager_->canHumanSelfDrawWin();
        snap.scoringText = turnManager_->scoringText();

        // Populate structured scoring data
        auto phase = turnManager_->phase();
        if (phase == GamePhase::SCORING || phase == GamePhase::ROUND_END || phase == GamePhase::GAME_OVER) {
            int winner = turnManager_->winnerIndex();
            if (winner >= 0) {
                snap.scoring.winnerIndex = winner;
                snap.scoring.selfDrawn = turnManager_->isSelfDrawn();
                snap.scoring.isDraw = false;
                const auto& fr = turnManager_->scoringResult();
                snap.scoring.totalFaan = fr.totalFaan;
                snap.scoring.isLimit = fr.isLimit;
                snap.scoring.basePoints = Scoring::faanToPoints(fr.totalFaan);
                for (const auto& entry : fr.breakdown) {
                    snap.scoring.breakdown.push_back({entry.nameCn, entry.nameEn, entry.faan});
                }
            } else {
                // Draw (wall exhausted)
                snap.scoring.isDraw = true;
            }
        }

        const Tile* ld = turnManager_->lastDiscard();
        if (ld) {
            snap.hasLastDiscard = true;
            snap.lastDiscard = *ld;
        }
    }

    snap.prevailingWind = prevailingWind_;
    snap.aiThinking = aiAdapting_.load(std::memory_order_relaxed);

    for (int i = 0; i < 4; i++) {
        auto& ps = snap.players[i];
        const Player* p = players_[i].get();
        ps.name = p->name();
        ps.seatWind = p->seatWind();
        ps.score = p->score();
        ps.isHuman = p->isHuman();
        ps.concealedCount = p->hand().concealedCount();
        ps.melds = p->hand().melds();
        ps.flowers = p->hand().flowers();
        ps.discards = p->discards();

        if (p->isHuman()) {
            ps.concealed = p->hand().concealed();
        }
    }

    return snap;
}

bool GameController::isGameOver() const {
    return gameOver_;
}

bool GameController::isRoundOver() const {
    return turnManager_ ? turnManager_->isRoundOver() : false;
}

GamePhase GameController::currentPhase() const {
    return turnManager_ ? turnManager_->phase() : GamePhase::DEALING;
}

std::vector<ClaimOption> GameController::humanClaimOptions() const {
    return turnManager_ ? turnManager_->getHumanClaimOptionsWithCombos() : std::vector<ClaimOption>{};
}

bool GameController::canHumanSelfDrawWin() const {
    return turnManager_ ? turnManager_->canHumanSelfDrawWin() : false;
}

std::vector<TurnManager::SelfKongOption> GameController::humanSelfKongOptions() const {
    return turnManager_ ? turnManager_->getHumanSelfKongOptions() : std::vector<TurnManager::SelfKongOption>{};
}

const std::vector<Tile>& GameController::humanHandTiles() const {
    return players_[0]->hand().concealed();
}

const Player* GameController::getPlayer(int index) const {
    if (index < 0 || index >= 4) return nullptr;
    return players_[index].get();
}

Player* GameController::getPlayer(int index) {
    if (index < 0 || index >= 4) return nullptr;
    return players_[index].get();
}
