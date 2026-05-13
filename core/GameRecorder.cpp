#include "GameRecorder.h"
#include "GameController.h"
#include "JsonHelpers.h"
#include <ctime>
#include <sstream>

void GameRecorder::attach(GameController& controller) {
    controller_ = &controller;

    controller.addDiscardObserver(
        [this](int playerIndex, Tile discarded, int turnCount, int wallRemaining) {
            onDiscard(playerIndex, discarded, turnCount, wallRemaining);
        });

    controller.addClaimObserver(
        [this](int playerIndex, ClaimType type, Tile claimedTile, int turnCount, int wallRemaining) {
            onClaim(playerIndex, type, claimedTile, turnCount, wallRemaining);
        });

    controller.addSelfKongObserver(
        [this](int playerIndex, Suit suit, uint8_t rank, int turnCount, int wallRemaining) {
            onSelfKong(playerIndex, suit, rank, turnCount, wallRemaining);
        });

    controller.addSelfDrawWinObserver(
        [this](int playerIndex, Tile winningTile, int turnCount, int wallRemaining) {
            onSelfDrawWin(playerIndex, winningTile, turnCount, wallRemaining);
        });

    controller.addRoundEndObserver(
        [this](int winnerIndex, bool selfDrawn, const FaanResult& result, bool isDraw) {
            onRoundEnd(winnerIndex, selfDrawn, result, isDraw);
        });
}

void GameRecorder::beginRound(const GameController& controller) {
    currentRound_ = RoundRecord{};
    currentRound_.roundNumber = controller.roundNumber();
    currentRound_.prevailingWind = controller.prevailingWind();
    currentRound_.dealerIndex = controller.dealerIndex();
}

void GameRecorder::setClientId(const std::string& id) {
    clientId_ = id;
}

void GameRecorder::onDiscard(int playerIndex, Tile discarded, int turnCount, int wallRemaining) {
    if (playerIndex != 0) return;  // Human player only

    Decision d;
    d.type = "discard";
    d.turnCount = turnCount;
    d.wallRemaining = wallRemaining;
    d.discardedTile = discarded;
    d.snapshot = controller_->snapshot();
    currentRound_.decisions.push_back(std::move(d));
}

void GameRecorder::onClaim(int playerIndex, ClaimType type, Tile claimedTile, int turnCount, int wallRemaining) {
    if (playerIndex != 0) return;

    Decision d;
    d.type = "claim";
    d.turnCount = turnCount;
    d.wallRemaining = wallRemaining;
    d.claimType = type;
    d.claimedTile = claimedTile;
    d.snapshot = controller_->snapshot();
    currentRound_.decisions.push_back(std::move(d));
}

void GameRecorder::onSelfKong(int playerIndex, Suit suit, uint8_t rank, int turnCount, int wallRemaining) {
    if (playerIndex != 0) return;

    Decision d;
    d.type = "selfKong";
    d.turnCount = turnCount;
    d.wallRemaining = wallRemaining;
    d.kongSuit = suit;
    d.kongRank = rank;
    d.snapshot = controller_->snapshot();  // Pre-action snapshot
    currentRound_.decisions.push_back(std::move(d));
}

void GameRecorder::onSelfDrawWin(int playerIndex, Tile winningTile, int turnCount, int wallRemaining) {
    if (playerIndex != 0) return;

    Decision d;
    d.type = "selfDrawWin";
    d.turnCount = turnCount;
    d.wallRemaining = wallRemaining;
    d.winningTile = winningTile;
    d.snapshot = controller_->snapshot();  // Pre-action snapshot
    currentRound_.decisions.push_back(std::move(d));
}

void GameRecorder::onRoundEnd(int winnerIndex, bool selfDrawn, const FaanResult& result, bool isDraw) {
    currentRound_.result.winnerIndex = winnerIndex;
    currentRound_.result.selfDrawn = selfDrawn;
    currentRound_.result.isDraw = isDraw;
    currentRound_.result.faanResult = result;

    // Capture final scores
    if (controller_) {
        for (int i = 0; i < 4; i++) {
            const Player* p = controller_->getPlayer(i);
            if (p) currentRound_.result.finalScores[i] = p->score();
        }
    }

    // Only store rounds with human decisions
    if (!currentRound_.decisions.empty()) {
        rounds_.push_back(std::move(currentRound_));
    }
    currentRound_ = RoundRecord{};
}

bool GameRecorder::hasData() const {
    return !rounds_.empty();
}

int GameRecorder::decisionCount() const {
    int count = 0;
    for (const auto& r : rounds_) {
        count += static_cast<int>(r.decisions.size());
    }
    return count;
}

void GameRecorder::clear() {
    rounds_.clear();
}

// Helper to serialize a GameSnapshot to JSON
static void appendSnapshotJson(std::ostringstream& os, const GameSnapshot& snap) {
    os << "{\"phase\":" << static_cast<int>(snap.phase)
       << ",\"activePlayerIndex\":" << snap.activePlayerIndex
       << ",\"prevailingWind\":" << static_cast<int>(snap.prevailingWind)
       << ",\"wallRemaining\":" << snap.wallRemaining
       << ",\"canSelfKong\":" << (snap.canSelfKong ? "true" : "false")
       << ",\"canSelfDrawWin\":" << (snap.canSelfDrawWin ? "true" : "false")
       << ",\"hasLastDiscard\":" << (snap.hasLastDiscard ? "true" : "false");

    if (snap.hasLastDiscard) {
        os << ",\"lastDiscard\":";
        json::appendTile(os, snap.lastDiscard);
    }

    os << ",\"players\":[";
    for (int i = 0; i < 4; i++) {
        if (i > 0) os << ",";
        const auto& p = snap.players[i];
        os << "{\"name\":\"" << json::escape(p.name) << "\""
           << ",\"seatWind\":" << static_cast<int>(p.seatWind)
           << ",\"score\":" << p.score
           << ",\"isHuman\":" << (p.isHuman ? "true" : "false")
           << ",\"concealedCount\":" << p.concealedCount;

        os << ",\"concealed\":";
        json::appendTileArray(os, p.concealed);
        os << ",\"melds\":";
        json::appendMeldArray(os, p.melds);
        os << ",\"flowers\":";
        json::appendTileArray(os, p.flowers);
        os << ",\"discards\":";
        json::appendTileArray(os, p.discards);
        os << "}";
    }
    os << "]}";
}

std::string GameRecorder::toJson() const {
    std::ostringstream os;
    os << "{\"version\":1";
    os << ",\"timestamp\":" << static_cast<long>(std::time(nullptr));
    os << ",\"clientId\":\"" << json::escape(clientId_) << "\"";
    os << ",\"rounds\":[";

    for (size_t ri = 0; ri < rounds_.size(); ri++) {
        if (ri > 0) os << ",";
        const auto& round = rounds_[ri];

        os << "{\"roundNumber\":" << round.roundNumber
           << ",\"prevailingWind\":" << static_cast<int>(round.prevailingWind)
           << ",\"dealerIndex\":" << round.dealerIndex;

        os << ",\"decisions\":[";
        for (size_t di = 0; di < round.decisions.size(); di++) {
            if (di > 0) os << ",";
            const auto& dec = round.decisions[di];

            os << "{\"type\":\"" << dec.type << "\""
               << ",\"turnCount\":" << dec.turnCount
               << ",\"wallRemaining\":" << dec.wallRemaining;

            if (dec.type == "discard") {
                os << ",\"discardedTile\":";
                json::appendTile(os, dec.discardedTile);
            } else if (dec.type == "claim") {
                os << ",\"claimType\":\"" << json::claimTypeToString(dec.claimType) << "\"";
                os << ",\"claimedTile\":";
                json::appendTile(os, dec.claimedTile);
            } else if (dec.type == "selfKong") {
                os << ",\"kongSuit\":" << static_cast<int>(dec.kongSuit);
                os << ",\"kongRank\":" << static_cast<int>(dec.kongRank);
            } else if (dec.type == "selfDrawWin") {
                os << ",\"winningTile\":";
                json::appendTile(os, dec.winningTile);
            }

            os << ",\"snapshot\":";
            appendSnapshotJson(os, dec.snapshot);
            os << "}";
        }
        os << "]";

        // Round result
        const auto& res = round.result;
        os << ",\"result\":{\"winnerIndex\":" << res.winnerIndex
           << ",\"selfDrawn\":" << (res.selfDrawn ? "true" : "false")
           << ",\"isDraw\":" << (res.isDraw ? "true" : "false");

        if (!res.isDraw && res.winnerIndex >= 0) {
            os << ",\"scoring\":";
            json::appendFaanResult(os, res.faanResult);
        }

        os << ",\"finalScores\":[";
        for (int i = 0; i < 4; i++) {
            if (i > 0) os << ",";
            os << res.finalScores[i];
        }
        os << "]}";

        os << "}";
    }
    os << "]}";

    return os.str();
}
