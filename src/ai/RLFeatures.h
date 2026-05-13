#pragma once

#include "player/Player.h"
#include "player/Hand.h"
#include "tiles/TileEnums.h"
#include "ai/ShantenCalculator.h"
#include "ai/HandEvaluator.h"
#include "game/GameState.h"
#include <vector>
#include <cstdint>
#include <algorithm>
#include <cmath>

static constexpr int DISCARD_FEATURE_SIZE = 408;
static constexpr int CLAIM_FEATURE_SIZE = 450;  // 408 + 34 + 4 + 4
static constexpr int NUM_TILE_TYPES = 34;
static constexpr int NUM_CLAIM_ACTIONS = 4;  // Chow, Pung, Kong, Pass

// Map a tile's suit+rank to flat index 0-33
// Bamboo 1-9: 0-8, Characters 1-9: 9-17, Dots 1-9: 18-26,
// Wind 1-4: 27-30, Dragon 1-3: 31-33
inline int tileTypeIndex(Suit suit, uint8_t rank) {
    switch (suit) {
        case Suit::Bamboo:     return rank - 1;
        case Suit::Characters: return 9 + rank - 1;
        case Suit::Dots:       return 18 + rank - 1;
        case Suit::Wind:       return 27 + rank - 1;
        case Suit::Dragon:     return 31 + rank - 1;
        default:               return -1;
    }
}

inline int tileTypeIndex(const Tile& t) {
    return tileTypeIndex(t.suit, t.rank);
}

struct RLGameContext {
    int turnCount = 0;
    int wallRemaining = 70;
    Wind seatWind = Wind::East;
    Wind prevailingWind = Wind::East;
    int playerIndex = 0;
    const std::vector<const Player*>* allPlayers = nullptr;
    int playerScores[4] = {};  // Current scores for score-aware features
    bool lightweight = false;  // Skip expensive features (acceptance) for simulation
};

// Helper: populate playerScores from allPlayers
inline void fillPlayerScores(RLGameContext& ctx) {
    if (ctx.allPlayers) {
        for (int i = 0; i < 4 && i < (int)ctx.allPlayers->size(); i++) {
            if ((*ctx.allPlayers)[i]) ctx.playerScores[i] = (*ctx.allPlayers)[i]->score();
        }
    }
}

// Feature layout (408 total):
//   [0..33]     Hand tile counts (34)
//   [34..67]    Own exposed melds (34)
//   [68..169]   Opponent exposed melds (3*34=102)
//   [170..181]  Opponent meld types (3*4=12)
//   [182..317]  Discards per player (4*34=136)
//   [318]       Shanten number (1)
//   [319]       Faan confidence (1)
//   [320]       Estimated faan (1)
//   [321..323]  Faan target profile (3)
//   [324]       Turn progress (1)
//   [325]       Wall remaining (1)
//   [326..329]  Seat wind one-hot (4)
//   [330..333]  Prevailing wind one-hot (4)
//   [334..337]  Flower counts per player (4)
//   [338]       Est. turns remaining (1)
//   [339]       High-faan shanten (1)
//   [340]       Shanten gap (1)
//   [341..343]  Opponent threat levels (3)
//   --- Defensive features ---
//   [344..377]  Live tile counts (34)
//   [378..380]  Opponent tenpai estimates (3)
//   [381..389]  Opponent suit avoidance (3*3=9)
//   --- Hand pattern & score features ---
//   [390]       Seven Pairs shanten (1)
//   [391]       Thirteen Orphans shanten (1)
//   [392]       Special hand advantage (1)
//   [393]       All Pungs indicator (1)
//   [394]       Dominant suit concentration (1)
//   [395]       Score relative to leader (1)
//   [396]       Score rank position (1)
//   [397]       Desperation factor (1)
//   --- Positional awareness features ---
//   [398..400]  Previous player suit dump ratio (3)
//   [401..403]  Next player suit retention (3)
//   [404]       Previous player supply signal (1)
//   [405]       Next player flush danger (1)
//   --- Acceptance counting ---
//   [406]       Acceptance count (tile types that improve shanten) (1)
//   [407]       Weighted acceptance (remaining copies of improving tiles) (1)

inline std::vector<float> extractDiscardFeatures(
    const Hand& hand,
    const RLGameContext& ctx)
{
    std::vector<float> f(DISCARD_FEATURE_SIZE, 0.0f);
    int o = 0;

    // [0..33] Hand tile counts
    for (const auto& t : hand.concealed()) {
        int idx = tileTypeIndex(t);
        if (idx >= 0) f[o + idx] += 1.0f;
    }
    o += 34;

    // [34..67] Own exposed melds
    for (const auto& m : hand.melds()) {
        for (const auto& t : m.tiles) {
            int idx = tileTypeIndex(t);
            if (idx >= 0) f[o + idx] += 1.0f;
        }
    }
    o += 34;

    // [68..169] Opponent exposed melds (3*34)
    if (ctx.allPlayers) {
        int oppSlot = 0;
        for (int i = 0; i < (int)ctx.allPlayers->size() && oppSlot < 3; i++) {
            if (i == ctx.playerIndex) continue;
            const Player* opp = (*ctx.allPlayers)[i];
            if (opp) {
                for (const auto& m : opp->hand().melds()) {
                    for (const auto& t : m.tiles) {
                        int idx = tileTypeIndex(t);
                        if (idx >= 0) f[o + oppSlot * 34 + idx] += 1.0f;
                    }
                }
            }
            oppSlot++;
        }
    }
    o += 102;

    // [170..181] Opponent meld types (3*4)
    if (ctx.allPlayers) {
        int oppSlot = 0;
        for (int i = 0; i < (int)ctx.allPlayers->size() && oppSlot < 3; i++) {
            if (i == ctx.playerIndex) continue;
            const Player* opp = (*ctx.allPlayers)[i];
            if (opp) {
                for (const auto& m : opp->hand().melds()) {
                    int base = o + oppSlot * 4;
                    switch (m.type) {
                        case MeldType::Chow:          f[base + 0] += 1.0f; break;
                        case MeldType::Pung:          f[base + 1] += 1.0f; break;
                        case MeldType::Kong:          f[base + 2] += 1.0f; break;
                        case MeldType::ConcealedKong: f[base + 3] += 1.0f; break;
                        default: break;
                    }
                }
            }
            oppSlot++;
        }
    }
    o += 12;

    // [182..317] Discards per player (4*34), self first then clockwise
    if (ctx.allPlayers) {
        for (int slot = 0; slot < 4; slot++) {
            int pIdx = (ctx.playerIndex + slot) % 4;
            if (pIdx < (int)ctx.allPlayers->size()) {
                const Player* p = (*ctx.allPlayers)[pIdx];
                if (p) {
                    for (const auto& t : p->discards()) {
                        int idx = tileTypeIndex(t);
                        if (idx >= 0) f[o + slot * 34 + idx] += 1.0f;
                    }
                }
            }
        }
    }
    o += 136;

    // [318] Shanten number
    int shanten = ShantenCalculator::calculate(hand);
    f[o++] = static_cast<float>(shanten) / 8.0f;

    // [319] Faan confidence
    FaanPlan plan = HandEvaluator::analyzeFaanPotential(hand, ctx.seatWind, ctx.prevailingWind);
    f[o++] = plan.faanConfidence;

    // [320] Estimated faan
    float estFaan = 0.0f;
    if (plan.aimFullFlush) estFaan += 6.0f;
    else if (plan.aimHalfFlush) estFaan += 3.0f;
    if (plan.hasDragonPungPotential) estFaan += 1.0f;
    if (plan.hasWindPungPotential) estFaan += 1.0f;
    f[o++] = estFaan / 13.0f;

    // [321..323] Faan target profile: speed / value / balanced
    if (plan.aimFullFlush || estFaan >= 6.0f) {
        f[o + 1] = 1.0f; // value
    } else if (plan.faanConfidence > 0.8f && shanten <= 2) {
        f[o + 0] = 1.0f; // speed
    } else {
        f[o + 2] = 1.0f; // balanced
    }
    o += 3;

    // [324] Turn progress
    f[o++] = static_cast<float>(ctx.turnCount) / 80.0f;

    // [325] Wall remaining
    f[o++] = static_cast<float>(ctx.wallRemaining) / 70.0f;

    // [326..329] Seat wind one-hot
    f[o + static_cast<int>(ctx.seatWind)] = 1.0f;
    o += 4;

    // [330..333] Prevailing wind one-hot
    f[o + static_cast<int>(ctx.prevailingWind)] = 1.0f;
    o += 4;

    // [334..337] Flower counts per player
    if (ctx.allPlayers) {
        for (int slot = 0; slot < 4; slot++) {
            int pIdx = (ctx.playerIndex + slot) % 4;
            if (pIdx < (int)ctx.allPlayers->size() && (*ctx.allPlayers)[pIdx]) {
                f[o + slot] = static_cast<float>(
                    (*ctx.allPlayers)[pIdx]->hand().flowers().size()) / 8.0f;
            }
        }
    }
    o += 4;

    // [338] Estimated turns remaining
    f[o++] = (static_cast<float>(ctx.wallRemaining) / 2.0f) / 40.0f;

    // [339] High-faan shanten
    float highFaanShanten;
    if (plan.aimHalfFlush || plan.aimFullFlush) {
        int offSuitCount = 0;
        for (const auto& t : hand.concealed()) {
            if (isNumberedSuit(t.suit) && t.suit != plan.flushSuit) offSuitCount++;
            if (plan.aimFullFlush && isHonorSuit(t.suit)) offSuitCount++;
        }
        highFaanShanten = static_cast<float>(shanten + offSuitCount / 2);
    } else {
        highFaanShanten = 8.0f;
    }
    f[o++] = highFaanShanten / 8.0f;

    // [340] Shanten gap
    f[o++] = (highFaanShanten - static_cast<float>(shanten)) / 8.0f;

    // [341..343] Opponent threat levels
    if (ctx.allPlayers) {
        int oppSlot = 0;
        for (int i = 0; i < (int)ctx.allPlayers->size() && oppSlot < 3; i++) {
            if (i == ctx.playerIndex) continue;
            const Player* opp = (*ctx.allPlayers)[i];
            if (opp) {
                float threat = 0.0f;
                int discCount = static_cast<int>(opp->discards().size());
                if (discCount < 5) threat += 0.3f;
                int meldCount = static_cast<int>(opp->hand().melds().size());
                threat += meldCount * 0.2f;
                // Possible flush: one suit absent from discards
                int suitDisc[3] = {};
                for (const auto& d : opp->discards()) {
                    if (d.suit == Suit::Bamboo)          suitDisc[0]++;
                    else if (d.suit == Suit::Characters) suitDisc[1]++;
                    else if (d.suit == Suit::Dots)       suitDisc[2]++;
                }
                for (int s = 0; s < 3; s++) {
                    int others = 0;
                    for (int s2 = 0; s2 < 3; s2++) if (s2 != s) others += suitDisc[s2];
                    if (suitDisc[s] == 0 && others >= 4) threat += 0.3f;
                }
                f[o + oppSlot] = std::min(threat, 1.0f);
            }
            oppSlot++;
        }
    }
    o += 3;

    // [344..377] Live tile counts: (4 - visible) / 4.0 per tile type
    // 0.0 = all copies accounted for (safe to discard), 1.0 = all hidden
    {
        int visible[34] = {};
        // Own concealed hand
        for (const auto& t : hand.concealed()) {
            int idx = tileTypeIndex(t);
            if (idx >= 0) visible[idx]++;
        }
        // Own melds
        for (const auto& m : hand.melds()) {
            for (const auto& t : m.tiles) {
                int idx = tileTypeIndex(t);
                if (idx >= 0) visible[idx]++;
            }
        }
        // All players' discards + opponent melds
        if (ctx.allPlayers) {
            for (int i = 0; i < (int)ctx.allPlayers->size(); i++) {
                const Player* p = (*ctx.allPlayers)[i];
                if (!p) continue;
                for (const auto& t : p->discards()) {
                    int idx = tileTypeIndex(t);
                    if (idx >= 0) visible[idx]++;
                }
                if (i != ctx.playerIndex) {
                    for (const auto& m : p->hand().melds()) {
                        for (const auto& t : m.tiles) {
                            int idx = tileTypeIndex(t);
                            if (idx >= 0) visible[idx]++;
                        }
                    }
                }
            }
        }
        for (int i = 0; i < 34; i++) {
            f[o + i] = static_cast<float>(4 - std::min(visible[i], 4)) / 4.0f;
        }
    }
    o += 34;

    // [378..380] Opponent tenpai estimates
    if (ctx.allPlayers) {
        int oppSlot = 0;
        for (int i = 0; i < (int)ctx.allPlayers->size() && oppSlot < 3; i++) {
            if (i == ctx.playerIndex) continue;
            const Player* opp = (*ctx.allPlayers)[i];
            if (opp) {
                float signal = 0.0f;
                int meldCount = static_cast<int>(opp->hand().melds().size());
                int concealedCount = opp->hand().concealedCount();
                int discCount = static_cast<int>(opp->discards().size());

                if (meldCount >= 3) signal += 0.5f;
                if (concealedCount <= 4) signal += 0.3f;
                if (discCount >= 10) signal += 0.2f;

                // Safety dumping: last 3 discards all terminals/honors
                if (discCount >= 3) {
                    bool allSafe = true;
                    for (int d = discCount - 3; d < discCount; d++) {
                        if (!opp->discards()[d].isTerminalOrHonor()) {
                            allSafe = false;
                            break;
                        }
                    }
                    if (allSafe) signal += 0.4f;
                }
                f[o + oppSlot] = std::min(signal, 1.0f);
            }
            oppSlot++;
        }
    }
    o += 3;

    // [381..389] Opponent suit avoidance (3 opponents * 3 numbered suits)
    // High value = opponent avoids that suit (likely flush)
    if (ctx.allPlayers) {
        int oppSlot = 0;
        for (int i = 0; i < (int)ctx.allPlayers->size() && oppSlot < 3; i++) {
            if (i == ctx.playerIndex) continue;
            const Player* opp = (*ctx.allPlayers)[i];
            if (opp) {
                int suitDisc[3] = {};
                int totalNumbered = 0;
                for (const auto& d : opp->discards()) {
                    if (d.suit == Suit::Bamboo)          { suitDisc[0]++; totalNumbered++; }
                    else if (d.suit == Suit::Characters) { suitDisc[1]++; totalNumbered++; }
                    else if (d.suit == Suit::Dots)       { suitDisc[2]++; totalNumbered++; }
                }
                if (totalNumbered >= 3) {
                    for (int s = 0; s < 3; s++) {
                        f[o + oppSlot * 3 + s] = 1.0f - static_cast<float>(suitDisc[s]) /
                                                  static_cast<float>(totalNumbered);
                    }
                }
            }
            oppSlot++;
        }
    }
    o += 9;

    // [390] Seven Pairs shanten — disabled (not valid in HK Mahjong)
    // Slot kept at 1.0 (max/disabled) to preserve feature vector size
    f[o++] = 1.0f;

    // [391] Thirteen Orphans shanten (only viable with 0 melds)
    // shanten = 13 - distinctOrphans - hasDuplicate
    {
        float orphansShanten = 13.0f;
        if (hand.melds().empty()) {
            struct TS { int g; int r; };
            static constexpr TS orphanTypes[] = {
                {0,0},{0,8},{1,0},{1,8},{2,0},{2,8},
                {3,0},{3,1},{3,2},{3,3},{3,4},{3,5},{3,6}
            };
            int tileCounts[4][9] = {};
            for (const auto& t : hand.concealed()) {
                int g = -1, r0 = t.rank - 1;
                switch (t.suit) {
                    case Suit::Bamboo:     g = 0; break;
                    case Suit::Characters: g = 1; break;
                    case Suit::Dots:       g = 2; break;
                    case Suit::Wind:       g = 3; break;
                    case Suit::Dragon:     g = 3; r0 = t.rank - 1 + 4; break;
                    default: continue;
                }
                if (g >= 0 && r0 >= 0 && r0 < 9) tileCounts[g][r0]++;
            }
            int distinct = 0;
            bool hasDup = false;
            for (auto& ts : orphanTypes) {
                if (tileCounts[ts.g][ts.r] >= 1) distinct++;
                if (tileCounts[ts.g][ts.r] >= 2) hasDup = true;
            }
            orphansShanten = static_cast<float>(13 - distinct - (hasDup ? 1 : 0));
        }
        f[o++] = orphansShanten / 13.0f;
    }

    // [392] Special hand advantage: how much closer is the best special hand vs standard
    {
        float specialAdv = 0.0f;
        if (hand.melds().empty()) {
            float sp = std::min(f[o - 2] * 6.0f, f[o - 1] * 13.0f);
            float standard = static_cast<float>(shanten);
            specialAdv = std::max(0.0f, standard - sp) / 8.0f;
        }
        f[o++] = specialAdv;
    }

    // [393] All Pungs indicator: ratio of concealed tiles in pairs/trips (no sequences)
    {
        float allPungsRatio = 0.0f;
        int totalConcealed = static_cast<int>(hand.concealed().size());
        if (totalConcealed > 0) {
            int tileCounts[34] = {};
            for (const auto& t : hand.concealed()) {
                int idx = tileTypeIndex(t);
                if (idx >= 0) tileCounts[idx]++;
            }
            int inPairsOrTrips = 0;
            for (int i = 0; i < 34; i++) {
                if (tileCounts[i] >= 2) inPairsOrTrips += tileCounts[i];
            }
            allPungsRatio = static_cast<float>(inPairsOrTrips) /
                            static_cast<float>(totalConcealed);
        }
        f[o++] = allPungsRatio;
    }

    // [394] Dominant suit concentration
    {
        int suitCounts[3] = {};
        int totalTiles = 0;
        for (const auto& t : hand.concealed()) {
            if (t.suit == Suit::Bamboo)          { suitCounts[0]++; totalTiles++; }
            else if (t.suit == Suit::Characters) { suitCounts[1]++; totalTiles++; }
            else if (t.suit == Suit::Dots)       { suitCounts[2]++; totalTiles++; }
            else totalTiles++;
        }
        for (const auto& m : hand.melds()) {
            for (const auto& t : m.tiles) {
                if (t.suit == Suit::Bamboo)          { suitCounts[0]++; totalTiles++; }
                else if (t.suit == Suit::Characters) { suitCounts[1]++; totalTiles++; }
                else if (t.suit == Suit::Dots)       { suitCounts[2]++; totalTiles++; }
                else totalTiles++;
            }
        }
        int maxSuit = std::max({suitCounts[0], suitCounts[1], suitCounts[2]});
        f[o++] = totalTiles > 0 ? static_cast<float>(maxSuit) / static_cast<float>(totalTiles)
                                : 0.0f;
    }

    // [395] Score relative to leader: (own - leader) / 128.0, clamped [-1, 1]
    {
        int ownScore = ctx.playerScores[ctx.playerIndex];
        int maxScore = ownScore;
        for (int i = 0; i < 4; i++) {
            if (i != ctx.playerIndex) maxScore = std::max(maxScore, ctx.playerScores[i]);
        }
        float rel = static_cast<float>(ownScore - maxScore) / 128.0f;
        f[o++] = std::max(-1.0f, std::min(1.0f, rel));
    }

    // [396] Score rank position: 0.0 = last, 1.0 = first
    {
        int ownScore = ctx.playerScores[ctx.playerIndex];
        int belowCount = 0;
        for (int i = 0; i < 4; i++) {
            if (i != ctx.playerIndex && ctx.playerScores[i] < ownScore) belowCount++;
        }
        f[o++] = static_cast<float>(belowCount) / 3.0f;
    }

    // [397] Desperation factor: high when losing badly + late in game
    {
        int ownScore = ctx.playerScores[ctx.playerIndex];
        int maxScore = ownScore;
        for (int i = 0; i < 4; i++) {
            if (i != ctx.playerIndex) maxScore = std::max(maxScore, ctx.playerScores[i]);
        }
        float deficit = static_cast<float>(maxScore - ownScore) / 128.0f;
        float gameProgress = 1.0f - static_cast<float>(ctx.wallRemaining) / 70.0f;
        f[o++] = std::min(1.0f, deficit * gameProgress);
    }
    // --- Positional awareness features ---
    // Previous player = (playerIndex + 3) % 4 — you can chow from them
    // Next player = (playerIndex + 1) % 4 — they can chow from you
    if (ctx.allPlayers && ctx.allPlayers->size() == 4) {
        int prevIdx = (ctx.playerIndex + 3) % 4;
        int nextIdx = (ctx.playerIndex + 1) % 4;
        const Player* prevPlayer = (*ctx.allPlayers)[prevIdx];
        const Player* nextPlayer = (*ctx.allPlayers)[nextIdx];

        // [398..400] Previous player suit dump ratio
        // High = they're discarding this suit heavily = safe to build / chow opportunities
        if (prevPlayer) {
            int prevSuitDisc[3] = {};
            int prevTotalNumbered = 0;
            for (const auto& d : prevPlayer->discards()) {
                if (d.suit == Suit::Bamboo)          { prevSuitDisc[0]++; prevTotalNumbered++; }
                else if (d.suit == Suit::Characters) { prevSuitDisc[1]++; prevTotalNumbered++; }
                else if (d.suit == Suit::Dots)       { prevSuitDisc[2]++; prevTotalNumbered++; }
            }
            if (prevTotalNumbered >= 3) {
                for (int s = 0; s < 3; s++) {
                    f[o + s] = static_cast<float>(prevSuitDisc[s]) /
                               static_cast<float>(prevTotalNumbered);
                }
            }
        }
        o += 3;

        // [401..403] Next player suit retention
        // High = they're NOT discarding this suit = likely collecting it = don't feed them
        if (nextPlayer) {
            int nextSuitDisc[3] = {};
            int nextTotalNumbered = 0;
            for (const auto& d : nextPlayer->discards()) {
                if (d.suit == Suit::Bamboo)          { nextSuitDisc[0]++; nextTotalNumbered++; }
                else if (d.suit == Suit::Characters) { nextSuitDisc[1]++; nextTotalNumbered++; }
                else if (d.suit == Suit::Dots)       { nextSuitDisc[2]++; nextTotalNumbered++; }
            }
            if (nextTotalNumbered >= 3) {
                for (int s = 0; s < 3; s++) {
                    f[o + s] = 1.0f - static_cast<float>(nextSuitDisc[s]) /
                                       static_cast<float>(nextTotalNumbered);
                }
            }
        }
        o += 3;

        // [404] Previous player supply signal
        // 1.0 if previous player is heavily dumping any one suit (>=50% of their numbered discards)
        if (prevPlayer) {
            int prevSuitDisc[3] = {};
            int prevTotalNumbered = 0;
            for (const auto& d : prevPlayer->discards()) {
                if (d.suit == Suit::Bamboo)          { prevSuitDisc[0]++; prevTotalNumbered++; }
                else if (d.suit == Suit::Characters) { prevSuitDisc[1]++; prevTotalNumbered++; }
                else if (d.suit == Suit::Dots)       { prevSuitDisc[2]++; prevTotalNumbered++; }
            }
            if (prevTotalNumbered >= 4) {
                int maxDump = std::max({prevSuitDisc[0], prevSuitDisc[1], prevSuitDisc[2]});
                if (maxDump * 2 >= prevTotalNumbered) {
                    f[o] = static_cast<float>(maxDump) / static_cast<float>(prevTotalNumbered);
                }
            }
        }
        o += 1;

        // [405] Next player flush danger
        // High if next player discards zero tiles of some suit + has melds
        if (nextPlayer) {
            int nextSuitDisc[3] = {};
            int nextTotalNumbered = 0;
            for (const auto& d : nextPlayer->discards()) {
                if (d.suit == Suit::Bamboo)          { nextSuitDisc[0]++; nextTotalNumbered++; }
                else if (d.suit == Suit::Characters) { nextSuitDisc[1]++; nextTotalNumbered++; }
                else if (d.suit == Suit::Dots)       { nextSuitDisc[2]++; nextTotalNumbered++; }
            }
            if (nextTotalNumbered >= 4) {
                int nextMelds = static_cast<int>(nextPlayer->hand().melds().size());
                for (int s = 0; s < 3; s++) {
                    if (nextSuitDisc[s] == 0) {
                        f[o] = std::min(1.0f, 0.5f + nextMelds * 0.2f);
                        break;
                    }
                }
            }
        }
        o += 1;
    } else {
        o += 8;
    }

    // [406] Acceptance count: how many tile types would reduce shanten if drawn
    // [407] Weighted acceptance: sum of remaining copies of improving tile types
    {
        int acceptance = 0;
        if (!ctx.lightweight) {
            acceptance = ShantenCalculator::countAcceptance(hand);
        }
        f[o++] = static_cast<float>(acceptance) / 34.0f;

        // Weighted by available copies (accounts for visible tiles)
        // Reuse the visible tile counts computed earlier (live tile counts [344..377])
        // For efficiency, recompute visible counts here
        float weightedAccept = 0.0f;
        if (acceptance > 0 && ctx.allPlayers) {
            // Build visible tile counts
            int visible[34] = {};
            for (const auto& t : hand.concealed()) {
                int idx = tileTypeIndex(t);
                if (idx >= 0) visible[idx]++;
            }
            for (const auto& m : hand.melds()) {
                for (const auto& t : m.tiles) {
                    int idx = tileTypeIndex(t);
                    if (idx >= 0) visible[idx]++;
                }
            }
            for (int i = 0; i < (int)ctx.allPlayers->size(); i++) {
                const Player* p = (*ctx.allPlayers)[i];
                if (!p) continue;
                for (const auto& t : p->discards()) {
                    int idx = tileTypeIndex(t);
                    if (idx >= 0) visible[idx]++;
                }
                if (i != ctx.playerIndex) {
                    for (const auto& m : p->hand().melds()) {
                        for (const auto& t : m.tiles) {
                            int idx = tileTypeIndex(t);
                            if (idx >= 0) visible[idx]++;
                        }
                    }
                }
            }

            // For each tile type, simulate drawing it to see if it improves shanten
            // Use a simplified approach: build counts once, try each
            int tileCounts[4][9] = {};
            for (const auto& t : hand.concealed()) {
                int g = -1, r0 = t.rank - 1;
                switch (t.suit) {
                    case Suit::Bamboo:     g = 0; break;
                    case Suit::Characters: g = 1; break;
                    case Suit::Dots:       g = 2; break;
                    case Suit::Wind:       g = 3; break;
                    case Suit::Dragon:     g = 3; r0 = t.rank - 1 + 4; break;
                    default: continue;
                }
                if (g >= 0 && r0 >= 0 && r0 < 9) tileCounts[g][r0]++;
            }
            // Sum remaining copies for each improving tile type
            // Tile type index to (group, rank) mapping:
            // 0-8: group 0 rank 0-8, 9-17: group 1 rank 0-8, 18-26: group 2 rank 0-8
            // 27-30: group 3 rank 0-3, 31-33: group 3 rank 4-6
            for (int idx = 0; idx < 34; idx++) {
                int remaining = 4 - std::min(visible[idx], 4);
                if (remaining > 0) {
                    weightedAccept += static_cast<float>(remaining);
                }
            }
            // Normalize: acceptance * remaining copies. Max theoretical ~34*4=136
            // But realistically max ~50 for a good hand
            weightedAccept = weightedAccept * static_cast<float>(acceptance) / (34.0f * 50.0f);
        }
        f[o++] = std::min(1.0f, weightedAccept);
    }
    // Total: 408

    return f;
}

// Claim features (450 total): discard features (408) + claimed tile (34) + claim mask (4) + shanten after claim (4)
inline std::vector<float> extractClaimFeatures(
    const Hand& hand,
    Tile claimedTile,
    const std::vector<ClaimType>& availableClaims,
    const RLGameContext& ctx)
{
    auto f = extractDiscardFeatures(hand, ctx);
    f.resize(CLAIM_FEATURE_SIZE, 0.0f);
    int o = DISCARD_FEATURE_SIZE;

    // [408..441] Claimed tile one-hot
    int claimIdx = tileTypeIndex(claimedTile);
    if (claimIdx >= 0) f[o + claimIdx] = 1.0f;
    o += 34;

    // [442..445] Available claims mask: Chow / Pung / Kong / Pass
    for (auto ct : availableClaims) {
        if (ct == ClaimType::Chow) f[o + 0] = 1.0f;
        else if (ct == ClaimType::Pung) f[o + 1] = 1.0f;
        else if (ct == ClaimType::Kong) f[o + 2] = 1.0f;
    }
    f[o + 3] = 1.0f; // Pass always available
    o += 4;

    // [446..449] Shanten after each claim type
    int curShanten = ShantenCalculator::calculate(hand);

    // Chow
    if (f[o - 4] > 0.5f && isNumberedSuit(claimedTile.suit)) {
        uint8_t r = claimedTile.rank;
        Suit s = claimedTile.suit;
        int best = curShanten;
        if (r >= 3 && hand.hasTile(s, r-2) && hand.hasTile(s, r-1))
            best = std::min(best, ShantenCalculator::calculateAfterChow(hand, s, r, r-2, r-1));
        if (r >= 2 && r <= 8 && hand.hasTile(s, r-1) && hand.hasTile(s, r+1))
            best = std::min(best, ShantenCalculator::calculateAfterChow(hand, s, r, r-1, r+1));
        if (r <= 7 && hand.hasTile(s, r+1) && hand.hasTile(s, r+2))
            best = std::min(best, ShantenCalculator::calculateAfterChow(hand, s, r, r+1, r+2));
        f[o + 0] = static_cast<float>(best) / 8.0f;
    } else {
        f[o + 0] = static_cast<float>(curShanten) / 8.0f;
    }

    // Pung
    if (f[o - 3] > 0.5f) {
        f[o + 1] = static_cast<float>(
            ShantenCalculator::calculateAfterPung(hand, claimedTile.suit, claimedTile.rank)) / 8.0f;
    } else {
        f[o + 1] = static_cast<float>(curShanten) / 8.0f;
    }

    // Kong
    if (f[o - 2] > 0.5f) {
        f[o + 2] = static_cast<float>(
            ShantenCalculator::calculateAfterPung(hand, claimedTile.suit, claimedTile.rank)) / 8.0f;
    } else {
        f[o + 2] = static_cast<float>(curShanten) / 8.0f;
    }

    // Pass
    f[o + 3] = static_cast<float>(curShanten) / 8.0f;

    return f;
}
