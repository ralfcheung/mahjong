#include "TileRenderer.h"
#include "rlgl.h"
#include <cmath>

static Color shadeColor(Color c, float factor) {
    return {
        (unsigned char)(fminf(c.r * factor, 255.0f)),
        (unsigned char)(fminf(c.g * factor, 255.0f)),
        (unsigned char)(fminf(c.b * factor, 255.0f)),
        c.a
    };
}

// Compute shade factor from surface normal for smooth per-vertex lighting
static float normalShade(float nx, float ny, float nz) {
    float f = 0.0f;
    f += (ny > 0 ? ny * 1.18f : -ny * 0.55f);
    f += (nz > 0 ? nz * 1.05f : -nz * 0.70f);
    f += (nx > 0 ? nx * 0.88f : -nx * 0.82f);
    return f;
}

void TileRenderer::init(TileTextureAtlas& atlas) {
    atlas_ = &atlas;
    faceTexture_ = atlas.texture();
}

void TileRenderer::unload() {
}

// Corner radius and segments for rounded edges
static constexpr float ROUND_R = 0.08f;
static constexpr int ROUND_N = 4;

void TileRenderer::drawTileBody(Vector3 pos, Vector3 size, float rotY, Color color) {
    drawTileBody(pos, size, rotY, color, color, -1.0f, 0);
}

void TileRenderer::drawTileBody(Vector3 pos, Vector3 size, float rotY,
                                 Color frontColor, Color backColor, float backFrac, int backDir) {
    float hx = size.x * 0.5f;
    float hy = size.y * 0.5f;
    float hz = size.z * 0.5f;
    float r = fminf(ROUND_R, fminf(hx, fminf(hy, hz)));

    float ix = hx - r;
    float iy = hy - r;
    float iz = hz - r;

    bool rz = (hz <= hy);
    
    // Face extents: always inset by radius where perpendicular to round axis
    float feYx = ix, feYz = iz; // Top/Bottom faces inset
    float feZx = ix, feZy = iy; // Front/Back faces inset
    float feXy = iy, feXz = iz; // Left/Right faces inset

    bool twoTone = (backDir != 0);
    int sa = twoTone ? (abs(backDir) - 1) : -1;
    float sd = (backDir > 0) ? 1.0f : -1.0f;
    float iExts[3] = {ix, iy, iz};
    float sc = 0.0f;
    if (twoTone) {
        float he = iExts[sa];
        sc = sd * he - sd * 2.0f * he * backFrac;
    }
    auto isGreen = [&](float coord) -> bool {
        return twoTone && (sd * coord > sd * sc);
    };

    static bool tablesInit = false;
    static float cs[ROUND_N + 1], sn[ROUND_N + 1];
    if (!tablesInit) {
        for (int i = 0; i <= ROUND_N; i++) {
            float a = (float)i / ROUND_N * PI * 0.5f;
            cs[i] = cosf(a);
            sn[i] = sinf(a);
        }
        tablesInit = true;
    }

    auto vc = [&](float nx, float ny, float nz, bool green = false) {
        Color base = green ? backColor : frontColor;
        
        // Fake Ambient Occlusion / Subsurface Depth for white face
        if (!green) {
            float grad = 1.0f;
            if (ny < 0) grad -= 0.15f * fabsf(ny);
            if (fabsf(nx) > 0.8f) grad -= 0.1f;
            if (grad < 1.0f) {
                base.r = (unsigned char)(base.r * grad);
                base.g = (unsigned char)(base.g * grad);
                base.b = (unsigned char)(base.b * grad);
            }
        }

        Color c = shadeColor(base, normalShade(nx, ny, nz));
        rlColor4ub(c.r, c.g, c.b, c.a);
        rlNormal3f(nx, ny, nz);
    };

    rlPushMatrix();
    rlTranslatef(pos.x, pos.y, pos.z);
    rlRotatef(rotY, 0, 1, 0);
    rlDisableBackfaceCulling();

    rlBegin(RL_TRIANGLES);

    // Helper to draw the seam (joint) between layers
    auto drawSeam = [&](int axis, float dim1Lo, float dim1Hi, float dim2Lo, float dim2Hi, float n1, float n2, float n3) {
        float seamW = 0.004f;
        float p0[3] = {0,0,0}, p1[3] = {0,0,0}, p2[3] = {0,0,0}, p3[3] = {0,0,0};
        int d1 = (axis + 1) % 3;
        int d2 = (axis + 2) % 3;
        p0[axis] = sc - seamW; p0[d1] = dim1Lo; p0[d2] = dim2Lo;
        p1[axis] = sc - seamW; p1[d1] = dim1Lo; p1[d2] = dim2Hi;
        p2[axis] = sc + seamW; p2[d1] = dim1Lo; p2[d2] = dim2Hi;
        p3[axis] = sc + seamW; p3[d1] = dim1Lo; p3[d2] = dim2Lo;
        Color seamCol = {30, 70, 50, 255};
        float shade = normalShade(n1, n2, n3);
        rlColor4ub((unsigned char)(seamCol.r*shade), (unsigned char)(seamCol.g*shade), (unsigned char)(seamCol.b*shade), 255);
        rlNormal3f(n1, n2, n3);
        rlVertex3f(p0[0], p0[1], p0[2]); rlVertex3f(p2[0], p2[1], p2[2]); rlVertex3f(p1[0], p1[1], p1[2]);
        rlVertex3f(p0[0], p0[1], p0[2]); rlVertex3f(p3[0], p3[1], p3[2]); rlVertex3f(p2[0], p2[1], p2[2]);
    };

    // Helper to draw a rounded cap (lid)
    auto drawRoundedCap = [&](int axis, float level, bool isTop) {
        float n[3] = {0,0,0}; n[axis] = isTop ? 1.0f : -1.0f;
        float capCoord = isTop ? level : -level;
        bool capGreen = twoTone && (sa == axis) ? isGreen(level) : false;
        int uAx = (axis == 1) ? 0 : 0; 
        int vAx = (axis == 1) ? 2 : 1; 
        float uExt = (axis == 1) ? ix : ix;
        float vExt = (axis == 1) ? iz : iy;
        
        float c0[3]={0}, c1[3]={0}, c2[3]={0}, c3[3]={0};
        c0[axis]=level; c0[uAx]=-uExt; c0[vAx]=-vExt;
        c1[axis]=level; c1[uAx]= uExt; c1[vAx]=-vExt;
        c2[axis]=level; c2[uAx]= uExt; c2[vAx]= vExt;
        c3[axis]=level; c3[uAx]=-uExt; c3[vAx]= vExt;
        
        vc(n[0],n[1],n[2], capGreen);
        if (isTop) {
            rlVertex3f(c0[0],c0[1],c0[2]); rlVertex3f(c2[0],c2[1],c2[2]); rlVertex3f(c1[0],c1[1],c1[2]);
            rlVertex3f(c0[0],c0[1],c0[2]); rlVertex3f(c2[0],c2[1],c2[2]); rlVertex3f(c3[0],c3[1],c3[2]);
        } else {
            // Fix winding order for bottom/back faces (CCW relative to camera looking at back)
            // 0->2->1 is correct for bottom faces with normal -Y
            rlVertex3f(c0[0],c0[1],c0[2]); rlVertex3f(c2[0],c2[1],c2[2]); rlVertex3f(c1[0],c1[1],c1[2]);
            rlVertex3f(c0[0],c0[1],c0[2]); rlVertex3f(c3[0],c3[1],c3[2]); rlVertex3f(c2[0],c2[1],c2[2]);
        }
        
        auto quad = [&](float u0, float v0, float u1, float v1) {
            float q0[3]={0}, q1[3]={0}, q2[3]={0}, q3[3]={0};
            q0[axis]=level; q0[uAx]=u0; q0[vAx]=v0;
            q1[axis]=level; q1[uAx]=u1; q1[vAx]=v0;
            q2[axis]=level; q2[uAx]=u1; q2[vAx]=v1;
            q3[axis]=level; q3[uAx]=u0; q3[vAx]=v1;
            vc(n[0],n[1],n[2], capGreen);
            if (isTop) {
                rlVertex3f(q0[0],q0[1],q0[2]); rlVertex3f(q2[0],q2[1],q2[2]); rlVertex3f(q1[0],q1[1],q1[2]);
                rlVertex3f(q0[0],q0[1],q0[2]); rlVertex3f(q3[0],q3[1],q3[2]); rlVertex3f(q2[0],q2[1],q2[2]);
            } else {
                rlVertex3f(q0[0],q0[1],q0[2]); rlVertex3f(q2[0],q2[1],q2[2]); rlVertex3f(q1[0],q1[1],q1[2]);
                rlVertex3f(q0[0],q0[1],q0[2]); rlVertex3f(q2[0],q2[1],q2[2]); rlVertex3f(q3[0],q3[1],q3[2]);
            }
        };
        quad(-uExt, vExt, uExt, vExt+r); 
        quad(-uExt, -vExt-r, uExt, -vExt); 
        quad(uExt, -vExt, uExt+r, vExt); 
        quad(-uExt-r, -vExt, -uExt, vExt); 
        
        auto fan = [&](float cx, float cy, float startAng) {
            float center[3]={0}; center[axis]=level; center[uAx]=cx; center[vAx]=cy;
            for (int i=0; i<ROUND_N; i++) {
                float a1 = startAng + (float)i/ROUND_N * PI*0.5f;
                float a2 = startAng + (float)(i+1)/ROUND_N * PI*0.5f;
                float p1[3]={0}, p2[3]={0};
                p1[axis]=level; p1[uAx]=cx + cosf(a1)*r; p1[vAx]=cy + sinf(a1)*r;
                p2[axis]=level; p2[uAx]=cx + cosf(a2)*r; p2[vAx]=cy + sinf(a2)*r;
                vc(n[0],n[1],n[2], capGreen);
                if (isTop) {
                    rlVertex3f(center[0],center[1],center[2]); rlVertex3f(p2[0],p2[1],p2[2]); rlVertex3f(p1[0],p1[1],p1[2]);
                } else {
                    rlVertex3f(center[0],center[1],center[2]); rlVertex3f(p1[0],p1[1],p1[2]); rlVertex3f(p2[0],p2[1],p2[2]);
                }
            }
        };
        fan(uExt, vExt, 0.0f);   
        fan(-uExt, vExt, PI*0.5f); 
        fan(-uExt, -vExt, PI);   
        fan(uExt, -vExt, PI*1.5f); 
    };

    // --- Top (+Y) ---
    if (!rz) drawRoundedCap(1, hy, true);
    else if (twoTone && sa == 2) {
        vc(0,1,0, isGreen(-feYz));
        rlVertex3f(-feYx, hy,-feYz); rlVertex3f(-feYx, hy, sc); rlVertex3f( feYx, hy, sc);
        rlVertex3f(-feYx, hy,-feYz); rlVertex3f( feYx, hy, sc); rlVertex3f( feYx, hy,-feYz);
        drawSeam(2, -feYx, feYx, hy, hy, 0, 1, 0);
        vc(0,1,0, isGreen(feYz));
        rlVertex3f(-feYx, hy, sc); rlVertex3f(-feYx, hy, feYz); rlVertex3f( feYx, hy, feYz);
        rlVertex3f(-feYx, hy, sc); rlVertex3f( feYx, hy, feYz); rlVertex3f( feYx, hy, sc);
    } else {
        vc(0,1,0, twoTone && sa == 1 && isGreen(iy));
        rlVertex3f(-feYx, hy,-feYz); rlVertex3f(-feYx, hy, feYz); rlVertex3f( feYx, hy, feYz);
        rlVertex3f(-feYx, hy,-feYz); rlVertex3f( feYx, hy, feYz); rlVertex3f( feYx, hy,-feYz);
    }

    // --- Bottom (-Y) ---
    if (!rz) drawRoundedCap(1, -hy, false);
    else if (twoTone && sa == 2) {
        vc(0,-1,0, isGreen(-feYz));
        rlVertex3f(-feYx,-hy, sc); rlVertex3f(-feYx,-hy,-feYz); rlVertex3f( feYx,-hy,-feYz);
        rlVertex3f(-feYx,-hy, sc); rlVertex3f( feYx,-hy,-feYz); rlVertex3f( feYx,-hy, sc);
        drawSeam(2, -feYx, feYx, -hy, -hy, 0, -1, 0);
        vc(0,-1,0, isGreen(feYz));
        rlVertex3f(-feYx,-hy, feYz); rlVertex3f(-feYx,-hy, sc); rlVertex3f( feYx,-hy, sc);
        rlVertex3f(-feYx,-hy, feYz); rlVertex3f( feYx,-hy, sc); rlVertex3f( feYx,-hy, feYz);
    } else {
        vc(0,-1,0, twoTone && sa == 1 && isGreen(-iy));
        rlVertex3f(-feYx,-hy, feYz); rlVertex3f(-feYx,-hy,-feYz); rlVertex3f( feYx,-hy,-feYz);
        rlVertex3f(-feYx,-hy, feYz); rlVertex3f( feYx,-hy,-feYz); rlVertex3f( feYx,-hy, feYz);
    }

    // --- Front (+Z) ---
    if (rz) drawRoundedCap(2, hz, true);
    else {
        if (twoTone && sa == 1) {
            vc(0,0,1, isGreen(-feZy));
            rlVertex3f(-feZx,-feZy, hz); rlVertex3f( feZx,-feZy, hz); rlVertex3f( feZx, sc, hz);
            rlVertex3f(-feZx,-feZy, hz); rlVertex3f( feZx, sc, hz); rlVertex3f(-feZx, sc, hz);
            // CORRECTED: Pass Z (hz, hz) then X (-feZx, feZx)
            drawSeam(1, hz, hz, -feZx, feZx, 0, 0, 1);
            vc(0,0,1, isGreen(feZy));
            rlVertex3f(-feZx, sc, hz); rlVertex3f( feZx, sc, hz); rlVertex3f( feZx, feZy, hz);
            rlVertex3f(-feZx, sc, hz); rlVertex3f( feZx, feZy, hz); rlVertex3f(-feZx, feZy, hz);
        } else {
            vc(0,0,1, twoTone && sa == 2 && isGreen(iz));
            rlVertex3f(-feZx,-feZy, hz); rlVertex3f( feZx,-feZy, hz); rlVertex3f( feZx, feZy, hz);
            rlVertex3f(-feZx,-feZy, hz); rlVertex3f( feZx, feZy, hz); rlVertex3f(-feZx, feZy, hz);
        }
    }

    // --- Back (-Z) ---
    if (rz) drawRoundedCap(2, -hz, false);
    else {
        if (twoTone && sa == 1) {
            vc(0,0,-1, isGreen(-feZy));
            rlVertex3f( feZx,-feZy,-hz); rlVertex3f(-feZx,-feZy,-hz); rlVertex3f(-feZx, sc,-hz);
            rlVertex3f( feZx,-feZy,-hz); rlVertex3f(-feZx, sc,-hz); rlVertex3f( feZx, sc,-hz);
            // CORRECTED: Pass Z (-hz, -hz) then X (-feZx, feZx)
            drawSeam(1, -hz, -hz, -feZx, feZx, 0, 0, -1);
            vc(0,0,-1, isGreen(feZy));
            rlVertex3f( feZx, sc,-hz); rlVertex3f(-feZx, sc,-hz); rlVertex3f(-feZx, feZy,-hz);
            rlVertex3f( feZx, sc,-hz); rlVertex3f(-feZx, feZy,-hz); rlVertex3f( feZx, feZy,-hz);
        } else {
            vc(0,0,-1, twoTone && sa == 2 && isGreen(-iz));
            rlVertex3f( feZx,-feZy,-hz); rlVertex3f(-feZx,-feZy,-hz); rlVertex3f(-feZx, feZy,-hz);
            rlVertex3f( feZx,-feZy,-hz); rlVertex3f(-feZx, feZy,-hz); rlVertex3f( feZx, feZy,-hz);
        }
    }

    // --- Right (+X) ---
    if (twoTone && sa == 1) {
        vc(1,0,0, isGreen(-feXy));
        rlVertex3f( hx,-feXy,-feXz); rlVertex3f( hx, sc,-feXz); rlVertex3f( hx, sc, feXz);
        rlVertex3f( hx,-feXy,-feXz); rlVertex3f( hx, sc, feXz); rlVertex3f( hx,-feXy, feXz);
        // CORRECTED: Pass Z (-feXz, feXz) then X (hx, hx)
        drawSeam(1, -feXz, feXz, hx, hx, 1, 0, 0);
        vc(1,0,0, isGreen(feXy));
        rlVertex3f( hx, sc,-feXz); rlVertex3f( hx, feXy,-feXz); rlVertex3f( hx, feXy, feXz);
        rlVertex3f( hx, sc,-feXz); rlVertex3f( hx, feXy, feXz); rlVertex3f( hx, sc, feXz);
    } else if (twoTone && sa == 2) {
        vc(1,0,0, isGreen(-feXz));
        rlVertex3f( hx,-feXy,-feXz); rlVertex3f( hx, feXy,-feXz); rlVertex3f( hx, feXy, sc);
        rlVertex3f( hx,-feXy,-feXz); rlVertex3f( hx, feXy, sc); rlVertex3f( hx,-feXy, sc);
        drawSeam(2, hx, hx, -feXy, feXy, 1, 0, 0);
        vc(1,0,0, isGreen(feXz));
        rlVertex3f( hx,-feXy, sc); rlVertex3f( hx, feXy, sc); rlVertex3f( hx, feXy, feXz);
        rlVertex3f( hx,-feXy, sc); rlVertex3f( hx, feXy, feXz); rlVertex3f( hx,-feXy, feXz);
    } else {
        vc(1,0,0);
        rlVertex3f( hx,-feXy,-feXz); rlVertex3f( hx, feXy,-feXz); rlVertex3f( hx, feXy, feXz);
        rlVertex3f( hx,-feXy,-feXz); rlVertex3f( hx, feXy, feXz); rlVertex3f( hx,-feXy, feXz);
    }

    // --- Left (-X) ---
    if (twoTone && sa == 1) {
        vc(-1,0,0, isGreen(-feXy));
        rlVertex3f(-hx,-feXy, feXz); rlVertex3f(-hx, sc, feXz); rlVertex3f(-hx, sc,-feXz);
        rlVertex3f(-hx,-feXy, feXz); rlVertex3f(-hx, sc,-feXz); rlVertex3f(-hx,-feXy,-feXz);
        // CORRECTED: Pass Z (-feXz, feXz) then X (-hx, -hx)
        drawSeam(1, -feXz, feXz, -hx, -hx, -1, 0, 0);
        vc(-1,0,0, isGreen(feXy));
        rlVertex3f(-hx, sc, feXz); rlVertex3f(-hx, feXy, feXz); rlVertex3f(-hx, feXy,-feXz);
        rlVertex3f(-hx, sc, feXz); rlVertex3f(-hx, feXy,-feXz); rlVertex3f(-hx, sc,-feXz);
    } else if (twoTone && sa == 2) {
        vc(-1,0,0, isGreen(-feXz));
        rlVertex3f(-hx,-feXy, sc); rlVertex3f(-hx, feXy, sc); rlVertex3f(-hx, feXy,-feXz);
        rlVertex3f(-hx,-feXy, sc); rlVertex3f(-hx, feXy,-feXz); rlVertex3f(-hx,-feXy,-feXz);
        drawSeam(2, -hx, -hx, -feXy, feXy, -1, 0, 0);
        vc(-1,0,0, isGreen(feXz));
        rlVertex3f(-hx,-feXy, feXz); rlVertex3f(-hx, feXy, feXz); rlVertex3f(-hx, feXy, sc);
        rlVertex3f(-hx,-feXy, feXz); rlVertex3f(-hx, feXy, sc); rlVertex3f(-hx,-feXy, feXz);
    } else {
        vc(-1,0,0);
        rlVertex3f(-hx,-feXy, feXz); rlVertex3f(-hx, feXy, feXz); rlVertex3f(-hx, feXy,-feXz);
        rlVertex3f(-hx,-feXy, feXz); rlVertex3f(-hx, feXy,-feXz); rlVertex3f(-hx,-feXy,-feXz);
    }

    struct EdgeDef { int axis; float lo, hi, cx, cy, cz, d1x, d1y, d1z, d2x, d2y, d2z; };
    EdgeDef edges[4];
    if (rz) {
        edges[0] = {2, -hz, hz,  ix, iy, 0,   0,1,0,  1,0,0};
        edges[1] = {2, -hz, hz, -ix, iy, 0,   0,1,0, -1,0,0};
        edges[2] = {2, -hz, hz,  ix,-iy, 0,   1,0,0,  0,-1,0};
        edges[3] = {2, -hz, hz, -ix,-iy, 0,   0,-1,0,-1,0,0};
    } else {
        edges[0] = {1, -hy, hy,  ix, 0, iz,   0,0,1,  1,0,0};
        edges[1] = {1, -hy, hy, -ix, 0, iz,   0,0,1, -1,0,0};
        edges[2] = {1, -hy, hy,  ix, 0,-iz,   1,0,0,  0,0,-1};
        edges[3] = {1, -hy, hy, -ix, 0,-iz,   0,0,-1,-1,0,0};
    }

    for (int e = 0; e < 4; e++) {
        const EdgeDef& ed = edges[e];
        bool needsSplit = twoTone && (ed.axis == sa);
        float edgeSplitCoord = (sa == 1) ? ed.cy : (sa == 2) ? ed.cz : 0.0f;
        bool edgeGreen = twoTone && !needsSplit && isGreen(edgeSplitCoord);

        for (int i = 0; i < ROUND_N; i++) {
            float n0x = cs[i]*ed.d1x + sn[i]*ed.d2x;
            float n0y = cs[i]*ed.d1y + sn[i]*ed.d2y;
            float n0z = cs[i]*ed.d1z + sn[i]*ed.d2z;
            float n1x = cs[i+1]*ed.d1x + sn[i+1]*ed.d2x;
            float n1y = cs[i+1]*ed.d1y + sn[i+1]*ed.d2y;
            float n1z = cs[i+1]*ed.d1z + sn[i+1]*ed.d2z;
            float o0x = r*n0x, o0y = r*n0y, o0z = r*n0z;
            float o1x = r*n1x, o1y = r*n1y, o1z = r*n1z;

            if (needsSplit) {
                float splits[2][2] = {{ed.lo, sc}, {sc, ed.hi}};
                bool segGreen[2] = {isGreen(ed.lo), isGreen(ed.hi)};
                for (int s = 0; s < 2; s++) {
                    float sLo = splits[s][0], sHi = splits[s][1];
                    float p0[3] = {ed.cx, ed.cy, ed.cz}, p1[3] = {ed.cx, ed.cy, ed.cz};
                    p0[ed.axis] = sLo; p1[ed.axis] = sHi;
                    float a0x=p0[0]+o0x,a0y=p0[1]+o0y,a0z=p0[2]+o0z;
                    float a1x=p1[0]+o0x,a1y=p1[1]+o0y,a1z=p1[2]+o0z;
                    float b0x=p0[0]+o1x,b0y=p0[1]+o1y,b0z=p0[2]+o1z;
                    float b1x=p1[0]+o1x,b1y=p1[1]+o1y,b1z=p1[2]+o1z;
                    vc(n0x,n0y,n0z,segGreen[s]); rlVertex3f(a0x,a0y,a0z);
                    vc(n0x,n0y,n0z,segGreen[s]); rlVertex3f(a1x,a1y,a1z);
                    vc(n1x,n1y,n1z,segGreen[s]); rlVertex3f(b1x,b1y,b1z);
                    vc(n0x,n0y,n0z,segGreen[s]); rlVertex3f(a0x,a0y,a0z);
                    vc(n1x,n1y,n1z,segGreen[s]); rlVertex3f(b1x,b1y,b1z);
                    vc(n1x,n1y,n1z,segGreen[s]); rlVertex3f(b0x,b0y,b0z);
                }
                float seamW = 0.004f;
                float p0[3] = {ed.cx, ed.cy, ed.cz}; p0[ed.axis] = sc - seamW;
                float p1[3] = {ed.cx, ed.cy, ed.cz}; p1[ed.axis] = sc + seamW;
                float a0x=p0[0]+o0x,a0y=p0[1]+o0y,a0z=p0[2]+o0z;
                float a1x=p1[0]+o0x,a1y=p1[1]+o0y,a1z=p1[2]+o0z;
                float b0x=p0[0]+o1x,b0y=p0[1]+o1y,b0z=p0[2]+o1z;
                float b1x=p1[0]+o1x,b1y=p1[1]+o1y,b1z=p1[2]+o1z;
                Color seamCol = {30, 70, 50, 255};
                float navX = (n0x+n1x)*0.5f, navY = (n0y+n1y)*0.5f, navZ = (n0z+n1z)*0.5f;
                float shade = normalShade(navX, navY, navZ);
                rlColor4ub((unsigned char)(seamCol.r*shade), (unsigned char)(seamCol.g*shade), (unsigned char)(seamCol.b*shade), 255);
                rlNormal3f(navX, navY, navZ);
                rlVertex3f(a0x,a0y,a0z); rlVertex3f(b1x,b1y,b1z); rlVertex3f(a1x,a1y,a1z);
                rlVertex3f(a0x,a0y,a0z); rlVertex3f(b0x,b0y,b0z); rlVertex3f(b1x,b1y,b1z);
            } else {
                float p0[3] = {ed.cx, ed.cy, ed.cz}, p1[3] = {ed.cx, ed.cy, ed.cz};
                p0[ed.axis] = ed.lo; p1[ed.axis] = ed.hi;
                float a0x=p0[0]+o0x,a0y=p0[1]+o0y,a0z=p0[2]+o0z;
                float a1x=p1[0]+o0x,a1y=p1[1]+o0y,a1z=p1[2]+o0z;
                float b0x=p0[0]+o1x,b0y=p0[1]+o1y,b0z=p0[2]+o1z;
                float b1x=p1[0]+o1x,b1y=p1[1]+o1y,b1z=p1[2]+o1z;
                vc(n0x,n0y,n0z,edgeGreen); rlVertex3f(a0x,a0y,a0z);
                vc(n0x,n0y,n0z,edgeGreen); rlVertex3f(a1x,a1y,a1z);
                vc(n1x,n1y,n1z,edgeGreen); rlVertex3f(b1x,b1y,b1z);
                vc(n0x,n0y,n0z,edgeGreen); rlVertex3f(a0x,a0y,a0z);
                vc(n1x,n1y,n1z,edgeGreen); rlVertex3f(b1x,b1y,b1z);
                vc(n1x,n1y,n1z,edgeGreen); rlVertex3f(b0x,b0y,b0z);
            }
        }
    }

    rlEnd();

    auto drawSoftHighlightGrid = [&](Vector3 center, Vector3 uVec, Vector3 vVec, 
                                     float uSize, float vTop, float vSplit, float vBot,
                                     int alphaTop, int alphaSplit, int alphaBot, 
                                     float fadeWidth, int uRes, int vRes) {
        
        auto getAlpha = [&](float tu, float tv) -> unsigned char {
            float distU = fabsf(tu - 0.5f) * 2.0f;
            float alphaH = 1.0f;
            float startFade = 1.0f - fadeWidth;
            if (distU > startFade) {
                alphaH = 1.0f - (distU - startFade) / fadeWidth;
            }
            if (alphaH < 0) alphaH = 0;

            float totalH = fabsf(vBot - vTop);
            if (totalH < 0.0001f) totalH = 1.0f;
            float tSplit = (vSplit - vTop) / (vBot - vTop);
            
            float alphaV = 0.0f;
            if (tv <= tSplit) {
                float localT = (tSplit > 0.001f) ? (tv / tSplit) : 1.0f;
                alphaV = alphaTop + (alphaSplit - alphaTop) * localT;
            } else {
                float localT = (tv - tSplit) / (1.0f - tSplit);
                alphaV = alphaSplit + (alphaBot - alphaSplit) * localT;
            }
            return (unsigned char)(alphaH * alphaV);
        };

        rlBegin(RL_QUADS);
        for (int j = 0; j < vRes; j++) {
            float tv0 = (float)j / vRes;
            float tv1 = (float)(j + 1) / vRes;
            float y0 = vTop + (vBot - vTop) * tv0;
            float y1 = vTop + (vBot - vTop) * tv1;

            for (int i = 0; i < uRes; i++) {
                float tu0 = (float)i / uRes;
                float tu1 = (float)(i + 1) / uRes;
                float x0 = -uSize + (2.0f * uSize) * tu0;
                float x1 = -uSize + (2.0f * uSize) * tu1;

                unsigned char a00 = getAlpha(tu0, tv0);
                unsigned char a10 = getAlpha(tu1, tv0);
                unsigned char a11 = getAlpha(tu1, tv1);
                unsigned char a01 = getAlpha(tu0, tv1);

                auto vtx = [&](float x, float y, unsigned char a) {
                    rlColor4ub(255, 255, 255, a);
                    Vector3 p = {
                        center.x + uVec.x * x + vVec.x * y,
                        center.y + uVec.y * x + vVec.y * y,
                        center.z + uVec.z * x + vVec.z * y
                    };
                    rlVertex3f(p.x, p.y, p.z);
                };

                vtx(x0, y0, a00);
                vtx(x0, y1, a01);
                vtx(x1, y1, a11);
                vtx(x1, y0, a10);
            }
        }
        rlEnd();
    };

    bool topIsFlatTile = twoTone && sa == 1;
    if (!topIsFlatTile || sd > 0) {
        float shy = hy + 0.002f;
        rlNormal3f(0, 1, 0);
        Vector3 center = {0, shy, 0};
        drawSoftHighlightGrid(center, {1,0,0}, {0,0,1}, ix, -iz, 0.0f, iz, 0, 60, 0, 0.5f, 3, 4);
    }

    if (twoTone && sa == 2 && sd < 0) {
        float shz = -hz - 0.002f;
        rlNormal3f(0, 0, -1);
        Vector3 center = {0, 0, shz};
        drawSoftHighlightGrid(center, {1,0,0}, {0,1,0}, ix, iy, 0.0f, -iy, 0, 60, 0, 0.5f, 3, 4);
    }

    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
    rlPopMatrix();
}

void TileRenderer::drawShadow(Vector3 basePos, float width, float depth, float rotY) {
    float sy = 0.003f;
    float hw = width * 0.55f;
    float hd = depth * 0.65f;
    float offsetZ = -depth * 0.15f;
    float radY = rotY * DEG2RAD;
    float cosR = cosf(radY);
    float sinR = sinf(radY);
    auto rot = [&](float lx, float lz) -> Vector3 {
        float oz = lz + offsetZ;
        return { lx * cosR + oz * sinR + basePos.x, sy, -lx * sinR + oz * cosR + basePos.z };
    };
    Vector3 c0 = rot(-hw, -hd);
    Vector3 c1 = rot( hw, -hd);
    Vector3 c2 = rot( hw,  hd);
    Vector3 c3 = rot(-hw,  hd);
    rlBegin(RL_QUADS);
    rlColor4ub(0, 0, 0, 45);
    rlNormal3f(0, 1, 0);
    rlVertex3f(c0.x, c0.y, c0.z); rlVertex3f(c1.x, c1.y, c1.z); rlVertex3f(c2.x, c2.y, c2.z); rlVertex3f(c3.x, c3.y, c3.z);
    rlEnd();
}

void TileRenderer::drawFaceQuad(Vector3 pos, float rotY, const Tile& tile, bool standing) {
    if (!atlas_ || faceTexture_.id == 0) return;
    Rectangle src = atlas_->getSourceRect(tile.suit, tile.rank);
    float texW = (float)atlas_->atlasWidth();
    float texH = (float)atlas_->atlasHeight();
    float u_left = src.x / texW;
    float u_right = (src.x + (float)atlas_->CELL_SIZE) / texW;
    float v_top = 1.0f - src.y / texH;
    float v_bottom = 1.0f - (src.y + (float)atlas_->CELL_SIZE) / texH;
    float radY = rotY * DEG2RAD;
    float cosR = cosf(radY);
    float sinR = sinf(radY);
    auto toWorld = [&](float lx, float ly, float lz) -> Vector3 {
        float wx = lx * cosR + lz * sinR + pos.x;
        float wz = -lx * sinR + lz * cosR + pos.z;
        return {wx, ly + pos.y, wz};
    };
    rlSetTexture(faceTexture_.id);
    rlBegin(RL_QUADS);
    rlColor4ub(255, 255, 255, 255);
    if (standing) {
        float hw = TILE_W * 0.5f - ROUND_R;
        float hh = TILE_H * 0.5f - ROUND_R;
        float fz = TILE_D * 0.502f;
        Vector3 bl = toWorld(-hw, -hh, fz);
        Vector3 br = toWorld( hw, -hh, fz);
        Vector3 tr = toWorld( hw,  hh, fz);
        Vector3 tl = toWorld(-hw,  hh, fz);
        rlNormal3f(sinR, 0.0f, cosR);
        rlTexCoord2f(u_left,  v_bottom); rlVertex3f(bl.x, bl.y, bl.z);
        rlTexCoord2f(u_right, v_bottom); rlVertex3f(br.x, br.y, br.z);
        rlTexCoord2f(u_right, v_top);    rlVertex3f(tr.x, tr.y, tr.z);
        rlTexCoord2f(u_left,  v_top);    rlVertex3f(tl.x, tl.y, tl.z);
    } else {
        float hw = TILE_W * 0.5f - ROUND_R;
        float hd = TILE_H * 0.5f - ROUND_R;
        float fy = TILE_D * 0.502f;
        Vector3 c0 = toWorld(-hw, fy,  hd);
        Vector3 c1 = toWorld( hw, fy,  hd);
        Vector3 c2 = toWorld( hw, fy, -hd);
        Vector3 c3 = toWorld(-hw, fy, -hd);
        rlNormal3f(0.0f, 1.0f, 0.0f);
        rlTexCoord2f(u_left,  v_bottom); rlVertex3f(c0.x, c0.y, c0.z);
        rlTexCoord2f(u_right, v_bottom); rlVertex3f(c1.x, c1.y, c1.z);
        rlTexCoord2f(u_right, v_top);    rlVertex3f(c2.x, c2.y, c2.z);
        rlTexCoord2f(u_left,  v_top);    rlVertex3f(c3.x, c3.y, c3.z);
    }
    rlEnd();
    rlSetTexture(0);
}

void TileRenderer::drawTileStanding(const Tile& tile, Vector3 position, float rotationYDeg,
                                     bool faceUp, bool highlighted) {
    static constexpr Color BACK_COLOR = {45, 110, 90, 255};
    static constexpr float GREEN_FRAC = 0.45f;
    Color bodyColor = highlighted ? Color{250, 248, 245, 255} : Color{240, 238, 234, 255};
    Vector3 bodyPos = {position.x, position.y + TILE_H * 0.5f, position.z};
    drawShadow(position, TILE_W, TILE_D, rotationYDeg);
    drawTileBody(bodyPos, {TILE_W, TILE_H, TILE_D}, rotationYDeg, bodyColor, BACK_COLOR, GREEN_FRAC, -3);
    if (faceUp) drawFaceQuad(bodyPos, rotationYDeg, tile, true);
}

void TileRenderer::drawTileFlat(const Tile& tile, Vector3 position, float rotationYDeg,
                                 bool highlighted) {
    static constexpr Color BACK_COLOR = {45, 110, 90, 255};
    static constexpr float GREEN_FRAC = 0.45f;
    Color bodyColor = highlighted ? Color{242, 240, 236, 255} : Color{230, 228, 224, 255};
    Vector3 bodyPos = {position.x, position.y + TILE_D * 0.5f, position.z};
    drawShadow(position, TILE_W, TILE_H, rotationYDeg);
    drawTileBody(bodyPos, {TILE_W, TILE_D, TILE_H}, rotationYDeg, bodyColor, BACK_COLOR, GREEN_FRAC, -2);
    drawFaceQuad(bodyPos, rotationYDeg, tile, false);
}

void TileRenderer::drawTileTilted(const Tile& tile, Vector3 position, float rotationYDeg,
                                   float tiltDeg, bool highlighted) {
    static constexpr Color BACK_COLOR = {45, 110, 90, 255};
    static constexpr float GREEN_FRAC = 0.45f;
    Color bodyColor = highlighted ? Color{250, 248, 245, 255} : Color{240, 238, 234, 255};
    float absRad = fabsf(tiltDeg) * DEG2RAD;
    float lift = (TILE_D * 0.5f) * sinf(absRad);
    drawShadow(position, TILE_W, TILE_D, rotationYDeg);
    rlPushMatrix();
    rlTranslatef(position.x, position.y + lift, position.z);
    rlRotatef(rotationYDeg, 0, 1, 0);
    rlRotatef(tiltDeg, 1, 0, 0);
    Vector3 localBodyPos = {0, TILE_H * 0.5f, 0};
    drawTileBody(localBodyPos, {TILE_W, TILE_H, TILE_D}, 0.0f, bodyColor, BACK_COLOR, GREEN_FRAC, -3);
    drawFaceQuad(localBodyPos, 0.0f, tile, true);
    rlPopMatrix();
}

void TileRenderer::drawTileBackStanding(Vector3 position, float rotationYDeg) {
    static constexpr Color BASE_COLOR = {240, 238, 234, 255};
    static constexpr Color BACK_COLOR = {45, 110, 90, 255};
    static constexpr float GREEN_FRAC = 0.45f;
    Vector3 bodyPos = {position.x, position.y + TILE_H * 0.5f, position.z};
    drawShadow(position, TILE_W, TILE_D, rotationYDeg);
    drawTileBody(bodyPos, {TILE_W, TILE_H, TILE_D}, rotationYDeg, BASE_COLOR, BACK_COLOR, GREEN_FRAC, -3);
}

void TileRenderer::drawTileFlatBack(Vector3 position, float rotationYDeg) {
    static constexpr Color BASE_COLOR = {240, 238, 234, 255};
    static constexpr Color BACK_COLOR = {45, 110, 90, 255};
    static constexpr float GREEN_FRAC = 0.45f;
    Vector3 bodyPos = {position.x, position.y + TILE_D * 0.5f, position.z};
    drawShadow(position, TILE_W, TILE_H, rotationYDeg);
    drawTileBody(bodyPos, {TILE_W, TILE_D, TILE_H}, rotationYDeg, BASE_COLOR, BACK_COLOR, GREEN_FRAC, 2);
}

void TileRenderer::drawGlowFlat(Vector3 position, float rotationYDeg, Color color, float pulse) {
    float radY = rotationYDeg * DEG2RAD;
    float cosR = cosf(radY);
    float sinR = sinf(radY);
    float expand = 0.18f + 0.10f * pulse;
    float hw = TILE_W * 0.5f + expand;
    float hd = TILE_H * 0.5f + expand;
    float gy = 0.004f;
    auto rot = [&](float lx, float lz) -> Vector3 {
        return { lx * cosR + lz * sinR + position.x, gy, -lx * sinR + lz * cosR + position.z };
    };
    unsigned char alpha = (unsigned char)(color.a * (0.5f + 0.4f * pulse));
    float hw2 = hw + 0.15f;
    float hd2 = hd + 0.15f;
    Vector3 o0 = rot(-hw2, -hd2), o1 = rot(hw2, -hd2);
    Vector3 o2 = rot(hw2, hd2), o3 = rot(-hw2, hd2);
    rlBegin(RL_QUADS);
    rlNormal3f(0, 1, 0);
    rlColor4ub(color.r, color.g, color.b, alpha / 3);
    rlVertex3f(o0.x, o0.y, o0.z); rlVertex3f(o1.x, o1.y, o1.z);
    rlVertex3f(o2.x, o2.y, o2.z); rlVertex3f(o3.x, o3.y, o3.z);
    rlEnd();
    Vector3 c0 = rot(-hw, -hd), c1 = rot(hw, -hd);
    Vector3 c2 = rot(hw, hd), c3 = rot(-hw, hd);
    rlBegin(RL_QUADS);
    rlNormal3f(0, 1, 0);
    rlColor4ub(color.r, color.g, color.b, alpha);
    rlVertex3f(c0.x, c0.y, c0.z); rlVertex3f(c1.x, c1.y, c1.z);
    rlVertex3f(c2.x, c2.y, c2.z); rlVertex3f(c3.x, c3.y, c3.z);
    rlEnd();
}

void TileRenderer::drawGlowTilted(Vector3 position, float rotationYDeg, float tiltDeg, Color color, float pulse) {
    float radY = rotationYDeg * DEG2RAD;
    float cosR = cosf(radY);
    float sinR = sinf(radY);
    float absRad = fabsf(tiltDeg) * DEG2RAD;
    float expand = 0.10f + 0.05f * pulse;
    float hw = TILE_W * 0.5f + expand;
    float projD = TILE_H * sinf(absRad) * 0.5f + TILE_D * cosf(absRad) * 0.5f + expand;
    float gy = 0.004f;
    auto rot = [&](float lx, float lz) -> Vector3 {
        return { lx * cosR + lz * sinR + position.x, gy, -lx * sinR + lz * cosR + position.z };
    };
    unsigned char alpha = (unsigned char)(color.a * (0.4f + 0.3f * pulse));
    Vector3 c0 = rot(-hw, -projD), c1 = rot(hw, -projD);
    Vector3 c2 = rot(hw, projD), c3 = rot(-hw, projD);
    rlBegin(RL_QUADS);
    rlNormal3f(0, 1, 0);
    rlColor4ub(color.r, color.g, color.b, alpha);
    rlVertex3f(c0.x, c0.y, c0.z); rlVertex3f(c1.x, c1.y, c1.z);
    rlVertex3f(c2.x, c2.y, c2.z); rlVertex3f(c3.x, c3.y, c3.z);
    rlEnd();
}
