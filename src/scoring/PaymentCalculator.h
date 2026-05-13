#pragma once
#include "Scoring.h"
#include "tiles/TileEnums.h"
#include <array>

struct PaymentResult {
    // How much each player pays (positive) or receives (negative)
    // Index corresponds to player seat index
    std::array<int, 4> payments = {};  // Positive = pay, negative = receive
};

class PaymentCalculator {
public:
    // Calculate payments for a won round
    // winnerIndex: seat index of winner
    // discarderIndex: seat index of discarder (-1 if self-drawn)
    // faan: total faan of the winning hand
    // dealerIndex: who is dealer (pays/receives double)
    static PaymentResult calculate(int winnerIndex, int discarderIndex,
                                   int faan, int dealerIndex);
};
