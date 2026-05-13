# Game Replay Upload for Cloud Training — Implementation Plan

## Context

Training currently runs locally on desktop with libtorch. To train in the cloud, clients (desktop + iOS) need to send human player game data to the server. Only human player (seat 0) decisions are recorded — AI decisions are not captured.

The existing `GameSnapshot` + JSON serialization in `mahjong_api.cpp` already captures the right data. We reuse it.

## Architecture

```
GameController observers (multi-observer, 5 types)
        │
        ▼
   GameRecorder (core/)
   - hooks into discard/claim/selfKong/selfDrawWin/roundEnd observers
   - filters to human player only (playerIndex == 0)
   - takes GameSnapshot at each human decision
   - serializes round to JSON using shared JsonHelpers
        │
        ▼
   DataUploader (abstract interface in core/)
        │
   ┌────┴────┐
   Desktop    iOS
   (POSIX     (NSURLSession
    socket)    via ObjC++)
        │
        ▼
   Flask server POST /api/games
   stores to PostgreSQL via SQLAlchemy
```

## Output JSON Format

```json
{
  "version": 1,
  "timestamp": 1740153600,
  "clientId": "desktop-abc123",
  "rounds": [
    {
      "roundNumber": 3,
      "prevailingWind": 0,
      "dealerIndex": 0,
      "decisions": [
        {
          "type": "discard",
          "turnCount": 5,
          "wallRemaining": 88,
          "discardedTile": {"suit": 0, "rank": 3, "id": 42},
          "snapshot": { /* GameSnapshot JSON */ }
        },
        {
          "type": "claim",
          "turnCount": 12,
          "wallRemaining": 76,
          "claimType": "pung",
          "claimedTile": {"suit": 1, "rank": 7, "id": 98},
          "snapshot": { ... }
        },
        {
          "type": "selfKong",
          "turnCount": 18,
          "wallRemaining": 64,
          "kongSuit": 0,
          "kongRank": 5,
          "snapshot": { ... }
        },
        {
          "type": "selfDrawWin",
          "turnCount": 22,
          "wallRemaining": 58,
          "winningTile": {"suit": 2, "rank": 1, "id": 110},
          "snapshot": { ... }
        }
      ],
      "result": {
        "winnerIndex": 0,
        "selfDrawn": true,
        "isDraw": false,
        "scoring": {
          "totalFaan": 5,
          "isLimit": false,
          "breakdown": [
            {"nameCn": "自摸", "nameEn": "Self-Drawn", "faan": 1},
            {"nameCn": "混一色", "nameEn": "Half Flush", "faan": 3}
          ]
        },
        "finalScores": [28000, 24000, 24000, 24000]
      }
    }
  ]
}
```

Event types: `"discard"` (human discarded a tile), `"claim"` (human claimed/passed on a discard), `"selfKong"` (human declared self-kong), `"selfDrawWin"` (human declared self-draw win).

---

## Step 1: Extract JSON Helpers into Shared Header

The JSON serialization helpers (`appendTileJson`, `appendMeldJson`, `escapeJson`) are currently `static` functions in `core/mahjong_api.cpp` (lines 112–142). GameRecorder needs them too.

### New file: `core/JsonHelpers.h`

Header-only, inline functions in `namespace json`:

- `json::escape(const std::string&)` — JSON string escaping
- `json::appendTile(std::ostringstream&, const Tile&)` — outputs `{"suit":0,"rank":3,"id":42}`
- `json::appendMeld(std::ostringstream&, const Meld&)` — meld with tiles array
- `json::appendTileArray(std::ostringstream&, const std::vector<Tile>&)` — JSON array of tiles
- `json::appendMeldArray(std::ostringstream&, const std::vector<Meld>&)` — JSON array of melds
- `json::claimTypeToString(ClaimType)` — enum to `"chow"`/`"pung"`/`"kong"`/`"win"`/`"none"`
- `json::appendFaanResult(std::ostringstream&, const FaanResult&)` — scoring breakdown JSON

Include guards: `#pragma once`. Includes: `tiles/Tile.h`, `player/Meld.h`, `scoring/Scoring.h`, `game/GameState.h`, `<sstream>`, `<string>`, `<vector>`.

### Modify: `core/mahjong_api.cpp`

- Remove the three `static` helper functions at lines 112–142 (`appendTileJson`, `appendMeldJson`, `escapeJson`)
- Add `#include "JsonHelpers.h"` at the top
- Replace all calls: `appendTileJson(os, t)` → `json::appendTile(os, t)`, `appendMeldJson(os, m)` → `json::appendMeld(os, m)`, `escapeJson(s)` → `json::escape(s)`

No CMake change needed (header-only).

---

## Step 2: Multi-Observer Support + New Observer Types

Currently `setDiscardObserver()` replaces the single observer. Both DQNTrainer and GameRecorder need to coexist. Additionally, self-kong and self-draw-win human decisions are not observable — these need new observer types.

### Step 2a: Convert existing observers to vectors

#### Modify: `src/game/TurnManager.h`

Change member storage from single `std::function` to `std::vector`:

```
discardObserver_   →  std::vector<DiscardObserver>  discardObservers_
claimObserver_     →  std::vector<ClaimObserver>    claimObservers_
roundEndObserver_  →  std::vector<RoundEndObserver> roundEndObservers_
```

Add new methods:
```cpp
void addDiscardObserver(DiscardObserver observer);
void addClaimObserver(ClaimObserver observer);
void addRoundEndObserver(RoundEndObserver observer);
void clearObservers();  // clears all vectors
```

Keep existing `set*Observer()` methods as deprecated wrappers (clear vector + push_back).

#### Modify: `src/game/TurnManager.cpp`

All 6 existing observer fire-sites become `for (auto& obs : ...Observers_) { obs(...); }`:

1. **Line ~110** (`playerDraw`): `roundEndObserver_` → iterate `roundEndObservers_`
2. **Line ~266** (`handleScoring`): `roundEndObserver_` → iterate `roundEndObservers_`
3. **Lines ~419–421** (`handlePlayerTurn`, human discard callback): `discardObserver_` → iterate `discardObservers_`
4. **Lines ~457–459** (`handlePlayerTurn`, AI discard callback): `discardObserver_` → iterate `discardObservers_`
5. **Line ~856** (`resolveClaim`): `claimObserver_` → iterate `claimObservers_`
6. **Lines ~918–920** (`update`, REPLACEMENT_DRAW wall exhaustion): `roundEndObserver_` → iterate `roundEndObservers_`

### Step 2b: Add self-kong and self-draw-win observers

#### Modify: `src/game/TurnManager.h`

Add two new observer type aliases:
```cpp
using SelfKongObserver = std::function<void(int playerIndex, Suit suit, uint8_t rank, int turnCount, int wallRemaining)>;
using SelfDrawWinObserver = std::function<void(int playerIndex, Tile winningTile, int turnCount, int wallRemaining)>;
```

Add storage and methods:
```cpp
std::vector<SelfKongObserver> selfKongObservers_;
std::vector<SelfDrawWinObserver> selfDrawWinObservers_;

void addSelfKongObserver(SelfKongObserver observer);
void addSelfDrawWinObserver(SelfDrawWinObserver observer);
```

#### Modify: `src/game/TurnManager.cpp`

- In `onHumanSelfKong()` (line ~386), fire self-kong observers **BEFORE** calling `resolveSelfKong()`:
  ```cpp
  void TurnManager::onHumanSelfKong(Suit suit, uint8_t rank) {
      if (phase_ != GamePhase::PLAYER_TURN || activePlayer_ != 0) return;
      waitingForHumanDiscard_ = false;
      for (auto& obs : selfKongObservers_) {
          obs(0, suit, rank, turnCount_, wall_.remaining());
      }
      resolveSelfKong(0, suit, rank);
  }
  ```

- In `onHumanSelfDrawWin()` (line ~192), fire self-draw-win observers **BEFORE** calling `handleScoring()`:
  ```cpp
  void TurnManager::onHumanSelfDrawWin() {
      if (!humanCanSelfDraw_ || activePlayer_ != 0) return;
      humanCanSelfDraw_ = false;
      waitingForHumanDiscard_ = false;
      for (auto& obs : selfDrawWinObservers_) {
          obs(0, lastDrawnTile_, turnCount_, wall_.remaining());
      }
      handleScoring(0, true, lastDrawnTile_);
  }
  ```

### Step 2c: Update GameController

#### Modify: `core/GameController.h`

Change stored observers from single to `std::vector` for all 5 types:

```cpp
// Stored observers (re-wired to each new TurnManager)
std::vector<TurnManager::DiscardObserver> discardObservers_;
std::vector<TurnManager::ClaimObserver> claimObservers_;
std::vector<TurnManager::RoundEndObserver> roundEndObservers_;
std::vector<TurnManager::SelfKongObserver> selfKongObservers_;
std::vector<TurnManager::SelfDrawWinObserver> selfDrawWinObservers_;
```

Add new public methods:
```cpp
void addDiscardObserver(TurnManager::DiscardObserver obs);
void addClaimObserver(TurnManager::ClaimObserver obs);
void addRoundEndObserver(TurnManager::RoundEndObserver obs);
void addSelfKongObserver(TurnManager::SelfKongObserver obs);
void addSelfDrawWinObserver(TurnManager::SelfDrawWinObserver obs);
```

Keep existing `set*Observer()` methods as deprecated wrappers (clear vector + push_back) for the original 3 types.

#### Modify: `core/GameController.cpp`

- `add*Observer()` methods: push to vector + forward to current TurnManager via `turnManager_->add*Observer()`
- `set*Observer()` wrappers: clear the corresponding vector, then call `add*Observer()`
- `wireTurnManagerObservers()`: iterate all 5 vectors, call `turnManager_->add*Observer()` for each stored observer

#### Modify: `src/game/Game.cpp` (under `#ifdef HAS_TORCH`)

Update `wireTrainerObservers()` to use `addDiscardObserver` / `addClaimObserver` / `addRoundEndObserver` instead of `set*Observer`.

---

## Step 3: GameRecorder (core/)

### New file: `core/GameRecorder.h`

```cpp
#pragma once
#include "GameSnapshot.h"
#include "game/GameState.h"
#include "tiles/Tile.h"
#include "tiles/TileEnums.h"
#include "player/Meld.h"
#include "scoring/Scoring.h"
#include <vector>
#include <string>
#include <array>
#include <functional>

class GameController;  // forward declare

class GameRecorder {
public:
    void attach(GameController& controller);
    void beginRound(const GameController& controller);
    void setClientId(const std::string& id);

    std::string toJson() const;
    void clear();
    bool hasData() const;
    int decisionCount() const;

private:
    void onDiscard(int playerIndex, Tile discarded, int turnCount, int wallRemaining);
    void onClaim(int playerIndex, ClaimType type, Tile claimedTile, int turnCount, int wallRemaining);
    void onSelfKong(int playerIndex, Suit suit, uint8_t rank, int turnCount, int wallRemaining);
    void onSelfDrawWin(int playerIndex, Tile winningTile, int turnCount, int wallRemaining);
    void onRoundEnd(int winnerIndex, bool selfDrawn, const FaanResult& result, bool isDraw);

    GameController* controller_ = nullptr;
    std::string clientId_;

    struct Decision {
        std::string type;  // "discard", "claim", "selfKong", "selfDrawWin"
        int turnCount = 0;
        int wallRemaining = 0;
        GameSnapshot snapshot;
        Tile discardedTile{};
        ClaimType claimType = ClaimType::None;
        Tile claimedTile{};
        Suit kongSuit{};
        uint8_t kongRank = 0;
        Tile winningTile{};
    };

    struct RoundResult {
        int winnerIndex = -1;
        bool selfDrawn = false;
        bool isDraw = false;
        FaanResult faanResult;
        std::array<int, 4> finalScores{};
    };

    struct RoundRecord {
        int roundNumber = 0;
        Wind prevailingWind = Wind::East;
        int dealerIndex = 0;
        std::vector<Decision> decisions;
        RoundResult result;
    };

    std::vector<RoundRecord> rounds_;
    RoundRecord currentRound_;
};
```

### New file: `core/GameRecorder.cpp`

- Include `GameRecorder.h`, `GameController.h`, `JsonHelpers.h`, `<ctime>`, `<sstream>`
- `attach()`: calls `controller.addDiscardObserver(...)`, `addClaimObserver(...)`, `addSelfKongObserver(...)`, `addSelfDrawWinObserver(...)`, `addRoundEndObserver(...)` binding to the private `on*` methods. Store `controller_` pointer.
- `beginRound()`: initialize `currentRound_` with `roundNumber`, `prevailingWind`, `dealerIndex` from the controller. Clear `currentRound_.decisions`.
- `setClientId()`: sets `clientId_`

Observer callback implementations:
- All callbacks filter `playerIndex == 0` only (human). If `playerIndex != 0`, return immediately.
- `onDiscard()`: create Decision with type="discard", set discardedTile, take snapshot via `controller_->snapshot()`, push to `currentRound_.decisions`.
- `onClaim()`: create Decision with type="claim", set claimType and claimedTile, take snapshot, push.
- `onSelfKong()`: create Decision with type="selfKong", set kongSuit/kongRank, take snapshot (pre-action since observer fires before `resolveSelfKong`), push.
- `onSelfDrawWin()`: create Decision with type="selfDrawWin", set winningTile, take snapshot (pre-action since observer fires before `handleScoring`), push.
- `onRoundEnd()`: store result including finalScores from `controller_->getPlayer(i)->score()` for i=0..3. If `currentRound_.decisions` is non-empty, move `currentRound_` into `rounds_`.

Serialization:
- `toJson()`: build full JSON using `JsonHelpers.h`. Output format matches the JSON specification above. Uses `json::appendTile()`, `json::appendMeld()`, `json::escape()`, `json::claimTypeToString()`, `json::appendFaanResult()`.
- `hasData()`: returns `!rounds_.empty()`
- `decisionCount()`: sum of `decisions.size()` across all rounds
- `clear()`: clears `rounds_` vector

### Modify: `core/CMakeLists.txt`

Add `${CMAKE_CURRENT_SOURCE_DIR}/GameRecorder.cpp` to `CORE_SOURCES`.

---

## Step 4: DataUploader Interface (core/)

### New file: `core/DataUploader.h`

```cpp
#pragma once
#include <string>
#include <functional>

class DataUploader {
public:
    virtual ~DataUploader() = default;
    virtual void uploadJson(const std::string& url, const std::string& jsonBody,
                           std::function<void(bool success, int statusCode, const std::string& response)> callback) = 0;
};
```

Header-only interface. No `.cpp` needed. No CMake change.

---

## Step 5: Server — PostgreSQL + SQLAlchemy ORM + Upload Endpoint

### Step 5a: SQLAlchemy Models

#### New file: `server/models.py`

```python
from flask_sqlalchemy import SQLAlchemy
from sqlalchemy.dialects.postgresql import JSONB, ARRAY
from datetime import datetime, timezone

db = SQLAlchemy()

class GameUpload(db.Model):
    __tablename__ = 'game_uploads'
    id          = db.Column(db.Integer, primary_key=True)
    client_id   = db.Column(db.Text, nullable=False, index=True)
    version     = db.Column(db.Integer, nullable=False, default=1)
    uploaded_at = db.Column(db.DateTime(timezone=True), nullable=False,
                            default=lambda: datetime.now(timezone.utc), index=True)
    raw_json    = db.Column(JSONB)
    rounds      = db.relationship('Round', backref='upload', cascade='all, delete-orphan')

class Round(db.Model):
    __tablename__ = 'rounds'
    id                = db.Column(db.Integer, primary_key=True)
    upload_id         = db.Column(db.Integer, db.ForeignKey('game_uploads.id'), nullable=False)
    round_number      = db.Column(db.Integer, nullable=False)
    prevailing_wind   = db.Column(db.SmallInteger, nullable=False)
    dealer_index      = db.Column(db.SmallInteger, nullable=False)
    winner_index      = db.Column(db.SmallInteger)            # NULL if draw
    self_drawn        = db.Column(db.Boolean, default=False)
    is_draw           = db.Column(db.Boolean, default=False)
    total_faan        = db.Column(db.Integer, default=0)
    is_limit          = db.Column(db.Boolean, default=False)
    scoring_breakdown = db.Column(JSONB)                      # [{nameCn, nameEn, faan}]
    final_scores      = db.Column(ARRAY(db.Integer))          # [25000, 24000, ...]
    decisions         = db.relationship('Decision', backref='round', cascade='all, delete-orphan')

class Decision(db.Model):
    __tablename__ = 'decisions'
    id             = db.Column(db.Integer, primary_key=True)
    round_id       = db.Column(db.Integer, db.ForeignKey('rounds.id'), nullable=False)
    seq            = db.Column(db.SmallInteger, nullable=False)  # order within round
    type           = db.Column(db.Text, nullable=False, index=True)
    turn_count     = db.Column(db.Integer, nullable=False)
    wall_remaining = db.Column(db.Integer, nullable=False)
    snapshot       = db.Column(JSONB, nullable=False)
    discarded_tile = db.Column(JSONB)           # {suit, rank, id}
    claim_type     = db.Column(db.Text)         # 'chow', 'pung', 'kong', 'win', 'none'
    claimed_tile   = db.Column(JSONB)           # {suit, rank, id}
    kong_suit      = db.Column(db.SmallInteger)
    kong_rank      = db.Column(db.SmallInteger)
    winning_tile   = db.Column(JSONB)           # {suit, rank, id}
```

### Step 5b: Modify `server/app.py`

- Add imports: `from models import db, GameUpload, Round, Decision`
- Add `from flask import request` to existing import
- Initialize Flask-SQLAlchemy:
  ```python
  db_url = os.environ.get("DATABASE_URL", "postgresql://localhost/mahjong")
  app.config['SQLALCHEMY_DATABASE_URI'] = db_url
  db.init_app(app)
  ```
- Add `--db-url` CLI arg (default: env `DATABASE_URL` or `postgresql://localhost/mahjong`). Set `app.config['SQLALCHEMY_DATABASE_URI']` from it.
- On startup in `main()`: within app context, call `db.create_all()` to auto-create tables.

Add `POST /api/games` route:
1. Validate JSON body has `"rounds"` array with decisions
2. Create `GameUpload` with `raw_json` backup of full body
3. For each round: create `Round` row with scoring data and `finalScores`
4. For each decision: create `Decision` row with type-specific fields + snapshot
5. `db.session.commit()` in a single transaction
6. Return 201 with `{"status":"ok","uploadId":N,"rounds":R,"decisions":D}`

Add `GET /api/training/decisions` route:
- Query params: `type` (filter by decision type), `limit` (default 100), `offset` (default 0), `since` (ISO timestamp)
- SQLAlchemy query: `Decision.query.filter_by(type=type).order_by(Decision.id).offset(offset).limit(limit)`
- Return paginated decisions with snapshots as JSON
- Response: `{"decisions": [...], "total": N, "limit": L, "offset": O}`

### Step 5c: Modify `server/requirements.txt`

Add:
```
flask-sqlalchemy>=3.1
psycopg2-binary>=2.9
```

### Step 5d: Docker Setup

#### New file: `server/Dockerfile`

```dockerfile
FROM python:3.11-slim
WORKDIR /app
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt
COPY . .
EXPOSE 8080
CMD ["python", "app.py", "--port", "8080"]
```

#### New file: `docker-compose.yml` (project root)

```yaml
services:
  server:
    build: ./server
    ports:
      - "8080:8080"
    environment:
      - DATABASE_URL=${DATABASE_URL:-postgresql://mahjong:mahjong@db:5432/mahjong}
    depends_on:
      db:
        condition: service_healthy

  db:
    image: postgres:16
    environment:
      POSTGRES_USER: mahjong
      POSTGRES_PASSWORD: mahjong
      POSTGRES_DB: mahjong
    ports:
      - "5432:5432"
    volumes:
      - pgdata:/var/lib/postgresql/data
    healthcheck:
      test: ["CMD-SHELL", "pg_isready -U mahjong"]
      interval: 5s
      timeout: 5s
      retries: 5

volumes:
  pgdata:
```

---

## Step 6: Desktop Integration

### New file: `src/net/HttpUploader.h`

```cpp
#pragma once
#include "DataUploader.h"
#include <string>

class HttpUploader : public DataUploader {
public:
    void uploadJson(const std::string& url, const std::string& jsonBody,
                   std::function<void(bool success, int statusCode, const std::string& response)> callback) override;
};
```

### New file: `src/net/HttpUploader.cpp`

- Implements `DataUploader` using POSIX sockets (`<sys/socket.h>`, `<netdb.h>`, `<unistd.h>`)
- Parse URL into host/port/path using string operations
- Open TCP socket, connect, send HTTP/1.1 POST with `Content-Type: application/json` and `Content-Length`
- Read response, parse status code from first line
- Close socket
- Call callback with success/failure
- Blocking call — caller wraps in `std::thread` if needed

### Modify: `src/game/Game.h`

Add (unconditionally, NOT behind `#ifdef HAS_TORCH`):
```cpp
#include "GameRecorder.h"

// ... in private section:
GameRecorder recorder_;
std::unique_ptr<DataUploader> uploader_;
void uploadReplayData();
```

Add forward declaration or include for `DataUploader.h`.

### Modify: `src/game/Game.cpp`

- Add `#include "net/HttpUploader.h"` and `#include "DataUploader.h"` at top
- In `init()`, after `controller_.startNewGame()`:
  ```cpp
  recorder_.setClientId("desktop");
  recorder_.attach(controller_);
  recorder_.beginRound(controller_);
  uploader_ = std::make_unique<HttpUploader>();
  ```
- Restructure `setRoundAdvanceCallback` — currently only set inside `#ifdef HAS_TORCH` in `wireTrainerObservers()`. The callback must be set unconditionally so it always calls `uploadReplayData()` + `recorder_.beginRound()`. Inside `#ifdef HAS_TORCH`, it additionally calls `onRoundComplete()`.

  Concretely, move `controller_.setRoundAdvanceCallback(...)` out of `wireTrainerObservers()` and into `init()`:
  ```cpp
  controller_.setRoundAdvanceCallback([this]() {
      uploadReplayData();
      recorder_.beginRound(controller_);
  #ifdef HAS_TORCH
      onRoundComplete();
  #endif
  });
  ```
  Then remove the `setRoundAdvanceCallback` call from `wireTrainerObservers()`.

- Implement `uploadReplayData()`:
  ```cpp
  void Game::uploadReplayData() {
      if (!recorder_.hasData()) return;
      std::string json = recorder_.toJson();
      const char* url = std::getenv("MAHJONG_UPLOAD_URL");
      std::string uploadUrl = url ? url : "http://localhost:8080/api/games";
      uploader_->uploadJson(uploadUrl, json, [this](bool success, int statusCode, const std::string& response) {
          if (success && statusCode >= 200 && statusCode < 300) {
              std::printf("[Replay] Upload successful (%d decisions)\n", recorder_.decisionCount());
              recorder_.clear();
          } else {
              std::printf("[Replay] Upload failed (status %d), retrying next round\n", statusCode);
          }
      });
  }
  ```

- In `shutdown()`, before existing cleanup: call `uploadReplayData()` to flush remaining data.

### Modify: `CMakeLists.txt`

Add `src/net/HttpUploader.cpp` to `DESKTOP_SOURCES`:
```cmake
set(DESKTOP_SOURCES
    ...
    ${CMAKE_CURRENT_SOURCE_DIR}/src/net/HttpUploader.cpp
)
```

---

## Step 7: iOS Integration

### New file: `ios/MahjongHK/NSURLSessionUploader.h`

```objc
#pragma once
#include "DataUploader.h"

class NSURLSessionUploader : public DataUploader {
public:
    void uploadJson(const std::string& url, const std::string& jsonBody,
                   std::function<void(bool success, int statusCode, const std::string& response)> callback) override;
};
```

### New file: `ios/MahjongHK/NSURLSessionUploader.mm`

- Implements `DataUploader::uploadJson` using `NSURLSession dataTaskWithRequest:completionHandler:`
- Creates `NSMutableURLRequest` with POST, `Content-Type: application/json`, body from `jsonBody`
- Fire-and-forget async POST
- Calls callback on completion handler

### Modify: `ios/MahjongHK/RaylibBridge.mm`

- Add includes: `#include "GameRecorder.h"`, `#include "NSURLSessionUploader.h"`
- Add ivars to `@implementation RaylibBridge { ... }`:
  ```cpp
  GameRecorder _recorder;
  std::unique_ptr<NSURLSessionUploader> _uploader;
  ```
- In `initWithWidth:...`, after `_controller->startNewGame()` and AI wiring:
  ```cpp
  _recorder.setClientId("ios");
  _recorder.attach(*_controller);
  _recorder.beginRound(*_controller);
  _uploader = std::make_unique<NSURLSessionUploader>();

  _controller->setRoundAdvanceCallback([this]() {
      if (_recorder.hasData()) {
          std::string json = _recorder.toJson();
          _uploader->uploadJson("http://your-server:8080/api/games", json,
              [this](bool ok, int status, const std::string&) {
                  if (ok && status >= 200 && status < 300) _recorder.clear();
              });
      }
      _recorder.beginRound(*_controller);
  });
  ```
- In `shutdown`: upload remaining data before cleanup.

---

## Files Summary

| File | Action | Step |
|------|--------|------|
| `core/JsonHelpers.h` | **New** — shared inline JSON helpers | 1 |
| `core/mahjong_api.cpp` | Remove static helpers, use JsonHelpers | 1 |
| `src/game/TurnManager.h` | Single → vector observer storage, add 2 new observer types | 2 |
| `src/game/TurnManager.cpp` | Fire all observers in vectors (6 existing + 2 new sites) | 2 |
| `core/GameController.h` | Vector observer storage for all 5 types, add*Observer API | 2 |
| `core/GameController.cpp` | Implement multi-observer add/wire for all 5 types | 2 |
| `src/game/Game.cpp` | Update wireTrainerObservers to use add*Observer | 2 |
| `core/GameRecorder.h` | **New** — records human game events (4 decision types) | 3 |
| `core/GameRecorder.cpp` | **New** — observer hooks + JSON serialization | 3 |
| `core/DataUploader.h` | **New** — abstract upload interface (header-only) | 4 |
| `core/CMakeLists.txt` | Add GameRecorder.cpp | 3 |
| `server/models.py` | **New** — SQLAlchemy ORM models (GameUpload, Round, Decision) | 5 |
| `server/app.py` | Add POST /api/games, GET /api/training/decisions, SQLAlchemy init | 5 |
| `server/requirements.txt` | Add flask-sqlalchemy, psycopg2-binary | 5 |
| `server/Dockerfile` | **New** — Python 3.11 slim container | 5 |
| `docker-compose.yml` | **New** — Flask + Postgres 16 | 5 |
| `src/net/HttpUploader.h` | **New** — POSIX socket HTTP POST header | 6 |
| `src/net/HttpUploader.cpp` | **New** — POSIX socket implementation | 6 |
| `src/game/Game.h` | Add GameRecorder + uploader members | 6 |
| `src/game/Game.cpp` | Wire recorder, restructure roundAdvanceCb, upload | 6 |
| `CMakeLists.txt` | Add HttpUploader.cpp to DESKTOP_SOURCES | 6 |
| `ios/MahjongHK/NSURLSessionUploader.h` | **New** — iOS uploader header | 7 |
| `ios/MahjongHK/NSURLSessionUploader.mm` | **New** — NSURLSession implementation | 7 |
| `ios/MahjongHK/RaylibBridge.mm` | Add GameRecorder, upload after round | 7 |

## Implementation Order

1. **Step 1** (JsonHelpers) — zero risk, pure refactor
2. **Step 2** (Multi-observer) — medium risk, core API change, test that game still works
3. **Step 5** (Server endpoint) — independent of C++ changes, can be done in parallel
4. **Step 3** (GameRecorder) — depends on steps 1+2
5. **Step 4** (DataUploader) — header-only interface
6. **Step 6** (Desktop integration) — depends on 3+4+5
7. **Step 7** (iOS integration) — depends on 3+4+5, can parallel with step 6

## Key Design Notes

### Snapshot Timing

- **Discard/claim** observers fire AFTER the action (tile removed from hand). The action tile is recorded separately so the pre-decision hand is reconstructable: `concealed + discardedTile`.
- **Self-kong and self-draw-win** observers fire BEFORE the action (inserted before `resolveSelfKong` / `handleScoring`), so the snapshot shows the hand at decision time.

### Memory

Each `GameSnapshot` is ~2KB. With ~15 human decisions per round, that's ~30KB per round. Even 10 rounds of failed uploads = ~300KB. `clear()` after successful upload keeps memory bounded.

### Retry

On upload failure, data is retained and retried on the next round end. No exponential backoff — best-effort. If the server is permanently down, data accumulates in memory until game exit (final upload attempt in `shutdown()`).

### Thread Safety

Upload is blocking on desktop (POSIX socket) and async on iOS (NSURLSession). On desktop, upload happens in the round-advance callback (single-threaded game loop). If upload latency becomes noticeable, wrap in `std::thread` with detach.

## Verification

1. After step 2: `cmake -B build && cmake --build build && ./build/mahjong` — game plays normally
2. Start server + DB: `docker compose up`
3. Health check: `curl http://localhost:8080/health`
4. Play a round on desktop → `[Replay] Upload successful` in terminal
5. Query database: `docker compose exec db psql -U mahjong -c "SELECT count(*) FROM decisions;"`
6. Inspect a decision: `docker compose exec db psql -U mahjong -c "SELECT type, snapshot->'humanConcealed' FROM decisions LIMIT 1;"`
7. Query training endpoint: `curl http://localhost:8080/api/training/decisions?type=discard&limit=5`
8. Play on iOS simulator → same data appears in database
