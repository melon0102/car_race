// debug_overlay.cpp — on-screen boot/status log for the Re-Volt Android port.
//
// Runtime bring-up happens on machines where logcat isn't reachable, so the
// important log lines are drawn INTO the frame with a built-in 5x7 pixel
// font (quads through the renderer's own TL-vertex path). RendererSwap()
// calls DbgOverlayDraw() right before eglSwapBuffers, so the overlay rides
// on top of whatever the game rendered — a screenshot becomes a log dump.

#define RV_NO_LONG32
#include "winshim/windows.h"
#include "winshim/d3d.h"
#include "gl_renderer.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <android/log.h>

// ---------------------------------------------------------------- log buffer

#define DBG_LINES 14
#define DBG_COLS  64

static char sLines[DBG_LINES][DBG_COLS];
static int sLineCount = 0;
static int sFrameCount = 0;
static int sLastLineFrame = 0;   // overlay auto-hides ~10s after the last message
static int sDrawCalls = 0, sDrawCallsLast = 0;
static int sTexUploads = 0;

extern "C" void DbgPrintf(const char *fmt, ...)
{
    char buf[DBG_COLS];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    __android_log_print(ANDROID_LOG_INFO, "revolt-dbg", "%s", buf);

    if (sLineCount == DBG_LINES) {
        memmove(sLines[0], sLines[1], sizeof(sLines) - sizeof(sLines[0]));
        sLineCount--;
    }
    strncpy(sLines[sLineCount], buf, DBG_COLS - 1);
    sLines[sLineCount][DBG_COLS - 1] = 0;
    sLineCount++;
    sLastLineFrame = sFrameCount;
}

extern "C" void DbgCountDraw(int vertexCount)
{
    sDrawCalls++;
    (void)vertexCount;
}

extern "C" void DbgCountTexUpload(void) { sTexUploads++; }

// ---------------------------------------------------------------- 5x7 font
// bits: column-major 5 bytes per glyph, LSB = top row; ASCII 32..95
// (uppercase only — lowercase is folded)

static const unsigned char kFont[64][5] = {
    {0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5f,0x00,0x00},{0x00,0x07,0x00,0x07,0x00},
    {0x14,0x7f,0x14,0x7f,0x14},{0x24,0x2a,0x7f,0x2a,0x12},{0x23,0x13,0x08,0x64,0x62},
    {0x36,0x49,0x55,0x22,0x50},{0x00,0x05,0x03,0x00,0x00},{0x00,0x1c,0x22,0x41,0x00},
    {0x00,0x41,0x22,0x1c,0x00},{0x14,0x08,0x3e,0x08,0x14},{0x08,0x08,0x3e,0x08,0x08},
    {0x00,0x50,0x30,0x00,0x00},{0x08,0x08,0x08,0x08,0x08},{0x00,0x60,0x60,0x00,0x00},
    {0x20,0x10,0x08,0x04,0x02},{0x3e,0x51,0x49,0x45,0x3e},{0x00,0x42,0x7f,0x40,0x00},
    {0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4b,0x31},{0x18,0x14,0x12,0x7f,0x10},
    {0x27,0x45,0x45,0x45,0x39},{0x3c,0x4a,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1e},{0x00,0x36,0x36,0x00,0x00},
    {0x00,0x56,0x36,0x00,0x00},{0x08,0x14,0x22,0x41,0x00},{0x14,0x14,0x14,0x14,0x14},
    {0x00,0x41,0x22,0x14,0x08},{0x02,0x01,0x51,0x09,0x06},{0x32,0x49,0x79,0x41,0x3e},
    {0x7e,0x11,0x11,0x11,0x7e},{0x7f,0x49,0x49,0x49,0x36},{0x3e,0x41,0x41,0x41,0x22},
    {0x7f,0x41,0x41,0x22,0x1c},{0x7f,0x49,0x49,0x49,0x41},{0x7f,0x09,0x09,0x09,0x01},
    {0x3e,0x41,0x49,0x49,0x7a},{0x7f,0x08,0x08,0x08,0x7f},{0x00,0x41,0x7f,0x41,0x00},
    {0x20,0x40,0x41,0x3f,0x01},{0x7f,0x08,0x14,0x22,0x41},{0x7f,0x40,0x40,0x40,0x40},
    {0x7f,0x02,0x0c,0x02,0x7f},{0x7f,0x04,0x08,0x10,0x7f},{0x3e,0x41,0x41,0x41,0x3e},
    {0x7f,0x09,0x09,0x09,0x06},{0x3e,0x41,0x51,0x21,0x5e},{0x7f,0x09,0x19,0x29,0x46},
    {0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7f,0x01,0x01},{0x3f,0x40,0x40,0x40,0x3f},
    {0x1f,0x20,0x40,0x20,0x1f},{0x3f,0x40,0x38,0x40,0x3f},{0x63,0x14,0x08,0x14,0x63},
    {0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43},{0x00,0x7f,0x41,0x41,0x00},
    {0x02,0x04,0x08,0x10,0x20},{0x00,0x41,0x41,0x7f,0x00},{0x04,0x02,0x01,0x02,0x04},
    {0x40,0x40,0x40,0x40,0x40},
};

// ---------------------------------------------------------------- rendering

static int sQuadCount;
#define MAX_OVERLAY_VERTS (4096 * 4)
static D3DTLVERTEX sVerts[MAX_OVERLAY_VERTS];

static void PushRect(float x, float y, float w, float h, DWORD color)
{
    if (sQuadCount * 4 + 4 > MAX_OVERLAY_VERTS) return;
    D3DTLVERTEX *v = &sVerts[sQuadCount * 4];
    for (int i = 0; i < 4; i++) {
        v[i].sz = 0.01f;
        v[i].rhw = 1.0f;
        v[i].color = color;
        v[i].specular = 0xff000000;   // fog factor 1 (no fog)
        v[i].tu = v[i].tv = 0.0f;
    }
    v[0].sx = x;     v[0].sy = y;
    v[1].sx = x + w; v[1].sy = y;
    v[2].sx = x + w; v[2].sy = y + h;
    v[3].sx = x;     v[3].sy = y + h;
    sQuadCount++;
}

static void DrawString(float x, float y, float px, const char *s, DWORD color)
{
    for (; *s; s++, x += 6 * px) {
        unsigned char c = (unsigned char)*s;
        if (c >= 'a' && c <= 'z') c -= 32;      // fold lowercase
        if (c < 32 || c > 95) c = '?';
        const unsigned char *glyph = kFont[c - 32];
        for (int col = 0; col < 5; col++)
            for (int row = 0; row < 7; row++)
                if (glyph[col] & (1 << row))
                    PushRect(x + col * px, y + row * px, px, px, color);
    }
}

// called by RendererSwap just before eglSwapBuffers
extern "C" void DbgOverlayDraw(int width, int height)
{
    sFrameCount++;
    sDrawCallsLast = sDrawCalls;
    sDrawCalls = 0;

    // fade away once nothing new has been logged for ~10s (600 frames);
    // everything still goes to logcat, and a fresh message re-shows the log
    if (sFrameCount - sLastLineFrame > 600) return;

    sQuadCount = 0;
    float px = (float)height / 400.0f;   // pixel scale
    if (px < 1) px = 1;
    float lineH = 9 * px;
    float x = 4 * px, y = 4 * px;

    char status[DBG_COLS];
    snprintf(status, sizeof(status), "FRM %d  DRW %d  TEX %d",
             sFrameCount, sDrawCallsLast, sTexUploads);
    DrawString(x, y, px, status, 0xff00ff40);
    y += lineH;

    for (int i = 0; i < sLineCount; i++, y += lineH)
        DrawString(x, y, px, sLines[i], 0xffffff00);

    if (sQuadCount == 0) return;

    // build an index list (two tris per quad) and draw via the renderer
    static WORD indices[4096 * 6];
    for (int q = 0; q < sQuadCount; q++) {
        WORD *ix = &indices[q * 6];
        WORD base = (WORD)(q * 4);
        ix[0] = base; ix[1] = (WORD)(base + 1); ix[2] = (WORD)(base + 2);
        ix[3] = base; ix[4] = (WORD)(base + 2); ix[5] = (WORD)(base + 3);
    }

    RendererSetRenderState(D3DRENDERSTATE_ZENABLE, 0 /*D3DZB_FALSE*/);
    RendererSetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, 0);
    RendererSetRenderState(D3DRENDERSTATE_CULLMODE, 1 /*D3DCULL_NONE*/);
    RendererSetRenderState(D3DRENDERSTATE_FOGENABLE, 0);
    RendererSetTexture(0, nullptr);
    RendererDraw(D3DPT_TRIANGLELIST, D3DFVF_TLVERTEX, sVerts,
                 (DWORD)(sQuadCount * 4), indices, (DWORD)(sQuadCount * 6));
    (void)width;
}
