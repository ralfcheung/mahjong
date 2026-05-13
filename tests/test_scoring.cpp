// Scoring test suite: 26 tests covering limit hands, standard hands,
// context-based faan, and stacking rules.
//
// Build:  cmake --build build --target test_scoring
// Run:    ./build/test_scoring

#include "scoring/Scoring.h"
#include "scoring/WinDetector.h"
#include "player/Hand.h"
#include "player/Meld.h"
#include "tiles/Tile.h"
#include <cstdio>
#include <string>

// --- Helpers ---

static uint8_t nextId = 0;

static Tile T(Suit s, uint8_t r) {
    return Tile{s, r, nextId++};
}

static Meld makePung(Suit s, uint8_t r, bool exposed = false) {
    Meld m;
    m.type = MeldType::Pung;
    m.exposed = exposed;
    m.tiles = {T(s, r), T(s, r), T(s, r)};
    return m;
}

static Meld makeKong(Suit s, uint8_t r) {
    Meld m;
    m.type = MeldType::Kong;
    m.exposed = true;
    m.tiles = {T(s, r), T(s, r), T(s, r), T(s, r)};
    return m;
}

static bool hasPattern(const FaanResult& result, const char* substr) {
    for (const auto& e : result.breakdown) {
        if (e.nameEn.find(substr) != std::string::npos) return true;
    }
    return false;
}

static int passed = 0;
static int failed = 0;

static void check(bool condition, const char* testName, const FaanResult& result) {
    if (condition) {
        printf("  PASS  %s\n", testName);
        passed++;
    } else {
        printf("  FAIL  %s (got %d faan", testName, result.totalFaan);
        if (!result.breakdown.empty()) {
            printf(": ");
            for (size_t i = 0; i < result.breakdown.size(); i++) {
                if (i > 0) printf(" + ");
                printf("%s(%d)", result.breakdown[i].nameEn.c_str(), result.breakdown[i].faan);
            }
        }
        printf(")\n");
        failed++;
    }
}

// --- Limit Hands ---

// 1. Thirteen Orphans = 13 faan
static void testThirteenOrphans() {
    nextId = 0;
    Hand hand;
    hand.addTile(T(Suit::Bamboo, 1));
    hand.addTile(T(Suit::Bamboo, 9));
    hand.addTile(T(Suit::Characters, 1));
    hand.addTile(T(Suit::Characters, 9));
    hand.addTile(T(Suit::Dots, 1));
    hand.addTile(T(Suit::Dots, 9));
    hand.addTile(T(Suit::Wind, 1));
    hand.addTile(T(Suit::Wind, 2));
    hand.addTile(T(Suit::Wind, 3));
    hand.addTile(T(Suit::Wind, 4));
    hand.addTile(T(Suit::Dragon, 1));
    hand.addTile(T(Suit::Dragon, 2));
    hand.addTile(T(Suit::Dragon, 3));

    Tile win = T(Suit::Dragon, 1);
    WinContext ctx;
    FaanResult r = Scoring::calculate(hand, win, ctx);
    check(r.totalFaan == 13 && r.isLimit, "Thirteen Orphans = 13", r);
}

// 2. All Honors = 10 faan
static void testAllHonors() {
    nextId = 0;
    Hand hand;
    for (int i = 0; i < 3; i++) hand.addTile(T(Suit::Wind, 1));
    for (int i = 0; i < 3; i++) hand.addTile(T(Suit::Wind, 2));
    for (int i = 0; i < 3; i++) hand.addTile(T(Suit::Wind, 3));
    for (int i = 0; i < 3; i++) hand.addTile(T(Suit::Dragon, 1));
    hand.addTile(T(Suit::Dragon, 2));

    Tile win = T(Suit::Dragon, 2);
    WinContext ctx;
    FaanResult r = Scoring::calculate(hand, win, ctx);
    check(r.totalFaan == 10 && r.isLimit, "All Honors = 10", r);
}

// 3. All Terminals = 10 faan
static void testAllTerminals() {
    nextId = 0;
    Hand hand;
    for (int i = 0; i < 3; i++) hand.addTile(T(Suit::Bamboo, 1));
    for (int i = 0; i < 3; i++) hand.addTile(T(Suit::Bamboo, 9));
    for (int i = 0; i < 3; i++) hand.addTile(T(Suit::Characters, 1));
    for (int i = 0; i < 3; i++) hand.addTile(T(Suit::Characters, 9));
    hand.addTile(T(Suit::Dots, 1));

    Tile win = T(Suit::Dots, 1);
    WinContext ctx;
    FaanResult r = Scoring::calculate(hand, win, ctx);
    check(r.totalFaan == 10 && r.isLimit, "All Terminals = 10", r);
}

// 4. Nine Gates = 10 faan
static void testNineGates() {
    nextId = 0;
    Hand hand;
    // Bamboo: 1,1,1,2,3,4,5,6,7,8,9,9,9
    for (int i = 0; i < 3; i++) hand.addTile(T(Suit::Bamboo, 1));
    hand.addTile(T(Suit::Bamboo, 2));
    hand.addTile(T(Suit::Bamboo, 3));
    hand.addTile(T(Suit::Bamboo, 4));
    hand.addTile(T(Suit::Bamboo, 5));
    hand.addTile(T(Suit::Bamboo, 6));
    hand.addTile(T(Suit::Bamboo, 7));
    hand.addTile(T(Suit::Bamboo, 8));
    for (int i = 0; i < 3; i++) hand.addTile(T(Suit::Bamboo, 9));

    Tile win = T(Suit::Bamboo, 5);
    WinContext ctx;
    FaanResult r = Scoring::calculate(hand, win, ctx);
    check(r.totalFaan == 10 && r.isLimit, "Nine Gates = 10", r);
}

// 5. Green Hand = 13 faan
static void testGreenHand() {
    nextId = 0;
    Hand hand;
    // B2,B3,B4 chow x2 + B6 pung + B8 pung + Dr2 pair
    hand.addTile(T(Suit::Bamboo, 2));
    hand.addTile(T(Suit::Bamboo, 3));
    hand.addTile(T(Suit::Bamboo, 4));
    hand.addTile(T(Suit::Bamboo, 2));
    hand.addTile(T(Suit::Bamboo, 3));
    hand.addTile(T(Suit::Bamboo, 4));
    for (int i = 0; i < 3; i++) hand.addTile(T(Suit::Bamboo, 6));
    for (int i = 0; i < 3; i++) hand.addTile(T(Suit::Bamboo, 8));
    hand.addTile(T(Suit::Dragon, 2));

    Tile win = T(Suit::Dragon, 2);
    WinContext ctx;
    FaanResult r = Scoring::calculate(hand, win, ctx);
    check(r.totalFaan == 13 && r.isLimit, "Green Hand = 13", r);
}

// 6. Red Peacock = 13 faan
static void testRedPeacock() {
    nextId = 0;
    Hand hand;
    // B1 pung + B5 pung + B7 pung + B9 pung + Dr1 pair
    for (int i = 0; i < 3; i++) hand.addTile(T(Suit::Bamboo, 1));
    for (int i = 0; i < 3; i++) hand.addTile(T(Suit::Bamboo, 5));
    for (int i = 0; i < 3; i++) hand.addTile(T(Suit::Bamboo, 7));
    for (int i = 0; i < 3; i++) hand.addTile(T(Suit::Bamboo, 9));
    hand.addTile(T(Suit::Dragon, 1));

    Tile win = T(Suit::Dragon, 1);
    WinContext ctx;
    FaanResult r = Scoring::calculate(hand, win, ctx);
    check(r.totalFaan == 13 && r.isLimit, "Red Peacock = 13", r);
}

// 7. Blue Hand = 13 faan
static void testBlueHand() {
    nextId = 0;
    Hand hand;
    // W1 pung + W2 pung + W3 pung + Dr3 pung + D8 pair
    for (int i = 0; i < 3; i++) hand.addTile(T(Suit::Wind, 1));
    for (int i = 0; i < 3; i++) hand.addTile(T(Suit::Wind, 2));
    for (int i = 0; i < 3; i++) hand.addTile(T(Suit::Wind, 3));
    for (int i = 0; i < 3; i++) hand.addTile(T(Suit::Dragon, 3));
    hand.addTile(T(Suit::Dots, 8));

    Tile win = T(Suit::Dots, 8);
    WinContext ctx;
    FaanResult r = Scoring::calculate(hand, win, ctx);
    check(r.totalFaan == 13 && r.isLimit, "Blue Hand = 13", r);
}

// 8. Great Four Winds = 13 faan
static void testGreatFourWinds() {
    nextId = 0;
    Hand hand;
    // W1-W4 pungs + B1 pair (not all honors due to B1)
    for (int i = 0; i < 3; i++) hand.addTile(T(Suit::Wind, 1));
    for (int i = 0; i < 3; i++) hand.addTile(T(Suit::Wind, 2));
    for (int i = 0; i < 3; i++) hand.addTile(T(Suit::Wind, 3));
    for (int i = 0; i < 3; i++) hand.addTile(T(Suit::Wind, 4));
    hand.addTile(T(Suit::Bamboo, 1));

    Tile win = T(Suit::Bamboo, 1);
    WinContext ctx;
    FaanResult r = Scoring::calculate(hand, win, ctx);
    check(r.totalFaan == 13 && r.isLimit && hasPattern(r, "Great Four Winds"),
          "Great Four Winds = 13", r);
}

// 9. Four Kongs = 13 faan
static void testFourKongs() {
    nextId = 0;
    Hand hand;
    hand.addMeld(makeKong(Suit::Bamboo, 1));
    hand.addMeld(makeKong(Suit::Bamboo, 2));
    hand.addMeld(makeKong(Suit::Bamboo, 3));
    hand.addMeld(makeKong(Suit::Bamboo, 4));
    hand.addTile(T(Suit::Dragon, 1));

    Tile win = T(Suit::Dragon, 1);
    WinContext ctx;
    FaanResult r = Scoring::calculate(hand, win, ctx);
    check(r.totalFaan == 13 && r.isLimit, "Four Kongs = 13", r);
}

// --- High-Value Hands ---

// 10. Great Three Dragons = 8 faan (replaces individual dragon pungs)
static void testGreatThreeDragons() {
    nextId = 0;
    Hand hand;
    // Exposed dragon pungs + mixed suits to avoid Half Flush
    hand.addMeld(makePung(Suit::Dragon, 1, true));
    hand.addMeld(makePung(Suit::Dragon, 2, true));
    // Concealed: Dr3x3, B1,B2,B3, C5 (7 tiles)
    for (int i = 0; i < 3; i++) hand.addTile(T(Suit::Dragon, 3));
    hand.addTile(T(Suit::Bamboo, 1));
    hand.addTile(T(Suit::Bamboo, 2));
    hand.addTile(T(Suit::Bamboo, 3));
    hand.addTile(T(Suit::Characters, 5));

    Tile win = T(Suit::Characters, 5);
    WinContext ctx;
    FaanResult r = Scoring::calculate(hand, win, ctx);
    // Great Three Dragons(8) + No Flowers(1) = 9
    check(hasPattern(r, "Great Three Dragons") && r.totalFaan == 9,
          "Great Three Dragons = 8 (total 9)", r);
}

// 11. Little Three Dragons = 5 faan (3 base + 2 dragon pungs)
static void testLittleThreeDragons() {
    nextId = 0;
    Hand hand;
    hand.addMeld(makePung(Suit::Dragon, 1, true));
    hand.addMeld(makePung(Suit::Dragon, 2, true));
    // Concealed: B1,B2,B3, C4,C5,C6, Dr3 (7 tiles)
    hand.addTile(T(Suit::Bamboo, 1));
    hand.addTile(T(Suit::Bamboo, 2));
    hand.addTile(T(Suit::Bamboo, 3));
    hand.addTile(T(Suit::Characters, 4));
    hand.addTile(T(Suit::Characters, 5));
    hand.addTile(T(Suit::Characters, 6));
    hand.addTile(T(Suit::Dragon, 3));

    Tile win = T(Suit::Dragon, 3);
    WinContext ctx;
    hand.addFlower(T(Suit::Flower, 3)); // suppress No Flowers
    FaanResult r = Scoring::calculate(hand, win, ctx);
    // Dragon Pung(1) + Dragon Pung(1) + Little Three Dragons(3) = 5
    check(hasPattern(r, "Little Three Dragons") && r.totalFaan == 5,
          "Little Three Dragons = 5 (total 5)", r);
}

// 12. Little Four Winds = 6 faan
static void testLittleFourWinds() {
    nextId = 0;
    Hand hand;
    hand.addMeld(makePung(Suit::Wind, 1, true));
    hand.addMeld(makePung(Suit::Wind, 2, true));
    hand.addMeld(makePung(Suit::Wind, 3, true));
    // Concealed: B1,B2,B3, W4 (4 tiles)
    hand.addTile(T(Suit::Bamboo, 1));
    hand.addTile(T(Suit::Bamboo, 2));
    hand.addTile(T(Suit::Bamboo, 3));
    hand.addTile(T(Suit::Wind, 4));
    hand.addFlower(T(Suit::Flower, 3)); // suppress No Flowers

    Tile win = T(Suit::Wind, 4);
    WinContext ctx;
    ctx.seatWind = Wind::North;
    ctx.prevailingWind = Wind::North;
    FaanResult r = Scoring::calculate(hand, win, ctx);
    // Little Four Winds(6) only — no wind bonuses (N is in pair, not pung)
    check(hasPattern(r, "Little Four Winds") && r.totalFaan == 6,
          "Little Four Winds = 6", r);
}

// 13. Four Concealed Pongs = 8 faan (suppresses Concealed Hand + All Pongs)
static void testFourConcealedPongs() {
    nextId = 0;
    Hand hand;
    // All concealed pungs, self-drawn
    for (int i = 0; i < 3; i++) hand.addTile(T(Suit::Bamboo, 1));
    for (int i = 0; i < 3; i++) hand.addTile(T(Suit::Bamboo, 5));
    for (int i = 0; i < 3; i++) hand.addTile(T(Suit::Characters, 1));
    for (int i = 0; i < 3; i++) hand.addTile(T(Suit::Wind, 3));
    hand.addTile(T(Suit::Dragon, 2));

    Tile win = T(Suit::Dragon, 2);
    WinContext ctx;
    ctx.selfDrawn = true;
    ctx.seatWind = Wind::South;
    ctx.prevailingWind = Wind::South;
    FaanResult r = Scoring::calculate(hand, win, ctx);
    check(hasPattern(r, "Four Concealed Pongs") &&
          !hasPattern(r, "Concealed Hand") &&
          !hasPattern(r, "All Pongs"),
          "Four Concealed Pongs suppresses CH+AP", r);
}

// --- Standard Hands ---

// 14. All Pongs = 3 faan
static void testAllPongs() {
    nextId = 0;
    Hand hand;
    hand.addMeld(makePung(Suit::Bamboo, 1, true));
    hand.addMeld(makePung(Suit::Characters, 5, true));
    for (int i = 0; i < 3; i++) hand.addTile(T(Suit::Wind, 3));
    for (int i = 0; i < 3; i++) hand.addTile(T(Suit::Characters, 8));
    hand.addTile(T(Suit::Dots, 2));
    hand.addFlower(T(Suit::Flower, 3));

    Tile win = T(Suit::Dots, 2);
    WinContext ctx;
    ctx.seatWind = Wind::South;
    ctx.prevailingWind = Wind::South;
    FaanResult r = Scoring::calculate(hand, win, ctx);
    check(hasPattern(r, "All Pongs") && r.totalFaan == 3,
          "All Pongs = 3", r);
}

// 15. All Chows = 1 faan
static void testAllChows() {
    nextId = 0;
    Hand hand;
    // B1,B2,B3 + C4,C5,C6 + D1,D2,D3 + D7,D8,D9 + B5 pair
    hand.addTile(T(Suit::Bamboo, 1));
    hand.addTile(T(Suit::Bamboo, 2));
    hand.addTile(T(Suit::Bamboo, 3));
    hand.addTile(T(Suit::Characters, 4));
    hand.addTile(T(Suit::Characters, 5));
    hand.addTile(T(Suit::Characters, 6));
    hand.addTile(T(Suit::Dots, 1));
    hand.addTile(T(Suit::Dots, 2));
    hand.addTile(T(Suit::Dots, 3));
    hand.addTile(T(Suit::Dots, 7));
    hand.addTile(T(Suit::Dots, 8));
    hand.addTile(T(Suit::Dots, 9));
    hand.addTile(T(Suit::Bamboo, 5));

    Tile win = T(Suit::Bamboo, 5);
    WinContext ctx;
    FaanResult r = Scoring::calculate(hand, win, ctx);
    check(hasPattern(r, "All Chows"), "All Chows present", r);
}

// 16. Half Flush = 3 faan
static void testHalfFlush() {
    nextId = 0;
    Hand hand;
    hand.addMeld(makePung(Suit::Wind, 3, true));
    // B1-B9 straight + B5 pair
    hand.addTile(T(Suit::Bamboo, 1));
    hand.addTile(T(Suit::Bamboo, 2));
    hand.addTile(T(Suit::Bamboo, 3));
    hand.addTile(T(Suit::Bamboo, 4));
    hand.addTile(T(Suit::Bamboo, 5));
    hand.addTile(T(Suit::Bamboo, 6));
    hand.addTile(T(Suit::Bamboo, 7));
    hand.addTile(T(Suit::Bamboo, 8));
    hand.addTile(T(Suit::Bamboo, 9));
    hand.addTile(T(Suit::Bamboo, 5));

    Tile win = T(Suit::Bamboo, 5);
    WinContext ctx;
    ctx.seatWind = Wind::South;
    ctx.prevailingWind = Wind::South;
    FaanResult r = Scoring::calculate(hand, win, ctx);
    check(hasPattern(r, "Half Flush"), "Half Flush present", r);
}

// 17. Full Flush = 7 faan
static void testFullFlush() {
    nextId = 0;
    Hand hand;
    // B1 pung + B2,B3,B4 chow + B5,B6,B7 chow + B8 pung + B9 pair
    for (int i = 0; i < 3; i++) hand.addTile(T(Suit::Bamboo, 1));
    hand.addTile(T(Suit::Bamboo, 2));
    hand.addTile(T(Suit::Bamboo, 3));
    hand.addTile(T(Suit::Bamboo, 4));
    hand.addTile(T(Suit::Bamboo, 5));
    hand.addTile(T(Suit::Bamboo, 6));
    hand.addTile(T(Suit::Bamboo, 7));
    for (int i = 0; i < 3; i++) hand.addTile(T(Suit::Bamboo, 8));
    hand.addTile(T(Suit::Bamboo, 9));

    Tile win = T(Suit::Bamboo, 9);
    WinContext ctx;
    FaanResult r = Scoring::calculate(hand, win, ctx);
    check(hasPattern(r, "Full Flush"), "Full Flush present", r);
}

// 18. Concealed Hand = 1 faan
static void testConcealedHand() {
    nextId = 0;
    Hand hand;
    hand.addTile(T(Suit::Bamboo, 1));
    hand.addTile(T(Suit::Bamboo, 2));
    hand.addTile(T(Suit::Bamboo, 3));
    hand.addTile(T(Suit::Characters, 4));
    hand.addTile(T(Suit::Characters, 5));
    hand.addTile(T(Suit::Characters, 6));
    hand.addTile(T(Suit::Dots, 7));
    hand.addTile(T(Suit::Dots, 8));
    hand.addTile(T(Suit::Dots, 9));
    for (int i = 0; i < 3; i++) hand.addTile(T(Suit::Wind, 3));
    hand.addTile(T(Suit::Bamboo, 5));

    Tile win = T(Suit::Bamboo, 5);
    WinContext ctx;
    ctx.seatWind = Wind::South;
    ctx.prevailingWind = Wind::South;
    FaanResult r = Scoring::calculate(hand, win, ctx);
    check(hasPattern(r, "Concealed Hand"), "Concealed Hand present", r);
}

// 19. Mixed Terminals + All Pongs = 4 faan
static void testMixedTerminalsAllPongs() {
    nextId = 0;
    Hand hand;
    hand.addMeld(makePung(Suit::Bamboo, 1, true));
    hand.addMeld(makePung(Suit::Bamboo, 9, true));
    // Concealed: C1x3, W3x3, C9 (7 tiles)
    for (int i = 0; i < 3; i++) hand.addTile(T(Suit::Characters, 1));
    for (int i = 0; i < 3; i++) hand.addTile(T(Suit::Wind, 3));
    hand.addTile(T(Suit::Characters, 9));
    hand.addFlower(T(Suit::Flower, 3));

    Tile win = T(Suit::Characters, 9);
    WinContext ctx;
    ctx.seatWind = Wind::South;
    ctx.prevailingWind = Wind::South;
    FaanResult r = Scoring::calculate(hand, win, ctx);
    check(hasPattern(r, "Mixed Terminals") && hasPattern(r, "All Pongs") && r.totalFaan == 4,
          "Mixed Terminals + All Pongs = 4", r);
}

// --- Context-Based ---

// 20. Self-Draw = 1 faan
static void testSelfDraw() {
    nextId = 0;
    Hand hand;
    hand.addMeld(makePung(Suit::Dragon, 1, true));
    hand.addTile(T(Suit::Bamboo, 1));
    hand.addTile(T(Suit::Bamboo, 2));
    hand.addTile(T(Suit::Bamboo, 3));
    hand.addTile(T(Suit::Characters, 4));
    hand.addTile(T(Suit::Characters, 5));
    hand.addTile(T(Suit::Characters, 6));
    hand.addTile(T(Suit::Dots, 7));
    hand.addTile(T(Suit::Dots, 8));
    hand.addTile(T(Suit::Dots, 9));
    hand.addTile(T(Suit::Bamboo, 5));
    hand.addFlower(T(Suit::Flower, 3));

    Tile win = T(Suit::Bamboo, 5);
    WinContext ctx;
    ctx.selfDrawn = true;
    ctx.seatWind = Wind::South;
    ctx.prevailingWind = Wind::South;
    FaanResult r = Scoring::calculate(hand, win, ctx);
    check(hasPattern(r, "Self-Draw"), "Self-Draw present", r);
}

// 21. No Flowers = 1 faan
static void testNoFlowers() {
    nextId = 0;
    Hand hand;
    hand.addMeld(makePung(Suit::Dragon, 1, true));
    hand.addTile(T(Suit::Bamboo, 1));
    hand.addTile(T(Suit::Bamboo, 2));
    hand.addTile(T(Suit::Bamboo, 3));
    hand.addTile(T(Suit::Characters, 4));
    hand.addTile(T(Suit::Characters, 5));
    hand.addTile(T(Suit::Characters, 6));
    hand.addTile(T(Suit::Dots, 7));
    hand.addTile(T(Suit::Dots, 8));
    hand.addTile(T(Suit::Dots, 9));
    hand.addTile(T(Suit::Bamboo, 5));

    Tile win = T(Suit::Bamboo, 5);
    WinContext ctx;
    ctx.seatWind = Wind::South;
    ctx.prevailingWind = Wind::South;
    FaanResult r = Scoring::calculate(hand, win, ctx);
    check(hasPattern(r, "No Flowers"), "No Flowers present", r);
}

// 22. Seat Flower bonus = 1 faan
static void testSeatFlower() {
    nextId = 0;
    Hand hand;
    hand.addMeld(makePung(Suit::Dragon, 1, true));
    hand.addTile(T(Suit::Bamboo, 1));
    hand.addTile(T(Suit::Bamboo, 2));
    hand.addTile(T(Suit::Bamboo, 3));
    hand.addTile(T(Suit::Characters, 4));
    hand.addTile(T(Suit::Characters, 5));
    hand.addTile(T(Suit::Characters, 6));
    hand.addTile(T(Suit::Dots, 7));
    hand.addTile(T(Suit::Dots, 8));
    hand.addTile(T(Suit::Dots, 9));
    hand.addTile(T(Suit::Bamboo, 5));
    hand.addFlower(T(Suit::Flower, 1)); // seat flower for East

    Tile win = T(Suit::Bamboo, 5);
    WinContext ctx;
    ctx.seatWind = Wind::East;
    ctx.prevailingWind = Wind::South;
    FaanResult r = Scoring::calculate(hand, win, ctx);
    check(hasPattern(r, "Seat Flower"), "Seat Flower present", r);
}

// --- Stacking ---

// 23. All Pongs + Half Flush = 6 faan
static void testAllPongsHalfFlush() {
    nextId = 0;
    Hand hand;
    hand.addMeld(makePung(Suit::Bamboo, 1, true));
    hand.addMeld(makePung(Suit::Bamboo, 5, true));
    for (int i = 0; i < 3; i++) hand.addTile(T(Suit::Bamboo, 9));
    for (int i = 0; i < 3; i++) hand.addTile(T(Suit::Wind, 3));
    hand.addTile(T(Suit::Bamboo, 3));
    hand.addFlower(T(Suit::Flower, 3));

    Tile win = T(Suit::Bamboo, 3);
    WinContext ctx;
    ctx.seatWind = Wind::South;
    ctx.prevailingWind = Wind::South;
    FaanResult r = Scoring::calculate(hand, win, ctx);
    check(hasPattern(r, "All Pongs") && hasPattern(r, "Half Flush") && r.totalFaan == 6,
          "All Pongs + Half Flush = 6", r);
}

// 24. Dragon Pung + Self-Draw + No Flowers = 3 faan (minimum to win)
static void testMinimumWin() {
    nextId = 0;
    Hand hand;
    hand.addMeld(makePung(Suit::Dragon, 1, true));
    hand.addTile(T(Suit::Bamboo, 1));
    hand.addTile(T(Suit::Bamboo, 2));
    hand.addTile(T(Suit::Bamboo, 3));
    hand.addTile(T(Suit::Characters, 4));
    hand.addTile(T(Suit::Characters, 5));
    hand.addTile(T(Suit::Characters, 6));
    hand.addTile(T(Suit::Dots, 7));
    hand.addTile(T(Suit::Dots, 8));
    hand.addTile(T(Suit::Dots, 9));
    hand.addTile(T(Suit::Bamboo, 5));

    Tile win = T(Suit::Bamboo, 5);
    WinContext ctx;
    ctx.selfDrawn = true;
    ctx.seatWind = Wind::South;
    ctx.prevailingWind = Wind::South;
    FaanResult r = Scoring::calculate(hand, win, ctx);
    // Dragon Pung(1) + Self-Draw(1) + No Flowers(1) = 3
    check(r.totalFaan == 3, "Minimum win = 3 faan", r);
}

// 25. Full Flush + All Pongs = 10 faan
static void testFullFlushAllPongs() {
    nextId = 0;
    Hand hand;
    hand.addMeld(makePung(Suit::Bamboo, 1, true));
    hand.addMeld(makePung(Suit::Bamboo, 3, true));
    for (int i = 0; i < 3; i++) hand.addTile(T(Suit::Bamboo, 5));
    for (int i = 0; i < 3; i++) hand.addTile(T(Suit::Bamboo, 7));
    hand.addTile(T(Suit::Bamboo, 9));
    hand.addFlower(T(Suit::Flower, 3));

    Tile win = T(Suit::Bamboo, 9);
    WinContext ctx;
    FaanResult r = Scoring::calculate(hand, win, ctx);
    check(hasPattern(r, "Full Flush") && hasPattern(r, "All Pongs") && r.totalFaan == 10,
          "Full Flush + All Pongs = 10", r);
}

// 26. Little Four Winds does NOT stack with Half Flush
static void testLittleFourWindsNoHalfFlush() {
    nextId = 0;
    Hand hand;
    hand.addMeld(makePung(Suit::Wind, 1, true));
    hand.addMeld(makePung(Suit::Wind, 2, true));
    hand.addMeld(makePung(Suit::Wind, 3, true));
    hand.addTile(T(Suit::Bamboo, 1));
    hand.addTile(T(Suit::Bamboo, 2));
    hand.addTile(T(Suit::Bamboo, 3));
    hand.addTile(T(Suit::Wind, 4));
    hand.addFlower(T(Suit::Flower, 3));

    Tile win = T(Suit::Wind, 4);
    WinContext ctx;
    ctx.seatWind = Wind::North;
    ctx.prevailingWind = Wind::North;
    FaanResult r = Scoring::calculate(hand, win, ctx);
    check(hasPattern(r, "Little Four Winds") && !hasPattern(r, "Half Flush"),
          "Little Four Winds suppresses Half Flush", r);
}

// --- Main ---

int main() {
    printf("=== Scoring Test Suite ===\n\n");

    printf("Limit Hands:\n");
    testThirteenOrphans();
    testAllHonors();
    testAllTerminals();
    testNineGates();
    testGreenHand();
    testRedPeacock();
    testBlueHand();
    testGreatFourWinds();
    testFourKongs();

    printf("\nHigh-Value Hands:\n");
    testGreatThreeDragons();
    testLittleThreeDragons();
    testLittleFourWinds();
    testFourConcealedPongs();

    printf("\nStandard Hands:\n");
    testAllPongs();
    testAllChows();
    testHalfFlush();
    testFullFlush();
    testConcealedHand();
    testMixedTerminalsAllPongs();

    printf("\nContext-Based:\n");
    testSelfDraw();
    testNoFlowers();
    testSeatFlower();

    printf("\nStacking:\n");
    testAllPongsHalfFlush();
    testMinimumWin();
    testFullFlushAllPongs();
    testLittleFourWindsNoHalfFlush();

    printf("\n=== Results: %d passed, %d failed out of %d ===\n",
           passed, failed, passed + failed);

    return failed > 0 ? 1 : 0;
}
