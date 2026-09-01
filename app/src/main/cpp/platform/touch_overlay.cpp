// touch_overlay.cpp — visible on-screen driving controls for the Android port.
//
// Draws the touch buttons INTO the frame (same TL-vertex path as
// debug_overlay.cpp, hooked from RendererSwap), laid out like the BlueStacks
// control overlay the port was tuned against: steer arrows bottom-left,
// accel/brake arrows stacked on the right edge, a flip-recover (car) button
// above them, and the fire zone sitting over the game's own pickup box on
// the left. The SAME rectangles drive hit testing — android_input.cpp calls
// TouchOverlayHit() — so the visuals and the input zones cannot drift apart.

#define RV_NO_LONG32
#include "winshim/windows.h"
#include "winshim/d3d.h"
#include "winshim/dinput.h"
#include "gl_renderer.h"

#include <math.h>

extern "C" int AInput_TouchHeld(int dik);   // android_input.cpp

// ---------------------------------------------------------------- layout
// Normalized [0,1] landscape coordinates, measured off the reference layout.

struct Zone { float x0, y0, x1, y1; int dik; };

static const Zone kZones[] = {
    { 0.020f, 0.755f, 0.125f, 0.975f, DIK_LEFT  },   // steer left
    { 0.135f, 0.755f, 0.240f, 0.975f, DIK_RIGHT },   // steer right
    { 0.868f, 0.535f, 0.978f, 0.745f, DIK_UP    },   // accelerate
    { 0.868f, 0.765f, 0.978f, 0.975f, DIK_DOWN  },   // brake / reverse
    { 0.868f, 0.300f, 0.978f, 0.510f, DIK_END   },   // right the car (flip recover)
    { 0.015f, 0.290f, 0.155f, 0.610f, DIK_LCONTROL } // fire (over the pickup box)
};

#define HIT_PAD 0.015f   // fingers are fatter than pixels

extern "C" int TouchOverlayHit(float x, float y)
{
    for (const Zone &z : kZones)
        if (x >= z.x0 - HIT_PAD && x <= z.x1 + HIT_PAD &&
            y >= z.y0 - HIT_PAD && y <= z.y1 + HIT_PAD)
            return z.dik;
    return 0;
}

// ---------------------------------------------------------------- geometry

#define MAX_TOUCH_VERTS 2048
static D3DTLVERTEX sVerts[MAX_TOUCH_VERTS];
static WORD sIndices[MAX_TOUCH_VERTS * 3];
static int sVertCount, sIndexCount;

static void PushVert(float x, float y, DWORD color)
{
    D3DTLVERTEX *v = &sVerts[sVertCount++];
    v->sx = x; v->sy = y; v->sz = 0.01f;
    v->rhw = 1.0f;
    v->color = color;
    v->specular = 0xff000000;   // fog factor 1
    v->tu = v->tv = 0.0f;
}

static void PushTri(float x0, float y0, float x1, float y1,
                    float x2, float y2, DWORD color)
{
    if (sVertCount + 3 > MAX_TOUCH_VERTS) return;
    WORD base = (WORD)sVertCount;
    PushVert(x0, y0, color); PushVert(x1, y1, color); PushVert(x2, y2, color);
    sIndices[sIndexCount++] = base;
    sIndices[sIndexCount++] = (WORD)(base + 1);
    sIndices[sIndexCount++] = (WORD)(base + 2);
}

static void PushQuad(float x0, float y0, float x1, float y1,
                     float x2, float y2, float x3, float y3, DWORD color)
{
    PushTri(x0, y0, x1, y1, x2, y2, color);
    PushTri(x0, y0, x2, y2, x3, y3, color);
}

static void PushRect(float x, float y, float w, float h, DWORD color)
{
    PushQuad(x, y, x + w, y, x + w, y + h, x, y + h, color);
}

// thick line segment as a rotated quad (ends extended by t/2 so joined
// borders meet without corner gaps)
static void PushLine(float x0, float y0, float x1, float y1, float t, DWORD color)
{
    float dx = x1 - x0, dy = y1 - y0;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.001f) return;
    dx /= len; dy /= len;
    float px = -dy * t * 0.5f, py = dx * t * 0.5f;
    x0 -= dx * t * 0.5f; y0 -= dy * t * 0.5f;
    x1 += dx * t * 0.5f; y1 += dy * t * 0.5f;
    PushQuad(x0 + px, y0 + py, x1 + px, y1 + py, x1 - px, y1 - py, x0 - px, y0 - py, color);
}

// filled triangle with border
static void PushArrow(const float pt[3][2], DWORD fill, DWORD border, float t)
{
    PushTri(pt[0][0], pt[0][1], pt[1][0], pt[1][1], pt[2][0], pt[2][1], fill);
    for (int i = 0; i < 3; i++) {
        int j = (i + 1) % 3;
        PushLine(pt[i][0], pt[i][1], pt[j][0], pt[j][1], t, border);
    }
}

// circle outline as segment quads
static void PushRing(float cx, float cy, float rx, float ry, float t, DWORD color)
{
    const int N = 24;
    for (int i = 0; i < N; i++) {
        float a0 = (float)i / N * 6.2831853f;
        float a1 = (float)(i + 1) / N * 6.2831853f;
        PushLine(cx + cosf(a0) * rx, cy + sinf(a0) * ry,
                 cx + cosf(a1) * rx, cy + sinf(a1) * ry, t, color);
    }
}

// ---------------------------------------------------------------- drawing

static DWORD Fill(int dik)   { return AInput_TouchHeld(dik) ? 0x78ffffff : 0x2effffff; }
static DWORD Border(int dik) { return AInput_TouchHeld(dik) ? 0xf0ffffff : 0xb4ffffff; }

// called by RendererSwap (before the debug overlay, so the log stays on top)
extern "C" void TouchOverlayDraw(int width, int height)
{
    float W = (float)width, H = (float)height;
    float t = H * 0.007f;          // border thickness
    if (t < 1.5f) t = 1.5f;
    sVertCount = sIndexCount = 0;

    for (const Zone &z : kZones) {
        float x0 = z.x0 * W, y0 = z.y0 * H, x1 = z.x1 * W, y1 = z.y1 * H;
        float xc = (x0 + x1) * 0.5f, yc = (y0 + y1) * 0.5f;
        DWORD fill = Fill(z.dik), border = Border(z.dik);

        switch (z.dik) {
            case DIK_LEFT: {
                float p[3][2] = {{x0, yc}, {x1, y0}, {x1, y1}};
                PushArrow(p, fill, border, t);
                break;
            }
            case DIK_RIGHT: {
                float p[3][2] = {{x1, yc}, {x0, y1}, {x0, y0}};
                PushArrow(p, fill, border, t);
                break;
            }
            case DIK_UP: {
                float p[3][2] = {{xc, y0}, {x1, y1}, {x0, y1}};
                PushArrow(p, fill, border, t);
                break;
            }
            case DIK_DOWN: {
                float p[3][2] = {{xc, y1}, {x0, y0}, {x1, y0}};
                PushArrow(p, fill, border, t);
                break;
            }
            case DIK_END: {
                // flip-recover: circled car with a drop arrow, like the reference
                float rx = (x1 - x0) * 0.5f, ry = (y1 - y0) * 0.5f;
                PushRing(xc, yc, rx, ry, t, border);
                float cw = rx * 1.1f, ch = ry * 0.42f;          // car body
                PushRect(xc - cw * 0.5f, yc - ch * 0.1f, cw, ch * 0.55f, border);
                PushRect(xc - cw * 0.28f, yc - ch * 0.5f, cw * 0.5f, ch * 0.45f, border); // cabin
                PushRect(xc - cw * 0.38f, yc + ch * 0.40f, cw * 0.18f, ch * 0.3f, border); // wheels
                PushRect(xc + cw * 0.20f, yc + ch * 0.40f, cw * 0.18f, ch * 0.3f, border);
                float ay = yc - ry * 0.62f;                      // drop arrow above the car
                PushTri(xc - rx * 0.22f, ay - ry * 0.16f, xc + rx * 0.22f, ay - ry * 0.16f,
                        xc, ay + ry * 0.10f, border);
                break;
            }
            case DIK_LCONTROL:
                // no visual: the zone sits over the pickup box the game's own
                // panel already draws (icon + shot count)
                break;
        }
    }

    if (sIndexCount == 0) return;

    RendererSetRenderState(D3DRENDERSTATE_ZENABLE, 0);
    RendererSetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, 1);
    RendererSetRenderState(D3DRENDERSTATE_SRCBLEND, D3DBLEND_SRCALPHA);
    RendererSetRenderState(D3DRENDERSTATE_DESTBLEND, D3DBLEND_INVSRCALPHA);
    RendererSetRenderState(D3DRENDERSTATE_CULLMODE, 1 /*D3DCULL_NONE*/);
    RendererSetRenderState(D3DRENDERSTATE_FOGENABLE, 0);
    RendererSetTexture(0, nullptr);
    RendererDraw(D3DPT_TRIANGLELIST, D3DFVF_TLVERTEX, sVerts,
                 (DWORD)sVertCount, sIndices, (DWORD)sIndexCount);
}
