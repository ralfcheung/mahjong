#pragma once
#include "raylib.h"
#include "TileTextureAtlas.h"
#include "tiles/Tile.h"
#include <vector>

class TileRenderer {
public:
    // Tile dimensions in world units
    static constexpr float TILE_W = 0.72f;   // width (narrow face)
    static constexpr float TILE_H = 0.98f;   // height (tall face)
    static constexpr float TILE_D = 0.50f;   // depth (thickness)

    void init(TileTextureAtlas& atlas);
    void unload();

    // Tilt angle for human player's hand tiles (degrees from vertical)
    static constexpr float PLAYER_TILT = -65.0f;

    // Draw a single tile standing upright at position, rotated around Y axis
    // faceUp: true = show tile face, false = show back (for AI hands)
    void drawTileStanding(const Tile& tile, Vector3 position, float rotationYDeg,
                          bool faceUp, bool highlighted = false);

    // Draw a tile lying flat on the table (for discards)
    void drawTileFlat(const Tile& tile, Vector3 position, float rotationYDeg,
                      bool highlighted = false);

    // Draw a tile tilted backward (for human player's hand - readable from above)
    void drawTileTilted(const Tile& tile, Vector3 position, float rotationYDeg,
                        float tiltDeg, bool highlighted = false);

    // Draw a tile back (face down, standing)
    void drawTileBackStanding(Vector3 position, float rotationYDeg);

    // Draw a tile back (face down, flat on table - for wall tiles)
    void drawTileFlatBack(Vector3 position, float rotationYDeg);

    // Draw a pulsing glow effect around a tile position (flat on table)
    void drawGlowFlat(Vector3 position, float rotationYDeg, Color color, float pulse);
    // Draw a pulsing glow effect around a tilted tile
    void drawGlowTilted(Vector3 position, float rotationYDeg, float tiltDeg, Color color, float pulse);

private:
    TileTextureAtlas* atlas_ = nullptr;
    Texture2D faceTexture_{};

    void drawTileBody(Vector3 pos, Vector3 size, float rotY, Color color);
    // Two-tone body: backDir encodes split axis+side (-3=back is -Z, +2=back is +Y, -2=back is -Y)
    void drawTileBody(Vector3 pos, Vector3 size, float rotY, Color frontColor,
                      Color backColor, float backFrac, int backDir);
    void drawShadow(Vector3 basePos, float width, float depth, float rotY);
    void drawFaceQuad(Vector3 pos, float rotY, const Tile& tile, bool standing);
};
