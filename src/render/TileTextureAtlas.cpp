#include "TileTextureAtlas.h"
#include "tiles/TileDefs.h"
#include <cstring>
#include <cmath>
#include <vector>

// Map suit+rank to PNG filename in assets/tiles/<suit>/<version>/
static const char* getTileImageFile(Suit suit, uint8_t rank, const std::string& version) {
    static char buf[128];
    switch (suit) {
        case Suit::Bamboo:
            snprintf(buf, sizeof(buf), "bamboo/%s/Sou%d.png", version.c_str(), rank);
            return buf;
        case Suit::Characters:
            snprintf(buf, sizeof(buf), "characters/%s/Man%d.png", version.c_str(), rank);
            return buf;
        case Suit::Dots:
            snprintf(buf, sizeof(buf), "dots/%s/Pin%d.png", version.c_str(), rank);
            return buf;
        case Suit::Wind: {
            const char* name = nullptr;
            switch (rank) {
                case 1: name = "Ton"; break;
                case 2: name = "Nan"; break;
                case 3: name = "Shaa"; break;
                case 4: name = "Pei"; break;
            }
            if (!name) return nullptr;
            snprintf(buf, sizeof(buf), "winds/%s/%s.png", version.c_str(), name);
            return buf;
        }
        case Suit::Dragon: {
            const char* name = nullptr;
            switch (rank) {
                case 1: name = "Chun"; break;
                case 2: name = "Hatsu"; break;
                case 3: name = "Haku"; break;
            }
            if (!name) return nullptr;
            snprintf(buf, sizeof(buf), "dragons/%s/%s.png", version.c_str(), name);
            return buf;
        }
        case Suit::Flower:
            snprintf(buf, sizeof(buf), "flowers/%s/Flower%d.png", version.c_str(), rank);
            return buf;
        case Suit::Season:
            snprintf(buf, sizeof(buf), "seasons/%s/Season%d.png", version.c_str(), rank);
            return buf;
        default:
            return nullptr;
    }
}

// ---- Flower/Season: character with decorative frame (procedural fallback) ----
static void drawBonusFace(int cx, int cy, int cellSize, Suit suit, uint8_t rank, Font& cnFont) {
    const char* cnText = (suit == Suit::Flower) ? CHAR_FLOWER_CN[rank-1] : CHAR_SEASON_CN[rank-1];
    const char* enText = (suit == Suit::Flower) ? CHAR_FLOWER_EN[rank-1] : CHAR_SEASON_EN[rank-1];
    Color col = (suit == Suit::Flower) ? Color{130, 40, 140, 255} : Color{180, 100, 20, 255};

    // Decorative corner lines
    int pad = cellSize / 8;
    int len = cellSize / 5;
    Color frameCol = Fade(col, 0.4f);
    DrawLine(cx + pad, cy + pad, cx + pad + len, cy + pad, frameCol);
    DrawLine(cx + pad, cy + pad, cx + pad, cy + pad + len, frameCol);
    DrawLine(cx + cellSize - pad, cy + pad, cx + cellSize - pad - len, cy + pad, frameCol);
    DrawLine(cx + cellSize - pad, cy + pad, cx + cellSize - pad, cy + pad + len, frameCol);
    DrawLine(cx + pad, cy + cellSize - pad, cx + pad + len, cy + cellSize - pad, frameCol);
    DrawLine(cx + pad, cy + cellSize - pad, cx + pad, cy + cellSize - pad - len, frameCol);
    DrawLine(cx + cellSize - pad, cy + cellSize - pad, cx + cellSize - pad - len, cy + cellSize - pad, frameCol);
    DrawLine(cx + cellSize - pad, cy + cellSize - pad, cx + cellSize - pad, cy + cellSize - pad - len, frameCol);

    // Number in top-left
    char numBuf[4];
    snprintf(numBuf, sizeof(numBuf), "%d", rank);
    DrawTextEx(cnFont, numBuf, {(float)(cx + pad + 4), (float)(cy + pad + 2)}, cellSize * 0.15f, 1, col);

    // Large Chinese character centered
    float fontSize = cellSize * 0.50f;
    Vector2 m = MeasureTextEx(cnFont, cnText, fontSize, 2);
    float tx = cx + (cellSize - m.x) / 2.0f;
    float ty = cy + (cellSize - m.y) / 2.0f - cellSize * 0.06f;
    DrawTextEx(cnFont, cnText, {tx, ty}, fontSize, 2, col);

    // English name at bottom
    float enSize = cellSize * 0.13f;
    Vector2 em = MeasureTextEx(cnFont, enText, enSize, 1);
    DrawTextEx(cnFont, enText, {cx + (cellSize - em.x) / 2.0f, (float)(cy + cellSize - pad - em.y)},
               enSize, 1, Fade(col, 0.6f));
}

// ====================================================================

// Padding around the tile graphic within each cell (in pixels).
// 0 = graphic fills the entire cell, positive = inset, negative = zoom in (crop edges).
static constexpr int TILE_MARGIN = 30;

void TileTextureAtlas::generate(Font& cnFont, Font& enFont, AssetResolver& assets) {
    int atlasW = GRID_COLS * CELL_SIZE;
    int atlasH = GRID_ROWS * CELL_SIZE;
    atlas_ = LoadRenderTexture(atlasW, atlasH);

    // Define tile order in atlas
    struct TileEntry { Suit suit; uint8_t rank; };
    std::vector<TileEntry> tiles;
    for (uint8_t r = 1; r <= 9; r++) tiles.push_back({Suit::Bamboo, r});
    for (uint8_t r = 1; r <= 9; r++) tiles.push_back({Suit::Characters, r});
    for (uint8_t r = 1; r <= 9; r++) tiles.push_back({Suit::Dots, r});
    for (uint8_t r = 1; r <= 4; r++) tiles.push_back({Suit::Wind, r});
    for (uint8_t r = 1; r <= 3; r++) tiles.push_back({Suit::Dragon, r});
    for (uint8_t r = 1; r <= 4; r++) tiles.push_back({Suit::Flower, r});
    for (uint8_t r = 1; r <= 4; r++) tiles.push_back({Suit::Season, r});

    // Pre-load PNG tile images at original resolution (before entering texture mode)
    struct LoadedTile { Texture2D tex; bool loaded; };
    std::vector<LoadedTile> loaded(tiles.size());

    for (size_t i = 0; i < tiles.size(); i++) {
        const char* fn = getTileImageFile(tiles[i].suit, tiles[i].rank, tileVersion_);
        loaded[i].loaded = false;
        if (fn) {
            std::string tilePath = "tiles/";
            tilePath += fn;
            std::string fullPath = assets.resolve(tilePath);
            Image img = LoadImage(fullPath.c_str());
            if (img.data) {
                loaded[i].tex = LoadTextureFromImage(img);
                SetTextureFilter(loaded[i].tex, TEXTURE_FILTER_BILINEAR);
                loaded[i].loaded = true;
                UnloadImage(img);
            }
        }
    }

    // Compose atlas
    BeginTextureMode(atlas_);
    ClearBackground(BLANK);

    int col = 0, row = 0;
    for (size_t i = 0; i < tiles.size(); i++) {
        int px = col * CELL_SIZE;
        int py = row * CELL_SIZE;

        if (loaded[i].loaded) {
            // Flowers/seasons: no margin (they have their own layout)
            // Other tiles: use TILE_MARGIN
            bool isBonus = (tiles[i].suit == Suit::Flower || tiles[i].suit == Suit::Season);
            int margin = isBonus ? 4 : TILE_MARGIN;
            Rectangle src = {0, 0, (float)loaded[i].tex.width, (float)loaded[i].tex.height};
            Rectangle dst = {(float)(px + margin), (float)(py + margin),
                             (float)(CELL_SIZE - margin * 2), (float)(CELL_SIZE - margin * 2)};
            DrawTexturePro(loaded[i].tex, src, dst, {0, 0}, 0, WHITE);
        } else {
            // Procedural fallback for flowers/seasons
            renderTileFace(cnFont, enFont, tiles[i].suit, tiles[i].rank, col, row);
        }

        uvMap_[tileKey(tiles[i].suit, tiles[i].rank)] = {(float)px, (float)py, (float)CELL_SIZE, (float)CELL_SIZE};

        col++;
        if (col >= GRID_COLS) { col = 0; row++; }
    }

    EndTextureMode();

    // Enable smooth filtering on the atlas texture
    SetTextureFilter(atlas_.texture, TEXTURE_FILTER_BILINEAR);

    // Create a regular Texture2D copy for 2D UI drawing.
    // RenderTextures store pixels bottom-to-top (OpenGL convention),
    // so we extract the image, flip it, and create a normal texture.
    Image atlasImg = LoadImageFromTexture(atlas_.texture);
    ImageFlipVertical(&atlasImg);
    atlas2D_ = LoadTextureFromImage(atlasImg);
    SetTextureFilter(atlas2D_, TEXTURE_FILTER_BILINEAR);
    UnloadImage(atlasImg);

    // Unload individual textures
    for (auto& lt : loaded) {
        if (lt.loaded) UnloadTexture(lt.tex);
    }
}

void TileTextureAtlas::renderTileFace(Font& cnFont, Font& enFont,
                                       Suit suit, uint8_t rank,
                                       int gridX, int gridY) {
    int px = gridX * CELL_SIZE;
    int py = gridY * CELL_SIZE;

    // Only used for flowers/seasons now
    if (suit == Suit::Flower || suit == Suit::Season) {
        drawBonusFace(px, py, CELL_SIZE, suit, rank, cnFont);
    }
}

Rectangle TileTextureAtlas::getSourceRect(Suit suit, uint8_t rank) const {
    auto it = uvMap_.find(tileKey(suit, rank));
    if (it != uvMap_.end()) {
        Rectangle r = it->second;
        r.height = -r.height;
        return r;
    }
    return {0, 0, (float)CELL_SIZE, -(float)CELL_SIZE};
}

Rectangle TileTextureAtlas::getSourceRect2D(Suit suit, uint8_t rank) const {
    auto it = uvMap_.find(tileKey(suit, rank));
    if (it != uvMap_.end()) {
        return it->second; // Raw pixel rect, positive height — works with atlas2D_
    }
    return {0, 0, (float)CELL_SIZE, (float)CELL_SIZE};
}

void TileTextureAtlas::unload() {
    UnloadRenderTexture(atlas_);
    if (atlas2D_.id > 0) UnloadTexture(atlas2D_);
}
