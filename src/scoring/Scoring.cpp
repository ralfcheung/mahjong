#include "Scoring.h"
#include <algorithm>

static bool allMeldsArePungOrKong(const WinDecomposition& d) {
    for (const auto& m : d.melds) {
        if (m.isChow()) return false;
    }
    return true;
}

static bool allMeldsAreChow(const WinDecomposition& d) {
    for (const auto& m : d.melds) {
        if (!m.isChow()) return false;
    }
    return true;
}

static bool isSingleSuit(const Hand& hand, const WinDecomposition& d, bool allowHonors) {
    Suit mainSuit = Suit::Bamboo;
    bool foundSuit = false;

    auto checkTile = [&](const Tile& t) -> bool {
        if (isHonorSuit(t.suit)) return allowHonors;
        if (!foundSuit) { mainSuit = t.suit; foundSuit = true; return true; }
        return t.suit == mainSuit;
    };

    for (const auto& m : d.melds) {
        for (const auto& t : m.tiles) {
            if (!checkTile(t)) return false;
        }
    }
    if (d.eyes.tiles.size() >= 2) {
        for (const auto& t : d.eyes.tiles) {
            if (!checkTile(t)) return false;
        }
    }
    return foundSuit; // Must have at least one numbered suit
}

static bool hasHonors(const WinDecomposition& d) {
    for (const auto& m : d.melds) {
        for (const auto& t : m.tiles) {
            if (isHonorSuit(t.suit)) return true;
        }
    }
    for (const auto& t : d.eyes.tiles) {
        if (isHonorSuit(t.suit)) return true;
    }
    return false;
}

static bool allHonors(const WinDecomposition& d) {
    for (const auto& m : d.melds) {
        for (const auto& t : m.tiles) {
            if (!isHonorSuit(t.suit)) return false;
        }
    }
    for (const auto& t : d.eyes.tiles) {
        if (!isHonorSuit(t.suit)) return false;
    }
    return true;
}

static bool allTerminals(const WinDecomposition& d) {
    for (const auto& m : d.melds) {
        for (const auto& t : m.tiles) {
            if (!t.isTerminal()) return false;
        }
    }
    for (const auto& t : d.eyes.tiles) {
        if (!t.isTerminal()) return false;
    }
    return true;
}

static int countDragonPungs(const WinDecomposition& d) {
    int count = 0;
    for (const auto& m : d.melds) {
        if (m.isPungOrKong() && m.suit() == Suit::Dragon) count++;
    }
    return count;
}

static int countWindPungs(const WinDecomposition& d, Wind wind) {
    int target = static_cast<int>(wind) + 1; // Wind enum 0-3, rank 1-4
    for (const auto& m : d.melds) {
        if (m.isPungOrKong() && m.suit() == Suit::Wind && m.rank() == target) return 1;
    }
    return 0;
}

static int countAllWindPungs(const WinDecomposition& d) {
    int count = 0;
    for (const auto& m : d.melds) {
        if (m.isPungOrKong() && m.suit() == Suit::Wind) count++;
    }
    return count;
}

static int countKongs(const WinDecomposition& d) {
    int count = 0;
    for (const auto& m : d.melds) {
        if (m.isKong()) count++;
    }
    return count;
}

static bool allTerminalsOrHonors(const WinDecomposition& d) {
    for (const auto& m : d.melds) {
        for (const auto& t : m.tiles) {
            if (!t.isTerminalOrHonor()) return false;
        }
    }
    for (const auto& t : d.eyes.tiles) {
        if (!t.isTerminalOrHonor()) return false;
    }
    return true;
}

static bool hasNumberedSuit(const WinDecomposition& d) {
    for (const auto& m : d.melds) {
        for (const auto& t : m.tiles) {
            if (isNumberedSuit(t.suit)) return true;
        }
    }
    for (const auto& t : d.eyes.tiles) {
        if (isNumberedSuit(t.suit)) return true;
    }
    return false;
}

// Check if all tiles match an allowed set
static bool allTilesMatch(const WinDecomposition& d,
    bool (*pred)(Suit suit, uint8_t rank)) {
    for (const auto& m : d.melds) {
        for (const auto& t : m.tiles) {
            if (!pred(t.suit, t.rank)) return false;
        }
    }
    for (const auto& t : d.eyes.tiles) {
        if (!pred(t.suit, t.rank)) return false;
    }
    return true;
}

// 綠一色 Green Hand: Bamboo 2,3,4,6,8 + Dragon 2 (發財)
static bool isGreenTile(Suit suit, uint8_t rank) {
    if (suit == Suit::Bamboo) {
        return rank == 2 || rank == 3 || rank == 4 || rank == 6 || rank == 8;
    }
    return suit == Suit::Dragon && rank == 2;
}

// 紅孔雀 Red Peacock: Bamboo 1,5,7,9 + Dragon 1 (紅中)
static bool isRedPeacockTile(Suit suit, uint8_t rank) {
    if (suit == Suit::Bamboo) {
        return rank == 1 || rank == 5 || rank == 7 || rank == 9;
    }
    return suit == Suit::Dragon && rank == 1;
}

// 藍一色 Blue Hand: Wind 1-4, Dragon 3 (白板), Dots 8
static bool isBlueTile(Suit suit, uint8_t rank) {
    if (suit == Suit::Wind) return true;
    if (suit == Suit::Dragon && rank == 3) return true;
    if (suit == Suit::Dots && rank == 8) return true;
    return false;
}

// 九蓮寶燈 Nine Gates: concealed hand, single numbered suit, 1112345678999 + any dup
static bool isNineGates(const Hand& hand, const WinDecomposition& d) {
    if (!hand.melds().empty()) return false;
    // Must be single numbered suit, no honors
    Suit mainSuit = Suit::Bamboo;
    bool found = false;
    for (const auto& m : d.melds) {
        for (const auto& t : m.tiles) {
            if (!isNumberedSuit(t.suit)) return false;
            if (!found) { mainSuit = t.suit; found = true; }
            else if (t.suit != mainSuit) return false;
        }
    }
    for (const auto& t : d.eyes.tiles) {
        if (!isNumberedSuit(t.suit)) return false;
        if (!found) { mainSuit = t.suit; found = true; }
        else if (t.suit != mainSuit) return false;
    }
    if (!found) return false;

    // Count tiles by rank
    int counts[10] = {};
    for (const auto& m : d.melds) {
        for (const auto& t : m.tiles) counts[t.rank]++;
    }
    for (const auto& t : d.eyes.tiles) counts[t.rank]++;

    // Need: 1≥3, 2-8≥1, 9≥3, total=14
    int total = 0;
    for (int r = 1; r <= 9; r++) total += counts[r];
    if (total != 14) return false;
    if (counts[1] < 3 || counts[9] < 3) return false;
    for (int r = 2; r <= 8; r++) {
        if (counts[r] < 1) return false;
    }
    return true;
}

FaanResult Scoring::calculateForDecomposition(
    const WinDecomposition& decomp,
    const Hand& hand,
    Tile /*winningTile*/,
    const WinContext& context) {

    FaanResult result;

    // === Limit hands (13 faan) ===

    // Thirteen Orphans
    if (decomp.specialType == WinDecomposition::SpecialType::ThirteenOrphans) {
        result.totalFaan = LIMIT_FAAN;
        result.isLimit = true;
        result.breakdown.push_back({"十三么", "Thirteen Orphans", LIMIT_FAAN});
        return result;
    }

    // Heavenly Hand (dealer wins on dealt hand)
    if (context.heavenlyHand) {
        result.totalFaan = LIMIT_FAAN;
        result.isLimit = true;
        result.breakdown.push_back({"天糊", "Heavenly Hand", LIMIT_FAAN});
        return result;
    }

    // Earthly Hand (non-dealer wins on dealer's first discard)
    if (context.earthlyHand) {
        result.totalFaan = LIMIT_FAAN;
        result.isLimit = true;
        result.breakdown.push_back({"地糊", "Earthly Hand", LIMIT_FAAN});
        return result;
    }

    // All Honors (10 faan 例牌)
    if (allHonors(decomp)) {
        result.totalFaan = 10;
        result.isLimit = true;
        result.breakdown.push_back({"字一色", "All Honors", 10});
        return result;
    }

    // All Terminals (10 faan 例牌)
    if (allTerminals(decomp)) {
        result.totalFaan = 10;
        result.isLimit = true;
        result.breakdown.push_back({"清么九", "All Terminals", 10});
        return result;
    }

    // Four Kongs
    if (countKongs(decomp) == 4) {
        result.totalFaan = LIMIT_FAAN;
        result.isLimit = true;
        result.breakdown.push_back({"十八羅漢", "All Kongs", LIMIT_FAAN});
        return result;
    }

    // Nine Gates (10 faan 例牌)
    if (isNineGates(hand, decomp)) {
        result.totalFaan = 10;
        result.isLimit = true;
        result.breakdown.push_back({"九蓮寶燈", "Nine Gates", 10});
        return result;
    }

    // Green Hand (13 faan 例牌)
    if (allTilesMatch(decomp, isGreenTile)) {
        result.totalFaan = LIMIT_FAAN;
        result.isLimit = true;
        result.breakdown.push_back({"綠一色", "Green Hand", LIMIT_FAAN});
        return result;
    }

    // Red Peacock (13 faan 例牌)
    if (allTilesMatch(decomp, isRedPeacockTile)) {
        result.totalFaan = LIMIT_FAAN;
        result.isLimit = true;
        result.breakdown.push_back({"紅孔雀", "Red Peacock", LIMIT_FAAN});
        return result;
    }

    // Blue Hand (13 faan 例牌)
    if (allTilesMatch(decomp, isBlueTile)) {
        result.totalFaan = LIMIT_FAAN;
        result.isLimit = true;
        result.breakdown.push_back({"藍一色", "Blue Hand", LIMIT_FAAN});
        return result;
    }

    // Human Hand (13 faan 例牌 - non-dealer first-turn self-draw)
    if (context.humanHand) {
        result.totalFaan = LIMIT_FAAN;
        result.isLimit = true;
        result.breakdown.push_back({"人糊", "Human Hand", LIMIT_FAAN});
        return result;
    }

    // All Eight Flowers (8 faan 例牌)
    if (context.allEightFlowers) {
        result.totalFaan = 8;
        result.isLimit = true;
        result.breakdown.push_back({"大花糊", "All Eight Flowers", 8});
        return result;
    }

    // === Accumulating faan ===

    // Dragon pungs (1 faan each)
    int dragonPungs = 0;
    for (const auto& m : decomp.melds) {
        if (m.isPungOrKong() && m.suit() == Suit::Dragon) {
            dragonPungs++;
            const char* cnName = "番牌";
            const char* enName = "Dragon Pung";
            switch (m.rank()) {
                case 1: cnName = "番牌 - 紅中"; enName = "Dragon Pung (Red)"; break;
                case 2: cnName = "番牌 - 發財"; enName = "Dragon Pung (Green)"; break;
                case 3: cnName = "番牌 - 白板"; enName = "Dragon Pung (Whiteboard)"; break;
            }
            result.breakdown.push_back({cnName, enName, 1});
        }
    }

    // Great Three Dragons (all 3 dragon pungs) - 8 faan (replaces individual dragon faan)
    if (dragonPungs == 3) {
        result.breakdown.clear();
        result.breakdown.push_back({"大三元", "Great Three Dragons", 8});
    }

    // Little Three Dragons (2 dragon pungs + pair of 3rd dragon) - 3 faan + the 2 dragon pungs
    if (dragonPungs == 2 && decomp.eyes.tiles.size() >= 2 &&
        decomp.eyes.suit() == Suit::Dragon) {
        result.breakdown.push_back({"小三元", "Little Three Dragons", 3});
    }

    // Wind pungs
    int seatWindRank = static_cast<int>(context.seatWind) + 1;
    int prevWindRank = static_cast<int>(context.prevailingWind) + 1;

    if (countWindPungs(decomp, context.seatWind)) {
        result.breakdown.push_back({"門風", "Seat Wind", 1});
    }
    if (countWindPungs(decomp, context.prevailingWind)) {
        result.breakdown.push_back({"圈風", "Prevailing Wind", 1});
    }

    // Big Four Winds (4 wind pungs) - limit
    int windPungs = countAllWindPungs(decomp);
    if (windPungs == 4) {
        result.totalFaan = LIMIT_FAAN;
        result.isLimit = true;
        result.breakdown.clear();
        result.breakdown.push_back({"大四喜", "Great Four Winds", LIMIT_FAAN});
        return result;
    }

    // Small Four Winds (3 wind pungs + wind pair) - 6 faan
    // Wiki: 不可另計混一色 (cannot additionally count Half Flush)
    bool hasLittleFourWinds = false;
    if (windPungs == 3 && decomp.eyes.tiles.size() >= 2 &&
        decomp.eyes.suit() == Suit::Wind) {
        result.breakdown.push_back({"小四喜", "Little Four Winds", 6});
        hasLittleFourWinds = true;
    }

    // Four Concealed Pongs (8 faan)
    // Wiki: 不另計門前清、對對糊 — suppresses Concealed Hand and All Pongs
    bool hasFourConcealedPongs = false;
    if (hand.melds().empty() && allMeldsArePungOrKong(decomp) && context.selfDrawn) {
        result.breakdown.push_back({"坎坎胡", "Four Concealed Pongs", 8});
        hasFourConcealedPongs = true;
    }

    // Consecutive Kong Win (8 faan)
    // Doesn't additionally count 槓上開花
    bool hasConsecutiveKongWin = false;
    if (context.consecutiveKongs >= 2 && context.selfDrawn) {
        result.breakdown.push_back({"連槓開花", "Consecutive Kong Win", 8});
        hasConsecutiveKongWin = true;
    }

    // All Pongs (3 faan) - suppressed by Four Concealed Pongs
    if (!hasFourConcealedPongs &&
        decomp.specialType == WinDecomposition::SpecialType::None && allMeldsArePungOrKong(decomp)) {
        result.breakdown.push_back({"對對糊", "All Pongs", 3});
    }

    // Mixed Terminals / 花幺九 (1 faan)
    // All tiles are terminals or honors, with both numbered suits and honors present
    if (allTerminalsOrHonors(decomp) && hasNumberedSuit(decomp) && hasHonors(decomp)) {
        result.breakdown.push_back({"花幺九", "Mixed Terminals", 1});
    }

    // All Chows / Ping Wu (1 faan) - all chows, pair not a value tile
    if (allMeldsAreChow(decomp) && !decomp.eyes.tiles.empty()) {
        bool eyesAreValue = decomp.eyes.suit() == Suit::Dragon ||
            (decomp.eyes.suit() == Suit::Wind &&
             (decomp.eyes.rank() == seatWindRank || decomp.eyes.rank() == prevWindRank));
        if (!eyesAreValue) {
            result.breakdown.push_back({"平糊", "All Chows", 1});
        }
    }

    // Half Flush (one suit + honors) - 3 faan
    // Cannot stack with 小四喜 (wiki: 不可另計混一色为九番)
    if (!hasLittleFourWinds && isSingleSuit(hand, decomp, true) && hasHonors(decomp)) {
        result.breakdown.push_back({"混一色", "Half Flush", 3});
    }

    // Full Flush (one suit only, no honors) - 7 faan
    if (isSingleSuit(hand, decomp, false)) {
        // Remove half flush if present
        result.breakdown.erase(
            std::remove_if(result.breakdown.begin(), result.breakdown.end(),
                [](const FaanEntry& e) { return e.nameEn == "Half Flush"; }),
            result.breakdown.end());
        result.breakdown.push_back({"清一色", "Full Flush", 7});
    }

    // Concealed Hand / 門前清 (1 faan)
    // No exposed melds and no concealed kongs — suppressed by Four Concealed Pongs
    if (!hasFourConcealedPongs && hand.melds().empty()) {
        result.breakdown.push_back({"門前清", "Concealed Hand", 1});
    }

    // Self-drawn (1 faan)
    if (context.selfDrawn) {
        result.breakdown.push_back({"自摸", "Self-Draw", 1});
    }

    // Last wall tile (1 faan)
    if (context.lastWallTile) {
        result.breakdown.push_back({"海底撈月", "Last Tile Win", 1});
    }

    // Win on kong replacement (1 faan) - suppressed by Consecutive Kong Win
    if (context.kongReplacement && !hasConsecutiveKongWin) {
        result.breakdown.push_back({"槓上開花", "Win on Kong", 1});
    }

    // Robbing the kong (1 faan)
    if (context.robbedKong) {
        result.breakdown.push_back({"搶槓", "Robbing the Kong", 1});
    }

    // No flowers (1 faan)
    if (hand.flowers().empty()) {
        result.breakdown.push_back({"無花", "No Flowers", 1});
    }

    // Seat flower/season bonus (1 faan each)
    int seatIdx = static_cast<int>(context.seatWind); // 0=East, 1=South, etc.
    int seatFlowerRank = seatIdx + 1;
    for (const auto& f : hand.flowers()) {
        if (f.suit == Suit::Flower && f.rank == seatFlowerRank) {
            result.breakdown.push_back({"正花", "Seat Flower", 1});
        }
        if (f.suit == Suit::Season && f.rank == seatFlowerRank) {
            result.breakdown.push_back({"正花", "Seat Season", 1});
        }
    }

    // All Flowers bonus (2 faan)
    int flowerCount = 0, seasonCount = 0;
    for (const auto& f : hand.flowers()) {
        if (f.suit == Suit::Flower) flowerCount++;
        if (f.suit == Suit::Season) seasonCount++;
    }
    if (flowerCount == 4) result.breakdown.push_back({"花糊", "All Flowers", 2});
    if (seasonCount == 4) result.breakdown.push_back({"花糊", "All Seasons", 2});

    // Sum up faan
    result.totalFaan = 0;
    for (const auto& entry : result.breakdown) {
        result.totalFaan += entry.faan;
    }

    // Cap at limit
    if (result.totalFaan >= LIMIT_FAAN) {
        result.totalFaan = LIMIT_FAAN;
        result.isLimit = true;
    }

    return result;
}

FaanResult Scoring::calculate(const Hand& hand, Tile winningTile,
                               const WinContext& context) {
    auto decompositions = WinDetector::findWins(hand, winningTile);

    FaanResult best;
    best.totalFaan = -1;

    for (const auto& decomp : decompositions) {
        FaanResult result = calculateForDecomposition(decomp, hand, winningTile, context);
        if (result.totalFaan > best.totalFaan) {
            best = result;
        }
    }

    if (best.totalFaan < 0) best.totalFaan = 0;

    return best;
}

int Scoring::faanToPoints(int faan) {
    // HK Old Style payment table
    if (faan <= 0) return 1;
    if (faan <= 1) return 2;
    if (faan <= 2) return 4;
    if (faan <= 3) return 8;
    if (faan <= 6) return 16;
    if (faan <= 9) return 32;
    return 64; // 10+ faan (limit)
}
