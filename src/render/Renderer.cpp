#include "Renderer.h"
#include "tiles/TileDefs.h"
#include "rlgl.h"
#include <cmath>
#include <cstring>
#include <cstdlib>

// Table dimensions
static constexpr float TABLE_SIZE = 18.0f;
static constexpr float HAND_DISTANCE = 7.5f;  // Distance from center for each player's hand
static constexpr float TILE_SPACING = 0.80f;   // Space between tiles in hand
static constexpr float MELD_OFFSET = 4.5f;     // Offset for melds from center of player's side
static constexpr float DISCARD_DISTANCE = 2.5f; // Distance from center for discard area

// Collect all unique codepoints needed for Chinese rendering
static int* getChineseCodepoints(int& count) {
    // We need codepoints for all Chinese characters used on tiles and UI
    static std::vector<int> codepoints;
    if (!codepoints.empty()) {
        count = (int)codepoints.size();
        return codepoints.data();
    }

    // Add ASCII range
    for (int i = 32; i < 127; i++) codepoints.push_back(i);

    // Add all Chinese characters from tile definitions
    auto addString = [&](const char* str) {
        int len = (int)strlen(str);
        int i = 0;
        while (i < len) {
            int bytesProcessed = 0;
            int cp = GetCodepoint(str + i, &bytesProcessed);
            if (cp > 127) {
                bool found = false;
                for (int existing : codepoints) {
                    if (existing == cp) { found = true; break; }
                }
                if (!found) codepoints.push_back(cp);
            }
            i += bytesProcessed;
        }
    };

    // Tile face characters
    addString("萬"); // used standalone in character tiles
    addString("一二三四五六七八九"); // Chinese numerals
    for (int i = 0; i < 9; i++) {
        addString(CHAR_WAN_CN[i]);
        addString(CHAR_BAMBOO_CN[i]);
        addString(CHAR_DOTS_CN[i]);
    }
    for (int i = 0; i < 4; i++) addString(CHAR_WIND_CN[i]);
    for (int i = 0; i < 3; i++) addString(CHAR_DRAGON_CN[i]);
    for (int i = 0; i < 4; i++) {
        addString(CHAR_FLOWER_CN[i]);
        addString(CHAR_SEASON_CN[i]);
    }

    // UI labels
    addString(UI_CHOW.cn); addString(UI_PUNG.cn); addString(UI_KONG.cn);
    addString(UI_WIN.cn); addString(UI_PASS.cn); addString(UI_DRAW.cn);
    for (int i = 0; i < 4; i++) addString(UI_SEAT_NAMES[i].cn);
    addString(UI_ROUND_WIND.cn); addString(UI_SEAT_WIND.cn);

    // Faan names
    for (const auto& f : FAAN_NAMES) addString(f.cn);

    count = (int)codepoints.size();
    return codepoints.data();
}

void Renderer::generateFeltTexture() {
    // Procedural nylon felt texture with fiber noise
    static constexpr int TEX_SIZE = 512;
    Image img = GenImageColor(TEX_SIZE, TEX_SIZE, Color{34, 100, 50, 255});

    Color* pixels = (Color*)img.data;

    // Seed for reproducible noise
    srand(42);

    // Layer 1: Fine fiber noise (individual nylon fibers)
    for (int y = 0; y < TEX_SIZE; y++) {
        for (int x = 0; x < TEX_SIZE; x++) {
            int idx = y * TEX_SIZE + x;
            Color base = pixels[idx];

            // Fine random noise for fiber grain
            int noise = (rand() % 21) - 10; // -10 to +10

            // Directional fiber streaks (nylon weave runs mostly in one direction)
            float fiberX = sinf(x * 0.8f + y * 0.15f) * 4.0f;
            float fiberY = cosf(y * 0.6f + x * 0.1f) * 3.0f;
            int fiber = (int)(fiberX + fiberY);

            // Subtle cross-weave pattern
            float weave = sinf(x * 0.12f) * sinf(y * 0.12f) * 6.0f;

            int r = base.r + noise + fiber + (int)weave;
            int g = base.g + noise + fiber + (int)weave;
            int b = base.b + noise / 2 + fiber / 2 + (int)(weave * 0.5f);

            // Clamp
            if (r < 0) r = 0; if (r > 255) r = 255;
            if (g < 0) g = 0; if (g > 255) g = 255;
            if (b < 0) b = 0; if (b > 255) b = 255;

            pixels[idx] = {(unsigned char)r, (unsigned char)g, (unsigned char)b, 255};
        }
    }

    // Layer 2: Occasional longer fiber highlights
    for (int i = 0; i < 3000; i++) {
        int x = rand() % TEX_SIZE;
        int y = rand() % TEX_SIZE;
        int len = 3 + rand() % 8;
        int bright = 8 + rand() % 12;
        float angle = (rand() % 60 - 30) * DEG2RAD; // mostly horizontal fibers
        for (int s = 0; s < len; s++) {
            int px = x + (int)(s * cosf(angle));
            int py = y + (int)(s * sinf(angle));
            if (px >= 0 && px < TEX_SIZE && py >= 0 && py < TEX_SIZE) {
                int idx = py * TEX_SIZE + px;
                Color c = pixels[idx];
                int r = c.r + bright; if (r > 255) r = 255;
                int g = c.g + bright; if (g > 255) g = 255;
                int b = c.b + bright / 2; if (b > 255) b = 255;
                pixels[idx] = {(unsigned char)r, (unsigned char)g, (unsigned char)b, 255};
            }
        }
    }

    feltTexture_ = LoadTextureFromImage(img);
    SetTextureFilter(feltTexture_, TEXTURE_FILTER_BILINEAR);
    SetTextureWrap(feltTexture_, TEXTURE_WRAP_REPEAT);
    UnloadImage(img);
}

bool Renderer::init(AssetResolver& assets, const UIConfig& uiConfig) {
    uiConfig_ = uiConfig;

    // Camera defaults (will be overridden by Game's orbit controls each frame)
    camera_.position = {0.0f, 22.0f, 14.0f};
    camera_.target = {0.0f, 0.0f, 0.0f};
    camera_.up = {0.0f, 1.0f, 0.0f};
    camera_.fovy = 50.0f;
    camera_.projection = CAMERA_PERSPECTIVE;

    // Load Chinese font with specific codepoints
    int cpCount = 0;
    int* codepoints = getChineseCodepoints(cpCount);
    std::string fontPath = assets.resolve("fonts/NotoSansTC-Regular.ttf");
    chineseFont_ = LoadFontEx(fontPath.c_str(), 96, codepoints, cpCount);
    if (chineseFont_.glyphCount == 0) {
        TraceLog(LOG_WARNING, "Failed to load Chinese font, using default");
        chineseFont_ = GetFontDefault();
    }
    SetTextureFilter(chineseFont_.texture, TEXTURE_FILTER_BILINEAR);

    // English font (use the same font, it includes Latin characters)
    englishFont_ = chineseFont_;

    // Generate felt texture for table
    generateFeltTexture();

    // Generate tile texture atlas
    atlas_.generate(chineseFont_, englishFont_, assets);

    // Init tile renderer
    tileRenderer_.init(atlas_);

    return true;
}

void Renderer::shutdown() {
    tileRenderer_.unload();
    atlas_.unload();
    if (feltTexture_.id > 0) UnloadTexture(feltTexture_);
    if (chineseFont_.glyphCount > 0) UnloadFont(chineseFont_);
}

float Renderer::getPlayerRotation(int playerIndex) const {
    // Player 0 = South (human, facing camera), 1 = East (right), 2 = North (opposite), 3 = West (left)
    switch (playerIndex) {
        case 0: return 0.0f;     // South - facing toward +Z (toward camera)
        case 1: return 90.0f;    // East - facing toward +X
        case 2: return 180.0f;   // North - facing toward -Z
        case 3: return 270.0f;   // West - facing toward -X
        default: return 0.0f;
    }
}

Vector3 Renderer::getHandTilePosition(int playerIndex, int tileIndex, int totalTiles) const {
    float offset = (tileIndex - totalTiles * 0.5f) * TILE_SPACING;

    switch (playerIndex) {
        case 0: // South (human) - along X axis, at +Z
            return {offset, 0.0f, HAND_DISTANCE};
        case 1: // East - along Z axis, at +X
            return {HAND_DISTANCE, 0.0f, -offset};
        case 2: // North - along X axis, at -Z
            return {-offset, 0.0f, -HAND_DISTANCE};
        case 3: // West - along Z axis, at -X
            return {-HAND_DISTANCE, 0.0f, offset};
        default:
            return {0, 0, 0};
    }
}

BoundingBox Renderer::getHandTileBBox(int playerIndex, int tileIndex, int totalTiles) const {
    Vector3 pos = getHandTilePosition(playerIndex, tileIndex, totalTiles);

    if (playerIndex == 0) {
        // Tilted tile bounding box
        float tiltRad = fabsf(TileRenderer::PLAYER_TILT) * DEG2RAD;
        float sinT = sinf(tiltRad);
        float cosT = cosf(tiltRad);
        float hw = TileRenderer::TILE_W * 0.5f;
        float hd = TileRenderer::TILE_D * 0.5f;
        float H = TileRenderer::TILE_H;
        float maxY = H * cosT + TileRenderer::TILE_D * sinT;
        float minZ = -H * sinT - hd * cosT;
        float maxZ = hd * cosT;
        return {{pos.x - hw, 0, pos.z + minZ},
                {pos.x + hw, maxY, pos.z + maxZ}};
    }

    float hw = TileRenderer::TILE_W * 0.5f;
    float hh = TileRenderer::TILE_H;
    float hd = TileRenderer::TILE_D * 0.5f;

    // Adjust bounding box based on player rotation
    if (playerIndex == 1 || playerIndex == 3) {
        // Rotated 90/270 degrees: swap width and depth
        return {{pos.x - hd, pos.y, pos.z - hw},
                {pos.x + hd, pos.y + hh, pos.z + hw}};
    }
    return {{pos.x - hw, pos.y, pos.z - hd},
            {pos.x + hw, pos.y + hh, pos.z + hd}};
}

void Renderer::drawTable() {
    float hs = TABLE_SIZE * 0.5f;
    float texRepeat = 4.0f; // how many times felt texture repeats across table

    // Textured felt surface
    rlSetTexture(feltTexture_.id);
    rlBegin(RL_QUADS);
    rlColor4ub(255, 255, 255, 255);
    rlNormal3f(0, 1, 0);
    rlTexCoord2f(0,          0);          rlVertex3f(-hs, 0,  hs);
    rlTexCoord2f(texRepeat,  0);          rlVertex3f( hs, 0,  hs);
    rlTexCoord2f(texRepeat,  texRepeat);  rlVertex3f( hs, 0, -hs);
    rlTexCoord2f(0,          texRepeat);  rlVertex3f(-hs, 0, -hs);
    rlEnd();
    rlSetTexture(0);

    // Disable depth test for decorative overlays to prevent Z-fighting
    // with the felt surface (coplanar geometry at ~0.001 unit separation
    // is below depth buffer precision at camera distance)
    rlDisableDepthTest();

    // Radial vignette overlay (darker edges, brighter center)
    struct Pt { float x, z; };
    Pt pts[8] = {
        {-hs, -hs}, {0, -hs},
        { hs, -hs}, {hs,  0},
        { hs,  hs}, {0,  hs},
        {-hs,  hs}, {-hs, 0}
    };

    rlBegin(RL_TRIANGLES);
    rlNormal3f(0, 1, 0);
    for (int i = 0; i < 8; i++) {
        int j = (i + 1) % 8;
        // Center: transparent (no darkening)
        rlColor4ub(0, 0, 0, 0);
        rlVertex3f(0, 0.001f, 0);
        // Edges: semi-dark vignette
        rlColor4ub(0, 0, 0, 80);
        rlVertex3f(pts[j].x, 0.001f, pts[j].z);
        rlColor4ub(0, 0, 0, 80);
        rlVertex3f(pts[i].x, 0.001f, pts[i].z);
    }
    rlEnd();

    // Specular highlight (overhead light reflection)
    // Elliptical bright spot offset slightly toward the camera
    float specY = 0.002f;
    float specRx = hs * 0.45f;  // X radius
    float specRz = hs * 0.30f;  // Z radius (shorter - elongated)
    float specCz = hs * 0.15f;  // offset toward camera (player 0)
    int specSegs = 16;

    rlBegin(RL_TRIANGLES);
    rlNormal3f(0, 1, 0);
    for (int i = 0; i < specSegs; i++) {
        float a0 = (float)i / specSegs * 2.0f * PI;
        float a1 = (float)(i + 1) / specSegs * 2.0f * PI;

        // Center: brightest
        rlColor4ub(255, 255, 255, 22);
        rlVertex3f(0, specY, specCz);
        // Edge: transparent
        rlColor4ub(255, 255, 255, 0);
        rlVertex3f(cosf(a1) * specRx, specY, sinf(a1) * specRz + specCz);
        rlColor4ub(255, 255, 255, 0);
        rlVertex3f(cosf(a0) * specRx, specY, sinf(a0) * specRz + specCz);
    }
    rlEnd();

    rlEnableDepthTest();

    // Table border/rim (polished wood with per-face shading + specular)
    float rimH = 0.35f;
    float rimW = 0.55f;
    float rimY = rimH * 0.5f;

    // Base wood colors per side (brighter toward camera)
    Color woodFront  = {110, 68, 38, 255};
    Color woodBack   = {58, 34, 18, 255};
    Color woodRight  = {85, 52, 28, 255};
    Color woodLeft   = {72, 42, 22, 255};

    // Helper: draw one rim segment with per-face shading
    auto drawRimSegment = [&](Vector3 pos, float sx, float sy, float sz, Color baseCol) {
        float hx = sx * 0.5f, hy = sy * 0.5f, hz = sz * 0.5f;
        float topF = 1.30f, frontF = 1.10f, sideF = 0.85f, backF = 0.65f, botF = 0.45f;

        auto shade = [](Color c, float f) -> Color {
            return {
                (unsigned char)fminf(c.r * f, 255.0f),
                (unsigned char)fminf(c.g * f, 255.0f),
                (unsigned char)fminf(c.b * f, 255.0f), c.a
            };
        };

        Color topC = shade(baseCol, topF);
        Color fntC = shade(baseCol, frontF);
        Color sidC = shade(baseCol, sideF);
        Color bakC = shade(baseCol, backF);
        Color botC = shade(baseCol, botF);

        rlPushMatrix();
        rlTranslatef(pos.x, pos.y, pos.z);

        rlBegin(RL_QUADS);
        // Top (+Y)
        rlColor4ub(topC.r, topC.g, topC.b, topC.a);
        rlNormal3f(0,1,0);
        rlVertex3f(-hx, hy, -hz); rlVertex3f(-hx, hy, hz);
        rlVertex3f( hx, hy, hz);  rlVertex3f( hx, hy,-hz);
        // Front (+Z)
        rlColor4ub(fntC.r, fntC.g, fntC.b, fntC.a);
        rlNormal3f(0,0,1);
        rlVertex3f(-hx,-hy, hz); rlVertex3f( hx,-hy, hz);
        rlVertex3f( hx, hy, hz); rlVertex3f(-hx, hy, hz);
        // Back (-Z)
        rlColor4ub(bakC.r, bakC.g, bakC.b, bakC.a);
        rlNormal3f(0,0,-1);
        rlVertex3f(-hx,-hy,-hz); rlVertex3f(-hx, hy,-hz);
        rlVertex3f( hx, hy,-hz); rlVertex3f( hx,-hy,-hz);
        // Right (+X)
        rlColor4ub(sidC.r, sidC.g, sidC.b, sidC.a);
        rlNormal3f(1,0,0);
        rlVertex3f(hx,-hy,-hz); rlVertex3f(hx, hy,-hz);
        rlVertex3f(hx, hy, hz); rlVertex3f(hx,-hy, hz);
        // Left (-X)
        rlColor4ub(sidC.r, sidC.g, sidC.b, sidC.a);
        rlNormal3f(-1,0,0);
        rlVertex3f(-hx,-hy,-hz); rlVertex3f(-hx,-hy, hz);
        rlVertex3f(-hx, hy, hz); rlVertex3f(-hx, hy,-hz);
        // Bottom (-Y)
        rlColor4ub(botC.r, botC.g, botC.b, botC.a);
        rlNormal3f(0,-1,0);
        rlVertex3f(-hx,-hy,-hz); rlVertex3f( hx,-hy,-hz);
        rlVertex3f( hx,-hy, hz); rlVertex3f(-hx,-hy, hz);
        rlEnd();

        // Specular highlight strip on top face (polished wood sheen)
        // Disable depth test to avoid Z-fighting with rim top face
        rlDisableDepthTest();
        float shy = hy + 0.002f;
        float shx = hx * 0.85f;
        float shz = hz * 0.15f; // narrow strip along length
        rlBegin(RL_QUADS);
        rlNormal3f(0,1,0);
        rlColor4ub(255, 255, 255, 50);
        rlVertex3f(-shx, shy, -shz);
        rlColor4ub(255, 255, 255, 20);
        rlVertex3f(-shx, shy,  shz);
        rlColor4ub(255, 255, 255, 20);
        rlVertex3f( shx, shy,  shz);
        rlColor4ub(255, 255, 255, 50);
        rlVertex3f( shx, shy, -shz);
        rlEnd();

        // Edge highlight on top front edge
        rlBegin(RL_LINES);
        rlColor4ub(255, 255, 255, 35);
        rlVertex3f(-hx, hy, hz); rlVertex3f(hx, hy, hz);
        rlColor4ub(0, 0, 0, 40);
        rlVertex3f(-hx,-hy, hz); rlVertex3f(hx,-hy, hz);
        rlVertex3f(-hx,-hy,-hz); rlVertex3f(hx,-hy,-hz);
        rlEnd();
        rlEnableDepthTest();

        rlPopMatrix();
    };

    float fullLen = TABLE_SIZE + rimW;
    // Front rim (closest to camera - brightest)
    drawRimSegment({0, rimY,  hs + rimW * 0.5f}, fullLen, rimH, rimW, woodFront);
    // Back rim (farthest - darkest)
    drawRimSegment({0, rimY, -hs - rimW * 0.5f}, fullLen, rimH, rimW, woodBack);
    // Right rim
    drawRimSegment({ hs + rimW * 0.5f, rimY, 0}, rimW, rimH, fullLen, woodRight);
    // Left rim
    drawRimSegment({-hs - rimW * 0.5f, rimY, 0}, rimW, rimH, fullLen, woodLeft);
}

void Renderer::drawPlayerHand(const Player* player, int playerIndex, bool faceUp, bool flat) {
    if (!player) return;
    const auto& tiles = player->hand().concealed();
    int total = (int)tiles.size();
    float rotY = getPlayerRotation(playerIndex);

    for (int i = 0; i < total; i++) {
        Vector3 pos = getHandTilePosition(playerIndex, i, total);
        if (flat && faceUp) {
            // Winner's revealed tiles laid flat on the table
            tileRenderer_.drawTileFlat(tiles[i], pos, rotY);
        } else if (playerIndex == 0 && faceUp) {
            // Human player's tiles tilted toward camera for readability
            tileRenderer_.drawTileTilted(tiles[i], pos, rotY, TileRenderer::PLAYER_TILT);
        } else if (faceUp) {
            tileRenderer_.drawTileStanding(tiles[i], pos, rotY, true, false);
        } else {
            tileRenderer_.drawTileBackStanding(pos, rotY);
        }
    }
}

void Renderer::drawPlayerDiscards(const Player* player, int playerIndex) {
    if (!player) return;
    const auto& discards = player->discards();
    int count = (int)discards.size();
    float rotY = getPlayerRotation(playerIndex);

    int cols = 6;
    for (int i = 0; i < count; i++) {
        int row = i / cols;
        int col = i % cols;
        float xOff = (col - cols * 0.5f + 0.5f) * TILE_SPACING;
        float zOff = row * 0.72f;

        Vector3 pos;
        switch (playerIndex) {
            case 0: pos = {xOff, 0.01f, DISCARD_DISTANCE + zOff}; break;
            case 1: pos = {DISCARD_DISTANCE + zOff, 0.01f, -xOff}; break;
            case 2: pos = {-xOff, 0.01f, -DISCARD_DISTANCE - zOff}; break;
            case 3: pos = {-DISCARD_DISTANCE - zOff, 0.01f, xOff}; break;
        }
        tileRenderer_.drawTileFlat(discards[i], pos, rotY);
    }
}

// Compute spacing for melds + flowers anchored at table edge, growing inward.
// Returns the spacing between tile centers.
static float computeMeldSpacing(int totalTiles) {
    static constexpr float TABLE_EDGE = TABLE_SIZE * 0.5f;
    float spacing = TileRenderer::TILE_W + 0.02f; // minimal margin
    if (totalTiles > 1) {
        float needed = totalTiles * spacing;
        if (needed > TABLE_EDGE) {
            spacing = TABLE_EDGE / totalTiles;
        }
    }
    return spacing;
}

// Position a meld/flower tile anchored from the table corner inward.
// tileIdx 0 = at the corner, higher indices move toward center along the edge.
static Vector3 meldTilePos(int playerIndex, int tileIdx, float spacing) {
    static constexpr float TABLE_EDGE = TABLE_SIZE * 0.5f;
    static constexpr float MARGIN = 0.3f; // inset from rim so tiles sit on the surface
    // Along the edge: first tile at corner, growing toward center
    float along = TABLE_EDGE - MARGIN - (tileIdx + 0.5f) * spacing;
    // Depth: near the table edge (one tile-height in from the rim)
    float depth = TABLE_EDGE - MARGIN - TileRenderer::TILE_H * 0.5f;

    switch (playerIndex) {
        case 0: return {along, 0.01f, depth};              // south: right edge, near bottom
        case 1: return {depth, 0.01f, -along};             // east: near right edge, grows up
        case 2: return {-along, 0.01f, -depth};            // north: left edge, near top
        case 3: return {-depth, 0.01f, along};             // west: near left edge, grows down
        default: return {along, 0.01f, depth};
    }
}

void Renderer::drawPlayerMelds(const Player* player, int playerIndex) {
    if (!player) return;
    const auto& melds = player->hand().melds();
    if (melds.empty()) return;

    float rotY = getPlayerRotation(playerIndex);

    int totalMeldTiles = 0;
    for (const auto& meld : melds) totalMeldTiles += meld.tileCount();
    int flowerCount = (int)player->hand().flowers().size();
    int totalTiles = totalMeldTiles + flowerCount;

    float spacing = computeMeldSpacing(totalTiles);

    int tileIdx = 0;
    for (const auto& meld : melds) {
        for (int j = 0; j < (int)meld.tiles.size(); j++) {
            Vector3 pos = meldTilePos(playerIndex, tileIdx, spacing);

            if (meld.type != MeldType::ConcealedKong) {
                tileRenderer_.drawTileFlat(meld.tiles[j], pos, rotY, true);
            } else {
                tileRenderer_.drawTileFlatBack(pos, rotY);
            }
            tileIdx++;
        }
    }
}

void Renderer::drawPlayerFlowers(const Player* player, int playerIndex) {
    if (!player) return;
    const auto& flowers = player->hand().flowers();
    if (flowers.empty()) return;

    float rotY = getPlayerRotation(playerIndex);

    const auto& melds = player->hand().melds();
    int totalMeldTiles = 0;
    for (const auto& meld : melds) totalMeldTiles += meld.tileCount();
    int totalTiles = totalMeldTiles + (int)flowers.size();

    float spacing = computeMeldSpacing(totalTiles);

    for (int i = 0; i < (int)flowers.size(); i++) {
        // Flowers go after meld tiles (further from edge, toward center)
        Vector3 pos = meldTilePos(playerIndex, totalMeldTiles + i, spacing);

        tileRenderer_.drawTileFlat(flowers[i], pos, rotY);
    }
}

void Renderer::drawWall(int wallRemaining) {
    if (wallRemaining <= 0) return;

    // HK mahjong wall: 4 sides, each up to 18 stacks of 2 tiles
    static constexpr int MAX_STACKS_PER_SIDE = 18;
    static constexpr float WALL_DISTANCE = 5.5f;
    float stackSpacing = TileRenderer::TILE_W * 1.05f;

    int totalStacks = (wallRemaining + 1) / 2;
    int tilesLeft = wallRemaining;

    // Distribute stacks evenly across all 4 sides
    int basePerSide = std::min(totalStacks / 4, MAX_STACKS_PER_SIDE);
    int remainder = totalStacks - basePerSide * 4;
    int stacksPerSide[4];
    for (int s = 0; s < 4; s++) {
        stacksPerSide[s] = basePerSide;
        if (remainder > 0) {
            stacksPerSide[s]++;
            remainder--;
        }
        if (stacksPerSide[s] > MAX_STACKS_PER_SIDE)
            stacksPerSide[s] = MAX_STACKS_PER_SIDE;
    }

    for (int side = 0; side < 4 && tilesLeft > 0; side++) {
        float rotY = getPlayerRotation(side);
        int stacksThisSide = stacksPerSide[side];
        float startOff = -stacksThisSide * 0.5f * stackSpacing + stackSpacing * 0.5f;

        for (int i = 0; i < stacksThisSide && tilesLeft > 0; i++) {
            float offset = startOff + i * stackSpacing;

            Vector3 pos;
            switch (side) {
                case 0: pos = {offset, 0.01f,  WALL_DISTANCE}; break;
                case 1: pos = { WALL_DISTANCE, 0.01f, -offset}; break;
                case 2: pos = {-offset, 0.01f, -WALL_DISTANCE}; break;
                case 3: pos = {-WALL_DISTANCE, 0.01f,  offset}; break;
            }

            // Bottom tile
            tileRenderer_.drawTileFlatBack(pos, rotY);
            tilesLeft--;

            // Top tile (stacked)
            if (tilesLeft > 0) {
                Vector3 topPos = {pos.x, pos.y + TileRenderer::TILE_D, pos.z};
                tileRenderer_.drawTileFlatBack(topPos, rotY);
                tilesLeft--;
            }
        }
    }
}

void Renderer::drawPlayerLabel(const Player* player, int playerIndex, bool isActive) {
    if (!player) return;

    int screenW = GetScreenWidth();
    int screenH = GetScreenHeight();
    float fontSize = uiConfig_.labelFontSize;

    const char* cnName = UI_SEAT_NAMES[playerIndex].cn;
    const char* enName = UI_SEAT_NAMES[playerIndex].en;

    char label[128];
    snprintf(label, sizeof(label), "%s %s  $%d", cnName, enName, player->score());

    Vector2 pos;
    switch (playerIndex) {
        case 0: pos = {screenW * 0.5f - 60, screenH - 50.0f}; break;
        case 1: pos = {screenW - 180.0f, screenH * 0.5f}; break;
        case 2: pos = {screenW * 0.5f - 60, 20.0f}; break;
        case 3: pos = {20.0f, screenH * 0.5f}; break;
    }

    Color col = isActive ? YELLOW : WHITE;
    DrawTextEx(chineseFont_, label, pos, fontSize, 1, col);
}

void Renderer::drawUIOverlay(const RenderState& state) {
    int screenW = GetScreenWidth();
    int screenH = GetScreenHeight();

    // Draw player labels
    for (int i = 0; i < 4; i++) {
        if (state.players[i].player) {
            drawPlayerLabel(state.players[i].player, i,
                           i == state.activePlayerIndex);
        }
    }

    // Round info at top center
    char roundInfo[128];
    const char* windCN = CHAR_WIND_CN[static_cast<int>(state.prevailingWind)];
    const char* windEN = CHAR_WIND_EN[static_cast<int>(state.prevailingWind)];
    snprintf(roundInfo, sizeof(roundInfo), "%s %s %s  |  Tiles: %d",
             UI_ROUND_WIND.cn, windCN, windEN, state.wallRemaining);
    float fs = uiConfig_.fontSize;
    Vector2 riSize = MeasureTextEx(chineseFont_, roundInfo, fs, 1);
    DrawTextEx(chineseFont_, roundInfo,
               {screenW * 0.5f - riSize.x * 0.5f, 5}, fs, 1, WHITE);

    // Phase indicator
    const char* phaseText = "";
    switch (state.phase) {
        case GamePhase::DEALING:           phaseText = "Dealing..."; break;
        case GamePhase::REPLACING_FLOWERS: phaseText = "Replacing flowers..."; break;
        case GamePhase::PLAYER_DRAW:       phaseText = "Drawing..."; break;
        case GamePhase::PLAYER_TURN:
            if (state.activePlayerIndex == 0) phaseText = "Your turn - click a tile to discard";
            else phaseText = "Opponent's turn...";
            break;
        case GamePhase::CLAIM_PHASE:       phaseText = "Claim?"; break;
        case GamePhase::SCORING:           phaseText = "Scoring!"; break;
        case GamePhase::ROUND_END:         phaseText = "Round Over"; break;
        case GamePhase::GAME_OVER:         phaseText = "Game Over"; break;
        default: break;
    }
    DrawTextEx(chineseFont_, phaseText, {10, screenH - fs - 5}, fs, 1, LIGHTGRAY);

    // AI thinking indicator
    if (state.aiThinking) {
        const char* thinkText = "AI Thinking...";
        Vector2 thinkSize = MeasureTextEx(chineseFont_, thinkText, fs, 1);
        float thinkX = screenW * 0.5f - thinkSize.x * 0.5f;
        float thinkY = screenH * 0.5f - thinkSize.y * 0.5f;
        DrawRectangle((int)(thinkX - 12), (int)(thinkY - 8),
                      (int)(thinkSize.x + 24), (int)(thinkSize.y + 16),
                      Color{0, 0, 0, 180});
        DrawTextEx(chineseFont_, thinkText, {thinkX, thinkY}, fs, 1, YELLOW);
    }

    // Self-kong and self-draw win buttons during player's turn
    if (state.phase == GamePhase::PLAYER_TURN && state.activePlayerIndex == 0 &&
        (state.canSelfKong || state.canSelfDrawWin)) {
        float btnW = uiConfig_.buttonMinWidth, btnH = uiConfig_.buttonHeight, gap = uiConfig_.buttonGap;
        int btnCount = (state.canSelfKong ? 1 : 0) + (state.canSelfDrawWin ? 1 : 0);
        float totalW = btnCount * (btnW + gap) - gap;
        float startX = screenW * 0.5f - totalW * 0.5f;
        float btnY = screenH - btnH - 50;
        int idx = 0;

        if (state.canSelfDrawWin) {
            Rectangle rect = {startX + idx * (btnW + gap), btnY, btnW, btnH};
            bool hover = CheckCollisionPointRec(GetMousePosition(), rect);
            DrawRectangleRec(rect, hover ? Color{120, 40, 40, 230} : Color{80, 20, 20, 200});
            DrawRectangleLinesEx(rect, 2, hover ? GOLD : Color{255, 100, 100, 255});

            char btnLabel[64];
            snprintf(btnLabel, sizeof(btnLabel), "%s %s", UI_WIN.cn, UI_WIN.en);
            Vector2 ts = MeasureTextEx(chineseFont_, btnLabel, fs, 1);
            DrawTextEx(chineseFont_, btnLabel,
                       {rect.x + (btnW - ts.x) * 0.5f, rect.y + (btnH - ts.y) * 0.5f},
                       fs, 1, WHITE);
            idx++;
        }

        if (state.canSelfKong) {
            Rectangle rect = {startX + idx * (btnW + gap), btnY, btnW, btnH};
            bool hover = CheckCollisionPointRec(GetMousePosition(), rect);
            DrawRectangleRec(rect, hover ? Color{80, 80, 80, 230} : Color{50, 50, 50, 200});
            DrawRectangleLinesEx(rect, 2, hover ? YELLOW : LIGHTGRAY);

            const char* btnLabel = "\xe6\xa7\x93 Kong";
            Vector2 ts = MeasureTextEx(chineseFont_, btnLabel, fs, 1);
            DrawTextEx(chineseFont_, btnLabel,
                       {rect.x + (btnW - ts.x) * 0.5f, rect.y + (btnH - ts.y) * 0.5f},
                       fs, 1, WHITE);
            idx++;
        }
    }

    // Claim buttons during claim phase (for human)
    if (state.phase == GamePhase::CLAIM_PHASE && !state.humanClaimOptions.empty()) {
        float btnH = uiConfig_.buttonHeight, gap = uiConfig_.buttonGap;
        float btnY = screenH - btnH - 50;

        // Build button labels - chow combos get wider buttons with sequence info
        struct BtnInfo { char label[64]; float width; };
        std::vector<BtnInfo> buttons;

        for (const auto& opt : state.humanClaimOptions) {
            BtnInfo btn;
            btn.width = uiConfig_.buttonMinWidth;
            switch (opt.type) {
                case ClaimType::Chow: {
                    // Show the sequence ranks, e.g. "上 Chow 3-4-5"
                    uint8_t ranks[3];
                    int ri = 0;
                    for (const auto& mt : opt.meldTiles) {
                        if (ri < 2) ranks[ri++] = mt.rank;
                    }
                    // Include the discard rank to form the full sequence
                    if (state.lastDiscard) {
                        if (ri < 3) ranks[ri++] = state.lastDiscard->rank;
                    }
                    // Sort ranks for display
                    for (int a = 0; a < ri - 1; a++)
                        for (int b = a + 1; b < ri; b++)
                            if (ranks[a] > ranks[b]) { uint8_t tmp = ranks[a]; ranks[a] = ranks[b]; ranks[b] = tmp; }
                    if (ri == 3)
                        snprintf(btn.label, sizeof(btn.label), "%s %d-%d-%d", UI_CHOW.cn, ranks[0], ranks[1], ranks[2]);
                    else
                        snprintf(btn.label, sizeof(btn.label), "%s %s", UI_CHOW.cn, UI_CHOW.en);
                    btn.width = uiConfig_.chowButtonWidth;
                    break;
                }
                case ClaimType::Pung:
                    if (state.lastDiscard)
                        snprintf(btn.label, sizeof(btn.label), "%s %s %s", UI_PUNG.cn, UI_PUNG.en, state.lastDiscard->chineseName());
                    else
                        snprintf(btn.label, sizeof(btn.label), "%s %s", UI_PUNG.cn, UI_PUNG.en);
                    break;
                case ClaimType::Kong:
                    if (state.lastDiscard)
                        snprintf(btn.label, sizeof(btn.label), "%s %s %s", UI_KONG.cn, UI_KONG.en, state.lastDiscard->chineseName());
                    else
                        snprintf(btn.label, sizeof(btn.label), "%s %s", UI_KONG.cn, UI_KONG.en);
                    break;
                case ClaimType::Win:
                    if (state.lastDiscard)
                        snprintf(btn.label, sizeof(btn.label), "%s %s %s", UI_WIN.cn, UI_WIN.en, state.lastDiscard->chineseName());
                    else
                        snprintf(btn.label, sizeof(btn.label), "%s %s", UI_WIN.cn, UI_WIN.en);
                    break;
                default: btn.label[0] = '\0'; break;
            }
            buttons.push_back(btn);
        }
        // Pass button
        BtnInfo passBtn;
        passBtn.width = uiConfig_.buttonMinWidth;
        snprintf(passBtn.label, sizeof(passBtn.label), "%s %s", UI_PASS.cn, UI_PASS.en);
        buttons.push_back(passBtn);

        // Calculate total width
        float totalW = 0;
        for (const auto& b : buttons) totalW += b.width + gap;
        float startX = screenW * 0.5f - totalW * 0.5f;

        // Draw tile face preview above claim buttons
        if (state.lastDiscard) {
            float tilePreviewH = btnH * 1.8f;
            float tilePreviewW = tilePreviewH * 0.73f; // match tile aspect ratio
            float previewX = screenW * 0.5f - tilePreviewW * 0.5f;
            float previewY = btnY - tilePreviewH - 20;

            // Background card
            float cardPad = 6;
            Rectangle cardRect = {previewX - cardPad, previewY - cardPad,
                                  tilePreviewW + cardPad * 2, tilePreviewH + cardPad * 2};
            DrawRectangleRounded(cardRect, 0.15f, 6, Color{30, 30, 35, 220});

            // Pulsing gold border
            float time = (float)GetTime();
            float pulse = (sinf(time * 4.0f) + 1.0f) * 0.5f;
            unsigned char borderAlpha = (unsigned char)(180 + 75 * pulse);
            DrawRectangleRoundedLinesEx(cardRect, 0.15f, 6, 2.5f,
                                        Color{255, 200, 50, borderAlpha});

            // Draw the tile face using the 2D atlas (regular texture, no Y-flip)
            Rectangle srcRect = atlas_.getSourceRect2D(state.lastDiscard->suit, state.lastDiscard->rank);
            Rectangle destRect = {previewX, previewY, tilePreviewW, tilePreviewH};
            DrawTexturePro(atlas_.texture2D(), srcRect, destRect, {0, 0}, 0.0f, WHITE);

            // Label below the tile preview
            const char* tileName = state.lastDiscard->chineseName();
            Vector2 nameSize = MeasureTextEx(chineseFont_, tileName, fs, 1);
            DrawTextEx(chineseFont_, tileName,
                       {screenW * 0.5f - nameSize.x * 0.5f, previewY - nameSize.y - 6},
                       fs, 1, Color{255, 200, 50, 255});
        }

        float xPos = startX;
        for (int i = 0; i < (int)buttons.size(); i++) {
            Rectangle rect = {xPos, btnY, buttons[i].width, btnH};
            bool hover = CheckCollisionPointRec(GetMousePosition(), rect);
            DrawRectangleRec(rect, hover ? Color{80, 80, 80, 230} : Color{50, 50, 50, 200});
            DrawRectangleLinesEx(rect, 2, hover ? YELLOW : LIGHTGRAY);

            Vector2 ts = MeasureTextEx(chineseFont_, buttons[i].label, fs, 1);
            DrawTextEx(chineseFont_, buttons[i].label,
                       {rect.x + (buttons[i].width - ts.x) * 0.5f, rect.y + (btnH - ts.y) * 0.5f},
                       fs, 1, WHITE);
            xPos += buttons[i].width + gap;
        }

        // 2D arrow indicator pointing at the claimed tile on the table
        if (claimTileScreenValid_) {
            float sx = claimTileScreenPos_.x;
            float sy = claimTileScreenPos_.y;
            // Only draw if the point is on screen
            if (sx > 0 && sx < screenW && sy > 0 && sy < screenH) {
                float time = (float)GetTime();
                float bob = sinf(time * 3.0f) * 5.0f; // gentle bobbing

                // Downward-pointing triangle
                float arrowW = 14.0f, arrowH = 18.0f;
                float ay = sy - 20 + bob; // above the projected point
                Vector2 v1 = {sx, ay + arrowH};          // tip (bottom center)
                Vector2 v2 = {sx - arrowW, ay};           // top left
                Vector2 v3 = {sx + arrowW, ay};           // top right
                DrawTriangle(v3, v2, v1, Color{255, 215, 60, 220});

                // Small ring around the projected point
                DrawCircleLines((int)sx, (int)sy, 12.0f + 2.0f * sinf(time * 4.0f),
                                Color{255, 215, 60, 180});
            }
        }
    }

    // Scoring overlay — size to fit text
    if (!state.scoringText.empty()) {
        float scoringFs = uiConfig_.labelFontSize, spacing = 1, padding = 25;
        Vector2 textSize = MeasureTextEx(chineseFont_, state.scoringText.c_str(), scoringFs, spacing);
        float boxW = textSize.x + padding * 2;
        float boxH = textSize.y + padding * 2;
        // Enforce minimum size
        if (boxW < 400) boxW = 400;
        if (boxH < 250) boxH = 250;
        float boxX = screenW * 0.5f - boxW * 0.5f;
        float boxY = screenH * 0.5f - boxH * 0.5f;
        DrawRectangle((int)boxX, (int)boxY, (int)boxW, (int)boxH, Color{20, 20, 20, 220});
        DrawRectangleLinesEx({boxX, boxY, boxW, boxH}, 3, GOLD);
        DrawTextEx(chineseFont_, state.scoringText.c_str(),
                   {boxX + padding, boxY + padding}, scoringFs, spacing, WHITE);
    }
}

void Renderer::drawClaimHighlights(const RenderState& state) {
    claimTileScreenValid_ = false;

    if (state.phase != GamePhase::CLAIM_PHASE) return;
    if (state.humanClaimOptions.empty()) return;
    if (!state.lastDiscard) return;

    float time = (float)GetTime();
    float pulse = (sinf(time * 4.0f) + 1.0f) * 0.5f; // 0..1 pulsing

    Color glowColor = {255, 215, 60, 255}; // bright golden glow

    // 1) Glow + light beam around the last discarded tile in the discard area
    const Player* discarder = nullptr;
    int discarderIdx = -1;
    for (int i = 0; i < 4; i++) {
        if (!state.players[i].player) continue;
        const auto& discards = state.players[i].player->discards();
        // The last discard is the most recent one from any non-human player
        if (!discards.empty() && discards.back().id == state.lastDiscard->id) {
            discarder = state.players[i].player;
            discarderIdx = i;
            break;
        }
    }

    if (discarder && discarderIdx >= 0) {
        const auto& discards = discarder->discards();
        int count = (int)discards.size();
        int cols = 6;
        int lastIdx = count - 1;
        int row = lastIdx / cols;
        int col = lastIdx % cols;
        float xOff = (col - cols * 0.5f + 0.5f) * TILE_SPACING;
        float zOff = row * 0.72f;
        float rotY = getPlayerRotation(discarderIdx);

        Vector3 pos;
        switch (discarderIdx) {
            case 0: pos = {xOff, 0.01f, DISCARD_DISTANCE + zOff}; break;
            case 1: pos = {DISCARD_DISTANCE + zOff, 0.01f, -xOff}; break;
            case 2: pos = {-xOff, 0.01f, -DISCARD_DISTANCE - zOff}; break;
            case 3: pos = {-DISCARD_DISTANCE - zOff, 0.01f, xOff}; break;
        }

        // Flat glow on table
        tileRenderer_.drawGlowFlat(pos, rotY, glowColor, pulse);

        // Vertical light beam rising from the tile
        float beamHeight = 2.5f;
        float beamHW = 0.15f + 0.05f * pulse; // half-width, pulsing
        unsigned char beamAlpha = (unsigned char)(100 + 80 * pulse);
        Color beamColor = {255, 215, 60, beamAlpha};
        Color beamTop = {255, 215, 60, 0}; // fades to transparent at top

        // Two cross-shaped quads for visibility from any angle
        Vector3 b0 = {pos.x - beamHW, pos.y, pos.z};
        Vector3 b1 = {pos.x + beamHW, pos.y, pos.z};
        Vector3 b2 = {pos.x + beamHW, pos.y + beamHeight, pos.z};
        Vector3 b3 = {pos.x - beamHW, pos.y + beamHeight, pos.z};

        Vector3 b4 = {pos.x, pos.y, pos.z - beamHW};
        Vector3 b5 = {pos.x, pos.y, pos.z + beamHW};
        Vector3 b6 = {pos.x, pos.y + beamHeight, pos.z + beamHW};
        Vector3 b7 = {pos.x, pos.y + beamHeight, pos.z - beamHW};

        rlBegin(RL_QUADS);
        rlNormal3f(0, 0, 1);
        // First quad (facing Z)
        rlColor4ub(beamColor.r, beamColor.g, beamColor.b, beamAlpha);
        rlVertex3f(b0.x, b0.y, b0.z);
        rlVertex3f(b1.x, b1.y, b1.z);
        rlColor4ub(beamTop.r, beamTop.g, beamTop.b, beamTop.a);
        rlVertex3f(b2.x, b2.y, b2.z);
        rlVertex3f(b3.x, b3.y, b3.z);
        // Back face
        rlColor4ub(beamTop.r, beamTop.g, beamTop.b, beamTop.a);
        rlVertex3f(b3.x, b3.y, b3.z);
        rlVertex3f(b2.x, b2.y, b2.z);
        rlColor4ub(beamColor.r, beamColor.g, beamColor.b, beamAlpha);
        rlVertex3f(b1.x, b1.y, b1.z);
        rlVertex3f(b0.x, b0.y, b0.z);

        rlNormal3f(1, 0, 0);
        // Second quad (facing X)
        rlColor4ub(beamColor.r, beamColor.g, beamColor.b, beamAlpha);
        rlVertex3f(b4.x, b4.y, b4.z);
        rlVertex3f(b5.x, b5.y, b5.z);
        rlColor4ub(beamTop.r, beamTop.g, beamTop.b, beamTop.a);
        rlVertex3f(b6.x, b6.y, b6.z);
        rlVertex3f(b7.x, b7.y, b7.z);
        // Back face
        rlColor4ub(beamTop.r, beamTop.g, beamTop.b, beamTop.a);
        rlVertex3f(b7.x, b7.y, b7.z);
        rlVertex3f(b6.x, b6.y, b6.z);
        rlColor4ub(beamColor.r, beamColor.g, beamColor.b, beamAlpha);
        rlVertex3f(b5.x, b5.y, b5.z);
        rlVertex3f(b4.x, b4.y, b4.z);
        rlEnd();

        // Cache screen position for 2D overlay arrow
        Vector3 abovePos = {pos.x, pos.y + beamHeight * 0.5f, pos.z};
        claimTileScreenPos_ = GetWorldToScreen(abovePos, camera_);
        claimTileScreenValid_ = true;
    }

    // 2) Glow under matching tiles in human player's hand
    const Player* human = state.players[0].player;
    if (!human) return;
    const auto& handTiles = human->hand().concealed();
    int total = (int)handTiles.size();
    float rotY = getPlayerRotation(0);
    Color handGlow = {100, 220, 255, 255}; // blue glow for hand tiles

    for (int i = 0; i < total; i++) {
        bool match = false;
        const Tile& ht = handTiles[i];
        const Tile& disc = *state.lastDiscard;

        // Check if this tile matches for pung/kong
        if (ht.sameAs(disc)) match = true;

        // Check if this tile is part of a possible chow sequence
        if (!match && isNumberedSuit(disc.suit) && ht.suit == disc.suit) {
            uint8_t r = disc.rank;
            // tile is r-2 or r-1 (for r-2,r-1,r sequence)
            if (r >= 3 && (ht.rank == r-2 || ht.rank == r-1) &&
                human->hand().hasTile(disc.suit, r-2) && human->hand().hasTile(disc.suit, r-1))
                match = true;
            // tile is r-1 or r+1 (for r-1,r,r+1 sequence)
            if (r >= 2 && r <= 8 && (ht.rank == r-1 || ht.rank == r+1) &&
                human->hand().hasTile(disc.suit, r-1) && human->hand().hasTile(disc.suit, r+1))
                match = true;
            // tile is r+1 or r+2 (for r,r+1,r+2 sequence)
            if (r <= 7 && (ht.rank == r+1 || ht.rank == r+2) &&
                human->hand().hasTile(disc.suit, r+1) && human->hand().hasTile(disc.suit, r+2))
                match = true;
        }

        if (match) {
            Vector3 pos = getHandTilePosition(0, i, total);
            tileRenderer_.drawGlowTilted(pos, rotY, TileRenderer::PLAYER_TILT, handGlow, pulse);
        }
    }
}

void Renderer::render(const RenderState& state) {
    BeginDrawing();
    ClearBackground({25, 25, 30, 255});

    BeginMode3D(camera_);

    drawTable();

    // Flush table geometry, then disable backface culling for tile bodies.
    // Tile bodies use two-sided rendering; toggling GL state per-tile would
    // desync with the batched draw (rlgl defers rendering to EndMode3D).
    rlDrawRenderBatchActive();
    rlDisableBackfaceCulling();

    drawWall(state.wallRemaining);

    // Draw each player's hand
    bool isScoring = (state.phase == GamePhase::SCORING ||
                      state.phase == GamePhase::ROUND_END ||
                      state.phase == GamePhase::GAME_OVER);
    for (int i = 0; i < 4; i++) {
        if (!state.players[i].player) continue;
        bool isWinner = isScoring && (i == state.winnerIndex);
        bool faceUp = (i == 0) || isWinner;
        bool flat = isWinner && (i != 0); // AI winner tiles laid flat
        drawPlayerHand(state.players[i].player, i, faceUp, flat);
        drawPlayerDiscards(state.players[i].player, i);
        drawPlayerMelds(state.players[i].player, i);
        drawPlayerFlowers(state.players[i].player, i);
    }

    // Claim phase highlights
    drawClaimHighlights(state);

    // Flush tile geometry and restore backface culling
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();

    EndMode3D();

    // 2D UI overlay
    drawUIOverlay(state);

    DrawFPS(GetScreenWidth() - 90, GetScreenHeight() - 25);
    EndDrawing();
}
