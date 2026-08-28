// dx_replacement.cpp — replaces game/dx.cpp for the Android port.
//
// dx.cpp owned DirectDraw/D3D device setup, the display-mode list, and the
// render-state cache globals that dx.h's macros mutate. On Android the EGL
// context lives in android_main.cpp; this file provides the same globals and
// functions so every other game module links and behaves identically.
// FlipBuffers/ClearBuffers get wired to the GLES renderer in Phase 2.

#include "revolt.h"
#include "main.h"   // Box()
#include "dx.h"
#include "gl_renderer.h"

// ---------------------------------------------------------------- globals
// state cache — dx.h macros read+write these (FOG_ON, ALPHA_SRC, ...)
short RenderTP = -1, RenderTP2 = -1;
short RenderFog = FALSE, RenderAlpha = FALSE;
short RenderAlphaSrc = 0, RenderAlphaDest = 0;
short RenderZcmp = 0, RenderZwrite = TRUE, RenderZbuffer = D3DZB_TRUE;
long RenderStateChange = 0, TextureStateChange = 0;

DX_STATE DxState;

// device objects: D3Ddevice/D3Dviewport/DD live in gl_device.cpp; the rest
// are dead on Android (only touched by replaced modules) but must link.
IDirectDrawSurface4 *FrontBuffer = nullptr;
IDirectDrawSurface4 *BackBuffer = nullptr;
IDirectDrawSurface4 *ZedBuffer = nullptr;
IDirectDrawGammaControl *GammaControl = nullptr;
IDirect3D3 *D3D = nullptr;
// D3Dviewport lives in gl_device.cpp (real instance, like D3Ddevice)
D3DDEVICEDESC D3Dcaps;
DDPIXELFORMAT ZedBufferFormat;

DWORD ScreenXsize = 640;   // updated from the EGL surface by android_main
DWORD ScreenYsize = 480;
DWORD ScreenBpp = 32;
DWORD ScreenRefresh = 60;
long GammaFlag = GAMMA_UNAVAILABLE;
long NoColorKey = FALSE;
long DrawDeviceNum = 1, CurrentDrawDevice = 0;
long DisplayModeCount = 0;
DRAW_DEVICE DrawDevices[MAX_DRAW_DEVICES];

static long BackgroundColor = 0;

// ---------------------------------------------------------------- functions

BOOL InitDD(void) { return TRUE; }

BOOL InitD3D(DWORD, DWORD, DWORD, DWORD)
{
    // the game reads texture size limits straight from the D3Dcaps global
    D3Ddevice->GetCaps(&D3Dcaps, nullptr);
    // dx.cpp enables color-key transparency globally after device creation;
    // texture.cpp only tags tpages with the key when DxState.ColorKey is set
    D3Ddevice->SetRenderState(D3DRENDERSTATE_COLORKEYENABLE, TRUE);
    return TRUE;
}
void ReleaseDX(void) {}
void ReleaseD3D(void) {}
void SetGamma(long, long) {}
void ErrorDX(HRESULT, char *mess) { Box((char *)"DX", mess, 0); }
void CheckSurfaces(void) {}

void SetBackgroundColor(long col) { BackgroundColor = col; }
long GetBackgroundColor(void) { return BackgroundColor; }

void FlipBuffers(void) { RendererSwap(); }
void ClearBuffers(void) { RendererClear(BackgroundColor); }
void SetFrontBufferRGB(long) {}

void SetupDxState(void)
{
    DxState.WireframeEnabled = FALSE;   DxState.Wireframe = D3DFILL_SOLID;
    DxState.PerspectiveEnabled = TRUE;  DxState.Perspective = TRUE;
    DxState.TextureFilterFlag = 1;      DxState.TextureFilter = 1;   // bilinear
    DxState.MipMapFlag = 1;             DxState.MipMap = 1;
    DxState.FogEnabled = TRUE;          DxState.Fog = TRUE;
    DxState.DitherEnabled = FALSE;      DxState.Dither = FALSE;      // meaningless in GL
    DxState.ColorKeyEnabled = TRUE;     DxState.ColorKey = TRUE;   // black = transparent (HUD arrows, fences)
    DxState.AntiAliasEnabled = FALSE;   DxState.AntiAlias = 0;
}

void GetDrawDevices(void)
{
    // exactly one "device" on Android: the EGL display
    DrawDeviceNum = 1;
    CurrentDrawDevice = 0;
    memset(&DrawDevices[0], 0, sizeof(DrawDevices[0]));
    strcpy(DrawDevices[0].Name, "Android EGL");
    DrawDevices[0].DisplayModeNum = 1;
    DrawDevices[0].BestDisplayMode = 0;
    DrawDevices[0].DisplayMode[0].Width = ScreenXsize;
    DrawDevices[0].DisplayMode[0].Height = ScreenYsize;
    DrawDevices[0].DisplayMode[0].Bpp = 32;
    DrawDevices[0].DisplayMode[0].Refresh = 60;
}

HRESULT CALLBACK EnumZedBufferCallback(DDPIXELFORMAT *, void *) { return S_OK; }
BOOL CALLBACK GetDrawDeviceCallback(GUID *, LPSTR, LPSTR, LPVOID) { return TRUE; }
BOOL CALLBACK CreateDrawDeviceCallback(GUID *, LPSTR, LPSTR, LPVOID) { return TRUE; }
HRESULT CALLBACK DisplayModesCallback(DDSURFACEDESC2 *, void *) { return S_OK; }
