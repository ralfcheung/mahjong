# Hong Kong Mahjong - Architecture Documentation

## Table of Contents

1. [Overview](#overview)
2. [Build System](#build-system)
3. [Directory Structure](#directory-structure)
4. [Module Reference](#module-reference)
   - [Entry Point](#entry-point)
   - [Game Core](#game-core-srcgame)
   - [Tile System](#tile-system-srctiles)
   - [Player System](#player-system-srcplayer)
   - [Scoring System](#scoring-system-srcscoring)
   - [AI System](#ai-system-srcai)
   - [Rendering](#rendering-srcrender)
5. [Data Flow](#data-flow)
6. [Game Loop](#game-loop)
7. [Turn State Machine](#turn-state-machine)
8. [HK Mahjong Rules Reference](#hk-mahjong-rules-reference)
9. [Neural Network AI](#neural-network-ai)
10. [Dependency Graph](#dependency-graph)
11. [Discard & Claim Decision Pipeline](#discard--claim-decision-pipeline)
12. [AI Improvement Roadmap](#ai-improvement-roadmap)

---

## Overview

A 3D Hong Kong Mahjong game written in C++17. One human player faces three AI opponents on a felt-covered table rendered with Raylib. The game implements standard HK rules (minimum 3 faan, 13-faan limit hands) with bilingual Chinese/English UI.

**Tech stack:**
- **Language:** C++17
- **Graphics:** Raylib 5.5 (fetched at build time)
- **AI (optional):** libtorch (PyTorch C++ frontend) for Double DQN reinforcement learning
- **Build:** CMake 3.14+

**Key design patterns:**
- **RenderState snapshot** — The renderer never reads game state directly. Each frame, `Game::buildRenderState()` produces an immutable snapshot that the renderer consumes.
- **Callback-based turns** — Player actions use `std::function` callbacks. AI resolves immediately; human stores the callback until UI input arrives.
- **Observer pattern** — TurnManager fires discard/claim/roundEnd observers for neural network training data collection.
- **Optional compilation** — All neural network code is guarded by `#ifdef HAS_TORCH`.

---

## Build System

### CMakeLists.txt

```
cmake_minimum_required(VERSION 3.14)
project(mahjong CXX)    # C++17 required
```

**Dependencies:**
| Dependency | Source | Required |
|---|---|---|
| Raylib 5.5 | FetchContent (git) | Yes |
| libtorch 2.5.1 | `third_party/libtorch/` | No |

**Key compile definitions:**
- `ASSETS_PATH` — Absolute path to `assets/` directory (set by CMake)
- `HAS_TORCH=1` — Defined when libtorch is found

**Build commands:**
```bash
cmake -B build && cmake --build build    # Build
./build/mahjong                          # Run
./scripts/download_libtorch.sh           # Optional: enable neural AI
```

---

## Directory Structure

```
mahjong/
├── CMakeLists.txt
├── docs/
│   └── ARCHITECTURE.md          ← This file
├── assets/
│   ├── fonts/
│   │   └── NotoSansTC-Regular.ttf   # Arial Unicode MS (renamed for compat)
│   ├── model/                        # Trained neural network weights
│   │   ├── discard_net.pt
│   │   ├── claim_net.pt
│   │   ├── replay_buffer.bin
│   │   └── training_stats.json
│   └── tiles/                        # Tile SVG assets (unused — procedural now)
├── scripts/
│   └── download_libtorch.sh          # Downloads libtorch for macOS/Linux
├── third_party/
│   └── libtorch/                     # Optional PyTorch C++ library
└── src/
    ├── main.cpp                      # Entry point
    ├── game/
    │   ├── Game.h / Game.cpp         # Top-level game class, main loop
    │   ├── GameState.h               # Phase enum, ClaimType, ClaimOption
    │   └── TurnManager.h / .cpp      # Round state machine
    ├── tiles/
    │   ├── Tile.h                    # Tile struct (suit, rank, id)
    │   ├── TileEnums.h               # Suit, Wind enums + helpers
    │   ├── TileDefs.h                # Chinese/English character mappings
    │   └── Wall.h / Wall.cpp         # 144-tile wall, shuffle, draw
    ├── player/
    │   ├── Player.h                  # Abstract base player
    │   ├── HumanPlayer.h / .cpp      # Stores callbacks for UI input
    │   ├── AIPlayer.h / .cpp         # Heuristic + neural network AI
    │   ├── Hand.h / Hand.cpp         # Concealed tiles + melds + flowers
    │   └── Meld.h                    # MeldType enum, Meld struct
    ├── scoring/
    │   ├── WinDetector.h / .cpp      # Win decomposition + special hands
    │   ├── Scoring.h / Scoring.cpp   # Faan calculation (27 patterns)
    │   └── PaymentCalculator.h / .cpp # Point-to-payment conversion
    ├── ai/
    │   ├── HandEvaluator.h / .cpp    # Heuristic discard/claim evaluation
    │   ├── ShantenCalculator.h / .cpp # Distance-to-win calculation
    │   ├── MahjongNet.h / .cpp       # DiscardNet + ClaimNet (libtorch)
    │   ├── DQNTrainer.h / .cpp       # Double DQN training loop
    │   └── RLFeatures.h              # 408/450-dim feature extraction
    └── render/
        ├── Renderer.h / Renderer.cpp       # Scene rendering, UI overlay
        ├── TileRenderer.h / .cpp           # 3D tile drawing primitives
        └── TileTextureAtlas.h / .cpp       # Procedural tile face textures
```

---

## Module Reference

### Entry Point

**`src/main.cpp`**

```
main() → Game::init() → Game::run() → Game::shutdown()
```

Minimal entry point. All logic lives in `Game`.

---

### Game Core (`src/game/`)

#### `Game` — Top-level orchestrator

| Member | Purpose |
|---|---|
| `players_[4]` | `unique_ptr<Player>`: index 0 = human, 1-3 = AI |
| `turnManager_` | Manages current round's state machine |
| `renderer_` | Owns the Raylib rendering pipeline |
| `prevailingWind_` | Rotates every 4 rounds (East → South → West → North) |
| `dealerIndex_` | Rotates each round (0 → 1 → 2 → 3 → 0...) |
| `roundNumber_` | 0-15, game ends after 16 rounds |

**Main loop** (`run()`): `handleInput()` → `update(dt)` → `render()` at 60 FPS.

**Camera:** Orbit camera controlled by mouse (right-drag = orbit, scroll = zoom, middle-drag = pan, WASD = pan, R = reset). State: yaw, pitch, distance, target point.

**Neural AI integration** (ifdef HAS_TORCH):
- `initNeuralAI()` — Loads models, assigns trainer to AI players
- `wireTrainerObservers()` — Connects TurnManager observers to DQNTrainer
- `onRoundComplete()` — Saves models, runs 20 self-play games between rounds

#### `GameState.h` — Enums and structs

**`GamePhase`** — 12-state enum:
```
DEALING → REPLACING_FLOWERS → PLAYER_DRAW → PLAYER_TURN →
DISCARD_ANIMATION → CLAIM_PHASE → CLAIM_RESOLUTION →
MELD_FORMATION → REPLACEMENT_DRAW → SCORING → ROUND_END → GAME_OVER
```

**`ClaimType`** — `None | Chow | Pung | Kong | Win`

**`ClaimOption`** — `{type, playerIndex, meldTiles[]}`. The `meldTiles` vector carries the specific hand tiles used in a chow combo, enabling the player to choose between multiple valid sequences.

#### `TurnManager` — Round state machine

Owns the `Wall` and drives all game phases for a single round. Created fresh each round by `Game::setupTurnManager()`.

**Public interface for human input:**
| Method | When called |
|---|---|
| `onHumanClaim(ClaimOption)` | Human clicks a claim button |
| `onHumanSelfKong(suit, rank)` | Human declares a self-kong |
| `onHumanSelfDrawWin()` | Human declares tsumo |

**Query methods:**
| Method | Returns |
|---|---|
| `getHumanClaimOptions()` | `vector<ClaimType>` — flat list of available claims |
| `getHumanClaimOptionsWithCombos()` | `vector<ClaimOption>` — expanded with per-chow-combo entries |
| `getHumanSelfKongOptions()` | List of tiles eligible for concealed/promoted kong |
| `canHumanSelfDrawWin()` | Whether the drawn tile completes a winning hand |

**Observer hooks:**
```cpp
setDiscardObserver(fn(playerIndex, tile, turnCount, wallRemaining))
setClaimObserver(fn(playerIndex, claimType, tile, turnCount, wallRemaining))
setRoundEndObserver(fn(winnerIndex, selfDrawn, faanResult, isDraw))
```

**Claim priority resolution:**
```
Win (priority 3) > Kong/Pung (priority 2) > Chow (priority 1)
Ties: closer to discarder in turn order wins.
Human and AI claims resolved simultaneously with priority comparison.
```

**Auto-win detection:** If any claim (Pung/Chow/Kong) is made on a tile that completes a winning hand with >= 3 faan, the game auto-declares a win via `canWinWith()` before forming the meld.

---

### Tile System (`src/tiles/`)

#### `Tile` — Core data struct

```cpp
struct Tile {
    Suit suit;       // Bamboo, Characters, Dots, Wind, Dragon, Flower, Season
    uint8_t rank;    // 1-9 (numbered), 1-4 (winds), 1-3 (dragons), 1-4 (bonus)
    uint8_t id;      // Unique 0-143, tracks individual tile instances
};
```

Helper predicates: `isHonor()`, `isTerminal()`, `isSimple()`, `isBonus()`, `sameAs(other)`.
Bilingual name accessors: `chineseName()`, `englishName()`, `toString()`.

#### `TileEnums.h` — Suit and Wind enums

```cpp
enum class Suit : uint8_t { Bamboo, Characters, Dots, Wind, Dragon, Flower, Season };
enum class Wind : uint8_t { East=0, South=1, West=2, North=3 };
```

Helper functions: `isNumberedSuit()`, `isHonorSuit()`, `isBonusSuit()`, `nextWind()`.

#### `TileDefs.h` — Character mappings

Chinese and English string arrays for every tile face. Also defines bilingual `BilingualString` structs for UI labels (`UI_CHOW`, `UI_PUNG`, `UI_KONG`, `UI_WIN`, `UI_PASS`, `UI_DRAW`) and all 27 faan pattern names (`FAAN_NAMES[]`).

#### `Wall` — Tile wall

Constructs all 144 tiles with unique IDs (0-143). `shuffle()` uses `std::mt19937`. Two draw endpoints:
- `draw()` — Front of wall (live wall, for normal draws)
- `drawReplacement()` — Back of wall (dead wall, for kong/flower replacement draws)

---

### Player System (`src/player/`)

#### `Player` — Abstract base class

```cpp
class Player {
    virtual bool isHuman() = 0;
    virtual void requestDiscard(function<void(Tile)> callback) = 0;
    virtual void requestClaimDecision(Tile disc, vector<ClaimOption>, function<void(ClaimType)>) = 0;

    Hand hand_;           // Concealed tiles + melds + flowers
    int score_ = 0;       // Running score across rounds
    vector<Tile> discards_;  // This player's discard pile
    Wind seatWind_;
    int seatIndex_;       // 0-3
};
```

#### `HumanPlayer`

Stores pending `std::function` callbacks. When `requestDiscard()` is called, it saves the callback. Later, `Game::handleInput()` detects a tile click and calls `onTileSelected(tileId)`, which invokes the stored callback.

#### `AIPlayer`

Two decision paths:
1. **Neural network** (when `DQNTrainer` is available and has trained models): Uses feature extraction → network inference → epsilon-greedy action selection.
2. **Heuristic fallback**: Uses `HandEvaluator` for strategic discard/claim evaluation.

Receives game context via `setGameContext(wind, allPlayers)` and `setTurnInfo(turnCount, wallRemaining)` before each decision.

#### `Hand` — Tile container

```cpp
class Hand {
    vector<Tile> concealed_;    // Hidden tiles in hand
    vector<Meld> melds_;        // Exposed (and concealed kong) melds
    vector<Tile> flowers_;      // Bonus tiles (flowers + seasons)
};
```

Key methods: `addTile()`, `removeTileById()`, `sortTiles()`, `hasTile(suit, rank)`, `countTile(suit, rank)`, `promoteToKong()`.

#### `Meld` — Meld struct

```cpp
enum class MeldType : uint8_t { Chow, Pung, Kong, ConcealedKong, Pair };
struct Meld {
    MeldType type;
    vector<Tile> tiles;
    bool exposed = false;
};
```

---

### Scoring System (`src/scoring/`)

#### `WinDetector` — Hand decomposition

**`findWins(hand, winningTile)`** — Returns all valid `WinDecomposition`s:
1. Checks special hands first (Seven Pairs, Thirteen Orphans) — only if no exposed melds
2. Recursive `decompose()` tries all pair/pung/chow extractions from sorted concealed tiles
3. Valid decomposition = 4 sets + 1 pair (accounting for existing melds)

**Other methods:**
- `isTenpai(hand)` — Brute-force: tries all 34 tile types as winning tile
- `waitingTiles(hand)` — Returns which tiles complete the hand

#### `Scoring` — Faan calculation

`calculate(hand, winningTile, context)` finds all decompositions via WinDetector, scores each, returns the highest.

**Constants:** `MIN_FAAN = 3`, `LIMIT_FAAN = 13`.

**Faan patterns recognized (27 total):**

| Faan | Pattern (CN) | Pattern (EN) |
|---|---|---|
| 1 | 自摸 | Self-Draw |
| 1 | 門前清 | Concealed Hand |
| 1 | 無花 | No Flowers |
| 1 | 正花 | Seat Flower |
| 1 | 番牌 | Dragon Pung |
| 1 | 門風 | Seat Wind |
| 1 | 圈風 | Prevailing Wind |
| 1 | 搶槓 | Robbing the Kong |
| 1 | 海底撈月 | Last Tile Win |
| 1 | 槓上開花 | Win on Kong |
| 2 | 花糊 | All Flowers/Seasons |
| 3 | 對對糊 | All Pongs |
| 3 | 混一色 | Half Flush |
| 3 | 小三元 | Little Three Dragons |
| 4 | 七對子 | Seven Pairs |
| 6 | 清一色 | Full Flush |
| 1 | 平糊 | All Chows |
| 13 | 大三元 | Great Three Dragons |
| 13 | 小四喜 | Little Four Winds |
| 13 | 大四喜 | Great Four Winds |
| 13 | 十三么 | Thirteen Orphans |
| 13 | 九子連環 | Nine Gates |
| 13 | 字一色 | All Honors |
| 13 | 清么九 | All Terminals |
| 13 | 四暗刻 | Four Concealed Pongs |
| 13 | 十八羅漢 | All Kongs |
| 13 | 天糊/地糊 | Heavenly/Earthly Hand |

#### `PaymentCalculator` — Point distribution

`calculate(winnerIndex, discarderIndex, faan, dealerIndex)` returns a `PaymentResult` with per-player payment amounts.

- **Discard win:** Discarder pays the winner. Other players are not affected.
- **Self-draw:** All three opponents pay the winner.
- **Dealer multiplier:** 2x when dealer is involved (as winner or payer).
- **Base points:** Converted from faan via `Scoring::faanToPoints()`.

---

### AI System (`src/ai/`)

#### `ShantenCalculator` — Distance to tenpai

Calculates the minimum number of tile changes needed to reach a winning hand.

```
-1 = already complete (winning)
 0 = tenpai (one tile away)
 1 = iishanten (two tiles away)
 N = N tiles away
```

Uses a compact `Counts` representation (4 groups × 9 ranks) and a recursive solver with pruning. Also checks special hand shanten (seven pairs, thirteen orphans).

Variants: `calculateAfterDiscard()`, `calculateAfterPung()`, `calculateAfterChow()` for evaluating hypothetical moves.

#### `HandEvaluator` — Heuristic AI

**`evaluateDiscards(hand, winds, allPlayers)`** — Returns discard candidates sorted best-first. Each candidate has:
- `shantenAfterDiscard` — How close to winning after this discard
- `acceptCount` — How many tiles improve shanten (hand breadth)
- `strategicScore` — Faan compatibility + defensive safety

**`analyzeFaanPotential(hand, winds)`** — Returns a `FaanPlan`:
- Whether to aim for half/full flush and which suit
- Dragon/wind pung potential
- `faanConfidence` (0-1) — Likelihood of reaching 3 faan minimum

**`evaluateClaim(hand, tile, claimType, ...)`** — Returns positive (claim) or negative (pass) score.

**`defensiveScore()`** — Estimates how safe a tile is to discard against opponents based on their discards and melds.

#### `MahjongNet` — Neural network definitions (libtorch)

Two CNN+FC hybrid networks that reshape flat feature vectors into tile channels (34 wide) plus scalar features. See [AI Improvement Roadmap](#ai-improvement-roadmap) for full architecture diagrams.

**DiscardNet:** Chooses which tile type to discard.
```
Input(408) → reshape to [10, 34] channels + [68] scalars
  → Conv1d(10→64→128→128, k=3, pad=1) with ReLU + Dropout
  → AvgPool → concat scalars → FC(196→256→34)
```

**ClaimNet:** Chooses whether to claim a discard.
```
Input(450) → reshape to [11, 34] channels + [76] scalars
  → Conv1d(11→64→128→128, k=3, pad=1) with ReLU + Dropout
  → AvgPool → concat scalars → FC(204→256→4)
```

#### `DQNTrainer` — Reinforcement learning

**Architecture:** Double DQN with experience replay.

| Parameter | Value |
|---|---|
| Replay buffer size | 50,000 transitions |
| Batch size | 64 |
| Discount factor (gamma) | 0.95 |
| Target network sync | Every 100 games |
| Exploration | Epsilon-greedy with decay |
| Optimizer | Adam |

**Training data sources:**
1. **Self-play** — 20 games run between rounds (no rendering)
2. **Human observation** — Records human discard/claim decisions as supervised samples
3. **Round-end rewards** — Terminal reward based on win/loss and points

**Persistence:** Models, replay buffers, and training stats saved to `assets/model/`.

#### `RLFeatures.h` — Feature extraction

**Discard features (408 dimensions)** and **Claim features (450 dimensions)** — see [Feature Extraction](#feature-extraction) in the Decision Pipeline section for the full layout.

---

### Rendering (`src/render/`)

#### `Renderer` — Scene manager

**`RenderState`** — Immutable snapshot consumed each frame:
```cpp
struct RenderState {
    GamePhase phase;
    int activePlayerIndex;
    Wind prevailingWind;
    int wallRemaining;
    array<PlayerView, 4> players;     // const Player* pointers
    vector<ClaimOption> humanClaimOptions;  // Claim buttons to show
    bool canSelfKong, canSelfDrawWin;
    string scoringText;
    const Tile* lastDiscard;          // For golden glow highlight
};
```

**Render pipeline** (`render(state)`):
1. `BeginMode3D` — 3D scene
   - `drawTable()` — Felt texture + wood rim + vignette + specular
   - `drawWall(remaining)` — Stacked tile pairs on 4 sides
   - Per player: `drawPlayerHand()`, `drawPlayerDiscards()`, `drawPlayerMelds()`, `drawPlayerFlowers()`
   - `drawClaimHighlights()` — Golden glow on discard, blue glow on matching hand tiles
2. `EndMode3D` → 2D overlay
   - `drawUIOverlay()` — Player labels, round info, phase indicator, claim buttons, self-kong/win buttons, scoring overlay

**Spatial layout constants:**
```
TABLE_SIZE     = 18.0   (table is 18×18 world units)
HAND_DISTANCE  = 7.5    (hand tiles from center)
TILE_SPACING   = 0.80   (gap between hand tiles)
MELD_OFFSET    = 4.5    (melds start offset from center)
DISCARD_DISTANCE = 2.5  (discard area from center)
```

**Player positions:** 0=South (human, +Z), 1=East (+X), 2=North (-Z), 3=West (-X). Rotation: 0°, 90°, 180°, 270°.

#### `TileRenderer` — 3D tile primitives

**Tile dimensions (world units):**
```
TILE_W = 0.72   (width, narrow face)
TILE_H = 0.98   (height, tall face)
TILE_D = 0.50   (depth/thickness)
```

**Drawing methods:**
| Method | Use | Shadow |
|---|---|---|
| `drawTileStanding` | AI hands (face-up/down) | Yes |
| `drawTileTilted` | Human hand (tilted -65° for readability) | Yes |
| `drawTileFlat` | Discards, melds, flowers (face-up) | Yes |
| `drawTileFlatBack` | Wall tiles, concealed kong (face-down) | Yes |
| `drawGlowFlat` | Pulsing highlight around flat tiles | — |
| `drawGlowTilted` | Pulsing highlight under tilted tiles | — |

**Rendering layers per tile:**
1. `drawShadow()` — Dark quad on table surface (y=0.003)
2. `drawTileBody()` — 6-face box with per-face directional shading + specular highlight + edge lines
3. `drawFaceQuad()` — Textured face from atlas (top for flat, front for standing)

#### `TileTextureAtlas` — Procedural tile faces

Generates a 7×7 grid (2048×2048 pixels, 256×256 per cell) at startup using Raylib's `RenderTexture2D`. Each cell contains one tile face rendered with Chinese characters and English subtitles. UV coordinates are computed for each suit+rank combination.

---

## Data Flow

```
Game::handleInput()          User clicks / keypresses
        │
        ▼
HumanPlayer callbacks        onTileSelected() / onClaimSelected()
        │
        ▼
TurnManager state machine    Processes turns, claims, scoring
        │
        ├──► Observer callbacks → DQNTrainer (training data)
        │
        ▼
Game::buildRenderState()     Snapshot of current state
        │
        ▼
Renderer::render(state)      3D scene + 2D UI overlay
```

---

## Game Loop

```
Game::run() {
    while (!WindowShouldClose()) {
        float dt = GetFrameTime();      // ~16.6ms at 60 FPS
        handleInput();                   // Camera + tile clicks + claim buttons
        update(dt);                      // TurnManager::update(dt)
        render();                        // buildRenderState() → Renderer::render()
    }
}
```

**Input handling (`handleInput()`):**
- Camera: always processed (orbit, zoom, pan)
- PLAYER_TURN phase: ray-cast tile selection, self-kong/win buttons
- CLAIM_PHASE: claim button clicks + keyboard shortcuts (C=Chow, P=Pung, K=Kong, H=Win, Space=Pass)
- SCORING/ROUND_END: Space or click advances to next round

---

## Turn State Machine

```
                    ┌─────────────┐
                    │   DEALING   │ Deal 13 tiles to each player
                    └──────┬──────┘
                           ▼
                ┌──────────────────────┐
                │  REPLACING_FLOWERS   │ Swap bonus tiles for wall draws
                └──────────┬───────────┘
                           ▼
              ┌────────────────────────┐
         ┌───►│     PLAYER_DRAW       │ Active player draws from wall
         │    └────────────┬───────────┘
         │                 │
         │    ┌─ if bonus ─┤── if wall empty ──► ROUND_END (draw)
         │    │            ▼
         │    │  ┌──────────────────┐
         │    │  │   PLAYER_TURN   │ Choose: discard / self-kong / tsumo
         │    │  └────────┬────────┘
         │    │           │
         │    │           ├── self-kong ──► REPLACEMENT_DRAW ──┐
         │    │           │                                     │
         │    │           ├── tsumo (self-draw win) ──► SCORING │
         │    │           │                                     │
         │    │           ▼                                     │
         │    │  ┌──────────────────┐                          │
         │    │  │   CLAIM_PHASE   │ Other players may claim    │
         │    │  └────────┬────────┘                          │
         │    │           │                                     │
         │    │           ├── Win claim ──► SCORING             │
         │    │           │                                     │
         │    │           ├── Pung/Kong/Chow ──► PLAYER_TURN   │
         │    │           │   (claimer's turn, no draw)         │
         │    │           │                                     │
         │    │           ▼                                     │
         │    └───────► next player ──► PLAYER_DRAW ───────────┘
         │
         └──── (loop continues until win or wall exhaustion)

         SCORING ──► ROUND_END ──► (next round or GAME_OVER)
```

**Timing:**
- AI turn delay: 0.3 seconds
- Human claim timeout: 15 seconds (auto-pass)
- AI claim resolution: 0.3 second delay

---

## HK Mahjong Rules Reference

### Tile Set (144 tiles)

| Category | Tiles | Count |
|---|---|---|
| Bamboo (索) | 1-9 × 4 copies | 36 |
| Characters (萬) | 1-9 × 4 copies | 36 |
| Dots (筒) | 1-9 × 4 copies | 36 |
| Winds (風) | East/South/West/North × 4 | 16 |
| Dragons (三元) | Red/Green/White × 4 | 12 |
| Flowers (花) | Plum/Orchid/Chrysanthemum/Bamboo | 4 |
| Seasons (季) | Spring/Summer/Autumn/Winter | 4 |

### Winning Hand Structure

Standard: **4 sets + 1 pair** (14 tiles total)
- Set = Chow (3 consecutive same suit) or Pung (3 identical) or Kong (4 identical)
- Pair = 2 identical tiles (the "eyes")

Special hands: Seven Pairs (7×2), Thirteen Orphans (one of each terminal/honor + 1 duplicate)

### Claiming Rules

| Claim | Who can claim | Requirement |
|---|---|---|
| Chow | Next player in turn order only | 2 tiles forming a sequence with the discard |
| Pung | Any player | 2 identical tiles in hand |
| Kong | Any player | 3 identical tiles in hand |
| Win | Any player | Discard completes winning hand with >= 3 faan |

### Scoring

- **Minimum 3 faan** to declare a win
- **Faan cap at 13** (limit hands)
- Self-draw: all opponents pay
- Discard win: discarder pays
- Dealer involvement: 2x multiplier

---

## Neural Network AI

### Architecture Overview

```
                    ┌─────────────────┐
  Game state ──────►│ Feature Extract  │──── 408 features ────►┌────────────┐
  (Hand, melds,     │  (RLFeatures.h)  │                       │ DiscardNet │──► tile type
   discards, etc.)  └─────────────────┘                       └────────────┘
                            │
                            │──── 450 features ────►┌──────────┐
                            │    (+claimed tile,     │ ClaimNet │──► action
                            │     claim mask,        └──────────┘
                            │     shanten after)
```

### Training Pipeline

```
  Live game                Self-play (20 games between rounds)
      │                              │
      ▼                              ▼
  Observer callbacks ──────► Experience replay buffer (50K)
      │                              │
      ▼                              ▼
  Human observations ──────► Supervised learning buffer
                                     │
                                     ▼
                              trainStep() per round
                                     │
                    ┌────────────────┼────────────────┐
                    ▼                ▼                ▼
             RL batch (64)    Supervised batch    Target sync
             from replay      from human obs     (every 100 games)
```

### Fallback Behavior

When no trained model exists (`assets/model/` empty):
1. `DQNTrainer::hasTrainedModels()` returns false
2. `AIPlayer` falls back to `heuristicDiscard()` / `heuristicClaim()`
3. Uses `HandEvaluator` + `ShantenCalculator` for decisions

---

## Dependency Graph

```
main.cpp
  └── Game
        ├── TurnManager
        │     ├── Wall
        │     ├── Player (polymorphic)
        │     │     ├── HumanPlayer
        │     │     └── AIPlayer
        │     │           ├── HandEvaluator
        │     │           ├── ShantenCalculator
        │     │           └── DQNTrainer (optional)
        │     │                 ├── MahjongNet (DiscardNet, ClaimNet)
        │     │                 └── RLFeatures
        │     ├── WinDetector
        │     ├── Scoring
        │     └── PaymentCalculator
        └── Renderer
              ├── TileRenderer
              └── TileTextureAtlas

Shared data types (no dependencies):
  Tile, TileEnums, TileDefs, Meld, Hand, GameState
```

**Separation boundary for mobile/server split:**
```
PLATFORM-INDEPENDENT (pure C++, no Raylib):
  src/game/*, src/tiles/*, src/player/*, src/scoring/*, src/ai/*

PLATFORM-DEPENDENT (Raylib):
  src/render/*, src/main.cpp, Game::handleInput(), Game::render()
```

---

## Discard & Claim Decision Pipeline

### Decision Hierarchy

Both discard and claim decisions cascade through up to 5 layers. Each layer either produces a final decision or falls through to the next.

**Discard flow** (`AIPlayer::requestDiscard` in `src/player/AIPlayer.cpp`):

```
requestDiscard()
  │
  ├─ [1] Cooperative ─── electTarget() → findCooperativeDiscard() → flushCheck
  │       ↓ fail
  ├─ [2] DQN ─────────── extractFeatures(408) → DiscardNet → flushCheck
  │       ↓ fail
  ├─ [3] Adapted ──────── extractFeatures(408) → AdaptiveEngine → flushCheck
  │       ↓ fail
  ├─ [4] Inference ────── extractFeatures(408) → InferenceEngine → flushCheck
  │       ↓ fail
  └─ [5] Heuristic ────── evaluateDiscards() → first non-flush candidate
                           ↓ all blocked
                           pick least-dangerous suit
```

**Claim flow** (`AIPlayer::requestClaimDecision`):

```
requestClaimDecision()
  │
  ├─ [0] Always claim Win immediately
  │
  ├─ [1] Cooperative ─── shouldSuppressClaim() → pass if target needs tile
  │       ↓ fail
  ├─ [2] DQN ─────────── extractFeatures(450) → ClaimNet → action
  │       ↓ fail
  ├─ [3] Adapted ──────── extractFeatures(450) → AdaptiveEngine → action
  │       ↓ fail
  ├─ [4] Inference ────── extractFeatures(450) → InferenceEngine → action
  │       ↓ fail
  └─ [5] Heuristic ────── evaluateClaim() per option → pick highest score
```

### Layer Details

#### Layer 1: Cooperative Strategy

**Files:** `src/ai/CooperativeStrategy.cpp`, `src/ai/CooperativeStrategy.h`

**Entry condition:** `cooperativeMode_` enabled (3 AIs coordinate against the human).

**Discard:** `electTarget()` picks the AI closest to winning (most exposed melds, tie-broken by suit concentration). `findCooperativeDiscard()` finds a tile the target needs (in the suit of target's melds, not already discarded by target) that the cooperator can afford (worsens own shanten by at most 1). The cooperative discard is still checked against `flushThreatFrom()` — it won't feed the human's flush.

**Claim:** `shouldSuppressClaim()` makes the AI pass on a tile that the elected target needs, even if the claim would improve the AI's own hand.

**Falls through when:** no target elected, no affordable tile found, or candidate feeds a flush.

#### Layer 2: DQN Trainer (Full libtorch)

**Files:** `src/ai/DQNTrainer.h/.cpp`, `src/ai/MahjongNet.h/.cpp`

**Entry condition:** `#ifdef HAS_TORCH` + `trainer_->hasTrainedModels()` + not in claim-only mode.

Extracts 408-dim feature vector via `extractDiscardFeatures()`, runs through DiscardNet (CNN+FC hybrid, see [AI Improvement Roadmap](#ai-improvement-roadmap)), applies valid action mask (only tiles in hand), selects highest logit. Decodes action index to suit+rank (0-8 Bamboo, 9-17 Characters, 18-26 Dots, 27-30 Wind, 31-33 Dragon). Checks `flushThreatFrom()` before committing.

For claims: 450-dim features through ClaimNet (CNN+FC hybrid, 4 outputs for Chow/Pung/Kong/Pass).

During training, uses epsilon-greedy exploration (epsilon decays from 1.0 to 0.1 floor). At inference time during gameplay, epsilon is at its floor — near-greedy.

**Falls through when:** no trained models, flush-blocked, or tile not found in hand.

#### Layer 3: Adapted Engine (Test-Time Fine-Tuning)

**Files:** `core/ai/AdaptiveEngine.cpp/.h`, `core/ai/HandSampler.cpp/.h`, `core/ai/RoundSimulator.cpp/.h`

**Entry condition:** `adaptedEngine_` exists (created by `adaptForRound()` at round start).

At the start of each round, `adaptForRound()`:
1. Deep-copies base InferenceEngine weights into a trainable AdaptiveEngine
2. Builds observable state from the table (visible melds, discards, flowers)
3. Runs N simulations (default 100) — samples plausible hidden hands, plays out rounds
4. Collects (state, action, TD-target) tuples from simulation outcomes
5. Runs M SGD steps (default 15) on mini-batches (size 32) with Huber loss and gradient clipping

Uses pMCPA (parametric Monte Carlo policy adaptation): conv layers are frozen during adaptation, only FC layers are trained. This reduces adapted parameters from ~135K to ~59K, preventing overfitting on the small simulation sample set while preserving learned spatial tile features.

The adapted engine then serves as a round-specific improvement over the base weights. Reset after each round via `resetAdaptation()`.

**Falls through when:** no adapted engine, flush-blocked, or tile not found.

#### Layer 4: Lightweight Inference Engine

**Files:** `core/ai/InferenceEngine.cpp/.h`

**Entry condition:** `inferenceEngine_` loaded with binary weights (exported via `scripts/export_weights.py`).

Pure C++ forward pass — manual conv1d + matrix multiply + bias + ReLU, no libtorch dependency. Same CNN+FC hybrid architecture as libtorch version but loads flat binary weight files instead of PyTorch checkpoints.

`selectBestAction(logits, validMask)` picks the highest logit among valid actions.

**Falls through when:** no weights loaded, flush-blocked, or tile not found.

#### Layer 5: Heuristic Fallback

**Files:** `src/ai/HandEvaluator.cpp/.h`, `src/ai/ShantenCalculator.cpp/.h`

Always available. Calls `HandEvaluator::evaluateDiscards()` to get candidates sorted by composite score (see [Discard Scoring](#handevaluator-discard-scoring)). Iterates candidates and picks the first one not blocked by `flushThreatFrom()`.

**When all candidates are flush-blocked:** counts opponent meld tiles per suit and picks the candidate in the suit with the fewest opponent meld tiles (least dangerous).

For claims: evaluates each option via `HandEvaluator::evaluateClaim()` (see [Claim Scoring](#handevaluator-claim-scoring)) and picks the highest score.

---

### Flush Threat Detection

**Function:** `flushThreatFrom()` in `src/player/AIPlayer.cpp`

Returns `-1` (safe) or the opponent player index that triggered the block.

**Three-signal detection system:**

| Signal | Condition | Meaning |
|--------|-----------|---------|
| Strong | `suitMeld >= 6` | 2+ melds of one suit in exposed tiles |
| Meld-count | `totalMelds >= 3 && suitMeld >= 3` | Close to winning + collecting this suit |
| Standard | `totalNumbered >= 10 && suitDisc <= 1 && suitMeld > 3` | Many numbered discards but hoarding this suit |

**Self-flush exemption:** If the AI itself has 9+ tiles of any single suit, flush threat detection is skipped (stay offensive for own flush).

**Where called:** Every neural/inference discard decision is checked before committing. The heuristic path uses it to filter candidates. `HandEvaluator::evaluateDiscards()` has its own parallel flush detection that adds +10.0 danger to flush-threatened tiles.

---

### HandEvaluator Discard Scoring

`evaluateDiscards()` in `src/ai/HandEvaluator.cpp` returns candidates sorted by a composite score with 5 components:

**1. Shanten (weight: ×-10.0)**
Lowest shanten-after-discard wins. This is the primary factor — a 1-shanten difference equals 10 points of strategic/defensive score.

**2. Strategic Score**
Per-candidate adjustments for hand strategy:
- **Special hands:** Thirteen Orphans (keep orphans -5.0, discard non-orphans +3.0), All Pungs (keep pairs -2.0, discard singletons +1.5), flush (discard off-suit +4.0, keep flush suit -3.0)
- **Honor value:** Dragon/value wind pairs (-6.0 to +2.5 depending on remaining copies), non-value honor singletons (+1.5)
- **Positional awareness:** Previous player dumping a suit = keep for chow potential (-1.5). Next player hoarding a suit = don't feed them (-1.5 to -3.0)
- **Defensive score:** Weighted by distance from winning (0.3× at shanten 1, 0.8× at shanten 4+). Considers genbutsu safety (+3.0), terminal/honor bonus (+0.5), live tile counts (+2.0 to +5.0), opponent flush signals (-1.5 to -3.0)

**3. Danger Level**
Computed when any opponent looks close to tenpai (3+ melds, or 2+ melds with 5 or fewer concealed):
- Base danger: 1.0 per threatening opponent (weighted by threat level 0.5-1.0)
- Suit hoarding: +1.5 to +3.0 if opponent avoids discarding this suit
- Middle tiles (3-7): +1.0 extra (complete more sequences)
- Opponent melds in suit: +0.5 per meld (or +2.0 if opponent has 3+ total melds)
- Remaining copies: +0.5 to +1.0 if few copies unaccounted for
- Genbutsu/kabe safe: skip that opponent entirely

**4. Flush Threat Overlay**
Independent of tenpai detection. If any opponent triggers flush threat signals (same three conditions as `flushThreatFrom()` plus `suitMeld >= 6`), adds +10.0 danger to tiles of that suit.

**5. Acceptance Count (tiebreaker)**
For candidates within 1 shanten of the best: how many of 34 tile types would reduce shanten if drawn after this discard. Higher is better.

**Final sort formula:**
```
score = -shantenAfterDiscard × 10.0 + strategicScore - dangerLevel × defMul
```
Where `defMul` scales from 0 (no threats) to 2.0 (flush threat detected, not self-flushing).

---

### HandEvaluator Claim Scoring

`evaluateClaim()` in `src/ai/HandEvaluator.cpp` returns a float score (positive = claim, negative = pass).

**Faan viability check** (Pung and Chow only): Estimates achievable faan after claiming — guaranteed faan from melds, potential from honor pairs, flush path, self-draw, flowers. If projected faan < 3 (HK minimum), applies soft penalty of `(3 - faan) × 0.8`.

| Claim Type | Base Score | Shanten Improvement | Key Bonuses | Key Penalties |
|------------|-----------|---------------------|-------------|---------------|
| Kong | +5.0 | +2.0 if improves | Free replacement draw | -2.0 if breaks flush plan |
| Pung | — | +3.0 if improves, +0.5 if same | Dragon +3.0, value wind +2.5, close to win +1.0 | Off-suit flush -3.0, opponent threat -0.25 |
| Chow | — | +3.0 if improves, +0.8 if same | Previous player dumping suit +1.5 | Off-suit flush -3.0, concealment -0.1, opponent threat -0.25 |

---

### Feature Extraction

**Discard features (408 dimensions)** — `extractDiscardFeatures()` in `src/ai/RLFeatures.h`:

| Offset | Size | Description |
|--------|------|-------------|
| 0-33 | 34 | Hand tile counts (concealed) |
| 34-67 | 34 | Own exposed meld tile counts |
| 68-169 | 102 | Opponent exposed melds (3×34) |
| 170-181 | 12 | Opponent meld type counts (3×4: Chow/Pung/Kong/ConcealedKong) |
| 182-317 | 136 | Discard history per player (4×34) |
| 318 | 1 | Shanten / 8.0 |
| 319 | 1 | Faan confidence (0-1) |
| 320 | 1 | Estimated faan / 13.0 |
| 321-323 | 3 | Strategy profile (speed / value / balanced one-hot) |
| 324 | 1 | Turn progress (turnCount / 80.0) |
| 325 | 1 | Wall remaining / 70.0 |
| 326-329 | 4 | Seat wind one-hot |
| 330-333 | 4 | Prevailing wind one-hot |
| 334-337 | 4 | Flower counts per player / 8.0 |
| 338 | 1 | Estimated turns remaining |
| 339 | 1 | High-faan shanten / 8.0 |
| 340 | 1 | Shanten gap (high-faan vs standard) / 8.0 |
| 341-343 | 3 | Opponent threat levels (0-1) |
| 344-377 | 34 | Live tile counts: (4 - visible) / 4.0 |
| 378-380 | 3 | Opponent tenpai estimates (0-1) |
| 381-389 | 9 | Opponent suit avoidance (3 opponents × 3 suits) |
| 390 | 1 | Seven Pairs shanten (reserved, always 1.0) |
| 391 | 1 | Thirteen Orphans shanten / 13.0 |
| 392 | 1 | Special hand advantage |
| 393 | 1 | All Pungs ratio (pairs+trips / total concealed) |
| 394 | 1 | Dominant suit concentration |
| 395 | 1 | Score relative to leader / 128.0, clamped [-1, 1] |
| 396 | 1 | Score rank position (0=last, 1=first) |
| 397 | 1 | Desperation factor |
| 398-400 | 3 | Previous player suit dump ratios |
| 401-403 | 3 | Next player suit retention ratios |
| 404 | 1 | Previous player supply signal |
| 405 | 1 | Next player flush danger |
| 406 | 1 | Acceptance count / 34.0 |
| 407 | 1 | Weighted acceptance |

**Claim features (450 dimensions)** — discard features (408) + claimed tile one-hot (34) + available claims mask (4) + shanten after each claim type (4).

---

### InferenceEngine vs DQN Trainer

| Aspect | DQN Trainer | Inference Engine |
|--------|-------------|------------------|
| Dependency | libtorch (~2GB) | None (pure C++ matrix math) |
| Compilation | `#ifdef HAS_TORCH` | Always compiled |
| Use case | Desktop training + inference | Mobile deployment (iOS/Android) |
| Weight format | PyTorch `.pt` checkpoints | Flat binary (row-major float32) |
| Forward pass | Via libtorch tensor ops | Manual matmul + bias + ReLU |
| Backpropagation | Full autograd | N/A (inference only) |
| Replay buffer | 50K transitions | N/A |
| Export | `scripts/export_weights.py` converts `.pt` → `.bin` | Loads `.bin` directly |

Both use identical CNN+FC hybrid architectures with Conv1d over 34-wide tile channels.

---

### Adaptive Engine Lifecycle

**Files:** `core/ai/AdaptiveEngine.cpp/.h`, `core/ai/HandSampler.cpp/.h`, `core/ai/RoundSimulator.cpp/.h`

Per-round test-time adaptation that fine-tunes the base inference weights for the current game state:

```
Round start
  │
  ├─ cloneFrom(InferenceEngine)     Deep-copy base weights
  │
  ├─ buildObservable()              Snapshot visible table state
  │
  ├─ for N simulations:             Default N=100
  │     ├─ sampleWorld()            Random plausible hidden hands
  │     ├─ simulate()               Play out round using adapted engine
  │     └─ collect samples          (state, action, TD-target) tuples
  │
  ├─ for M SGD steps:               Default M=15
  │     ├─ sample mini-batch        Size 32 from collected samples
  │     └─ trainDiscardBatch()      Huber loss, gradient clipping (norm ≤ 1.0)
  │
  ├─ [use adapted engine for game decisions during round]
  │
  └─ resetAdaptation()              Clear at round end
```

**Configuration** (`AdaptationConfig`):
- `learningRate`: 0.001
- `numSimulations`: 100
- `maxSGDSteps`: 15
- `batchSize`: 32
- `gamma`: 0.95
- `gradClipNorm`: 1.0

Configurable per AI via `setAdaptationConfig()` or tier-based presets via `setAdaptationTier()`.

---

## AI Improvement Roadmap

### Suphx Comparison

[Suphx](https://arxiv.org/abs/2003.13590) (Microsoft Research, 2020) achieved superhuman performance in Japanese Mahjong on the Tenhou platform. Key architectural decisions that differ from our current system:

| Aspect | Current System | Suphx |
|--------|---------------|-------|
| **Architecture** | CNN+FC hybrid (Conv1d over 34-wide tile channels) | CNN over tile-type channels |
| **Training** | Double DQN from scratch (self-play) | Supervised pretraining → RL fine-tuning |
| **Exploration** | Epsilon-greedy (decaying) | Oracle guiding (global features during training) |
| **Search** | Test-time adaptation (Monte Carlo simulation) | pMCPA (parametric Monte Carlo policy adaptation) |
| **Variant** | Hong Kong (3-faan minimum, 144 tiles) | Japanese Riichi (more complex scoring) |
| **Scale** | ~135K parameters, single machine | Millions of parameters, distributed training |

### CNN Architecture

The network uses Conv1d over 34-wide tile channels to learn spatial relationships between adjacent tile types (e.g., 4-5-6 bamboo chow awareness). This follows Suphx's insight that tile-type columns have semantic meaning.

**Feature channel layout (discard, 408 dimensions):**

| Channel | Offset | Width | Description |
|---------|--------|-------|-------------|
| 0 | [0-33] | 34 | Hand tile counts (concealed) |
| 1 | [34-67] | 34 | Own exposed meld tile counts |
| 2 | [68-101] | 34 | Opponent 1 exposed melds |
| 3 | [102-135] | 34 | Opponent 2 exposed melds |
| 4 | [136-169] | 34 | Opponent 3 exposed melds |
| 5 | [182-215] | 34 | Self discards |
| 6 | [216-249] | 34 | Opponent 1 discards |
| 7 | [250-283] | 34 | Opponent 2 discards |
| 8 | [284-317] | 34 | Opponent 3 discards |
| 9 | [344-377] | 34 | Live tile counts |

**Scalar features (68 dimensions, not tile-aligned):**

| Offset | Size | Description |
|--------|------|-------------|
| [170-181] | 12 | Opponent meld type counts |
| [318-343] | 26 | Game context (shanten, faan, winds, etc.) |
| [378-407] | 30 | Additional features (tenpai, suit avoidance, etc.) |

**ClaimNet adds:** 1 extra channel (claimed tile one-hot, [408-441]) = 11 channels total, plus 8 extra scalars ([442-449] claim mask + shanten after) = 76 scalars total.

**DiscardNet architecture:**
```
Tile channels: [batch, 10, 34]
  → Conv1d(10, 64, kernel=3, padding=1) + ReLU + Dropout(0.1)
  → Conv1d(64, 128, kernel=3, padding=1) + ReLU + Dropout(0.1)
  → Conv1d(128, 128, kernel=3, padding=1) + ReLU
  → AdaptiveAvgPool1d(1)  →  [batch, 128]
  → Concat(scalars)       →  [batch, 196]
  → Linear(196, 256) + ReLU + Dropout(0.1)
  → Linear(256, 34)
```

**ClaimNet architecture:**
```
Tile channels: [batch, 11, 34]
  → Conv1d(11, 64, kernel=3, padding=1) + ReLU + Dropout(0.1)
  → Conv1d(64, 128, kernel=3, padding=1) + ReLU + Dropout(0.1)
  → Conv1d(128, 128, kernel=3, padding=1) + ReLU
  → AdaptiveAvgPool1d(1)  →  [batch, 128]
  → Concat(scalars)       →  [batch, 204]
  → Linear(204, 256) + ReLU + Dropout(0.1)
  → Linear(256, 4)
```

The flat 408/450-dim feature vector from `RLFeatures.h` is unchanged. Reshaping into channels + scalars happens inside the network's `forward()` method using known offsets. This keeps the feature extraction code and heuristic system backward-compatible.

### Implementation Across Tiers

| Component | Architecture |
|-----------|-------------|
| `MahjongNet` (libtorch) | PyTorch Conv1d + Linear modules, full autograd |
| `InferenceEngine` (mobile) | Manual conv1d + matmul, loads flat binary weights |
| `AdaptiveEngine` (test-time) | Full forward pass, FC-only backward (conv frozen per pMCPA) |
| `export_weights.py` | Exports conv (3D) + linear (2D) weights sequentially |

### Improvement Roadmap

**Phase 1 (done): CNN architecture**
Replace MLP with Conv1d over tile channels. Captures spatial tile relationships. Training restarts from scratch (old weights incompatible).

**Phase 2: Supervised pretraining**
Pre-train on recorded game data (human or strong heuristic play) before RL. Suphx showed this dramatically accelerates convergence and avoids early random-play instabilities.

**Phase 3: Oracle guiding**
During training, provide the network with "oracle" features (e.g., perfect information about opponents' hands). Gradually reduce oracle feature weight as training progresses. This guides exploration toward sensible strategies early in training.

**Phase 4 (done): pMCPA (parametric Monte Carlo policy adaptation)**
Conv layers are frozen during test-time adaptation; only FC layers (~59K params) are trained on Monte Carlo rollout samples. This follows Suphx's approach of adapting only the decision head while preserving learned spatial features, reducing overfitting risk on the small per-round sample set.
