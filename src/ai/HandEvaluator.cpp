#include "HandEvaluator.h"
#include "ShantenCalculator.h"
#include "RLFeatures.h"
#include <algorithm>
#include <set>
#include <cmath>

FaanPlan HandEvaluator::analyzeFaanPotential(
    const Hand& hand, Wind seatWind, Wind prevailingWind) {

    FaanPlan plan;

    // Count tiles by suit (concealed + melds)
    int suitCounts[3] = {}; // Bamboo, Characters, Dots
    int honorCount = 0;

    auto countTile = [&](const Tile& t) {
        if (t.suit == Suit::Bamboo)          suitCounts[0]++;
        else if (t.suit == Suit::Characters) suitCounts[1]++;
        else if (t.suit == Suit::Dots)       suitCounts[2]++;
        else if (isHonorSuit(t.suit))        honorCount++;
    };

    for (const auto& t : hand.concealed()) countTile(t);
    for (const auto& m : hand.melds()) {
        for (const auto& t : m.tiles) countTile(t);
    }

    // Find dominant suit
    int maxIdx = 0;
    for (int i = 1; i < 3; i++) {
        if (suitCounts[i] > suitCounts[maxIdx]) maxIdx = i;
    }

    int totalTiles = suitCounts[0] + suitCounts[1] + suitCounts[2] + honorCount;
    int dominantCount = suitCounts[maxIdx];
    int offSuitCount = totalTiles - dominantCount - honorCount;

    // Half flush viability: if off-suit tiles are few (<=3), aim for half flush
    if (offSuitCount <= 3 && dominantCount >= 5) {
        plan.aimHalfFlush = true;
        plan.flushSuit = static_cast<Suit>(maxIdx); // Bamboo=0, Characters=1, Dots=2
    }
    if (offSuitCount == 0 && honorCount == 0 && dominantCount > 0) {
        plan.aimFullFlush = true;
        plan.aimHalfFlush = false;
        plan.flushSuit = static_cast<Suit>(maxIdx);
    }

    // Dragon pung potential
    for (uint8_t r = 1; r <= 3; r++) {
        if (hand.countTile(Suit::Dragon, r) >= 2) plan.hasDragonPungPotential = true;
    }

    // Wind pung potential (seat or prevailing wind)
    uint8_t seatRank = static_cast<uint8_t>(seatWind) + 1;
    uint8_t prevRank = static_cast<uint8_t>(prevailingWind) + 1;
    if (hand.countTile(Suit::Wind, seatRank) >= 2) plan.hasWindPungPotential = true;
    if (hand.countTile(Suit::Wind, prevRank) >= 2) plan.hasWindPungPotential = true;

    // Special hand detection (only viable with no melds)
    if (hand.melds().empty()) {
        int tileCounts[4][9] = {};
        for (const auto& t : hand.concealed()) {
            int g = -1, r = -1;
            if (t.suit == Suit::Bamboo)           { g = 0; r = t.rank - 1; }
            else if (t.suit == Suit::Characters)  { g = 1; r = t.rank - 1; }
            else if (t.suit == Suit::Dots)        { g = 2; r = t.rank - 1; }
            else if (t.suit == Suit::Wind)        { g = 3; r = t.rank - 1; }
            else if (t.suit == Suit::Dragon)      { g = 3; r = t.rank - 1 + 4; }
            if (g >= 0 && g < 4 && r >= 0 && r < 9) tileCounts[g][r]++;
        }

        // Thirteen Orphans: shanten = 13 - distinct - hasDup
        struct TS { int g; int r; };
        static constexpr TS orphanTypes[] = {
            {0,0},{0,8},{1,0},{1,8},{2,0},{2,8},
            {3,0},{3,1},{3,2},{3,3},{3,4},{3,5},{3,6}
        };
        int distinct = 0;
        bool hasDup = false;
        for (auto& ts : orphanTypes) {
            if (tileCounts[ts.g][ts.r] >= 1) distinct++;
            if (tileCounts[ts.g][ts.r] >= 2) hasDup = true;
        }
        plan.thirteenOrphansShanten = 13 - distinct - (hasDup ? 1 : 0);
        if (plan.thirteenOrphansShanten <= 4) plan.aimThirteenOrphans = true;

        // All Pungs: check if tiles are mostly pairs/trips (no sequence potential)
        int inPairsOrTrips = 0;
        int totalConcealed = static_cast<int>(hand.concealed().size());
        for (int g = 0; g < 4; g++) {
            int maxR = (g < 3) ? 9 : 7;
            for (int r = 0; r < maxR; r++) {
                if (tileCounts[g][r] >= 2) inPairsOrTrips += tileCounts[g][r];
            }
        }
        if (totalConcealed > 0 &&
            static_cast<float>(inPairsOrTrips) / static_cast<float>(totalConcealed) >= 0.7f) {
            plan.aimAllPungs = true;
        }
    }

    // Guaranteed faan from existing melds
    int guaranteedFaan = 0;
    if (hand.flowers().empty()) guaranteedFaan += 1; // No Flowers bonus

    for (const auto& m : hand.melds()) {
        if (m.isPungOrKong() && m.suit() == Suit::Dragon) guaranteedFaan += 1;
        if (m.isPungOrKong() && m.suit() == Suit::Wind && m.rank() == seatRank) guaranteedFaan += 1;
        if (m.isPungOrKong() && m.suit() == Suit::Wind && m.rank() == prevRank) guaranteedFaan += 1;
    }

    // Self-draw is always possible (+1 faan if we draw our winning tile)
    // Count it as a 50% chance
    float expectedFaan = static_cast<float>(guaranteedFaan) + 0.5f;

    if (plan.aimHalfFlush) expectedFaan += 3.0f;
    if (plan.aimFullFlush) expectedFaan += 6.0f;
    if (plan.hasDragonPungPotential) expectedFaan += 0.7f;
    if (plan.hasWindPungPotential) expectedFaan += 0.7f;
    if (plan.aimThirteenOrphans) expectedFaan = std::max(expectedFaan, 13.0f);
    if (plan.aimAllPungs) expectedFaan += 3.0f;

    plan.faanConfidence = (expectedFaan >= 3.0f) ? 1.0f : expectedFaan / 3.0f;

    return plan;
}

float HandEvaluator::defensiveScore(const Tile& candidate,
                                     const std::vector<const Player*>& allPlayers,
                                     int selfIndex) {
    float safety = 0.0f;

    for (int i = 0; i < static_cast<int>(allPlayers.size()); i++) {
        if (i == selfIndex || !allPlayers[i]) continue;
        for (const auto& disc : allPlayers[i]->discards()) {
            if (disc.sameAs(candidate)) {
                safety += 3.0f; // Opponent discarded same tile type = very safe
            }
        }
    }

    // Terminal and honor tiles are generally safer
    if (candidate.isTerminalOrHonor()) safety += 0.5f;

    // Live tile safety: count all visible copies across the table
    int visibleCount = 0;
    for (int i = 0; i < static_cast<int>(allPlayers.size()); i++) {
        if (!allPlayers[i]) continue;
        for (const auto& disc : allPlayers[i]->discards()) {
            if (disc.sameAs(candidate)) visibleCount++;
        }
        for (const auto& m : allPlayers[i]->hand().melds()) {
            for (const auto& t : m.tiles) {
                if (t.sameAs(candidate)) visibleCount++;
            }
        }
    }
    // Count in own concealed hand
    if (selfIndex >= 0 && selfIndex < static_cast<int>(allPlayers.size()) && allPlayers[selfIndex]) {
        for (const auto& t : allPlayers[selfIndex]->hand().concealed()) {
            if (t.sameAs(candidate)) visibleCount++;
        }
    }
    if (visibleCount >= 4) {
        safety += 5.0f;  // All copies accounted for — completely safe
    } else if (visibleCount >= 3) {
        safety += 2.0f;  // Only 1 copy unaccounted for — very safe
    }

    // Flush danger: penalize discarding into opponent's likely flush suit
    if (isNumberedSuit(candidate.suit)) {
        int candidateSuitIdx = static_cast<int>(candidate.suit);  // Bamboo=0, Char=1, Dots=2
        for (int i = 0; i < static_cast<int>(allPlayers.size()); i++) {
            if (i == selfIndex || !allPlayers[i]) continue;
            const Player* opp = allPlayers[i];
            int suitDisc[3] = {};
            int totalNumbered = 0;
            for (const auto& d : opp->discards()) {
                if (d.suit == Suit::Bamboo)          { suitDisc[0]++; totalNumbered++; }
                else if (d.suit == Suit::Characters) { suitDisc[1]++; totalNumbered++; }
                else if (d.suit == Suit::Dots)       { suitDisc[2]++; totalNumbered++; }
            }
            if (totalNumbered >= 4 && suitDisc[candidateSuitIdx] == 0) {
                int meldCount = static_cast<int>(opp->hand().melds().size());
                if (meldCount >= 2) {
                    safety -= 3.0f;  // Likely flush, advanced hand
                } else {
                    safety -= 1.5f;  // Possible flush, early stage
                }
            }
        }
    }

    return safety;
}

std::vector<DiscardCandidate> HandEvaluator::evaluateDiscards(
    const Hand& hand, Wind seatWind, Wind prevailingWind,
    const std::vector<const Player*>& allPlayers) {

    const auto& tiles = hand.concealed();
    if (tiles.empty()) return {};

    FaanPlan plan = analyzeFaanPotential(hand, seatWind, prevailingWind);
    int currentShanten = ShantenCalculator::calculate(hand);

    // Find self index from allPlayers
    int selfIndex = -1;
    for (int i = 0; i < static_cast<int>(allPlayers.size()); i++) {
        if (allPlayers[i] && &allPlayers[i]->hand() == &hand) {
            selfIndex = i;
            break;
        }
    }

    // Deduplicate tiles (same suit+rank only needs one evaluation)
    struct TileKey {
        Suit suit; uint8_t rank;
        bool operator<(const TileKey& o) const {
            if (suit != o.suit) return suit < o.suit;
            return rank < o.rank;
        }
    };
    std::set<TileKey> evaluated;

    std::vector<DiscardCandidate> candidates;

    for (int i = 0; i < static_cast<int>(tiles.size()); i++) {
        TileKey key{tiles[i].suit, tiles[i].rank};
        if (evaluated.count(key)) continue;
        evaluated.insert(key);

        DiscardCandidate dc;
        dc.tile = tiles[i];
        dc.shantenAfterDiscard = ShantenCalculator::calculateAfterDiscard(
            hand, tiles[i].suit, tiles[i].rank);
        dc.acceptCount = 0;
        dc.strategicScore = 0.0f;

        candidates.push_back(dc);
    }

    // Find best (lowest) shanten
    int bestShanten = 99;
    for (auto& dc : candidates) {
        bestShanten = std::min(bestShanten, dc.shantenAfterDiscard);
    }

    // For top candidates (equal shanten), compute acceptance count
    // Acceptance = how many of the 34 tile types would reduce shanten after this discard
    for (auto& dc : candidates) {
        if (dc.shantenAfterDiscard > bestShanten + 1) continue; // Only compute for competitive candidates
        dc.acceptCount = ShantenCalculator::countAcceptanceAfterDiscard(
            hand, dc.tile.suit, dc.tile.rank);
    }

    // Compute strategic score for all candidates
    uint8_t seatRank = static_cast<uint8_t>(seatWind) + 1;
    uint8_t prevRank = static_cast<uint8_t>(prevailingWind) + 1;

    for (auto& dc : candidates) {
        float score = 0.0f;

        // Special hand path bonuses
        if (plan.aimThirteenOrphans) {
            bool isOrphan = dc.tile.isTerminalOrHonor();
            int copies = hand.countTile(dc.tile.suit, dc.tile.rank);
            if (isOrphan && copies <= 1) {
                score -= 5.0f; // Never discard needed orphan tiles
            } else if (!isOrphan) {
                score += 3.0f; // Discard non-orphan tiles
            } else if (isOrphan && copies > 2) {
                score += 1.0f; // Extra copies of orphans can go
            }
        }

        if (plan.aimAllPungs) {
            int copies = hand.countTile(dc.tile.suit, dc.tile.rank);
            if (copies >= 2) {
                score -= 2.0f; // Keep pairs/trips for All Pungs
            } else if (copies == 1) {
                score += 1.5f; // Singletons are good discards
            }
        }

        // Faan plan compatibility
        if (plan.aimHalfFlush || plan.aimFullFlush) {
            if (isNumberedSuit(dc.tile.suit) && dc.tile.suit != plan.flushSuit) {
                score += 4.0f; // Good discard: off-suit tile when pursuing flush
            }
            if (dc.tile.suit == plan.flushSuit) {
                score -= 3.0f; // Bad discard: losing a tile of our flush suit
            }
        }

        // Honor tiles: dragons and value winds are worth 1 faan as pungs
        if (dc.tile.isHonor()) {
            int copies = hand.countTile(dc.tile.suit, dc.tile.rank);
            bool isValueWind = (dc.tile.suit == Suit::Wind &&
                (dc.tile.rank == seatRank || dc.tile.rank == prevRank));
            bool isDragon = (dc.tile.suit == Suit::Dragon);

            if (isDragon || isValueWind) {
                // Count how many copies opponents have discarded or exposed
                int othersVisible = 0;
                if (selfIndex >= 0) {
                    for (int p = 0; p < static_cast<int>(allPlayers.size()); p++) {
                        if (p == selfIndex || !allPlayers[p]) continue;
                        for (const auto& d : allPlayers[p]->discards()) {
                            if (d.sameAs(dc.tile)) othersVisible++;
                        }
                        for (const auto& m : allPlayers[p]->hand().melds()) {
                            for (const auto& t : m.tiles) {
                                if (t.sameAs(dc.tile)) othersVisible++;
                            }
                        }
                    }
                }
                int remaining = 4 - copies - othersVisible;

                if (copies >= 2) {
                    // Pair/trip of dragon or value wind — strong keep
                    if (remaining >= 1)
                        score -= 6.0f;  // Pung still achievable
                    else
                        score -= 2.0f;  // Last copy gone, keep as pair (eyes)
                } else {
                    // Singleton: keep if copies remain, discard if others took them
                    if (remaining >= 2)
                        score -= 3.0f;  // 2+ unseen, pung path viable
                    else if (remaining == 1)
                        score -= 1.0f;  // 1 unseen, pair possible
                    else
                        score += 2.5f;  // All other copies visible, safe to discard
                }
            } else {
                // Non-value honor (other winds): isolated = good discard
                if (copies == 1) {
                    score += 1.5f;
                }
            }
            // Half-flush: keep all honors
            if (plan.aimHalfFlush) score -= 1.0f;
        }

        // Terminal tiles are less flexible (fewer sequence possibilities)
        if (isNumberedSuit(dc.tile.suit) && (dc.tile.rank == 1 || dc.tile.rank == 9)) {
            score += 0.5f;
        }

        // Positional awareness: previous player (chow source) and next player (feeds from you)
        if (selfIndex >= 0 && allPlayers.size() == 4 && isNumberedSuit(dc.tile.suit)) {
            int prevIdx = (selfIndex + 3) % 4;
            int nextIdx = (selfIndex + 1) % 4;
            int candidateSuitIdx = static_cast<int>(dc.tile.suit);

            // Previous player dumping candidate's suit = good to keep (chow opportunities)
            if (allPlayers[prevIdx]) {
                int prevSuitDisc[3] = {};
                int prevTotal = 0;
                for (const auto& d : allPlayers[prevIdx]->discards()) {
                    if (d.suit == Suit::Bamboo)          { prevSuitDisc[0]++; prevTotal++; }
                    else if (d.suit == Suit::Characters) { prevSuitDisc[1]++; prevTotal++; }
                    else if (d.suit == Suit::Dots)       { prevSuitDisc[2]++; prevTotal++; }
                }
                if (prevTotal >= 4 && prevSuitDisc[candidateSuitIdx] * 2 >= prevTotal) {
                    // Previous player dumping this suit — keep tiles in it for chow potential
                    score -= 1.5f;
                }
            }

            // Next player retaining candidate's suit = dangerous to discard
            if (allPlayers[nextIdx]) {
                int nextSuitDisc[3] = {};
                int nextTotal = 0;
                for (const auto& d : allPlayers[nextIdx]->discards()) {
                    if (d.suit == Suit::Bamboo)          { nextSuitDisc[0]++; nextTotal++; }
                    else if (d.suit == Suit::Characters) { nextSuitDisc[1]++; nextTotal++; }
                    else if (d.suit == Suit::Dots)       { nextSuitDisc[2]++; nextTotal++; }
                }
                if (nextTotal >= 4 && nextSuitDisc[candidateSuitIdx] == 0) {
                    // Next player hasn't discarded this suit at all — likely collecting
                    int nextMelds = static_cast<int>(allPlayers[nextIdx]->hand().melds().size());
                    score -= 1.5f + nextMelds * 0.5f;
                }
            }
        }

        // Defensive score — graduated weight by distance from winning
        if (selfIndex >= 0) {
            float defWeight;
            if (currentShanten <= 1) defWeight = 0.3f;
            else if (currentShanten <= 3) defWeight = 0.5f;
            else defWeight = 0.8f;
            score += defensiveScore(dc.tile, allPlayers, selfIndex) * defWeight;
        }

        dc.strategicScore = score;
    }

    // --- Flush threat detection: opponent NOT discarding a suit + melds in it ---
    bool flushThreat[4][3] = {};
    bool anyFlushThreat = false;

    if (selfIndex >= 0) {
        for (int i = 0; i < static_cast<int>(allPlayers.size()); i++) {
            if (i == selfIndex || !allPlayers[i]) continue;
            const Player* opp = allPlayers[i];
            int suitDisc[3] = {};
            int totalNumbered = 0;

            for (const auto& d : opp->discards()) {
                if (d.suit == Suit::Bamboo)          { suitDisc[0]++; totalNumbered++; }
                else if (d.suit == Suit::Characters) { suitDisc[1]++; totalNumbered++; }
                else if (d.suit == Suit::Dots)       { suitDisc[2]++; totalNumbered++; }
            }

            int suitMeld[3] = {};
            for (const auto& m : opp->hand().melds()) {
                for (const auto& t : m.tiles) {
                    if (t.suit == Suit::Bamboo)          suitMeld[0]++;
                    else if (t.suit == Suit::Characters) suitMeld[1]++;
                    else if (t.suit == Suit::Dots)       suitMeld[2]++;
                }
            }

            int totalMelds = static_cast<int>(opp->hand().melds().size());
            for (int s = 0; s < 3; s++) {
                if (suitMeld[s] >= 6) {
                    flushThreat[i][s] = true;
                    anyFlushThreat = true;
                }
                if (totalMelds >= 3 && suitMeld[s] >= 3) {
                    flushThreat[i][s] = true;
                    anyFlushThreat = true;
                }
                if (totalNumbered >= 4 && suitDisc[s] <= 1 && suitMeld[s] >= 3) {
                    flushThreat[i][s] = true;
                    anyFlushThreat = true;
                }
            }
        }
    }

    // --- Danger level computation: estimate how dangerous each tile is ---
    // Detect opponents who look close to winning
    bool anyTenpai = false;
    if (selfIndex >= 0) {
        for (int i = 0; i < static_cast<int>(allPlayers.size()); i++) {
            if (i == selfIndex || !allPlayers[i]) continue;
            const Player* opp = allPlayers[i];
            int meldCount = static_cast<int>(opp->hand().melds().size());
            int concealedCount = opp->hand().concealedCount();
            if (meldCount >= 3 || (meldCount >= 2 && concealedCount <= 5)) {
                anyTenpai = true;
                break;
            }
        }
    }

    if (anyTenpai && selfIndex >= 0) {
        // Build visible tile counts once for kabe safety
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
        for (int i = 0; i < static_cast<int>(allPlayers.size()); i++) {
            if (!allPlayers[i]) continue;
            for (const auto& d : allPlayers[i]->discards()) {
                int idx = tileTypeIndex(d);
                if (idx >= 0) visible[idx]++;
            }
            if (i != selfIndex) {
                for (const auto& m : allPlayers[i]->hand().melds()) {
                    for (const auto& t : m.tiles) {
                        int idx = tileTypeIndex(t);
                        if (idx >= 0) visible[idx]++;
                    }
                }
            }
        }

        for (auto& dc : candidates) {
            float totalDanger = 0.0f;
            int tileIdx = tileTypeIndex(dc.tile);

            // Kabe: all 4 copies visible = safe against everyone
            if (tileIdx >= 0 && visible[tileIdx] >= 4) {
                dc.dangerLevel = 0.0f;
                continue;
            }

            for (int i = 0; i < static_cast<int>(allPlayers.size()); i++) {
                if (i == selfIndex || !allPlayers[i]) continue;
                const Player* opp = allPlayers[i];
                int meldCount = static_cast<int>(opp->hand().melds().size());
                int concealedCount = opp->hand().concealedCount();
                int discCount = static_cast<int>(opp->discards().size());

                // Compute opponent threat level
                float oppThreat = 0.0f;
                if (meldCount >= 3 && concealedCount <= 4) oppThreat = 1.0f;
                else if (meldCount >= 3) oppThreat = 0.7f;
                else if (meldCount >= 2 && concealedCount <= 5) oppThreat = 0.5f;
                else continue; // Not threatening

                // Safety dumping signal: last 3 discards all terminals/honors
                if (discCount >= 3) {
                    bool allSafe = true;
                    for (int d = discCount - 3; d < discCount; d++) {
                        if (!opp->discards()[d].isTerminalOrHonor()) {
                            allSafe = false;
                            break;
                        }
                    }
                    if (allSafe) oppThreat = std::min(1.0f, oppThreat + 0.3f);
                }

                // Genbutsu: opponent already discarded this tile type = safe
                bool genbutsu = false;
                for (const auto& d : opp->discards()) {
                    if (d.sameAs(dc.tile)) { genbutsu = true; break; }
                }
                if (genbutsu) continue;

                float tileDanger = 1.0f; // Base danger for non-genbutsu tile

                if (isNumberedSuit(dc.tile.suit)) {
                    int suitDisc[3] = {};
                    int totalNumbered = 0;
                    for (const auto& d : opp->discards()) {
                        if (d.suit == Suit::Bamboo)          { suitDisc[0]++; totalNumbered++; }
                        else if (d.suit == Suit::Characters) { suitDisc[1]++; totalNumbered++; }
                        else if (d.suit == Suit::Dots)       { suitDisc[2]++; totalNumbered++; }
                    }
                    int sIdx = static_cast<int>(dc.tile.suit);

                    // Opponent hasn't discarded this suit = likely collecting it
                    if (totalNumbered >= 4 && suitDisc[sIdx] == 0) {
                        tileDanger += 3.0f;
                    } else if (totalNumbered >= 4 &&
                               suitDisc[sIdx] * 3 < totalNumbered) {
                        tileDanger += 1.5f;
                    }

                    // Middle tiles (3-7) complete more sequences = more dangerous
                    if (dc.tile.rank >= 3 && dc.tile.rank <= 7)
                        tileDanger += 1.0f;
                    else if (dc.tile.rank == 2 || dc.tile.rank == 8)
                        tileDanger += 0.3f;

                    // Opponent has exposed melds in this suit = actively building it
                    int oppMeldCount = static_cast<int>(opp->hand().melds().size());
                    for (const auto& m : opp->hand().melds()) {
                        if (!m.tiles.empty() && m.tiles[0].suit == dc.tile.suit) {
                            float meldDanger = (oppMeldCount >= 3) ? 2.0f : 0.5f;
                            tileDanger += meldDanger;
                        }
                    }
                } else if (dc.tile.isHonor()) {
                    // Honor tiles: less dangerous in general, but check concealed count
                    if (concealedCount <= 4) tileDanger += 0.5f;
                }

                // Fewer remaining copies = more dangerous (opponent more likely to need it)
                if (tileIdx >= 0) {
                    int remaining = 4 - std::min(visible[tileIdx], 4);
                    if (remaining <= 1) tileDanger += 1.0f;
                    else if (remaining <= 2) tileDanger += 0.5f;
                }

                totalDanger += tileDanger * oppThreat;
            }

            dc.dangerLevel = totalDanger;
        }
    }

    // --- Flush threat danger: applies independently of tenpai detection ---
    if (anyFlushThreat && selfIndex >= 0) {
        for (auto& dc : candidates) {
            if (!isNumberedSuit(dc.tile.suit)) continue;
            int sIdx = static_cast<int>(dc.tile.suit);

            for (int i = 0; i < static_cast<int>(allPlayers.size()); i++) {
                if (i == selfIndex || !allPlayers[i]) continue;
                if (flushThreat[i][sIdx]) {
                    dc.dangerLevel += 10.0f;
                }
            }
        }
    }

    // Combined score sort: shanten is primary but strategic value and danger
    // can override small shanten differences. This allows keeping valuable tiles
    // (dragon pairs, flush tiles) and avoiding dangerous discards.
    float defMul = 0.0f;
    if (anyTenpai) {
        if (currentShanten >= 3)      defMul = 1.5f;
        else if (currentShanten >= 2) defMul = 1.0f;
        else if (currentShanten >= 1) defMul = 0.3f;
    }
    if (anyFlushThreat) {
        // Check if AI is building its own flush (9+ tiles of any single suit)
        bool selfFlush = false;
        if (selfIndex >= 0 && selfIndex < static_cast<int>(allPlayers.size()) && allPlayers[selfIndex]) {
            int selfSuit[3] = {};
            for (const auto& t : hand.concealed()) {
                if (t.suit == Suit::Bamboo)          selfSuit[0]++;
                else if (t.suit == Suit::Characters) selfSuit[1]++;
                else if (t.suit == Suit::Dots)       selfSuit[2]++;
            }
            for (const auto& m : hand.melds()) {
                for (const auto& t : m.tiles) {
                    if (t.suit == Suit::Bamboo)          selfSuit[0]++;
                    else if (t.suit == Suit::Characters) selfSuit[1]++;
                    else if (t.suit == Suit::Dots)       selfSuit[2]++;
                }
            }
            for (int s = 0; s < 3; s++) {
                if (selfSuit[s] >= 9) { selfFlush = true; break; }
            }
        }
        // Balance: full defense if not flush-building, half if also building a flush
        defMul = std::max(defMul, selfFlush ? 1.0f : 2.0f);
    }

    std::sort(candidates.begin(), candidates.end(),
        [defMul](const DiscardCandidate& a, const DiscardCandidate& b) {
            float scoreA = -a.shantenAfterDiscard * 10.0f
                           + a.strategicScore
                           - a.dangerLevel * defMul;
            float scoreB = -b.shantenAfterDiscard * 10.0f
                           + b.strategicScore
                           - b.dangerLevel * defMul;
            if (std::abs(scoreA - scoreB) > 0.01f) return scoreA > scoreB;
            return a.acceptCount > b.acceptCount;
        });

    return candidates;
}

float HandEvaluator::evaluateClaim(
    const Hand& hand, Tile claimedTile, ClaimType claimType,
    Wind seatWind, Wind prevailingWind, int currentShanten,
    const std::vector<const Player*>& allPlayers, int selfIndex) {

    float score = 0.0f;
    FaanPlan plan = analyzeFaanPotential(hand, seatWind, prevailingWind);

    // Never claim melds if pursuing Thirteen Orphans (must stay concealed)
    if (plan.aimThirteenOrphans && plan.thirteenOrphansShanten <= 4) {
        if (claimType != ClaimType::Kong) {
            return -10.0f;
        }
    }

    uint8_t seatRank = static_cast<uint8_t>(seatWind) + 1;
    uint8_t prevRank = static_cast<uint8_t>(prevailingWind) + 1;

    // Post-claim faan viability: can we still reach minimum 3 faan?
    // Applies to Pung and Chow (not Kong — kongs give replacement draw and are low-risk)
    if (claimType == ClaimType::Pung || claimType == ClaimType::Chow) {
        float maxFaan = 0.0f;

        // Guaranteed faan from existing exposed melds
        for (const auto& m : hand.melds()) {
            if (m.isPungOrKong() && m.suit() == Suit::Dragon) maxFaan += 1.0f;
            if (m.isPungOrKong() && m.suit() == Suit::Wind && m.rank() == seatRank) maxFaan += 1.0f;
            if (m.isPungOrKong() && m.suit() == Suit::Wind && m.rank() == prevRank) maxFaan += 1.0f;
        }

        // Faan from this new meld
        if (claimType == ClaimType::Pung) {
            if (claimedTile.suit == Suit::Dragon) maxFaan += 1.0f;
            if (claimedTile.suit == Suit::Wind && claimedTile.rank == seatRank) maxFaan += 1.0f;
            if (claimedTile.suit == Suit::Wind && claimedTile.rank == prevRank) maxFaan += 1.0f;
        }

        // Potential faan from concealed honor pairs (future pungs)
        for (uint8_t r = 1; r <= 3; r++) {
            if (hand.countTile(Suit::Dragon, r) >= 2) maxFaan += 1.0f;
        }
        if (hand.countTile(Suit::Wind, seatRank) >= 2) maxFaan += 1.0f;
        if (hand.countTile(Suit::Wind, prevRank) >= 2) maxFaan += 1.0f;

        // Flush potential
        if (plan.aimFullFlush) maxFaan += 6.0f;
        else if (plan.aimHalfFlush) {
            // Check if this claim is compatible with the flush
            if (!isNumberedSuit(claimedTile.suit) || claimedTile.suit == plan.flushSuit ||
                isHonorSuit(claimedTile.suit)) {
                maxFaan += 3.0f;
            }
            // Off-suit claim breaks flush — don't count it
        }

        // All Pungs potential
        if (plan.aimAllPungs && claimType == ClaimType::Pung) {
            bool allPungs = true;
            for (const auto& m : hand.melds()) {
                if (!m.isPungOrKong()) { allPungs = false; break; }
            }
            if (allPungs) maxFaan += 3.0f;
        }

        // All Chows potential: if claiming a Chow and all existing melds are chows
        if (claimType == ClaimType::Chow) {
            bool allChows = true;
            for (const auto& m : hand.melds()) {
                if (m.isPungOrKong()) { allChows = false; break; }
            }
            if (allChows) maxFaan += 1.0f;
        }

        // Self-draw always possible (+1 faan)
        maxFaan += 1.0f;

        // No flowers bonus
        if (hand.flowers().empty()) maxFaan += 1.0f;

        // Flower faan (正花: 1 faan per matching flower/season)
        int seatIdx = static_cast<int>(seatWind);
        int seatFlowerRank = seatIdx + 1;
        for (const auto& fl : hand.flowers()) {
            if (fl.suit == Suit::Flower && fl.rank == seatFlowerRank) maxFaan += 1.0f;
            if (fl.suit == Suit::Season && fl.rank == seatFlowerRank) maxFaan += 1.0f;
        }
        // All Flowers / All Seasons bonus (2 faan each)
        int flCount = 0, seCount = 0;
        for (const auto& fl : hand.flowers()) {
            if (fl.suit == Suit::Flower) flCount++;
            if (fl.suit == Suit::Season) seCount++;
        }
        if (flCount == 4) maxFaan += 2.0f;
        if (seCount == 4) maxFaan += 2.0f;

        // Faan viability: soft penalty for projected faan below 3.
        // Don't hard-reject — claims that reduce shanten are valuable even without
        // guaranteed faan, as the player may acquire faan-earning tiles after claiming.
        if (maxFaan < 3.0f) {
            float faanDeficit = 3.0f - maxFaan;
            score -= faanDeficit * 0.8f;  // Soft penalty (overcome by shanten improvement)
        }
    }

    // Opponent threat penalty for exposing melds (Pung/Chow only, not Kong)
    float oppThreatPenalty = 0.0f;
    if (claimType != ClaimType::Kong && selfIndex >= 0) {
        for (int i = 0; i < static_cast<int>(allPlayers.size()); i++) {
            if (i == selfIndex || !allPlayers[i]) continue;
            int meldCount = static_cast<int>(allPlayers[i]->hand().melds().size());
            int discCount = static_cast<int>(allPlayers[i]->discards().size());
            if (meldCount >= 3 || (meldCount >= 2 && discCount >= 8)) {
                oppThreatPenalty -= 0.25f;
            }
        }
    }

    if (claimType == ClaimType::Kong) {
        // Kong is almost always good: free meld + replacement draw
        int newShanten = ShantenCalculator::calculateAfterPung(hand,
            claimedTile.suit, claimedTile.rank);
        score += 5.0f; // Base kong bonus
        if (newShanten < currentShanten) score += 2.0f;

        // Penalty if it breaks flush plan
        if (plan.aimHalfFlush && isNumberedSuit(claimedTile.suit) &&
            claimedTile.suit != plan.flushSuit)
            score -= 2.0f;

        return score;
    }

    if (claimType == ClaimType::Pung) {
        int newShanten = ShantenCalculator::calculateAfterPung(hand,
            claimedTile.suit, claimedTile.rank);

        if (newShanten < currentShanten) score += 3.0f;
        else if (newShanten == currentShanten) score += 0.5f;

        // Honor pungs provide direct faan value
        if (claimedTile.suit == Suit::Dragon) score += 3.0f;
        if (claimedTile.suit == Suit::Wind &&
            (claimedTile.rank == seatRank || claimedTile.rank == prevRank))
            score += 2.5f;

        // Penalty if off-suit when pursuing flush
        if (plan.aimHalfFlush && isNumberedSuit(claimedTile.suit) &&
            claimedTile.suit != plan.flushSuit)
            score -= 3.0f;

        // Pung when close to winning is good
        if (currentShanten <= 2) score += 1.0f;

        score += oppThreatPenalty;
        return score;
    }

    if (claimType == ClaimType::Chow) {
        // Chow is the weakest claim - it breaks concealment and provides no faan
        // Only claim if it reduces shanten AND we have a faan path

        // Try all possible chow combinations to find best shanten
        int bestChowShanten = 99;
        if (isNumberedSuit(claimedTile.suit)) {
            uint8_t r = claimedTile.rank;
            Suit s = claimedTile.suit;

            // r-2, r-1, r
            if (r >= 3 && hand.hasTile(s, r - 2) && hand.hasTile(s, r - 1)) {
                int sh = ShantenCalculator::calculateAfterChow(hand, s, r, r - 2, r - 1);
                bestChowShanten = std::min(bestChowShanten, sh);
            }
            // r-1, r, r+1
            if (r >= 2 && r <= 8 && hand.hasTile(s, r - 1) && hand.hasTile(s, r + 1)) {
                int sh = ShantenCalculator::calculateAfterChow(hand, s, r, r - 1, r + 1);
                bestChowShanten = std::min(bestChowShanten, sh);
            }
            // r, r+1, r+2
            if (r <= 7 && hand.hasTile(s, r + 1) && hand.hasTile(s, r + 2)) {
                int sh = ShantenCalculator::calculateAfterChow(hand, s, r, r + 1, r + 2);
                bestChowShanten = std::min(bestChowShanten, sh);
            }
        }

        if (bestChowShanten < currentShanten) score += 3.0f;
        else if (bestChowShanten == currentShanten) score += 0.8f;

        // Penalty for off-suit chow when pursuing flush
        if (plan.aimHalfFlush && claimedTile.suit != plan.flushSuit) score -= 3.0f;

        // Small concealment penalty (chows don't give faan but speed up hand)
        score -= 0.1f;

        // Bonus if previous player is dumping this suit (more future chow opportunities)
        if (selfIndex >= 0 && static_cast<int>(allPlayers.size()) == 4) {
            int prevIdx = (selfIndex + 3) % 4;
            if (allPlayers[prevIdx] && isNumberedSuit(claimedTile.suit)) {
                int prevSuitDisc[3] = {};
                int prevTotal = 0;
                for (const auto& d : allPlayers[prevIdx]->discards()) {
                    if (d.suit == Suit::Bamboo)          { prevSuitDisc[0]++; prevTotal++; }
                    else if (d.suit == Suit::Characters) { prevSuitDisc[1]++; prevTotal++; }
                    else if (d.suit == Suit::Dots)       { prevSuitDisc[2]++; prevTotal++; }
                }
                int sIdx = static_cast<int>(claimedTile.suit);
                if (prevTotal >= 4 && prevSuitDisc[sIdx] * 2 >= prevTotal) {
                    score += 1.5f; // Previous player dumping this suit = more chows coming
                }
            }
        }

        score += oppThreatPenalty;
        return score;
    }

    return score;
}
