#include "ShantenCalculator.h"
#include <algorithm>

ShantenCalculator::Counts ShantenCalculator::buildCounts(const Hand& hand) {
    Counts counts;
    counts.existingMelds = static_cast<int>(hand.melds().size());

    for (const auto& t : hand.concealed()) {
        int group = -1;
        int rank0 = t.rank - 1;

        switch (t.suit) {
            case Suit::Bamboo:     group = 0; break;
            case Suit::Characters: group = 1; break;
            case Suit::Dots:       group = 2; break;
            case Suit::Wind:       group = 3; break;
            case Suit::Dragon:     group = 3; rank0 = t.rank - 1 + 4; break;
            default: continue;
        }
        if (group >= 0 && rank0 >= 0 && rank0 < 9) {
            counts.c[group][rank0]++;
        }
    }

    return counts;
}

// Combined solver that processes all 4 suit groups sequentially in one recursion.
struct CombinedSolver {
    int g[4][9];
    int meldsNeeded;
    int bestShanten;

    void solve() {
        solveSuitGroup(0, 0, 0, 0, false);
    }

    void solveSuitGroup(int gi, int pos, int melds, int partials, bool hasJantai) {
        // Pruning: can we possibly beat bestShanten?
        int effectivePartials = std::min(partials, meldsNeeded - melds);
        if (effectivePartials < 0) effectivePartials = 0;
        int lowerBound = (meldsNeeded - melds) * 2 - effectivePartials - (hasJantai ? 1 : 0);
        if (lowerBound >= bestShanten) return;

        // Skip empty positions
        while (pos <= 8 && g[gi][pos] == 0) pos++;

        if (pos > 8) {
            // Finished this suit group - advance to next
            if (gi < 2) {
                solveSuitGroup(gi + 1, 0, melds, partials, hasJantai);
            } else if (gi == 2) {
                solveHonorGroup(0, melds, partials, hasJantai);
            }
            return;
        }

        // Try pung (3 of same rank)
        if (g[gi][pos] >= 3) {
            g[gi][pos] -= 3;
            solveSuitGroup(gi, pos, melds + 1, partials, hasJantai);
            g[gi][pos] += 3;
        }

        // Try chow (pos, pos+1, pos+2)
        if (pos <= 6 && g[gi][pos + 1] >= 1 && g[gi][pos + 2] >= 1) {
            g[gi][pos]--; g[gi][pos + 1]--; g[gi][pos + 2]--;
            solveSuitGroup(gi, pos, melds + 1, partials, hasJantai);
            g[gi][pos]++; g[gi][pos + 1]++; g[gi][pos + 2]++;
        }

        // Try pair as jantai (eyes)
        if (g[gi][pos] >= 2 && !hasJantai) {
            g[gi][pos] -= 2;
            solveSuitGroup(gi, pos, melds, partials, true);
            g[gi][pos] += 2;
        }

        // Try pair as partial meld
        if (g[gi][pos] >= 2) {
            g[gi][pos] -= 2;
            solveSuitGroup(gi, pos, melds, partials + 1, hasJantai);
            g[gi][pos] += 2;
        }

        // Try adjacent partial (pos, pos+1)
        if (pos <= 7 && g[gi][pos + 1] >= 1) {
            g[gi][pos]--; g[gi][pos + 1]--;
            solveSuitGroup(gi, pos, melds, partials + 1, hasJantai);
            g[gi][pos]++; g[gi][pos + 1]++;
        }

        // Try gap partial (pos, pos+2)
        if (pos <= 6 && g[gi][pos + 2] >= 1) {
            g[gi][pos]--; g[gi][pos + 2]--;
            solveSuitGroup(gi, pos, melds, partials + 1, hasJantai);
            g[gi][pos]++; g[gi][pos + 2]++;
        }

        // Leave as isolated (skip all remaining copies at this position)
        {
            int saved = g[gi][pos];
            g[gi][pos] = 0;
            solveSuitGroup(gi, pos + 1, melds, partials, hasJantai);
            g[gi][pos] = saved;
        }
    }

    void solveHonorGroup(int pos, int melds, int partials, bool hasJantai) {
        // Pruning
        int effectivePartials = std::min(partials, meldsNeeded - melds);
        if (effectivePartials < 0) effectivePartials = 0;
        int lowerBound = (meldsNeeded - melds) * 2 - effectivePartials - (hasJantai ? 1 : 0);
        if (lowerBound >= bestShanten) return;

        // Skip empty positions
        while (pos <= 6 && g[3][pos] == 0) pos++;

        if (pos > 6) {
            // All groups done - compute final shanten
            int usable = std::min(partials, meldsNeeded - melds);
            if (usable < 0) usable = 0;
            int s = (meldsNeeded - melds) * 2 - usable - (hasJantai ? 1 : 0);
            bestShanten = std::min(bestShanten, s);
            return;
        }

        // Try pung
        if (g[3][pos] >= 3) {
            g[3][pos] -= 3;
            solveHonorGroup(pos, melds + 1, partials, hasJantai);
            g[3][pos] += 3;
        }

        // Try pair as jantai
        if (g[3][pos] >= 2 && !hasJantai) {
            g[3][pos] -= 2;
            solveHonorGroup(pos, melds, partials, true);
            g[3][pos] += 2;
        }

        // Try pair as partial
        if (g[3][pos] >= 2) {
            g[3][pos] -= 2;
            solveHonorGroup(pos, melds, partials + 1, hasJantai);
            g[3][pos] += 2;
        }

        // Skip (isolated honor)
        {
            int saved = g[3][pos];
            g[3][pos] = 0;
            solveHonorGroup(pos + 1, melds, partials, hasJantai);
            g[3][pos] = saved;
        }
    }
};

int ShantenCalculator::calcFromCounts(const Counts& counts) {
    int meldsNeeded = 4 - counts.existingMelds;

    CombinedSolver solver;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 9; j++)
            solver.g[i][j] = counts.c[i][j];
    solver.meldsNeeded = meldsNeeded;
    solver.bestShanten = meldsNeeded * 2; // worst case
    solver.solve();

    return solver.bestShanten;
}

int ShantenCalculator::calcSpecialShanten(const Counts& counts) {
    int best = 999;

    // Thirteen Orphans: shanten = 13 - distinctTypes - hasDuplicate
    struct TypeSpec { int g; int r; };
    static constexpr TypeSpec types[] = {
        {0, 0}, {0, 8}, {1, 0}, {1, 8}, {2, 0}, {2, 8},
        {3, 0}, {3, 1}, {3, 2}, {3, 3}, {3, 4}, {3, 5}, {3, 6}
    };
    int distinct = 0;
    bool hasDup = false;
    for (auto& t : types) {
        if (counts.c[t.g][t.r] >= 1) distinct++;
        if (counts.c[t.g][t.r] >= 2) hasDup = true;
    }
    best = std::min(best, 13 - distinct - (hasDup ? 1 : 0));

    return best;
}

int ShantenCalculator::calculate(const Hand& hand) {
    Counts counts = buildCounts(hand);
    int shanten = calcFromCounts(counts);

    if (counts.existingMelds == 0) {
        shanten = std::min(shanten, calcSpecialShanten(counts));
    }

    return shanten;
}

int ShantenCalculator::calculateAfterDiscard(const Hand& hand, Suit suit, uint8_t rank) {
    Counts counts = buildCounts(hand);

    int group = -1;
    int rank0 = rank - 1;
    switch (suit) {
        case Suit::Bamboo:     group = 0; break;
        case Suit::Characters: group = 1; break;
        case Suit::Dots:       group = 2; break;
        case Suit::Wind:       group = 3; break;
        case Suit::Dragon:     group = 3; rank0 = rank - 1 + 4; break;
        default: return 99;
    }

    if (counts.c[group][rank0] <= 0) return 99;
    counts.c[group][rank0]--;

    int shanten = calcFromCounts(counts);
    if (counts.existingMelds == 0) {
        shanten = std::min(shanten, calcSpecialShanten(counts));
    }
    return shanten;
}

int ShantenCalculator::calculateAfterPung(const Hand& hand, Suit suit, uint8_t rank) {
    Counts counts = buildCounts(hand);

    int group = -1;
    int rank0 = rank - 1;
    switch (suit) {
        case Suit::Bamboo:     group = 0; break;
        case Suit::Characters: group = 1; break;
        case Suit::Dots:       group = 2; break;
        case Suit::Wind:       group = 3; break;
        case Suit::Dragon:     group = 3; rank0 = rank - 1 + 4; break;
        default: return 99;
    }

    if (counts.c[group][rank0] < 2) return 99;
    counts.c[group][rank0] -= 2;
    counts.existingMelds++;

    return calcFromCounts(counts);
}

int ShantenCalculator::calculateAfterChow(const Hand& hand, Suit claimedSuit,
                                           uint8_t /*claimedRank*/,
                                           uint8_t handRank1, uint8_t handRank2) {
    Counts counts = buildCounts(hand);

    int group = -1;
    switch (claimedSuit) {
        case Suit::Bamboo:     group = 0; break;
        case Suit::Characters: group = 1; break;
        case Suit::Dots:       group = 2; break;
        default: return 99;
    }

    int r1 = handRank1 - 1;
    int r2 = handRank2 - 1;
    if (counts.c[group][r1] < 1 || counts.c[group][r2] < 1) return 99;
    counts.c[group][r1]--;
    counts.c[group][r2]--;
    counts.existingMelds++;

    return calcFromCounts(counts);
}

int ShantenCalculator::countAcceptanceAfterDiscard(const Hand& hand, Suit discardSuit, uint8_t discardRank) {
    Counts counts = buildCounts(hand);

    // Remove the discarded tile
    int group = -1;
    int rank0 = discardRank - 1;
    switch (discardSuit) {
        case Suit::Bamboo:     group = 0; break;
        case Suit::Characters: group = 1; break;
        case Suit::Dots:       group = 2; break;
        case Suit::Wind:       group = 3; break;
        case Suit::Dragon:     group = 3; rank0 = discardRank - 1 + 4; break;
        default: return 0;
    }
    if (counts.c[group][rank0] <= 0) return 0;
    counts.c[group][rank0]--;

    // Calculate shanten of this reduced hand
    int baseShanten = calcFromCounts(counts);
    if (counts.existingMelds == 0) {
        baseShanten = std::min(baseShanten, calcSpecialShanten(counts));
    }

    // Try adding each of the 34 tile types and check if shanten improves
    int acceptance = 0;

    // Numbered suits: groups 0-2, ranks 0-8
    for (int g = 0; g < 3; g++) {
        for (int r = 0; r < 9; r++) {
            if (counts.c[g][r] >= 4) continue; // Can't draw a 5th copy
            counts.c[g][r]++;
            int newShanten = calcFromCounts(counts);
            if (counts.existingMelds == 0) {
                newShanten = std::min(newShanten, calcSpecialShanten(counts));
            }
            if (newShanten < baseShanten) acceptance++;
            counts.c[g][r]--;
        }
    }

    // Honors: group 3, ranks 0-6 (winds 0-3, dragons 4-6)
    for (int r = 0; r < 7; r++) {
        if (counts.c[3][r] >= 4) continue;
        counts.c[3][r]++;
        int newShanten = calcFromCounts(counts);
        if (counts.existingMelds == 0) {
            newShanten = std::min(newShanten, calcSpecialShanten(counts));
        }
        if (newShanten < baseShanten) acceptance++;
        counts.c[3][r]--;
    }

    return acceptance;
}

// how many of the 34 tile types would improve my hand if I drew one
// 1. Compute baseShanten for the current hand
// 2. Loop through all 34 tile types (27 numbered + 4 winds + 3 dragons)
// 3. For each: temporarily add one copy to the counts, recompute shanten
// 4. If newShanten < baseShanten → that tile type is an "acceptance" (useful draw)
// 5. Remove it, try the next
//
// So if your hand is 3-shanten, it checks which tiles would bring you to 2-shanten. A hand with acceptance=15 has many paths to improve;
// acceptance=2 means you're stuck waiting for specific tiles.
//
// This is why it's expensive: 34 calls to calcFromCounts(), each doing the full recursive backtracking decomposition.
int ShantenCalculator::countAcceptance(const Hand& hand) {
    Counts counts = buildCounts(hand);

    int baseShanten = calcFromCounts(counts);
    if (counts.existingMelds == 0) {
        baseShanten = std::min(baseShanten, calcSpecialShanten(counts));
    }

    int acceptance = 0;

    for (int g = 0; g < 3; g++) {
        for (int r = 0; r < 9; r++) {
            if (counts.c[g][r] >= 4) continue;
            counts.c[g][r]++;
            int newShanten = calcFromCounts(counts);
            if (counts.existingMelds == 0) {
                newShanten = std::min(newShanten, calcSpecialShanten(counts));
            }
            if (newShanten < baseShanten) acceptance++;
            counts.c[g][r]--;
        }
    }

    for (int r = 0; r < 7; r++) {
        if (counts.c[3][r] >= 4) continue;
        counts.c[3][r]++;
        int newShanten = calcFromCounts(counts);
        if (counts.existingMelds == 0) {
            newShanten = std::min(newShanten, calcSpecialShanten(counts));
        }
        if (newShanten < baseShanten) acceptance++;
        counts.c[3][r]--;
    }

    return acceptance;
}
