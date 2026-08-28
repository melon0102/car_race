// gl_renderer.h — GLES3 renderer for the Re-Volt Android port.
//
// Sits behind the IDirect3DDevice3 shim: the game's transformed-lit vertex
// submissions (D3DTLVERTEX streams) become GL draws here. The game does its
// own transform/projection to screen space, so no matrices are needed —
// the vertex shader maps screen coords to NDC and restores perspective-
// correct interpolation from RHW.
#pragma once

#include "winshim/windows.h"
#include "winshim/d3d.h"

// NOTE: no system includes here — this header is consumed by game-world TUs
// (where `long` is #defined to int); EGL handles pass as void*. Only
// fixed-width types cross this interface.

// lifecycle (called from android_main)
bool RendererInit(int screenWidth, int screenHeight, int esMajorVersion);
void RendererShutdown();
void RendererSetEGL(void *eglDisplay, void *eglSurface);
void RendererResize(int screenWidth, int screenHeight);

// frame control (called from dx_replacement's FlipBuffers/ClearBuffers)
void RendererClear(int32_t backgroundColorRGB);
void RendererSwap();
int  RendererGetSwapCount();   // total presents — lets the outer loop detect a frame

// state + draw (called from the IDirect3DDevice3 methods)
void RendererSetRenderState(D3DRENDERSTATETYPE state, DWORD value);
void RendererSetTexture(DWORD stage, IDirect3DTexture2 *texture);
void RendererDraw(D3DPRIMITIVETYPE type, DWORD fvf, const void *vertices,
                  DWORD vertexCount, const WORD *indices, DWORD indexCount);
