# Mahjong AI / ML Architecture

## Overview

The AI system uses a **three-tier fallback architecture** for both discard and claim decisions:

1. **DQNTrainer** (libtorch, desktop only) - Double DQN with experience replay and supervised learning
2. **InferenceEngine** (pure C++, all platforms) - Lightweight forward pass using pre-exported binary weights
3. **Heuristic** (always available) - Rule-based decisions via HandEvaluator + ShantenCalculator

Building without libtorch still produces a fully functional AI using heuristics. Mobile apps use the InferenceEngine with pre-trained weights exported from the desktop trainer.

---

## File Layout

```
src/ai/
  MahjongNet.h/.cpp      # PyTorch network definitions (DiscardNet, ClaimNet)
  DQNTrainer.h/.cpp       # Double DQN training loop, replay buffers, self-play
  RLFeatures.h            # Feature extraction (header-only, 344/386-dim vectors)
  HandEvaluator.h/.cpp    # Heuristic discard/claim evaluation
  ShantenCalculator.h/.cpp # Shanten calculation (tiles-to-win distance)

core/ai/
  InferenceEngine.h/.cpp  # Lightweight NN forward pass (no libtorch dependency)

src/player/
  AIPlayer.h/.cpp         # Integrates all three tiers

scripts/
  export_weights.py       # Converts .pt models to .bin for InferenceEngine
  download_libtorch.sh    # Downloads libtorch to third_party/

server/
  app.py                  # Flask API for serving model weights to mobile clients

assets/model/
  discard_net.pt          # Trained PyTorch model (discard decisions)
  claim_net.pt            # Trained PyTorch model (claim decisions)
  discard_weights.bin     # Exported binary weights for InferenceEngine
  claim_weights.bin       # Exported binary weights for InferenceEngine
  replay_buffer.bin       # Persisted experience replay buffer
  training_stats.json     # Training metrics (games played, epsilon, loss)
```

---

## Neural Network Architecture

### DiscardNet

Selects which tile to discard from the player's hand.

| Layer | Shape | Activation |
|-------|-------|-----------|
| fc1 | 344 -> 256 | ReLU + Dropout(0.1) |
| fc2 | 256 -> 128 | ReLU + Dropout(0.1) |
| fc3 | 128 -> 34 | None (raw logits) |

- **Input:** 344-float feature vector (see Feature Extraction below)
- **Output:** 34 logits, one per tile type (Bamboo 1-9, Characters 1-9, Dots 1-9, Winds 1-4, Dragons 1-3)

### ClaimNet

Decides whether to claim a discarded tile (and how).

| Layer | Shape | Activation |
|-------|-------|-----------|
| fc1 | 386 -> 256 | ReLU + Dropout(0.1) |
| fc2 | 256 -> 128 | ReLU |
| fc3 | 128 -> 4 | None (raw logits) |

- **Input:** 386-float feature vector (344 base features + 42 claim-specific)
- **Output:** 4 logits for [Chow, Pung, Kong, Pass]

Both are defined in `MahjongNet.h/.cpp` using PyTorch's `TORCH_MODULE` macro.

---

## Feature Extraction (`RLFeatures.h`)

### Tile Type Indexing (34 types)

```
Bamboo 1-9:     indices 0-8
Characters 1-9: indices 9-17
Dots 1-9:       indices 18-26
Winds (E/S/W/N): indices 27-30
Dragons (R/G/W): indices 31-33
```

### Discard Features (344 floats)

```
[0..33]     Hand tile counts (count of each tile type in hand)
[34..67]    Own exposed melds (tile types in exposed melds)
[68..169]   Opponent exposed melds (3 opponents x 34 = 102)
[170..181]  Opponent meld types (3 opponents x 4 meld types = 12)
[182..317]  Discards per player (4 players x 34 = 136)
[318]       Shanten number (/ 8.0)
[319]       Faan confidence score (0-1)
[320]       Estimated faan value (/ 13.0)
[321..323]  Faan target profile: speed/value/balanced (one-hot)
[324]       Turn progress (/ 80.0)
[325]       Wall remaining (/ 70.0)
[326..329]  Seat wind (one-hot)
[330..333]  Prevailing wind (one-hot)
[334..337]  Flower counts per player
[338]       Estimated turns remaining (normalized)
[339]       High-faan shanten gap
[340]       Shanten gap (high vs normal)
[341..343]  Opponent threat levels (3 opponents)
```

Extraction function: `extractDiscardFeatures(const Hand&, const RLGameContext&) -> vector<float>`

### Claim Features (386 floats)

344 discard features plus:

```
[344..377]  Claimed tile (one-hot, 34)
[378..381]  Available claims mask: Chow/Pung/Kong/Pass
[382..385]  Shanten after each claim action (4 values)
```

Extraction function: `extractClaimFeatures(const Hand&, Tile, const vector<ClaimType>&, const RLGameContext&) -> vector<float>`

### Game Context

```cpp
struct RLGameContext {
    int turnCount = 0;
    int wallRemaining = 70;
    Wind seatWind = Wind::East;
    Wind prevailingWind = Wind::East;
    int playerIndex = 0;
    const std::vector<const Player*>* allPlayers = nullptr;
};
```

---

## DQN Training (`DQNTrainer.h/.cpp`)

### Algorithm: Double Deep Q-Learning

- **Main networks:** `discardNet_`, `claimNet_` (trained via gradient descent)
- **Target networks:** `discardTargetNet_`, `claimTargetNet_` (synced every 100 games)
- Target value: `r + gamma * targetNet(s')[argmax(mainNet(s'))]`

### Hyperparameters

| Parameter | Value |
|-----------|-------|
| Batch size | 64 |
| Replay buffer max | 50,000 transitions |
| Gamma (discount) | 0.95 |
| Learning rate | 0.001 (Adam) |
| Epsilon start | 1.0 |
| Epsilon end | 0.05 |
| Epsilon decay period | 500 games |
| Target sync interval | 100 games |
| Loss function | Smooth L1 (Huber) |
| Gradient clip | norm <= 1.0 |
| Dropout | 0.1 |

### Training Loop

1. **During gameplay:** Record transitions via `recordDiscardTransition(state, action, reward, nextState, done)` and `recordClaimTransition(...)`.

2. **At round end:** Call `trainStep()` 4 times, which runs:
   - `trainDiscardBatch()` - Sample batch from discard replay buffer, compute Double DQN loss, backprop
   - `trainClaimBatch()` - Same for claim decisions
   - `trainSupervisedDiscard()` - Cross-entropy loss on observed human discards
   - `trainSupervisedClaim()` - Cross-entropy loss on observed human claims

3. **Exploration:** Epsilon-greedy action selection. At rate epsilon, pick random valid action; otherwise pick highest Q-value action.

### Reward Signal

| Event | Reward |
|-------|--------|
| Shanten improvement | +0.5 |
| Shanten worsening | -0.3 |
| Successful claim (shanten improves) | +1.0 |
| Draw (wall exhausted) | -1.0 |
| Win | +5.0 + faanToPoints(totalFaan) |

### Self-Play

```cpp
void runSelfPlay(int numGames, Wind prevailingWind, int dealerIndex)
```

Runs lightweight games without rendering. All 4 players use the same networks. Records transitions for training.

### Supervised Learning

Records human player decisions for imitation learning:
```cpp
void recordHumanDiscard(const vector<float>& state, int action);  // up to 1000 samples
void recordHumanClaim(const vector<float>& state, int action);
```
Trained with cross-entropy loss, mini-batch size 16.

### Model Persistence

```cpp
void saveModels(const string& modelDir);  // Saves .pt, replay_buffer.bin, training_stats.json
bool loadModels(const string& modelDir);
```

---

## InferenceEngine (`core/ai/InferenceEngine.h/.cpp`)

Zero-dependency C++ forward pass for mobile and desktop inference.

### API

```cpp
class InferenceEngine {
    bool loadDiscardWeights(const string& path);  // Load .bin weights
    bool loadClaimWeights(const string& path);
    bool hasDiscardWeights() const;
    bool hasClaimWeights() const;

    vector<float> inferDiscard(const vector<float>& features) const;  // 344 -> 34
    vector<float> inferClaim(const vector<float>& features) const;    // 386 -> 4

    static int selectBestAction(const vector<float>& logits, const vector<bool>& validMask);
};
```

### Binary Weight Format (`.bin`)

For each layer (`fc1`, `fc2`, `fc3`) in sequence:
```
weight matrix: out_features x in_features floats (row-major, little-endian float32)
bias vector:   out_features floats
```

No headers or metadata. DiscardNet binary is ~500 KB, ClaimNet is ~400 KB.

### Forward Pass

For each layer except last: `output = ReLU(W * input + b)`. Last layer: raw logits (no activation). No dropout at inference time.

---

## AI Player Integration (`AIPlayer.h/.cpp`)

### Decision Flow

```
requestDiscard(callback):
  1. Build RLGameContext from current game state
  2. Extract features: extractDiscardFeatures(hand, ctx) -> 344 floats
  3. Try Tier 1: DQNTrainer (if HAS_TORCH and trained models exist)
     -> epsilon-greedy Q-value selection -> action index 0-33
  4. Try Tier 2: InferenceEngine (if .bin weights loaded)
     -> forward pass -> selectBestAction with valid tile mask
  5. Fallback Tier 3: HandEvaluator heuristic
     -> evaluate all discard candidates -> pick best

requestClaimDecision(options, callback):
  1. Always claim Win immediately if available
  2. Same three-tier fallback for Chow/Pung/Kong/Pass decisions
```

### Build-Time Configuration

```cmake
# HAS_TORCH gates all libtorch-dependent code
if(Torch_FOUND)
    target_compile_definitions(mahjong PRIVATE HAS_TORCH=1)
endif()
```

- `#ifdef HAS_TORCH` wraps: MahjongNet, DQNTrainer, torch includes in AIPlayer
- InferenceEngine + HandEvaluator + ShantenCalculator are always available

---

## Heuristic AI

### HandEvaluator (`src/ai/HandEvaluator.h/.cpp`)

Rule-based evaluation considering:
- **FaanPlan analysis:** Detects flush patterns (half/full), dragon/wind pung potential, faan confidence
- **Discard scoring:** Shanten after discard, acceptance count (tile connectivity), flush compatibility, defensive safety
- **Claim scoring:** Base scores by claim type (Kong > Pung > Chow), shanten improvement bonus, faan value of claimed melds

### ShantenCalculator (`src/ai/ShantenCalculator.h/.cpp`)

Computes minimum tiles-to-win distance (-1 = winning, 0 = tenpai, 1+ = further).

- Backtracking solver over 4 tile groups (3 suits + honors)
- Special patterns: Seven Pairs, Thirteen Orphans
- Variants: `calculate(hand)`, `calculateAfterDiscard(...)`, `calculateAfterPung(...)`, `calculateAfterChow(...)`

---

## Weight Export Pipeline

```
Desktop Training                     Mobile Deployment
────────────────                     ─────────────────
DQNTrainer.saveModels()
  -> discard_net.pt                  InferenceEngine.loadDiscardWeights()
  -> claim_net.pt                      <- discard_weights.bin
       |                                  <- claim_weights.bin
       v
python scripts/export_weights.py
  -> discard_weights.bin
  -> claim_weights.bin
       |
       v (optional)
Flask server (server/app.py)
  GET /api/models/discard  -> binary download
  GET /api/models/claim    -> binary download
  GET /api/models/version  -> version + SHA-256 checksums
```

### Export Script Usage

```bash
python scripts/export_weights.py [model_dir]  # defaults to assets/model/
```

Reads `discard_net.pt` and `claim_net.pt`, writes `discard_weights.bin` and `claim_weights.bin`.

### Server Usage

```bash
pip install flask
python server/app.py --model-dir assets/model --port 8080
```

---

## Build Instructions

### Desktop with libtorch (enables training)

```bash
./scripts/download_libtorch.sh          # Downloads to third_party/libtorch/
cmake -B build && cmake --build build   # Detects libtorch, defines HAS_TORCH
./build/mahjong                         # Full game with neural AI + training
```

### Desktop without libtorch (heuristic only)

```bash
cmake -B build && cmake --build build
./build/mahjong                         # Heuristic AI, no training
```

### Mobile (iOS/Android)

Core library always includes InferenceEngine. No libtorch needed. Pre-exported `.bin` weights are bundled as assets.
