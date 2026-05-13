# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & Run Commands

```bash
# Desktop app (C++17 + Raylib)
cmake -B build && cmake --build build
./build/mahjong

# With neural network AI (requires libtorch)
./scripts/download_libtorch.sh          # one-time setup
cmake -B build && cmake --build build   # auto-detects libtorch in third_party/

# Flask weight server (serves trained models to mobile clients)
cd server && pip install -r requirements.txt
python server/app.py --model-dir assets/model --port 8080

# Export trained weights for mobile
python scripts/export_weights.py assets/model
```

No automated test suite exists yet.

## Architecture

### Platform Layers

The project is a Hong Kong Mahjong game with four platform targets sharing a common core:

- **`core/`** — Static library (`libmahjong-core.a`). Pure C++17 with zero external dependencies. Contains all game logic, scoring, AI heuristics, and a C ABI (`mahjong_api.h`) for mobile bindings.
- **`src/`** — Desktop app. Adds Raylib rendering (`src/render/`) and the `Game` orchestrator (`src/game/Game.cpp`). Optionally links libtorch for neural network AI.
- **`ios/`** — Swift UI + Objective-C++ bridge to core. Uses `RaylibBridge` and `IOSAssetResolver`.
- **`android/`** — Kotlin UI + JNI bridge to core. Uses `AssetExtractor` for APK assets.
- **`server/`** — Single-file Flask app serving trained model weights to mobile clients.

### Key Design Patterns

**RenderState Snapshot** — Renderer never reads game state directly. Each frame `Game::buildRenderState()` produces an immutable `RenderState` snapshot. This decouples game logic from presentation.

**Callback-Based Turns** — Player actions use `std::function` callbacks. `AIPlayer` resolves immediately; `HumanPlayer` stores the callback until UI input arrives. This keeps the game loop single-threaded.

**Observer Pattern** — `TurnManager` fires discard/claim/roundEnd observers. `DQNTrainer` subscribes to collect training data with zero coupling to game logic.

**Optional Torch Compilation** — All neural network code (`MahjongNet`, `DQNTrainer`, `RLFeatures`) is guarded by `#ifdef HAS_TORCH`. Without libtorch, `AIPlayer` falls back to `HandEvaluator` heuristics.

### Turn State Machine (TurnManager)

The round progresses through 12 phases:
```
DEALING → REPLACING_FLOWERS → PLAYER_DRAW → PLAYER_TURN →
DISCARD_ANIMATION → CLAIM_PHASE → CLAIM_RESOLUTION →
MELD_FORMATION → REPLACEMENT_DRAW → SCORING → ROUND_END → GAME_OVER
```

### Scoring System

Implements HK rules: minimum 3 faan to win, cap at 13 faan (limit hands). 27 scoring patterns in `src/scoring/Scoring.cpp`. Win detection handles standard (4 melds + pair), Seven Pairs, and Thirteen Orphans.

### Neural Network AI

Double DQN with experience replay (50K buffer, batch 64, gamma 0.95). Two networks:
- **DiscardNet**: 408 features → 256 → 128 → 34 (tile types)
- **ClaimNet**: 450 features → 256 → 128 → 4 (chow/pung/kong/pass)

Feature extraction in `src/ai/RLFeatures.h`. Models save to `assets/model/`. Mobile clients download weights from the Flask server.

### Claiming Priority

Win (3) > Kong/Pung (2) > Chow (1). Chow restricted to next player in turn order. Ties broken by proximity to discarder.

## Key Source Paths

| Path | Purpose |
|------|---------|
| `src/game/TurnManager.cpp` | Round state machine, phase transitions |
| `src/game/Game.cpp` | Top-level orchestrator, main loop, camera, RenderState builder |
| `src/player/AIPlayer.cpp` | AI decision logic (heuristic + neural) |
| `src/scoring/Scoring.cpp` | 27 faan pattern calculations |
| `src/scoring/WinDetector.cpp` | Hand decomposition, special hand detection |
| `src/ai/ShantenCalculator.cpp` | Distance-to-win metric |
| `src/ai/HandEvaluator.cpp` | Heuristic discard/claim evaluation |
| `core/GameController.cpp` | C++ API wrapper aggregating game state |
| `core/mahjong_api.cpp` | C ABI for mobile platform bindings |
| `docs/ARCHITECTURE.md` | Comprehensive 750+ line architecture reference |
