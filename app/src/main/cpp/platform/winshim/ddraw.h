// ddraw.h — DirectDraw shim for the Re-Volt Android port.
// dx.cpp / texture-upload paths get rewritten on GLES; this covers the types
// that leak into game headers (surface pointers in structs, descs).
#pragma once
#include "windows.h"

// opaque COM interfaces — forward-declared so both `IDirectDrawSurface4 *x`
// and `LPDIRECTDRAWSURFACE4 x` forms in the game code work
struct IDirectDraw;         struct IDirectDraw2;
struct IDirectDraw7;
struct IDirectDrawSurface;  struct IDirectDrawSurface2;
struct IDirectDrawSurface3;
struct IDirectDrawSurface7;
struct IDirectDrawClipper;  struct IDirectDrawGammaControl;
struct IDirectDraw4;        // real class — declared at end of this header
struct IDirectDrawSurface4; // real class — declared at end of this header
struct IDirectDrawPalette;  // real class — declared at end of this header
struct IDirect3DTexture2;   // real class — declared in d3d.h

typedef struct IDirectDraw          *LPDIRECTDRAW;
typedef struct IDirectDraw2         *LPDIRECTDRAW2;
typedef struct IDirectDraw4         *LPDIRECTDRAW4;
typedef struct IDirectDraw7         *LPDIRECTDRAW7;
typedef struct IDirectDrawSurface   *LPDIRECTDRAWSURFACE;
typedef struct IDirectDrawSurface2  *LPDIRECTDRAWSURFACE2;
typedef struct IDirectDrawSurface3  *LPDIRECTDRAWSURFACE3;
typedef struct IDirectDrawSurface4  *LPDIRECTDRAWSURFACE4;
typedef struct IDirectDrawSurface7  *LPDIRECTDRAWSURFACE7;
typedef struct IDirectDrawPalette   *LPDIRECTDRAWPALETTE;
typedef struct IDirectDrawClipper   *LPDIRECTDRAWCLIPPER;
typedef struct IDirectDrawGammaControl *LPDIRECTDRAWGAMMACONTROL;

typedef struct _DDCOLORKEY {
    DWORD dwColorSpaceLowValue;
    DWORD dwColorSpaceHighValue;
} DDCOLORKEY, *LPDDCOLORKEY;

typedef struct _DDPIXELFORMAT {
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwFourCC;
    union {
        DWORD dwRGBBitCount;
        DWORD dwYUVBitCount;
        DWORD dwZBufferBitDepth;
        DWORD dwAlphaBitDepth;
    };
    union { DWORD dwRBitMask;        DWORD dwYBitMask; };
    union { DWORD dwGBitMask;        DWORD dwUBitMask; };
    union { DWORD dwBBitMask;        DWORD dwVBitMask; };
    union { DWORD dwRGBAlphaBitMask; DWORD dwYUVAlphaBitMask; };
} DDPIXELFORMAT, *LPDDPIXELFORMAT;

typedef struct _DDSCAPS {
    DWORD dwCaps;
} DDSCAPS, *LPDDSCAPS;

typedef struct _DDSCAPS2 {
    DWORD dwCaps;
    DWORD dwCaps2;
    DWORD dwCaps3;
    DWORD dwCaps4;
} DDSCAPS2, *LPDDSCAPS2;

typedef struct _DDSURFACEDESC {
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwHeight;
    DWORD dwWidth;
    union { LONG lPitch; DWORD dwLinearSize; };
    DWORD dwBackBufferCount;
    union { DWORD dwMipMapCount; DWORD dwZBufferBitDepth; DWORD dwRefreshRate; };
    DWORD dwAlphaBitDepth;
    DWORD dwReserved;
    LPVOID lpSurface;
    DDCOLORKEY ddckCKDestOverlay;
    DDCOLORKEY ddckCKDestBlt;
    DDCOLORKEY ddckCKSrcOverlay;
    DDCOLORKEY ddckCKSrcBlt;
    DDPIXELFORMAT ddpfPixelFormat;
    DDSCAPS ddsCaps;
} DDSURFACEDESC, *LPDDSURFACEDESC;

typedef struct _DDSURFACEDESC2 {
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwHeight;
    DWORD dwWidth;
    union { LONG lPitch; DWORD dwLinearSize; };
    DWORD dwBackBufferCount;
    union { DWORD dwMipMapCount; DWORD dwRefreshRate; };
    DWORD dwAlphaBitDepth;
    DWORD dwReserved;
    LPVOID lpSurface;
    DDCOLORKEY ddckCKDestOverlay;
    DDCOLORKEY ddckCKDestBlt;
    DDCOLORKEY ddckCKSrcOverlay;
    DDCOLORKEY ddckCKSrcBlt;
    DDPIXELFORMAT ddpfPixelFormat;
    DDSCAPS2 ddsCaps;
    DWORD dwTextureStage;
} DDSURFACEDESC2, *LPDDSURFACEDESC2;

typedef struct _DDBLTFX {
    DWORD dwSize;
    DWORD dwDDFX;
    DWORD dwROP;
    DWORD dwDDROP;
    DWORD dwRotationAngle;
    DWORD dwFillColor;      // (real struct has big unions; game uses fill)
    DWORD dwFillDepth;
    DWORD dwReserved[16];
} DDBLTFX, *LPDDBLTFX;

// flags — subset, real values
#define DDSD_CAPS            0x00000001
#define DDSD_HEIGHT          0x00000002
#define DDSD_WIDTH           0x00000004
#define DDSD_PITCH           0x00000008
#define DDSD_PIXELFORMAT     0x00001000
#define DDSD_MIPMAPCOUNT     0x00020000
#define DDSD_TEXTURESTAGE    0x00100000

#define DDSCAPS_COMPLEX      0x00000008
#define DDSCAPS_TEXTURE      0x00001000
#define DDSCAPS_MIPMAP       0x00400000
#define DDSCAPS_PRIMARYSURFACE 0x00000200
#define DDSCAPS_OFFSCREENPLAIN 0x00000040
#define DDSCAPS_ZBUFFER      0x00020000
#define DDSCAPS_VIDEOMEMORY  0x00004000
#define DDSCAPS_SYSTEMMEMORY 0x00000800
#define DDSCAPS_LOCALVIDMEM  0x10000000
#define DDSCAPS_ALLOCONLOAD  0x04000000
#define DDSCAPS2_OPAQUE      0x80000000

#define DDPF_ALPHAPIXELS     0x00000001
#define DDPF_ALPHA           0x00000002
#define DDPF_PALETTEINDEXED8 0x00000020
#define DDPF_RGB             0x00000040
#define DDPF_ZBUFFER         0x00000400

#define DDPCAPS_8BIT         0x00000004
#define DDPCAPS_ALLOW256     0x00000040

#define DDCKEY_COLORSPACE    0x00000001
#define DDCKEY_DESTBLT       0x00000002
#define DDCKEY_SRCBLT        0x00000008

#define DDLOCK_WAIT          0x00000001
#define DDLOCK_READONLY      0x00000010
#define DDLOCK_WRITEONLY     0x00000020

#define DDBLT_COLORFILL      0x00000400
#define DDBLT_WAIT           0x01000000

#define DDLOCK_SURFACEMEMORYPTR 0x00000000

#define DDENUMRET_CANCEL     0
#define DDENUMRET_OK         1

#define DD_OK                S_OK
#define DDERR_SURFACELOST    ((HRESULT)0x887601C2L)

// ---------------------------------------------------------------------------
// REAL classes in this port (implemented in platform/gl_device.cpp).
// Surfaces carry genuine pixel memory, so the game's own BMP loading,
// palette expansion and mipmap generation in texture.cpp work unmodified;
// the GLES renderer uploads from these buffers at draw time.
// ---------------------------------------------------------------------------

struct IDirectDrawPalette {
    PALETTEENTRY entries[256];
    LONG refCount;
    HRESULT SetEntries(DWORD flags, DWORD start, DWORD count, LPPALETTEENTRY src);
    HRESULT GetEntries(DWORD flags, DWORD start, DWORD count, LPPALETTEENTRY dst);
    ULONG   Release();
};

struct IDirectDrawSurface4 {
    DDSURFACEDESC2 desc;                // dimensions, format, pitch
    void *pixels;                       // owned pixel memory
    IDirectDrawSurface4 *nextMip;       // next level in a mipmap chain
    IDirectDrawPalette *palette;
    struct IDirect3DTexture2 *texIface; // lazily-created texture interface
    LONG refCount;
    unsigned int glTex;                 // GL texture name (renderer-owned)
    int dirty;                          // pixels changed since last GL upload
    int hasColorKey;                    // SetColorKey called (key in desc.ddckCKSrcBlt)

    HRESULT Lock(LPRECT rect, LPDDSURFACEDESC2 desc, DWORD flags, HANDLE event);
    HRESULT Unlock(LPRECT rect);
    HRESULT Blt(LPRECT dst, IDirectDrawSurface4 *src, LPRECT srcRect,
                DWORD flags, LPDDBLTFX fx);
    HRESULT Flip(IDirectDrawSurface4 *override, DWORD flags);
    HRESULT GetDC(HDC *dc);
    HRESULT ReleaseDC(HDC dc);
    HRESULT IsLost();
    HRESULT Restore();
    HRESULT GetAttachedSurface(LPDDSCAPS2 caps, IDirectDrawSurface4 **out);
    HRESULT AddAttachedSurface(IDirectDrawSurface4 *surface);
    HRESULT SetClipper(LPDIRECTDRAWCLIPPER clipper);
    HRESULT SetPalette(IDirectDrawPalette *palette);
    HRESULT SetColorKey(DWORD flags, LPDDCOLORKEY key);
    HRESULT QueryInterface(REFIID riid, LPVOID *out);
    ULONG   AddRef();
    ULONG   Release();
};

struct IDirectDraw4 {
    HRESULT CreateSurface(LPDDSURFACEDESC2 desc, IDirectDrawSurface4 **out,
                          void *unkOuter);
    HRESULT CreatePalette(DWORD flags, LPPALETTEENTRY entries,
                          IDirectDrawPalette **out, void *unkOuter);
    ULONG   Release();
};
