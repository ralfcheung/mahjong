#include "TurnManager.h"
#include "player/HumanPlayer.h"
#include "player/AIPlayer.h"
#include "scoring/WinDetector.h"
#include <algorithm>
#include <cstdio>

extern bool g_mahjong_verbose;

TurnManager::TurnManager(std::array<std::unique_ptr<Player>, 4>& players,
                         Wind prevailingWind, int dealerIndex)
    : players_(players), prevailingWind_(prevailingWind), dealerIndex_(dealerIndex) {
    activePlayer_ = dealerIndex;
}

void TurnManager::addDiscardObserver(DiscardObserver observer) {
    discardObservers_.push_back(std::move(observer));
}

void TurnManager::addClaimObserver(ClaimObserver observer) {
    claimObservers_.push_back(std::move(observer));
}

void TurnManager::addRoundEndObserver(RoundEndObserver observer) {
    roundEndObservers_.push_back(std::move(observer));
}

void TurnManager::addSelfKongObserver(SelfKongObserver observer) {
    selfKongObservers_.push_back(std::move(observer));
}

void TurnManager::addSelfDrawWinObserver(SelfDrawWinObserver observer) {
    selfDrawWinObservers_.push_back(std::move(observer));
}

void TurnManager::clearObservers() {
    discardObservers_.clear();
    claimObservers_.clear();
    roundEndObservers_.clear();
    selfKongObservers_.clear();
    selfDrawWinObservers_.clear();
}

void TurnManager::setDiscardObserver(DiscardObserver observer) {
    discardObservers_.clear();
    discardObservers_.push_back(std::move(observer));
}

void TurnManager::setClaimObserver(ClaimObserver observer) {
    claimObservers_.clear();
    claimObservers_.push_back(std::move(observer));
}

void TurnManager::setRoundEndObserver(RoundEndObserver observer) {
    roundEndObservers_.clear();
    roundEndObservers_.push_back(std::move(observer));
}

void TurnManager::startRound() {
    wall_ = Wall();
    wall_.shuffle();
    roundOver_ = false;
    winnerIndex_ = -1;
    selfDrawn_ = false;
    lastDiscard_ = false;
    waitingForHumanDiscard_ = false;
    humanCanSelfDraw_ = false;
    firstTurn_ = true;
    turnCount_ = 0;
    isKongReplacement_ = false;
    consecutiveKongs_ = 0;
    scoringText_.clear();
    aiClaimPlayer_ = -1;
    aiClaimType_ = ClaimType::None;
    humanClaimPending_ = false;
    humanClaimResolved_ = false;

    for (auto& p : players_) {
        p->hand().clear();
        p->clearDiscards();
    }

    enterPhase(GamePhase::DEALING);
}

void TurnManager::enterPhase(GamePhase newPhase) {
    phase_ = newPhase;
    phaseTimer_ = 0.0f;
    if (newPhase == GamePhase::CLAIM_PHASE) {
        claimTimer_ = 0.0f;
        humanClaimPending_ = false;
        humanClaimResolved_ = false;
        aiClaimPlayer_ = -1;
        aiClaimType_ = ClaimType::None;
        humanCanSelfDraw_ = false;
    }
    if (newPhase == GamePhase::PLAYER_DRAW) {
        humanCanSelfDraw_ = false;
    }
}

void TurnManager::deal() {
    for (int round = 0; round < 13; round++) {
        for (int p = 0; p < 4; p++) {
            int idx = (dealerIndex_ + p) % 4;
            if (!wall_.isEmpty()) {
                players_[idx]->hand().addTile(wall_.draw());
            }
        }
    }

    for (auto& p : players_) {
        p->hand().sortTiles();
    }

    enterPhase(GamePhase::REPLACING_FLOWERS);
}

void TurnManager::replaceFlowers() {
    bool replaced = true;
    while (replaced) {
        replaced = false;
        for (int p = 0; p < 4; p++) {
            auto& hand = players_[p]->hand();
            auto& concealed = hand.concealedMut();

            for (auto it = concealed.begin(); it != concealed.end(); ) {
                if (it->isBonus()) {
                    Tile bonus = *it;
                    it = concealed.erase(it);
                    hand.addFlower(bonus);

                    // Check for All Eight Flowers instant win
                    if (hand.flowers().size() == 8) {
                        activePlayer_ = p;
                        WinContext ctx;
                        ctx.selfDrawn = true;
                        ctx.allEightFlowers = true;
                        ctx.seatWind = players_[p]->seatWind();
                        ctx.prevailingWind = prevailingWind_;
                        ctx.isDealer = (p == dealerIndex_);
                        winnerIndex_ = p;
                        selfDrawn_ = true;
                        roundOver_ = true;
                        enterPhase(GamePhase::SCORING);
                        scoringResult_.totalFaan = 8;
                        scoringResult_.isLimit = true;
                        scoringResult_.breakdown.push_back({"大花糊", "All Eight Flowers", 8});
                        // Build scoring text
                        char buf[512];
                        snprintf(buf, sizeof(buf), "%s wins! (All Eight Flowers 大花糊)\n\n",
                                 players_[p]->name().c_str());
                        scoringText_ = buf;
                        snprintf(buf, sizeof(buf), "  大花糊 All Eight Flowers: 8 faan\n\nTotal: 8 faan\nBase points: %d\n\nPress SPACE for next round",
                                 Scoring::faanToPoints(8));
                        scoringText_ += buf;

                        int discarderIdx = -1;
                        PaymentResult payments = PaymentCalculator::calculate(
                            p, discarderIdx, 8, dealerIndex_);
                        for (int i = 0; i < 4; i++) {
                            players_[i]->adjustScore(-payments.payments[i]);
                        }
                        for (auto& obs : roundEndObservers_) {
                            obs(p, true, scoringResult_, false);
                        }
                        return;
                    }

                    if (!wall_.isEmpty()) {
                        Tile replacement = wall_.drawReplacement();
                        hand.addTile(replacement);
                        replaced = true;
                    }
                } else {
                    ++it;
                }
            }
        }
    }

    for (auto& p : players_) {
        p->hand().sortTiles();
    }

    activePlayer_ = dealerIndex_;
    enterPhase(GamePhase::PLAYER_DRAW);
}

void TurnManager::playerDraw() {
    isKongReplacement_ = false;
    consecutiveKongs_ = 0;

    if (wall_.isEmpty()) {
        roundOver_ = true;
        scoringText_ = "Wall exhausted - Draw!\n\nPress SPACE for next round";
        for (auto& obs : roundEndObservers_) {
            FaanResult empty;
            obs(-1, false, empty, true);
        }
        enterPhase(GamePhase::ROUND_END);
        return;
    }

    Tile drawn = wall_.draw();
    if (g_mahjong_verbose) fprintf(stderr, "[playerDraw] player=%d drew %s (bonus=%d)\n",
           activePlayer_, drawn.toString().c_str(), drawn.isBonus());

    if (drawn.isBonus()) {
        players_[activePlayer_]->hand().addFlower(drawn);
        // Check All Eight Flowers
        if (players_[activePlayer_]->hand().flowers().size() == 8) {
            handleScoring(activePlayer_, true, drawn);
            return;
        }
        if (!wall_.isEmpty()) {
            Tile replacement = wall_.drawReplacement();
            if (g_mahjong_verbose) fprintf(stderr, "[playerDraw] replacement=%s (bonus=%d)\n",
                   replacement.toString().c_str(), replacement.isBonus());
            if (replacement.isBonus()) {
                players_[activePlayer_]->hand().addFlower(replacement);
                // Check All Eight Flowers on replacement too
                if (players_[activePlayer_]->hand().flowers().size() == 8) {
                    handleScoring(activePlayer_, true, replacement);
                    return;
                }
                enterPhase(GamePhase::REPLACEMENT_DRAW);
                return;
            }
            players_[activePlayer_]->hand().addTile(replacement);
        }
    } else {
        players_[activePlayer_]->hand().addTile(drawn);
    }

    lastDrawnTile_ = drawn;
    hasLastDrawn_ = !drawn.isBonus(); // Track the actual drawn tile (or replacement)
    if (g_mahjong_verbose) fprintf(stderr, "[playerDraw] lastDrawnTile_=%s hasLastDrawn_=%d\n",
           lastDrawnTile_.toString().c_str(), hasLastDrawn_);

    players_[activePlayer_]->hand().sortTiles();

    // Check for self-draw win (tsumo)
    checkSelfDrawWin();
    if (phase_ == GamePhase::SCORING) return; // Won!

    enterPhase(GamePhase::PLAYER_TURN);
}

void TurnManager::checkSelfDrawWin() {
    if (!hasLastDrawn_) { return; }

    Hand& hand = players_[activePlayer_]->hand();
    const auto& tiles = hand.concealed();
    if (tiles.size() < 2) { return; }

    Tile drawnTile = lastDrawnTile_;

    // Log full hand
    // fprintf(stderr, "[checkSelfDrawWin] Hand: ");
    // for (const auto& t : tiles) fprintf(stderr, "%s ", t.toString().c_str());
    // fprintf(stderr, "\n");
    // fprintf(stderr, "[checkSelfDrawWin] Melds: ");
    // for (const auto& m : hand.melds()) {
    //     for (const auto& t : m.tiles) fprintf(stderr, "%s ", t.toString().c_str());
    //     fprintf(stderr, "| ");
    // }
    // fprintf(stderr, "\n");

    // Create a temporary hand without the drawn tile for win checking
    Hand tempHand;
    bool skippedDrawn = false;
    for (const auto& t : tiles) {
        if (!skippedDrawn && t.id == drawnTile.id) {
            skippedDrawn = true;
            continue;
        }
        tempHand.addTile(t);
    }
    for (const auto& m : hand.melds()) tempHand.addMeld(m);
    for (const auto& f : hand.flowers()) tempHand.addFlower(f);


    auto wins = WinDetector::findWins(tempHand, drawnTile);
    if (wins.empty()) { if (g_mahjong_verbose) fprintf(stderr, "[checkSelfDrawWin] NO valid win decompositions found\n"); return; }

    WinContext ctx;
    ctx.selfDrawn = true;
    ctx.seatWind = players_[activePlayer_]->seatWind();
    ctx.prevailingWind = prevailingWind_;
    ctx.isDealer = (activePlayer_ == dealerIndex_);
    ctx.lastWallTile = wall_.isEmpty();
    ctx.kongReplacement = isKongReplacement_;
    ctx.heavenlyHand = (firstTurn_ && turnCount_ == 0 && activePlayer_ == dealerIndex_);
    ctx.humanHand = (firstTurn_ && turnCount_ == 0 && activePlayer_ != dealerIndex_);
    ctx.consecutiveKongs = consecutiveKongs_;
    ctx.turnCount = turnCount_;

    FaanResult best = Scoring::calculate(tempHand, drawnTile, ctx);

    if (best.totalFaan >= Scoring::MIN_FAAN) {
        if (!players_[activePlayer_]->isHuman()) {
            handleScoring(activePlayer_, true, drawnTile);
        } else {
            // Human can choose to win during their turn
            humanCanSelfDraw_ = true;
            if (g_mahjong_verbose) fprintf(stderr, "[checkSelfDrawWin] humanCanSelfDraw_ = true!\n");
        }
    } else {
        if (g_mahjong_verbose) fprintf(stderr, "[checkSelfDrawWin] BELOW MIN_FAAN - no win offered\n");
    }
}

void TurnManager::onHumanSelfDrawWin() {
    if (!humanCanSelfDraw_ || activePlayer_ != 0) return;
    humanCanSelfDraw_ = false;
    waitingForHumanDiscard_ = false;
    for (auto& obs : selfDrawWinObservers_) {
        obs(0, lastDrawnTile_, turnCount_, wall_.remaining());
    }
    handleScoring(0, true, lastDrawnTile_);
}

void TurnManager::handleScoring(int winner, bool selfDraw, Tile winTile) {
    winnerIndex_ = winner;
    selfDrawn_ = selfDraw;
    roundOver_ = true;
    enterPhase(GamePhase::SCORING);

    // Create temporary hand for scoring (concealed tiles minus the winning tile)
    Hand& hand = players_[winner]->hand();
    Hand tempHand;
    const auto& tiles = hand.concealed();
    bool skipped = false;
    for (const auto& t : tiles) {
        if (!skipped && t.id == winTile.id) {
            skipped = true;
            continue;
        }
        tempHand.addTile(t);
    }
    // If winTile wasn't in concealed (e.g., discard claim), add all concealed
    if (!skipped) {
        tempHand = Hand();
        for (const auto& t : tiles) tempHand.addTile(t);
    }
    for (const auto& m : hand.melds()) tempHand.addMeld(m);
    for (const auto& f : hand.flowers()) tempHand.addFlower(f);

    WinContext ctx;
    ctx.selfDrawn = selfDraw;
    ctx.seatWind = players_[winner]->seatWind();
    ctx.prevailingWind = prevailingWind_;
    ctx.isDealer = (winner == dealerIndex_);
    ctx.lastWallTile = wall_.isEmpty();
    ctx.kongReplacement = isKongReplacement_;
    ctx.heavenlyHand = (selfDraw && firstTurn_ && turnCount_ == 0 && winner == dealerIndex_);
    ctx.humanHand = (selfDraw && firstTurn_ && turnCount_ == 0 && winner != dealerIndex_);
    ctx.allEightFlowers = (hand.flowers().size() == 8);
    ctx.consecutiveKongs = consecutiveKongs_;
    ctx.turnCount = turnCount_;

    // For All Eight Flowers, the hand may not form valid win decompositions
    if (ctx.allEightFlowers) {
        scoringResult_.totalFaan = 8;
        scoringResult_.isLimit = true;
        scoringResult_.breakdown.push_back({"大花糊", "All Eight Flowers", 8});
    } else {
        scoringResult_ = Scoring::calculate(tempHand, winTile, ctx);
    }

    // Calculate payments
    int discarderIdx = selfDraw ? -1 : lastDiscardPlayer_;
    PaymentResult payments = PaymentCalculator::calculate(
        winner, discarderIdx, scoringResult_.totalFaan, dealerIndex_);

    for (int i = 0; i < 4; i++) {
        players_[i]->adjustScore(-payments.payments[i]); // negative payment = receive
    }

    // Build scoring text
    char buf[512];
    const char* winnerName = players_[winner]->name().c_str();
    snprintf(buf, sizeof(buf), "%s wins! (%s)\n\n",
             winnerName, selfDraw ? "Self-Draw \xe8\x87\xaa\xe6\x91\xb8" : "Discard Win");
    scoringText_ = buf;

    for (const auto& entry : scoringResult_.breakdown) {
        snprintf(buf, sizeof(buf), "  %s %s: %d faan\n",
                 entry.nameCn.c_str(), entry.nameEn.c_str(), entry.faan);
        scoringText_ += buf;
    }

    snprintf(buf, sizeof(buf), "\nTotal: %d faan%s\n",
             scoringResult_.totalFaan,
             scoringResult_.isLimit ? " (LIMIT!)" : "");
    scoringText_ += buf;

    int pts = Scoring::faanToPoints(scoringResult_.totalFaan);
    snprintf(buf, sizeof(buf), "Base points: %d\n\nPress SPACE for next round", pts);
    scoringText_ += buf;

    for (auto& obs : roundEndObservers_) {
        obs(winner, selfDraw, scoringResult_, false);
    }
}

// Get self-kong options for any player (concealed kong or promote pung to kong)
static std::vector<TurnManager::SelfKongOption> getSelfKongOptionsForPlayer(const Hand& hand) {
    std::vector<TurnManager::SelfKongOption> options;
    const auto& concealed = hand.concealed();

    // Concealed kong: 4 identical tiles in concealed hand
    // Track which suit+rank we've already checked
    std::vector<std::pair<Suit,uint8_t>> checked;
    for (const auto& t : concealed) {
        bool alreadyChecked = false;
        for (auto& c : checked) {
            if (c.first == t.suit && c.second == t.rank) { alreadyChecked = true; break; }
        }
        if (alreadyChecked) continue;
        checked.push_back({t.suit, t.rank});

        if (hand.countTile(t.suit, t.rank) >= 4) {
            options.push_back({t.suit, t.rank, false});
        }
    }

    // Promoted kong: have a tile in concealed hand that matches an exposed pung
    for (const auto& meld : hand.melds()) {
        if (meld.type == MeldType::Pung && meld.exposed) {
            if (hand.hasTile(meld.suit(), meld.rank())) {
                options.push_back({meld.suit(), meld.rank(), true});
            }
        }
    }

    return options;
}

void TurnManager::resolveSelfKong(int playerIdx, Suit suit, uint8_t rank) {
    consecutiveKongs_++;
    isKongReplacement_ = true;

    Hand& hand = players_[playerIdx]->hand();
    auto& concealed = hand.concealedMut();

    // Check if it's a promote kong (matching an exposed pung)
    bool promoted = hand.promoteToKong(suit, rank,
        *std::find_if(concealed.begin(), concealed.end(),
            [suit, rank](const Tile& t) { return t.suit == suit && t.rank == rank; }));

    if (promoted) {
        // Remove the one tile from concealed
        auto it = std::find_if(concealed.begin(), concealed.end(),
            [suit, rank](const Tile& t) { return t.suit == suit && t.rank == rank; });
        if (it != concealed.end()) concealed.erase(it);
    } else {
        // Concealed kong: take all 4 from concealed
        Meld meld;
        meld.type = MeldType::ConcealedKong;
        meld.exposed = false;
        for (int i = 0; i < 4; i++) {
            auto it = std::find_if(concealed.begin(), concealed.end(),
                [suit, rank](const Tile& t) { return t.suit == suit && t.rank == rank; });
            if (it != concealed.end()) {
                meld.tiles.push_back(*it);
                concealed.erase(it);
            }
        }
        hand.addMeld(std::move(meld));
    }

    hand.sortTiles();

    // Draw replacement tile
    if (!wall_.isEmpty()) {
        Tile replacement = wall_.drawReplacement();
        if (replacement.isBonus()) {
            hand.addFlower(replacement);
            if (hand.flowers().size() == 8) {
                handleScoring(playerIdx, true, replacement);
                return;
            }
            // Keep drawing replacements for bonus tiles
            while (!wall_.isEmpty()) {
                replacement = wall_.drawReplacement();
                if (replacement.isBonus()) {
                    hand.addFlower(replacement);
                    if (hand.flowers().size() == 8) {
                        handleScoring(playerIdx, true, replacement);
                        return;
                    }
                } else {
                    hand.addTile(replacement);
                    break;
                }
            }
        } else {
            hand.addTile(replacement);
        }
        hand.sortTiles();

        lastDrawnTile_ = replacement;
        hasLastDrawn_ = !replacement.isBonus();
    }

    // Check self-draw win after replacement
    checkSelfDrawWin();
    if (phase_ == GamePhase::SCORING) return;

    // Stay in PLAYER_TURN - player still needs to discard
    waitingForHumanDiscard_ = false;
    enterPhase(GamePhase::PLAYER_TURN);
}

std::vector<TurnManager::SelfKongOption> TurnManager::getHumanSelfKongOptions() const {
    if (phase_ != GamePhase::PLAYER_TURN) return {};
    if (activePlayer_ != 0) return {};
    if (wall_.remaining() <= 0) return {};
    auto options = getSelfKongOptionsForPlayer(players_[0]->hand());
    // Filter out kongs the human has already declined
    options.erase(std::remove_if(options.begin(), options.end(),
        [this](const SelfKongOption& opt) {
            for (const auto& d : declinedSelfKongs_) {
                if (d.first == opt.suit && d.second == opt.rank) return true;
            }
            return false;
        }), options.end());
    return options;
}

void TurnManager::onHumanSelfKong(Suit suit, uint8_t rank) {
    if (phase_ != GamePhase::PLAYER_TURN || activePlayer_ != 0) return;
    waitingForHumanDiscard_ = false;
    for (auto& obs : selfKongObservers_) {
        obs(0, suit, rank, turnCount_, wall_.remaining());
    }
    resolveSelfKong(0, suit, rank);
}

void TurnManager::handlePlayerTurn(float dt) {
    if (waitingForHumanDiscard_) return;

    Player* current = players_[activePlayer_].get();

    if (current->isHuman()) {
        if (!waitingForHumanDiscard_) {
            waitingForHumanDiscard_ = true;
            current->requestDiscard([this](Tile discarded) {
                // Human chose to discard instead of using any available self-kong
                auto kongOpts = getSelfKongOptionsForPlayer(players_[0]->hand());
                for (const auto& opt : kongOpts) {
                    bool already = false;
                    for (const auto& d : declinedSelfKongs_) {
                        if (d.first == opt.suit && d.second == opt.rank) { already = true; break; }
                    }
                    if (!already) declinedSelfKongs_.push_back({opt.suit, opt.rank});
                }
                Player* p = players_[activePlayer_].get();
                p->hand().removeTileById(discarded.id);
                p->hand().sortTiles();
                p->addDiscard(discarded);
                lastDiscardTile_ = discarded;
                lastDiscard_ = true;
                lastDiscardPlayer_ = activePlayer_;
                waitingForHumanDiscard_ = false;
                firstTurn_ = false;
                consecutiveKongs_ = 0;
                isKongReplacement_ = false;
                turnCount_++;
                for (auto& obs : discardObservers_) {
                    obs(activePlayer_, discarded, turnCount_, wall_.remaining());
                }
                enterPhase(GamePhase::CLAIM_PHASE);
            });
        }
    } else {
        phaseTimer_ += dt;
        if (phaseTimer_ < 0.3f) return;

        // Provide game context to AI for smarter decisions
        AIPlayer* ai = dynamic_cast<AIPlayer*>(current);
        if (ai) {
            std::vector<const Player*> allPlayers;
            for (auto& p : players_) allPlayers.push_back(p.get());
            ai->setGameContext(prevailingWind_, allPlayers);
            ai->setTurnInfo(turnCount_, wall_.remaining());
        }

        // AI: check for self-kong before discarding
        if (wall_.remaining() > 0) {
            auto kongOpts = getSelfKongOptionsForPlayer(current->hand());
            if (!kongOpts.empty()) {
                // AI always declares self-kong
                resolveSelfKong(activePlayer_, kongOpts[0].suit, kongOpts[0].rank);
                return;
            }
        }

        current->requestDiscard([this](Tile discarded) {
            Player* p = players_[activePlayer_].get();
            p->hand().removeTileById(discarded.id);
            p->hand().sortTiles();
            p->addDiscard(discarded);
            lastDiscardTile_ = discarded;
            lastDiscard_ = true;
            lastDiscardPlayer_ = activePlayer_;
            firstTurn_ = false;
            consecutiveKongs_ = 0;
            isKongReplacement_ = false;
            turnCount_++;
            for (auto& obs : discardObservers_) {
                obs(activePlayer_, discarded, turnCount_, wall_.remaining());
            }
            enterPhase(GamePhase::CLAIM_PHASE);
        });
    }
}

// Check what claims a specific player can make on a discard
static std::vector<ClaimType> getClaimOptionsForPlayer(
    int playerIdx, int discardPlayerIdx,
    const Hand& hand, Tile disc,
    bool canWin)
{
    std::vector<ClaimType> options;
    if (playerIdx == discardPlayerIdx) return options;

    // Pung: anyone can claim (need 2 copies in concealed hand)
    if (hand.countTile(disc.suit, disc.rank) >= 2) {
        options.push_back(ClaimType::Pung);
    }

    // Kong: anyone can claim (need 3 copies in concealed hand)
    if (hand.countTile(disc.suit, disc.rank) >= 3) {
        options.push_back(ClaimType::Kong);
    }

    // Chow: only from the player to your left (previous in turn order)
    int leftPlayer = (playerIdx + 3) % 4; // the player who plays before you
    if (discardPlayerIdx == leftPlayer && isNumberedSuit(disc.suit)) {
        uint8_t r = disc.rank;
        bool canChow = false;
        if (r >= 3 && hand.hasTile(disc.suit, r-2) && hand.hasTile(disc.suit, r-1))
            canChow = true;
        if (!canChow && r >= 2 && r <= 8 && hand.hasTile(disc.suit, r-1) && hand.hasTile(disc.suit, r+1))
            canChow = true;
        if (!canChow && r <= 7 && hand.hasTile(disc.suit, r+1) && hand.hasTile(disc.suit, r+2))
            canChow = true;
        if (canChow) options.push_back(ClaimType::Chow);
    }

    // Win
    if (canWin) {
        options.push_back(ClaimType::Win);
    }

    return options;
}

void TurnManager::handleClaimPhase(float dt) {
    claimTimer_ += dt;

    // On first entry, determine the best AI claim (with priority resolution)
    // Priority: Win > Kong/Pung > Chow; ties broken by turn-order proximity to discarder
    if (!humanClaimPending_ && !humanClaimResolved_ && claimTimer_ < 0.05f) {
        int bestAI = -1;
        ClaimType bestType = ClaimType::None;
        int bestPriority = -1;

        for (int i = 1; i <= 3; i++) {
            int pIdx = (lastDiscardPlayer_ + i) % 4;
            if (players_[pIdx]->isHuman()) continue;

            const Hand& hand = players_[pIdx]->hand();
            bool canWin = canWinWith(pIdx, lastDiscardTile_);
            auto opts = getClaimOptionsForPlayer(pIdx, lastDiscardPlayer_, hand, lastDiscardTile_, canWin);

            // Provide game context and ask AI what they want to do
            if (!opts.empty()) {
                AIPlayer* aiP = dynamic_cast<AIPlayer*>(players_[pIdx].get());
                if (aiP) {
                    std::vector<const Player*> allPlayers;
                    for (auto& p : players_) allPlayers.push_back(p.get());
                    aiP->setGameContext(prevailingWind_, allPlayers);
                    aiP->setTurnInfo(turnCount_, wall_.remaining());
                    aiP->setLastDiscarder(lastDiscardPlayer_);
                }
                // Build ClaimOption list for requestClaimDecision
                std::vector<ClaimOption> claimOpts;
                for (auto ct : opts) {
                    claimOpts.push_back({ct});
                }

                ClaimType aiChoice = ClaimType::None;
                players_[pIdx]->requestClaimDecision(lastDiscardTile_, claimOpts,
                    [&aiChoice](ClaimType chosen) { aiChoice = chosen; });

                if (aiChoice != ClaimType::None) {
                    int priority = 0;
                    if (aiChoice == ClaimType::Win) priority = 3;
                    else if (aiChoice == ClaimType::Kong) priority = 2;
                    else if (aiChoice == ClaimType::Pung) priority = 2;
                    else if (aiChoice == ClaimType::Chow) priority = 1;

                    // Higher priority wins; equal priority: closer to discarder wins (lower i)
                    if (priority > bestPriority) {
                        bestPriority = priority;
                        bestType = aiChoice;
                        bestAI = pIdx;
                    }
                }
            }
        }

        aiClaimPlayer_ = bestAI;
        aiClaimType_ = bestType;
    }

    // Check if human has claim options (human always gets a chance to respond)
    bool humanHasOptions = false;
    std::vector<ClaimType> humanOptions;
    if (!players_[0]->isHuman() || lastDiscardPlayer_ == 0) {
        // no human claim needed
    } else {
        humanOptions = getHumanClaimOptions();
        humanHasOptions = !humanOptions.empty();
    }

    // If human has options, wait for human decision first
    if (humanHasOptions && !humanClaimPending_ && !humanClaimResolved_) {
        humanClaimPending_ = true;
        humanClaimResolved_ = false;
        return;
    }

    // Timeout: auto-pass for human after time limit
    if (humanClaimPending_ && !humanClaimResolved_ && claimTimer_ < CLAIM_TIME_LIMIT) {
        return; // still waiting for human
    }
    if (humanClaimPending_ && !humanClaimResolved_ && claimTimer_ >= CLAIM_TIME_LIMIT) {
        humanClaimChoice_ = ClaimOption{};
        humanClaimResolved_ = true;
    }

    if (humanClaimResolved_) {
        // Resolve priority between human choice and best AI claim
        ClaimType humanType = humanClaimChoice_.type;
        int humanPriority = 0;
        if (humanType == ClaimType::Win) humanPriority = 3;
        else if (humanType == ClaimType::Kong) humanPriority = 2;
        else if (humanType == ClaimType::Pung) humanPriority = 2;
        else if (humanType == ClaimType::Chow) humanPriority = 1;

        int aiPriority = 0;
        if (aiClaimType_ == ClaimType::Win) aiPriority = 3;
        else if (aiClaimType_ == ClaimType::Kong) aiPriority = 2;
        else if (aiClaimType_ == ClaimType::Pung) aiPriority = 2;
        else if (aiClaimType_ == ClaimType::Chow) aiPriority = 1;

        // Determine winner: higher priority wins; on tie, closer to discarder wins
        int resolvedPlayer = -1;
        ClaimType resolvedType = ClaimType::None;
        std::vector<Tile> resolvedMeldTiles;

        if (humanPriority > aiPriority) {
            resolvedPlayer = 0;
            resolvedType = humanType;
            resolvedMeldTiles = humanClaimChoice_.meldTiles;
        } else if (aiPriority > humanPriority) {
            resolvedPlayer = aiClaimPlayer_;
            resolvedType = aiClaimType_;
        } else if (humanPriority > 0 && aiPriority > 0) {
            // Equal priority: compare turn-order distance from discarder
            int humanDist = (0 - lastDiscardPlayer_ + 4) % 4;
            int aiDist = (aiClaimPlayer_ - lastDiscardPlayer_ + 4) % 4;
            if (humanDist <= aiDist) {
                resolvedPlayer = 0;
                resolvedType = humanType;
                resolvedMeldTiles = humanClaimChoice_.meldTiles;
            } else {
                resolvedPlayer = aiClaimPlayer_;
                resolvedType = aiClaimType_;
            }
        }

        if (resolvedType == ClaimType::None) {
            // Nobody claims - advance to next player after discarder
            activePlayer_ = lastDiscardPlayer_;
            advanceToNextPlayer();
            enterPhase(GamePhase::PLAYER_DRAW);
        } else if (resolvedType == ClaimType::Win) {
            handleScoring(resolvedPlayer, false, lastDiscardTile_);
        } else {
            resolveClaim(resolvedType, resolvedPlayer, resolvedMeldTiles);
        }

        humanClaimPending_ = false;
        humanClaimResolved_ = false;
        aiClaimPlayer_ = -1;
        aiClaimType_ = ClaimType::None;
        return;
    }

    // No human options - just resolve AI claim after short delay
    if (!humanHasOptions && claimTimer_ > 0.3f) {
        if (aiClaimType_ == ClaimType::Win) {
            handleScoring(aiClaimPlayer_, false, lastDiscardTile_);
        } else if (aiClaimType_ != ClaimType::None && aiClaimPlayer_ >= 0) {
            resolveClaim(aiClaimType_, aiClaimPlayer_);
        } else {
            activePlayer_ = lastDiscardPlayer_;
            advanceToNextPlayer();
            enterPhase(GamePhase::PLAYER_DRAW);
        }
        aiClaimPlayer_ = -1;
        aiClaimType_ = ClaimType::None;
    }
}

void TurnManager::advanceToNextPlayer() {
    activePlayer_ = (activePlayer_ + 1) % 4;
}

std::vector<ClaimType> TurnManager::getHumanClaimOptions() const {
    if (!lastDiscard_) return {};
    if (lastDiscardPlayer_ == 0) return {};

    const Hand& hand = players_[0]->hand();
    bool canWin = canWinWith(0, lastDiscardTile_);
    return getClaimOptionsForPlayer(0, lastDiscardPlayer_, hand, lastDiscardTile_, canWin);
}

std::vector<ClaimOption> TurnManager::getHumanClaimOptionsWithCombos() const {
    if (!lastDiscard_) return {};
    if (lastDiscardPlayer_ == 0) return {};

    const Hand& hand = players_[0]->hand();
    Tile disc = lastDiscardTile_;
    std::vector<ClaimOption> result;

    // Win
    if (canWinWith(0, disc)) {
        ClaimOption opt;
        opt.type = ClaimType::Win;
        opt.playerIndex = 0;
        result.push_back(opt);
    }

    // Kong
    if (hand.countTile(disc.suit, disc.rank) >= 3) {
        ClaimOption opt;
        opt.type = ClaimType::Kong;
        opt.playerIndex = 0;
        result.push_back(opt);
    }

    // Pung
    if (hand.countTile(disc.suit, disc.rank) >= 2) {
        ClaimOption opt;
        opt.type = ClaimType::Pung;
        opt.playerIndex = 0;
        result.push_back(opt);
    }

    // Chow: only from the player to your left
    int leftPlayer = (0 + 3) % 4;
    if (lastDiscardPlayer_ == leftPlayer && isNumberedSuit(disc.suit)) {
        uint8_t r = disc.rank;
        // r-2, r-1, r
        if (r >= 3 && hand.hasTile(disc.suit, r-2) && hand.hasTile(disc.suit, r-1)) {
            ClaimOption opt;
            opt.type = ClaimType::Chow;
            opt.playerIndex = 0;
            // meldTiles = the 2 hand tiles used (not including the discard)
            opt.meldTiles = {Tile{disc.suit, static_cast<uint8_t>(r-2), 0},
                             Tile{disc.suit, static_cast<uint8_t>(r-1), 0}};
            result.push_back(opt);
        }
        // r-1, r, r+1
        if (r >= 2 && r <= 8 && hand.hasTile(disc.suit, r-1) && hand.hasTile(disc.suit, r+1)) {
            ClaimOption opt;
            opt.type = ClaimType::Chow;
            opt.playerIndex = 0;
            opt.meldTiles = {Tile{disc.suit, static_cast<uint8_t>(r-1), 0},
                             Tile{disc.suit, static_cast<uint8_t>(r+1), 0}};
            result.push_back(opt);
        }
        // r, r+1, r+2
        if (r <= 7 && hand.hasTile(disc.suit, r+1) && hand.hasTile(disc.suit, r+2)) {
            ClaimOption opt;
            opt.type = ClaimType::Chow;
            opt.playerIndex = 0;
            opt.meldTiles = {Tile{disc.suit, static_cast<uint8_t>(r+1), 0},
                             Tile{disc.suit, static_cast<uint8_t>(r+2), 0}};
            result.push_back(opt);
        }
    }

    return result;
}

void TurnManager::onHumanClaim(const ClaimOption& option) {
    humanClaimChoice_ = option;
    humanClaimResolved_ = true;
}

void TurnManager::resolveClaim(ClaimType type, int claimingPlayer, const std::vector<Tile>& meldTiles) {
    Tile disc = lastDiscardTile_;
    Hand& hand = players_[claimingPlayer]->hand();

    // Auto-detect win: if the discard completes a winning hand with enough faan,
    // declare win regardless of which claim type was chosen (Pung/Chow/Kong)
    if (canWinWith(claimingPlayer, disc)) {
        players_[lastDiscardPlayer_]->removeLastDiscard();
        handleScoring(claimingPlayer, false, disc);
        return;
    }

    // Remove the claimed tile from the discarder's discard pile
    players_[lastDiscardPlayer_]->removeLastDiscard();

    if (type == ClaimType::Pung) {
        Meld meld;
        meld.type = MeldType::Pung;
        meld.exposed = true;
        meld.tiles.push_back(disc);
        for (int i = 0; i < 2; i++) {
            auto& concealed = hand.concealedMut();
            auto it = std::find_if(concealed.begin(), concealed.end(),
                [&disc](const Tile& t) { return t.sameAs(disc); });
            if (it != concealed.end()) {
                meld.tiles.push_back(*it);
                concealed.erase(it);
            }
        }
        hand.addMeld(std::move(meld));
        activePlayer_ = claimingPlayer;
        enterPhase(GamePhase::PLAYER_TURN);
    } else if (type == ClaimType::Kong) {
        Meld meld;
        meld.type = MeldType::Kong;
        meld.exposed = true;
        meld.tiles.push_back(disc);
        for (int i = 0; i < 3; i++) {
            auto& concealed = hand.concealedMut();
            auto it = std::find_if(concealed.begin(), concealed.end(),
                [&disc](const Tile& t) { return t.sameAs(disc); });
            if (it != concealed.end()) {
                meld.tiles.push_back(*it);
                concealed.erase(it);
            }
        }
        hand.addMeld(std::move(meld));
        activePlayer_ = claimingPlayer;
        isKongReplacement_ = true;
        consecutiveKongs_ = 1;
        if (!wall_.isEmpty()) {
            Tile replacement = wall_.drawReplacement();
            hand.addTile(replacement);
            hand.sortTiles();
        }
        enterPhase(GamePhase::PLAYER_TURN);
    } else if (type == ClaimType::Chow) {
        uint8_t r = disc.rank;
        Meld meld;
        meld.type = MeldType::Chow;
        meld.exposed = true;
        meld.tiles.push_back(disc);

        if (!meldTiles.empty()) {
            // Use the specific combo chosen by the player
            auto& concealed = hand.concealedMut();
            for (const auto& mt : meldTiles) {
                auto it = std::find_if(concealed.begin(), concealed.end(),
                    [&](const Tile& t) { return t.suit == mt.suit && t.rank == mt.rank; });
                if (it != concealed.end()) {
                    meld.tiles.push_back(*it);
                    concealed.erase(it);
                }
            }
        } else {
            // Auto-pick first valid sequence (AI path)
            auto trySequence = [&](uint8_t r1, uint8_t r2) -> bool {
                if (r1 < 1 || r2 > 9) return false;
                if (!hand.hasTile(disc.suit, r1) || !hand.hasTile(disc.suit, r2)) return false;

                auto& concealed = hand.concealedMut();
                for (uint8_t needed : {r1, r2}) {
                    auto it = std::find_if(concealed.begin(), concealed.end(),
                        [&](const Tile& t) { return t.suit == disc.suit && t.rank == needed; });
                    if (it != concealed.end()) {
                        meld.tiles.push_back(*it);
                        concealed.erase(it);
                    }
                }
                return true;
            };

            bool found = false;
            if (r >= 3) found = trySequence(r-2, r-1);
            if (!found && r >= 2 && r <= 8) found = trySequence(r-1, r+1);
            if (!found && r <= 7) found = trySequence(r+1, r+2);
        }

        std::sort(meld.tiles.begin(), meld.tiles.end(),
            [](const Tile& a, const Tile& b) { return a.rank < b.rank; });
        hand.addMeld(std::move(meld));

        activePlayer_ = claimingPlayer;
        enterPhase(GamePhase::PLAYER_TURN);
    }

    for (auto& obs : claimObservers_) {
        obs(claimingPlayer, type, disc, turnCount_, wall_.remaining());
    }

    lastDiscard_ = false;
}

bool TurnManager::canWinWith(int playerIndex, Tile tile) const {
    const Hand& hand = players_[playerIndex]->hand();

    auto wins = WinDetector::findWins(hand, tile);
    if (wins.empty()) return false;

    // Check minimum faan
    WinContext ctx;
    ctx.selfDrawn = false;
    ctx.seatWind = players_[playerIndex]->seatWind();
    ctx.prevailingWind = prevailingWind_;
    ctx.isDealer = (playerIndex == dealerIndex_);
    ctx.turnCount = turnCount_;

    FaanResult result = Scoring::calculate(hand, tile, ctx);
    return result.totalFaan >= Scoring::MIN_FAAN;
}

void TurnManager::update(float dt) {
    if (roundOver_) return;

    switch (phase_) {
        case GamePhase::DEALING:
            deal();
            break;

        case GamePhase::REPLACING_FLOWERS:
            replaceFlowers();
            break;

        case GamePhase::PLAYER_DRAW:
            playerDraw();
            break;

        case GamePhase::PLAYER_TURN:
            handlePlayerTurn(dt);
            break;

        case GamePhase::CLAIM_PHASE:
            handleClaimPhase(dt);
            break;

        case GamePhase::REPLACEMENT_DRAW:
            if (!wall_.isEmpty()) {
                Tile t = wall_.drawReplacement();
                if (t.isBonus()) {
                    players_[activePlayer_]->hand().addFlower(t);
                    // Check All Eight Flowers
                    if (players_[activePlayer_]->hand().flowers().size() == 8) {
                        handleScoring(activePlayer_, true, t);
                        break;
                    }
                } else {
                    players_[activePlayer_]->hand().addTile(t);
                    players_[activePlayer_]->hand().sortTiles();
                    enterPhase(GamePhase::PLAYER_TURN);
                }
            } else {
                roundOver_ = true;
                scoringText_ = "Wall exhausted - Draw!\n\nPress SPACE for next round";
                for (auto& obs : roundEndObservers_) {
                    FaanResult empty;
                    obs(-1, false, empty, true);
                }
                enterPhase(GamePhase::ROUND_END);
            }
            break;

        case GamePhase::SCORING:
        case GamePhase::ROUND_END:
        case GamePhase::GAME_OVER:
            break;

        default:
            break;
    }
}
