// gl_device.cpp — IDirect3DDevice3 implementation for the Re-Volt Android port.
//
// The game issues all rendering through this 13-method surface (directly and
// via dx.h's state macros). Phase 2 fills these in with the GLES3 renderer:
// render states map to GL state, DrawPrimitive/DrawIndexedPrimitive submit
// D3DTLVERTEX arrays through a single shader pair.
//
// Current state: everything is accepted and dropped (no-ops), so game logic
// can run headless while the renderer is brought up.

#define RV_NO_LONG32   // pure platform TU; only fixed-width types cross to game code
#include "winshim/d3d.h"
#include "gl_renderer.h"

// The game's global device/viewport pointers (normally defined in dx.cpp,
// which is replaced by this platform layer).
static IDirect3DDevice3 g_Device;
LPDIRECT3DDEVICE3 D3Ddevice = &g_Device;

static IDirect3DViewport3 g_Viewport;
LPDIRECT3DVIEWPORT3 D3Dviewport = &g_Viewport;

// ---------------------------------------------------------------- viewport

static D3DVIEWPORT2 g_ViewportDesc;

HRESULT IDirect3DViewport3::SetViewport2(LPD3DVIEWPORT2 vp)
{
    if (vp) g_ViewportDesc = *vp;   // GLES3: glViewport + depth range
    return S_OK;
}

HRESULT IDirect3DViewport3::GetViewport2(LPD3DVIEWPORT2 vp)
{
    if (vp) *vp = g_ViewportDesc;
    return S_OK;
}

HRESULT IDirect3DViewport3::Clear2(DWORD, LPD3DRECT, DWORD, D3DCOLOR, D3DVALUE, DWORD)
{
    return S_OK;                    // GLES3: glClearColor + glClear
}

ULONG IDirect3DViewport3::Release() { return 0; }

// ---------------------------------------------------------------- surfaces
// Surfaces are real objects with owned pixel memory, so texture.cpp's BMP
// loading / palette expansion / mip generation works unmodified. The GLES
// renderer uploads from `pixels` when `dirty` is set.

const GUID IID_IDirect3DTexture2 =
    {0x93281502, 0x8cf8, 0x11d0, {0x89, 0xab, 0x00, 0xa0, 0xc9, 0x05, 0x41, 0x29}};

// the game's DirectDraw object (dx.h: extern IDirectDraw4 *DD)
static IDirectDraw4 g_DDraw;
IDirectDraw4 *DD = &g_DDraw;

// registry of live top-level surfaces, so GL texture names can be
// invalidated when Android destroys the EGL context (pause/resume)
#define MAX_LIVE_SURFACES 512
static IDirectDrawSurface4 *g_LiveSurfaces[MAX_LIVE_SURFACES];

static void SurfaceRegister(IDirectDrawSurface4 *s)
{
    for (int i = 0; i < MAX_LIVE_SURFACES; i++) {
        if (!g_LiveSurfaces[i]) { g_LiveSurfaces[i] = s; return; }
    }
}

static void SurfaceUnregister(IDirectDrawSurface4 *s)
{
    for (int i = 0; i < MAX_LIVE_SURFACES; i++) {
        if (g_LiveSurfaces[i] == s) { g_LiveSurfaces[i] = nullptr; return; }
    }
}

// called from android_main when the EGL context is torn down: GL names died
// with the context; mark everything dirty for re-upload on the next bind
extern "C" void GLDevice_InvalidateTextures(void)
{
    for (int i = 0; i < MAX_LIVE_SURFACES; i++) {
        IDirectDrawSurface4 *s = g_LiveSurfaces[i];
        if (s) {
            s->glTex = 0;
            s->dirty = TRUE;
        }
    }
}

static DWORD SurfaceBytesPerPixel(const DDPIXELFORMAT *pf)
{
    if (pf->dwFlags & DDPF_PALETTEINDEXED8) return 1;
    DWORD bits = pf->dwRGBBitCount ? pf->dwRGBBitCount : 32;
    return (bits + 7) / 8;
}

static IDirectDrawSurface4 *SurfaceAlloc(const DDSURFACEDESC2 *want,
                                         DWORD width, DWORD height)
{
    IDirectDrawSurface4 *s = (IDirectDrawSurface4 *)calloc(1, sizeof(*s));
    if (!s) return nullptr;
    s->desc = *want;
    s->desc.dwWidth = width;
    s->desc.dwHeight = height;
    DWORD bpp = SurfaceBytesPerPixel(&want->ddpfPixelFormat);
    s->desc.lPitch = (LONG)(width * bpp);
    s->pixels = calloc(1, (size_t)width * height * bpp);
    s->desc.lpSurface = s->pixels;
    s->refCount = 1;
    return s;
}

HRESULT IDirectDraw4::CreateSurface(LPDDSURFACEDESC2 want,
                                    IDirectDrawSurface4 **out, void *)
{
    if (!want || !out) return E_FAIL;
    DWORD w = (want->dwFlags & DDSD_WIDTH) ? want->dwWidth : 256;
    DWORD h = (want->dwFlags & DDSD_HEIGHT) ? want->dwHeight : 256;

    IDirectDrawSurface4 *top = SurfaceAlloc(want, w, h);
    if (!top) return E_FAIL;

    // build the mipmap chain if requested
    if ((want->dwFlags & DDSD_MIPMAPCOUNT) && want->dwMipMapCount > 1) {
        IDirectDrawSurface4 *prev = top;
        for (DWORD level = 1; level < want->dwMipMapCount && w > 1 && h > 1; level++) {
            w >>= 1; h >>= 1;
            IDirectDrawSurface4 *mip = SurfaceAlloc(want, w, h);
            if (!mip) break;
            prev->nextMip = mip;
            prev = mip;
        }
    }
    SurfaceRegister(top);
    *out = top;
    return DD_OK;
}

HRESULT IDirectDraw4::CreatePalette(DWORD, LPPALETTEENTRY entries,
                                    IDirectDrawPalette **out, void *)
{
    if (!out) return E_FAIL;
    IDirectDrawPalette *p = (IDirectDrawPalette *)calloc(1, sizeof(*p));
    if (!p) return E_FAIL;
    if (entries) memcpy(p->entries, entries, sizeof(p->entries));
    p->refCount = 1;
    *out = p;
    return DD_OK;
}

ULONG IDirectDraw4::Release() { return 0; }

HRESULT IDirectDrawPalette::SetEntries(DWORD, DWORD start, DWORD count, LPPALETTEENTRY src)
{
    if (!src || start + count > 256) return E_FAIL;
    memcpy(entries + start, src, count * sizeof(PALETTEENTRY));
    return DD_OK;
}

HRESULT IDirectDrawPalette::GetEntries(DWORD, DWORD start, DWORD count, LPPALETTEENTRY dst)
{
    if (!dst || start + count > 256) return E_FAIL;
    memcpy(dst, entries + start, count * sizeof(PALETTEENTRY));
    return DD_OK;
}

ULONG IDirectDrawPalette::Release()
{
    if (--refCount <= 0) { free(this); return 0; }
    return (ULONG)refCount;
}

HRESULT IDirectDrawSurface4::Lock(LPRECT, LPDDSURFACEDESC2 out, DWORD, HANDLE)
{
    if (!out || !pixels) return E_FAIL;
    *out = desc;
    out->lpSurface = pixels;
    dirty = TRUE;   // assume the caller writes; renderer re-uploads
    return DD_OK;
}

HRESULT IDirectDrawSurface4::Unlock(LPRECT) { return DD_OK; }
HRESULT IDirectDrawSurface4::Blt(LPRECT, IDirectDrawSurface4 *, LPRECT, DWORD, LPDDBLTFX) { return DD_OK; }
HRESULT IDirectDrawSurface4::Flip(IDirectDrawSurface4 *, DWORD) { return DD_OK; }
// GetDC lives in winshim_impl.cpp (mini-GDI: hands out a surface-backed DC)
HRESULT IDirectDrawSurface4::ReleaseDC(HDC) { return DD_OK; }
HRESULT IDirectDrawSurface4::IsLost() { return DD_OK; }
HRESULT IDirectDrawSurface4::Restore() { return DD_OK; }

HRESULT IDirectDrawSurface4::GetAttachedSurface(LPDDSCAPS2, IDirectDrawSurface4 **out)
{
    if (!out) return E_FAIL;
    *out = nextMip;
    if (!nextMip) return E_FAIL;
    nextMip->AddRef();   // DirectDraw semantics: caller Releases what it got
    return DD_OK;
}

HRESULT IDirectDrawSurface4::AddAttachedSurface(IDirectDrawSurface4 *) { return DD_OK; }
HRESULT IDirectDrawSurface4::SetClipper(LPDIRECTDRAWCLIPPER) { return DD_OK; }

HRESULT IDirectDrawSurface4::SetPalette(IDirectDrawPalette *pal)
{
    palette = pal;
    dirty = TRUE;
    return DD_OK;
}

HRESULT IDirectDrawSurface4::SetColorKey(DWORD, LPDDCOLORKEY key)
{
    if (key) desc.ddckCKSrcBlt = *key;
    hasColorKey = TRUE;
    dirty = TRUE;   // re-upload with keyed texels marked transparent
    return DD_OK;
}

HRESULT IDirectDrawSurface4::QueryInterface(REFIID, LPVOID *out)
{
    // only ever asked for IID_IDirect3DTexture2 by the game
    if (!out) return E_FAIL;
    if (!texIface) {
        texIface = (IDirect3DTexture2 *)calloc(1, sizeof(IDirect3DTexture2));
        if (!texIface) return E_FAIL;
        texIface->surface = this;
    }
    refCount++;
    *out = texIface;
    return S_OK;
}

ULONG IDirectDrawSurface4::AddRef() { return (ULONG)++refCount; }

ULONG IDirectDrawSurface4::Release()
{
    if (--refCount <= 0) {
        SurfaceUnregister(this);
        if (nextMip) nextMip->Release();
        free(texIface);
        free(pixels);
        free(this);
        return 0;
    }
    return (ULONG)refCount;
}

// ---------------------------------------------------------------- texture2

HRESULT IDirect3DTexture2::Load(IDirect3DTexture2 *src)
{
    // copy the source surface chain's pixels into ours (system -> "video")
    IDirectDrawSurface4 *d = surface;
    IDirectDrawSurface4 *s = src ? src->surface : nullptr;
    while (d && s) {
        if (d->pixels && s->pixels &&
            d->desc.dwWidth == s->desc.dwWidth &&
            d->desc.dwHeight == s->desc.dwHeight) {
            memcpy(d->pixels, s->pixels,
                   (size_t)d->desc.lPitch * d->desc.dwHeight);
        }
        d->palette = s->palette;
        d->dirty = TRUE;
        d = d->nextMip;
        s = s->nextMip;
    }
    return DD_OK;
}

ULONG IDirect3DTexture2::AddRef() { if (surface) surface->AddRef(); return 1; }
ULONG IDirect3DTexture2::Release() { return surface ? surface->Release() : 0; }

HRESULT IDirect3DDevice3::SetRenderState(D3DRENDERSTATETYPE state, DWORD value)
{
    RendererSetRenderState(state, value);
    return S_OK;
}

HRESULT IDirect3DDevice3::SetTexture(DWORD stage, LPDIRECT3DTEXTURE2 texture)
{
    RendererSetTexture(stage, texture);
    return S_OK;
}

HRESULT IDirect3DDevice3::SetTextureStageState(DWORD, D3DTEXTURESTAGESTATETYPE, DWORD) { return S_OK; }
HRESULT IDirect3DDevice3::SetTransform(D3DTRANSFORMSTATETYPE, LPD3DMATRIX) { return S_OK; }
HRESULT IDirect3DDevice3::BeginScene() { return S_OK; }
HRESULT IDirect3DDevice3::EndScene() { return S_OK; }

HRESULT IDirect3DDevice3::DrawPrimitive(D3DPRIMITIVETYPE type, DWORD fvf,
                                        LPVOID vertices, DWORD vertexCount, DWORD)
{
    RendererDraw(type, fvf, vertices, vertexCount, nullptr, 0);
    return S_OK;
}

HRESULT IDirect3DDevice3::DrawIndexedPrimitive(D3DPRIMITIVETYPE type, DWORD fvf,
                                               LPVOID vertices, DWORD vertexCount,
                                               LPWORD indices, DWORD indexCount, DWORD)
{
    RendererDraw(type, fvf, vertices, vertexCount, indices, indexCount);
    return S_OK;
}

HRESULT IDirect3DDevice3::EnumTextureFormats(LPD3DENUMPIXELFORMATSCALLBACK callback,
                                             LPVOID ctx)
{
    // Advertise 32-bit XRGB. NOTE: no DDPF_ALPHAPIXELS — the game's
    // FindTextureCallback REJECTS formats with an alpha channel.
    if (callback) {
        DDPIXELFORMAT fmt = {};
        fmt.dwSize = sizeof(fmt);
        fmt.dwFlags = DDPF_RGB;
        fmt.dwRGBBitCount = 32;
        fmt.dwRBitMask = 0x00ff0000;
        fmt.dwGBitMask = 0x0000ff00;
        fmt.dwBBitMask = 0x000000ff;
        fmt.dwRGBAlphaBitMask = 0;
        callback(&fmt, ctx);
    }
    return S_OK;
}

HRESULT IDirect3DDevice3::GetCaps(D3DDEVICEDESC *hwDesc, D3DDEVICEDESC *helDesc)
{
    // texture.cpp reads min/max texture sizes out of these caps
    D3DDEVICEDESC desc = {};
    desc.dwSize = sizeof(desc);
    desc.dwMinTextureWidth = 1;
    desc.dwMinTextureHeight = 1;
    desc.dwMaxTextureWidth = 2048;
    desc.dwMaxTextureHeight = 2048;
    if (hwDesc) *hwDesc = desc;
    if (helDesc) *helDesc = desc;
    return S_OK;
}

HRESULT IDirect3DDevice3::AddViewport(LPDIRECT3DVIEWPORT3) { return S_OK; }
HRESULT IDirect3DDevice3::SetCurrentViewport(LPDIRECT3DVIEWPORT3) { return S_OK; }
ULONG   IDirect3DDevice3::Release() { return 0; }
