// d3d.h — Direct3D 6 (IM) shim for the Re-Volt Android port.
// The game's rendering CALLS get reimplemented on GLES3; this header only has
// to make the game's TYPES and CONSTANTS parse. Values match the real DX6
// headers where it matters (FVF bits, colour packing).
#pragma once
#include "windows.h"
#include "ddraw.h"

// ---------------------------------------------------------------- basics
typedef float    D3DVALUE, *LPD3DVALUE;
typedef DWORD    D3DCOLOR, *LPD3DCOLOR;
typedef LONG     D3DFIXED;
typedef DWORD    D3DTEXTUREHANDLE;
typedef DWORD    D3DMATERIALHANDLE;

// opaque COM interfaces — forward-declared so both `IDirect3DTexture2 *x`
// and `LPDIRECT3DTEXTURE2 x` forms in the game code work
struct IDirect3D;          struct IDirect3D2;
struct IDirect3D3;         struct IDirect3D7;
struct IDirect3DDevice;    struct IDirect3DDevice2;
struct IDirect3DDevice7;
struct IDirect3DDevice3;   // real class — declared below
struct IDirect3DTexture;   // (IDirect3DTexture2 is a real class — see below)
struct IDirect3DViewport;  struct IDirect3DViewport2;
struct IDirect3DViewport3; // real class — declared below
struct IDirect3DMaterial;
struct IDirect3DMaterial2; struct IDirect3DMaterial3;
struct IDirect3DLight;     struct IDirect3DVertexBuffer;

typedef struct IDirect3D          *LPDIRECT3D;
typedef struct IDirect3D2         *LPDIRECT3D2;
typedef struct IDirect3D3         *LPDIRECT3D3;
typedef struct IDirect3D7         *LPDIRECT3D7;
typedef struct IDirect3DDevice    *LPDIRECT3DDEVICE;
typedef struct IDirect3DDevice2   *LPDIRECT3DDEVICE2;
typedef struct IDirect3DDevice3   *LPDIRECT3DDEVICE3;
typedef struct IDirect3DDevice7   *LPDIRECT3DDEVICE7;
typedef struct IDirect3DTexture   *LPDIRECT3DTEXTURE;
typedef struct IDirect3DTexture2  *LPDIRECT3DTEXTURE2;
typedef struct IDirect3DViewport  *LPDIRECT3DVIEWPORT;
typedef struct IDirect3DViewport2 *LPDIRECT3DVIEWPORT2;
typedef struct IDirect3DViewport3 *LPDIRECT3DVIEWPORT3;
typedef struct IDirect3DMaterial  *LPDIRECT3DMATERIAL;
typedef struct IDirect3DMaterial2 *LPDIRECT3DMATERIAL2;
typedef struct IDirect3DMaterial3 *LPDIRECT3DMATERIAL3;
typedef struct IDirect3DLight     *LPDIRECT3DLIGHT;
typedef struct IDirect3DVertexBuffer *LPDIRECT3DVERTEXBUFFER;

// ---------------------------------------------------------------- colour
#define RGBA_MAKE(r, g, b, a) \
    ((D3DCOLOR)((((DWORD)(a)) << 24) | (((DWORD)(r)) << 16) | \
                (((DWORD)(g)) << 8)  |  ((DWORD)(b))))
#define RGB_MAKE(r, g, b)  RGBA_MAKE(r, g, b, 0xff)
#define D3DRGBA(r, g, b, a) \
    RGBA_MAKE((DWORD)((r) * 255.0f), (DWORD)((g) * 255.0f), \
              (DWORD)((b) * 255.0f), (DWORD)((a) * 255.0f))
#define D3DRGB(r, g, b) D3DRGBA(r, g, b, 1.0f)
#define RGBA_GETALPHA(rgb) ((rgb) >> 24)
#define RGBA_GETRED(rgb)   (((rgb) >> 16) & 0xff)
#define RGBA_GETGREEN(rgb) (((rgb) >> 8) & 0xff)
#define RGBA_GETBLUE(rgb)  ((rgb) & 0xff)

// ---------------------------------------------------------------- vectors
typedef struct _D3DVECTOR {
    union { D3DVALUE x; D3DVALUE dvX; };
    union { D3DVALUE y; D3DVALUE dvY; };
    union { D3DVALUE z; D3DVALUE dvZ; };
} D3DVECTOR, *LPD3DVECTOR;

typedef struct _D3DMATRIX {
    D3DVALUE _11, _12, _13, _14;
    D3DVALUE _21, _22, _23, _24;
    D3DVALUE _31, _32, _33, _34;
    D3DVALUE _41, _42, _43, _44;
} D3DMATRIX, *LPD3DMATRIX;

// ---------------------------------------------------------------- vertices
typedef struct _D3DVERTEX {
    union { D3DVALUE x;  D3DVALUE dvX; };
    union { D3DVALUE y;  D3DVALUE dvY; };
    union { D3DVALUE z;  D3DVALUE dvZ; };
    union { D3DVALUE nx; D3DVALUE dvNX; };
    union { D3DVALUE ny; D3DVALUE dvNY; };
    union { D3DVALUE nz; D3DVALUE dvNZ; };
    union { D3DVALUE tu; D3DVALUE dvTU; };
    union { D3DVALUE tv; D3DVALUE dvTV; };
} D3DVERTEX, *LPD3DVERTEX;

typedef struct _D3DLVERTEX {
    union { D3DVALUE x; D3DVALUE dvX; };
    union { D3DVALUE y; D3DVALUE dvY; };
    union { D3DVALUE z; D3DVALUE dvZ; };
    DWORD dwReserved;
    union { D3DCOLOR color;    D3DCOLOR dcColor; };
    union { D3DCOLOR specular; D3DCOLOR dcSpecular; };
    union { D3DVALUE tu; D3DVALUE dvTU; };
    union { D3DVALUE tv; D3DVALUE dvTV; };
} D3DLVERTEX, *LPD3DLVERTEX;

typedef struct _D3DTLVERTEX {
    union { D3DVALUE sx;  D3DVALUE dvSX; };
    union { D3DVALUE sy;  D3DVALUE dvSY; };
    union { D3DVALUE sz;  D3DVALUE dvSZ; };
    union { D3DVALUE rhw; D3DVALUE dvRHW; };
    union { D3DCOLOR color;    D3DCOLOR dcColor; };
    union { D3DCOLOR specular; D3DCOLOR dcSpecular; };
    union { D3DVALUE tu; D3DVALUE dvTU; };
    union { D3DVALUE tv; D3DVALUE dvTV; };
} D3DTLVERTEX, *LPD3DTLVERTEX;

// vertex with 2 sets of UVs (FVF_TEX2 style), used for env-mapped polys
typedef struct _D3DTLVERTEX2 {
    D3DVALUE sx, sy, sz, rhw;
    D3DCOLOR color, specular;
    D3DVALUE tu, tv;
    D3DVALUE tu2, tv2;
} D3DTLVERTEX2;

// ---------------------------------------------------------------- FVF bits
#define D3DFVF_RESERVED0     0x001
#define D3DFVF_XYZ           0x002
#define D3DFVF_XYZRHW        0x004
#define D3DFVF_NORMAL        0x010
#define D3DFVF_DIFFUSE       0x040
#define D3DFVF_SPECULAR      0x080
#define D3DFVF_TEX0          0x000
#define D3DFVF_TEX1          0x100
#define D3DFVF_TEX2          0x200
#define D3DFVF_TLVERTEX \
    (D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_SPECULAR | D3DFVF_TEX1)
#define D3DFVF_LVERTEX \
    (D3DFVF_XYZ | D3DFVF_RESERVED0 | D3DFVF_DIFFUSE | D3DFVF_SPECULAR | D3DFVF_TEX1)
#define D3DFVF_VERTEX (D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX1)

// ---------------------------------------------------------------- draw flags
#define D3DDP_WAIT                0x00000001
#define D3DDP_OUTOFORDER          0x00000002
#define D3DDP_DONOTCLIP           0x00000004
#define D3DDP_DONOTUPDATEEXTENTS  0x00000008
#define D3DDP_DONOTLIGHT          0x00000010

#define D3DCLEAR_TARGET  0x00000001
#define D3DCLEAR_ZBUFFER 0x00000002

#define D3DANTIALIAS_NONE            1
#define D3DANTIALIAS_SORTDEPENDENT   2
#define D3DANTIALIAS_SORTINDEPENDENT 3

// ---------------------------------------------------------------- enums
typedef enum _D3DPRIMITIVETYPE {
    D3DPT_POINTLIST     = 1,
    D3DPT_LINELIST      = 2,
    D3DPT_LINESTRIP     = 3,
    D3DPT_TRIANGLELIST  = 4,
    D3DPT_TRIANGLESTRIP = 5,
    D3DPT_TRIANGLEFAN   = 6,
} D3DPRIMITIVETYPE;

typedef enum _D3DBLEND {
    D3DBLEND_ZERO         = 1,
    D3DBLEND_ONE          = 2,
    D3DBLEND_SRCCOLOR     = 3,
    D3DBLEND_INVSRCCOLOR  = 4,
    D3DBLEND_SRCALPHA     = 5,
    D3DBLEND_INVSRCALPHA  = 6,
    D3DBLEND_DESTALPHA    = 7,
    D3DBLEND_INVDESTALPHA = 8,
    D3DBLEND_DESTCOLOR    = 9,
    D3DBLEND_INVDESTCOLOR = 10,
    D3DBLEND_SRCALPHASAT  = 11,
} D3DBLEND;

typedef enum _D3DCMPFUNC {
    D3DCMP_NEVER        = 1,
    D3DCMP_LESS         = 2,
    D3DCMP_EQUAL        = 3,
    D3DCMP_LESSEQUAL    = 4,
    D3DCMP_GREATER      = 5,
    D3DCMP_NOTEQUAL     = 6,
    D3DCMP_GREATEREQUAL = 7,
    D3DCMP_ALWAYS       = 8,
} D3DCMPFUNC;

typedef enum _D3DCULL {
    D3DCULL_NONE = 1,
    D3DCULL_CW   = 2,
    D3DCULL_CCW  = 3,
} D3DCULL;

typedef enum _D3DSHADEMODE {
    D3DSHADE_FLAT    = 1,
    D3DSHADE_GOURAUD = 2,
} D3DSHADEMODE;

typedef enum _D3DFILLMODE {
    D3DFILL_POINT     = 1,
    D3DFILL_WIREFRAME = 2,
    D3DFILL_SOLID     = 3,
} D3DFILLMODE;

typedef enum _D3DFOGMODE {
    D3DFOG_NONE   = 0,
    D3DFOG_EXP    = 1,
    D3DFOG_EXP2   = 2,
    D3DFOG_LINEAR = 3,
} D3DFOGMODE;

typedef enum _D3DTEXTUREFILTER {
    D3DFILTER_NEAREST         = 1,
    D3DFILTER_LINEAR          = 2,
    D3DFILTER_MIPNEAREST      = 3,
    D3DFILTER_MIPLINEAR       = 4,
    D3DFILTER_LINEARMIPNEAREST = 5,
    D3DFILTER_LINEARMIPLINEAR = 6,
} D3DTEXTUREFILTER;

typedef enum _D3DTEXTUREBLEND {
    D3DTBLEND_DECAL        = 1,
    D3DTBLEND_MODULATE     = 2,
    D3DTBLEND_DECALALPHA   = 3,
    D3DTBLEND_MODULATEALPHA = 4,
    D3DTBLEND_COPY         = 7,
    D3DTBLEND_ADD          = 8,
} D3DTEXTUREBLEND;

// renderstates (DX6 D3DRENDERSTATE_*) — subset the game uses
typedef enum _D3DRENDERSTATETYPE {
    D3DRENDERSTATE_TEXTUREHANDLE       = 1,
    D3DRENDERSTATE_ANTIALIAS           = 2,
    D3DRENDERSTATE_TEXTUREPERSPECTIVE  = 4,
    D3DRENDERSTATE_ZENABLE             = 7,
    D3DRENDERSTATE_FILLMODE            = 8,
    D3DRENDERSTATE_SHADEMODE           = 9,
    D3DRENDERSTATE_TEXTUREMAG          = 17,
    D3DRENDERSTATE_TEXTUREMIN          = 18,
    D3DRENDERSTATE_SRCBLEND            = 19,
    D3DRENDERSTATE_DESTBLEND           = 20,
    D3DRENDERSTATE_TEXTUREMAPBLEND     = 21,
    D3DRENDERSTATE_CULLMODE            = 22,
    D3DRENDERSTATE_ZFUNC               = 23,
    D3DRENDERSTATE_ALPHAREF            = 24,
    D3DRENDERSTATE_ALPHAFUNC           = 25,
    D3DRENDERSTATE_DITHERENABLE        = 26,
    D3DRENDERSTATE_ALPHABLENDENABLE    = 27,
    D3DRENDERSTATE_FOGENABLE           = 28,
    D3DRENDERSTATE_SPECULARENABLE      = 29,
    D3DRENDERSTATE_ZWRITEENABLE        = 14,
    D3DRENDERSTATE_ALPHATESTENABLE     = 15,
    D3DRENDERSTATE_FOGCOLOR            = 34,
    D3DRENDERSTATE_FOGTABLEMODE        = 35,
    D3DRENDERSTATE_FOGTABLESTART       = 36,
    D3DRENDERSTATE_FOGTABLEEND         = 37,
    D3DRENDERSTATE_FOGTABLEDENSITY     = 38,
    D3DRENDERSTATE_COLORKEYENABLE      = 41,
    D3DRENDERSTATE_STIPPLEDALPHA       = 47,
    D3DRENDERSTATE_WRAPU               = 5,
    D3DRENDERSTATE_WRAPV               = 6,
    D3DRENDERSTATE_TRANSLUCENTSORTINDEPENDENT = 127,
    D3DRENDERSTATE_FORCE_DWORD         = 0x7fffffff,
} D3DRENDERSTATETYPE;

// texture stage states (DX6)
typedef enum _D3DTEXTURESTAGESTATETYPE {
    D3DTSS_COLOROP   = 1,
    D3DTSS_COLORARG1 = 2,
    D3DTSS_COLORARG2 = 3,
    D3DTSS_ALPHAOP   = 4,
    D3DTSS_ALPHAARG1 = 5,
    D3DTSS_ALPHAARG2 = 6,
    D3DTSS_MAGFILTER = 16,
    D3DTSS_MINFILTER = 17,
    D3DTSS_MIPFILTER = 18,
    D3DTSS_TEXCOORDINDEX = 11,
    D3DTSS_ADDRESS   = 12,
    D3DTSS_ADDRESSU  = 13,
    D3DTSS_ADDRESSV  = 14,
    D3DTSS_MIPMAPLODBIAS = 19,
} D3DTEXTURESTAGESTATETYPE;

typedef enum _D3DTEXTUREOP {
    D3DTOP_DISABLE     = 1,
    D3DTOP_SELECTARG1  = 2,
    D3DTOP_SELECTARG2  = 3,
    D3DTOP_MODULATE    = 4,
    D3DTOP_MODULATE2X  = 5,
    D3DTOP_MODULATE4X  = 6,
    D3DTOP_ADD         = 7,
    D3DTOP_SUBTRACT    = 10,
} D3DTEXTUREOP;

typedef enum _D3DTEXTUREADDRESS {
    D3DTADDRESS_WRAP   = 1,
    D3DTADDRESS_MIRROR = 2,
    D3DTADDRESS_CLAMP  = 3,
    D3DTADDRESS_BORDER = 4,
} D3DTEXTUREADDRESS;

#define D3DTA_DIFFUSE  0x00000000
#define D3DTA_CURRENT  0x00000001
#define D3DTA_TEXTURE  0x00000002

typedef enum _D3DTEXTUREMAGFILTER {
    D3DTFG_POINT       = 1,
    D3DTFG_LINEAR      = 2,
    D3DTFG_ANISOTROPIC = 4,
} D3DTEXTUREMAGFILTER;

typedef enum _D3DTEXTUREMINFILTER {
    D3DTFN_POINT       = 1,
    D3DTFN_LINEAR      = 2,
    D3DTFN_ANISOTROPIC = 3,
} D3DTEXTUREMINFILTER;

typedef enum _D3DTEXTUREMIPFILTER {
    D3DTFP_NONE   = 1,
    D3DTFP_POINT  = 2,
    D3DTFP_LINEAR = 3,
} D3DTEXTUREMIPFILTER;

typedef enum _D3DZBUFFERTYPE {
    D3DZB_FALSE = 0,
    D3DZB_TRUE  = 1,
    D3DZB_USEW  = 2,
} D3DZBUFFERTYPE;

typedef enum _D3DTRANSFORMSTATETYPE {
    D3DTRANSFORMSTATE_WORLD      = 1,
    D3DTRANSFORMSTATE_VIEW       = 2,
    D3DTRANSFORMSTATE_PROJECTION = 3,
} D3DTRANSFORMSTATETYPE;

// ---------------------------------------------------------------- misc
typedef struct _D3DRECT {
    union { LONG x1; LONG lX1; };
    union { LONG y1; LONG lY1; };
    union { LONG x2; LONG lX2; };
    union { LONG y2; LONG lY2; };
} D3DRECT, *LPD3DRECT;

typedef struct _D3DVIEWPORT2 {
    DWORD    dwSize;
    DWORD    dwX, dwY;
    DWORD    dwWidth, dwHeight;
    D3DVALUE dvClipX, dvClipY;
    D3DVALUE dvClipWidth, dvClipHeight;
    D3DVALUE dvMinZ, dvMaxZ;
} D3DVIEWPORT2, *LPD3DVIEWPORT2;

typedef struct _D3DPRIMCAPS {
    DWORD dwSize;
    DWORD dwMiscCaps;
    DWORD dwRasterCaps;
    DWORD dwZCmpCaps;
    DWORD dwSrcBlendCaps;
    DWORD dwDestBlendCaps;
    DWORD dwAlphaCmpCaps;
    DWORD dwShadeCaps;
    DWORD dwTextureCaps;
    DWORD dwTextureFilterCaps;
    DWORD dwTextureBlendCaps;
    DWORD dwTextureAddressCaps;
    DWORD dwStippleWidth, dwStippleHeight;
} D3DPRIMCAPS;

// caps-flag subset the game tests (our GetCaps zeroes dwFlags, so the legacy
// restriction branches are skipped)
#define D3DDD_COLORMODEL        0x00000001
#define D3DDD_DEVCAPS           0x00000002
#define D3DDD_TRICAPS           0x00000040
#define D3DPTEXTURECAPS_SQUAREONLY       0x00000020
#define D3DDEVCAPS_TEXTURENONLOCALVIDMEM 0x00001000

typedef struct _D3DDEVICEDESC {
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dcmColorModel;
    DWORD dwDevCaps;
    D3DPRIMCAPS dpcLineCaps;
    D3DPRIMCAPS dpcTriCaps;
    DWORD dwDeviceRenderBitDepth;
    DWORD dwDeviceZBufferBitDepth;
    DWORD dwMaxBufferSize;
    DWORD dwMaxVertexCount;
    DWORD dwMinTextureWidth, dwMinTextureHeight;
    DWORD dwMaxTextureWidth, dwMaxTextureHeight;
    DWORD dwMinStippleWidth, dwMaxStippleWidth;
    DWORD dwMinStippleHeight, dwMaxStippleHeight;
} D3DDEVICEDESC, *LPD3DDEVICEDESC;

#define D3DDEVICEDESC_SIZE sizeof(D3DDEVICEDESC)

typedef HRESULT (*LPD3DENUMPIXELFORMATSCALLBACK)(LPDDPIXELFORMAT fmt, LPVOID ctx);

// ---------------------------------------------------------------------------
// IDirect3DDevice3 — REAL class in this port. The game calls these methods
// directly (and via dx.h's ALPHA_ON()/FOG_ON()/... macros); the Android
// implementations live in platform/gl_device.cpp and forward to the GLES3
// renderer. This is the seam where DirectX becomes OpenGL ES.
// ---------------------------------------------------------------------------
struct IDirect3DDevice3 {
    HRESULT SetRenderState(D3DRENDERSTATETYPE state, DWORD value);
    HRESULT SetTexture(DWORD stage, LPDIRECT3DTEXTURE2 texture);
    HRESULT SetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE type, DWORD value);
    HRESULT SetTransform(D3DTRANSFORMSTATETYPE state, LPD3DMATRIX matrix);
    HRESULT BeginScene();
    HRESULT EndScene();
    HRESULT DrawPrimitive(D3DPRIMITIVETYPE type, DWORD fvf, LPVOID vertices,
                          DWORD vertexCount, DWORD flags);
    HRESULT DrawIndexedPrimitive(D3DPRIMITIVETYPE type, DWORD fvf, LPVOID vertices,
                                 DWORD vertexCount, LPWORD indices,
                                 DWORD indexCount, DWORD flags);
    HRESULT EnumTextureFormats(LPD3DENUMPIXELFORMATSCALLBACK callback, LPVOID ctx);
    HRESULT GetCaps(D3DDEVICEDESC *hwDesc, D3DDEVICEDESC *helDesc);
    HRESULT AddViewport(LPDIRECT3DVIEWPORT3 viewport);
    HRESULT SetCurrentViewport(LPDIRECT3DVIEWPORT3 viewport);
    ULONG   Release();
};

// ---------------------------------------------------------------------------
// IDirect3DTexture2 — REAL class (implemented in platform/gl_device.cpp).
// Obtained from a surface via QueryInterface(IID_IDirect3DTexture2); wraps
// that surface's pixel memory for the GLES renderer.
// ---------------------------------------------------------------------------
extern const GUID IID_IDirect3DTexture2;

struct IDirect3DTexture2 {
    struct IDirectDrawSurface4 *surface;  // back-pointer to owning surface
    HRESULT Load(IDirect3DTexture2 *src); // copy src surface chain's pixels
    ULONG   AddRef();
    ULONG   Release();
};

// ---------------------------------------------------------------------------
// IDirect3DViewport3 — REAL class (implemented in platform/gl_device.cpp).
// SetViewport2 -> glViewport params; Clear2 -> glClear.
// ---------------------------------------------------------------------------
struct IDirect3DViewport3 {
    HRESULT SetViewport2(LPD3DVIEWPORT2 viewport);
    HRESULT GetViewport2(LPD3DVIEWPORT2 viewport);
    HRESULT Clear2(DWORD count, LPD3DRECT rects, DWORD flags,
                   D3DCOLOR color, D3DVALUE z, DWORD stencil);
    ULONG   Release();
};
