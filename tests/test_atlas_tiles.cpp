// Visual test: draws every tile from the atlas in a labelled grid.
// Verifies that getSourceRect2D + texture2D produce the correct tile face.
//
// Build:  cmake --build build --target test_atlas_tiles
// Run:    ./build/test_atlas_tiles
//
// The window shows a 7-column grid of all 42 tile types.
// Each cell has the tile image from the atlas and a text label below it.
// Visually confirm that every label matches its image.
// Press ESC or Q to quit, S to save a screenshot.

#include "raylib.h"
#include "render/TileTextureAtlas.h"
#include "tiles/Tile.h"
#include "AssetResolver.h"
#include <cstdio>
#include <vector>

struct TileEntry {
    Suit suit;
    uint8_t rank;
};

int main(int argc, char** argv) {
    bool headless = (argc > 1 && std::string(argv[1]) == "--headless");
    // Build the canonical tile list (same order as atlas generation)
    std::vector<TileEntry> tiles;
    for (uint8_t r = 1; r <= 9; r++) tiles.push_back({Suit::Bamboo, r});
    for (uint8_t r = 1; r <= 9; r++) tiles.push_back({Suit::Characters, r});
    for (uint8_t r = 1; r <= 9; r++) tiles.push_back({Suit::Dots, r});
    for (uint8_t r = 1; r <= 4; r++) tiles.push_back({Suit::Wind, r});
    for (uint8_t r = 1; r <= 3; r++) tiles.push_back({Suit::Dragon, r});
    for (uint8_t r = 1; r <= 4; r++) tiles.push_back({Suit::Flower, r});
    for (uint8_t r = 1; r <= 4; r++) tiles.push_back({Suit::Season, r});

    const int cols = TileTextureAtlas::GRID_COLS;  // 7
    const int rows = (int)tiles.size() / cols + ((tiles.size() % cols) ? 1 : 0);

    const int cellW = 120;
    const int cellH = 150;  // extra space for label
    const int tileImgH = 110;
    const int margin = 10;
    int winW = cols * cellW + margin * 2;
    int winH = rows * cellH + margin * 2;

    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(winW, winH, "Atlas Tile Test — verify each label matches its image");
    SetTargetFPS(30);

    // Load fonts and generate atlas (same as game)
    DesktopAssetResolver assets(ASSETS_PATH);
    Font cnFont = LoadFontEx(assets.resolveC("fonts/NotoSansSC-Regular.ttf"), 48, nullptr, 0x9FFF);
    Font enFont = GetFontDefault();

    TileTextureAtlas atlas;
    atlas.generate(cnFont, enFont, assets);

    printf("=== Atlas Tile Test ===\n");
    printf("Tiles: %zu  Grid: %dx%d  Atlas: %dx%d\n",
           tiles.size(), cols, rows, atlas.atlasWidth(), atlas.atlasHeight());

    // Verify getSourceRect2D returns expected positions
    int errors = 0;
    for (size_t i = 0; i < tiles.size(); i++) {
        int expectedCol = (int)(i % cols);
        int expectedRow = (int)(i / cols);
        float expectedX = (float)(expectedCol * TileTextureAtlas::CELL_SIZE);
        float expectedY = (float)(expectedRow * TileTextureAtlas::CELL_SIZE);

        Rectangle r = atlas.getSourceRect2D(tiles[i].suit, tiles[i].rank);
        Tile t{tiles[i].suit, tiles[i].rank, 0};

        if (r.x != expectedX || r.y != expectedY ||
            r.width != (float)TileTextureAtlas::CELL_SIZE ||
            r.height != (float)TileTextureAtlas::CELL_SIZE) {
            printf("FAIL [%zu] %s: expected (%.0f,%.0f,%.0f,%.0f) got (%.0f,%.0f,%.0f,%.0f)\n",
                   i, t.toString().c_str(),
                   expectedX, expectedY,
                   (float)TileTextureAtlas::CELL_SIZE, (float)TileTextureAtlas::CELL_SIZE,
                   r.x, r.y, r.width, r.height);
            errors++;
        } else {
            printf("OK   [%zu] %-20s  src=(%.0f,%.0f)\n", i, t.toString().c_str(), r.x, r.y);
        }
    }

    if (errors == 0) {
        printf("\nAll %zu tiles PASSED coordinate check.\n", tiles.size());
    } else {
        printf("\n%d/%zu tiles FAILED coordinate check.\n", errors, tiles.size());
    }
    printf("Visual check: verify each image matches its label in the window.\n\n");
    fflush(stdout);

    if (headless) {
        atlas.unload();
        UnloadFont(cnFont);
        CloseWindow();
        return errors;
    }

    while (!WindowShouldClose()) {
        if (IsKeyPressed(KEY_Q)) break;
        if (IsKeyPressed(KEY_S)) {
            TakeScreenshot("atlas_tile_test.png");
            printf("Screenshot saved to atlas_tile_test.png\n");
        }

        BeginDrawing();
        ClearBackground(Color{40, 40, 45, 255});

        for (size_t i = 0; i < tiles.size(); i++) {
            int col = (int)(i % cols);
            int row = (int)(i / cols);
            float x = margin + col * cellW;
            float y = margin + row * cellH;

            // Draw tile image from atlas2D
            Rectangle src = atlas.getSourceRect2D(tiles[i].suit, tiles[i].rank);
            float imgW = (float)(cellW - 10);
            float imgH = (float)tileImgH - 10;
            Rectangle dst = {x + 5, y + 2, imgW, imgH};

            // Background
            DrawRectangleRec({x + 2, y + 2, (float)cellW - 4, (float)cellH - 4},
                             Color{60, 60, 65, 255});
            DrawTexturePro(atlas.texture2D(), src, dst, {0, 0}, 0.0f, WHITE);

            // Label
            Tile t{tiles[i].suit, tiles[i].rank, 0};
            const char* cn = t.chineseName();
            const char* en = t.englishName();
            char label[64];
            snprintf(label, sizeof(label), "%s %s", cn, en);
            DrawTextEx(cnFont, label, {x + 5, y + tileImgH - 5}, 14, 1, WHITE);
        }

        DrawText("Press S to save screenshot, ESC/Q to quit", margin, winH - 20, 10, GRAY);
        EndDrawing();
    }

    atlas.unload();
    UnloadFont(cnFont);
    CloseWindow();

    return errors;
}
