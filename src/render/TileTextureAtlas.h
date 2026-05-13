#pragma once
#include "raylib.h"
#include "tiles/TileEnums.h"
#include "AssetResolver.h"
#include <unordered_map>

class TileTextureAtlas {
public:
    /// Set the tile image version folder (e.g. "v1", "v2"). Defaults to "v1".
    void setTileVersion(const std::string& version) { tileVersion_ = version; }
    const std::string& tileVersion() const { return tileVersion_; }

    void generate(Font& chineseFont, Font& englishFont, AssetResolver& assets);
    void unload();

    // Get the atlas texture (RenderTexture — for 3D face quads)
    Texture2D texture() const { return atlas_.texture; }

    // Get source rectangle for 3D use (Y-flipped for RenderTexture)
    Rectangle getSourceRect(Suit suit, uint8_t rank) const;

    // Regular texture for 2D UI drawing (no Y-flip issues)
    Texture2D texture2D() const { return atlas2D_; }

    // Get source rectangle for 2D DrawTexturePro (standard orientation)
    Rectangle getSourceRect2D(Suit suit, uint8_t rank) const;

    static constexpr int CELL_SIZE = 256;
    static constexpr int GRID_COLS = 7;
    static constexpr int GRID_ROWS = 7;

    int atlasWidth() const { return GRID_COLS * CELL_SIZE; }
    int atlasHeight() const { return GRID_ROWS * CELL_SIZE; }

private:
    RenderTexture2D atlas_{};
    Texture2D atlas2D_{};  // Regular copy for 2D UI drawing
    std::unordered_map<uint16_t, Rectangle> uvMap_; // stores raw pixel rects (before flip)
    std::string tileVersion_ = "live";

    uint16_t tileKey(Suit suit, uint8_t rank) const {
        return (static_cast<uint16_t>(suit) << 8) | rank;
    }

    void renderTileFace(Font& cnFont, Font& enFont,
                        Suit suit, uint8_t rank,
                        int gridX, int gridY);
};
