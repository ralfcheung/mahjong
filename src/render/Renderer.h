#pragma once
#include "raylib.h"
#include "TileTextureAtlas.h"
#include "TileRenderer.h"
#include "tiles/TileEnums.h"
#include "player/Player.h"
#include "game/GameState.h"
#include "AssetResolver.h"
#include <array>
#include <memory>
#include <vector>
#include <string>

// Platform-specific UI sizing. Desktop uses smaller defaults,
// mobile uses touch-friendly sizes (44pt+ tap targets).
struct UIConfig {
    float buttonHeight = 45.0f;     // Claim/action button height
    float buttonMinWidth = 100.0f;  // Minimum button width
    float chowButtonWidth = 120.0f; // Wider button for chow combos
    float buttonGap = 15.0f;        // Gap between buttons
    float fontSize = 20.0f;         // Base UI font size
    float labelFontSize = 22.0f;    // Player label font size
    float safeAreaTop = 0.0f;       // Safe area insets (for notch/status bar)
    float safeAreaBottom = 0.0f;
    float safeAreaLeft = 0.0f;
    float safeAreaRight = 0.0f;

    static UIConfig desktop() { return UIConfig{}; }

    static UIConfig mobile() {
        UIConfig c;
        c.buttonHeight = 70.0f;
        c.buttonMinWidth = 140.0f;
        c.chowButtonWidth = 180.0f;
        c.buttonGap = 20.0f;
        c.fontSize = 32.0f;
        c.labelFontSize = 36.0f;
        return c;
    }
};

struct RenderState {
    GamePhase phase = GamePhase::DEALING;
    int activePlayerIndex = 0;
    Wind prevailingWind = Wind::East;
    int wallRemaining = 0;

    struct PlayerView {
        const Player* player = nullptr;
    };
    std::array<PlayerView, 4> players;

    // Claim phase UI (with chow combos expanded)
    std::vector<ClaimOption> humanClaimOptions;

    // Self-kong options during player's turn
    bool canSelfKong = false;

    // Self-draw win option during player's turn
    bool canSelfDrawWin = false;

    // Scoring display
    std::string scoringText;
    int winnerIndex = -1; // Player who won (-1 if no winner yet)

    // Last discarded tile (for highlight)
    const Tile* lastDiscard = nullptr;

    // AI thinking indicator
    bool aiThinking = false;
};

class Renderer {
public:
    bool init(AssetResolver& assets, const UIConfig& uiConfig = UIConfig::desktop());
    void shutdown();

    void render(const RenderState& state);

    Camera3D& camera() { return camera_; }
    Font& chineseFont() { return chineseFont_; }
    Font& englishFont() { return englishFont_; }
    TileRenderer& tileRenderer() { return tileRenderer_; }

    // For input: get the position of a tile in the human player's hand
    Vector3 getHandTilePosition(int playerIndex, int tileIndex, int totalTiles) const;
    BoundingBox getHandTileBBox(int playerIndex, int tileIndex, int totalTiles) const;

    const UIConfig& uiConfig() const { return uiConfig_; }

private:
    Camera3D camera_{};
    Font chineseFont_{};
    Font englishFont_{};
    TileTextureAtlas atlas_;
    TileRenderer tileRenderer_;
    Texture2D feltTexture_{};
    UIConfig uiConfig_;

    // Cached during 3D pass for 2D overlay use
    mutable Vector2 claimTileScreenPos_{};
    mutable bool claimTileScreenValid_ = false;

    void generateFeltTexture();

    void drawTable();
    void drawPlayerHand(const Player* player, int playerIndex, bool faceUp, bool flat = false);
    void drawPlayerDiscards(const Player* player, int playerIndex);
    void drawPlayerMelds(const Player* player, int playerIndex);
    void drawPlayerFlowers(const Player* player, int playerIndex);
    void drawWall(int wallRemaining);
    void drawClaimHighlights(const RenderState& state);
    void drawUIOverlay(const RenderState& state);
    void drawPlayerLabel(const Player* player, int playerIndex, bool isActive);

    // Spatial layout helpers
    float getPlayerRotation(int playerIndex) const;
};
