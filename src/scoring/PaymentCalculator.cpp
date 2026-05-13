#include "PaymentCalculator.h"

PaymentResult PaymentCalculator::calculate(int winnerIndex, int discarderIndex,
                                            int faan, int dealerIndex) {
    PaymentResult result;
    int basePoints = Scoring::faanToPoints(faan);
    bool selfDrawn = (discarderIndex < 0);

    for (int i = 0; i < 4; i++) {
        if (i == winnerIndex) continue;

        int amount = basePoints;

        if (selfDrawn) {
            // Self-draw: all opponents pay 2x
            amount *= 2;
        } else if (i == discarderIndex) {
            // Discarder pays 2x
            amount *= 2;
        }

        // Dealer multiplier: if winner or payer is dealer, double
        if (winnerIndex == dealerIndex || i == dealerIndex) {
            amount *= 2;
        }

        result.payments[i] = amount;
    }

    // Winner receives the sum
    int totalReceived = 0;
    for (int i = 0; i < 4; i++) {
        totalReceived += result.payments[i];
    }
    result.payments[winnerIndex] = -totalReceived;

    return result;
}
