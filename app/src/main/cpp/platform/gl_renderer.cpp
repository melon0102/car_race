// gl_renderer.cpp — GLES3 renderer implementation (see gl_renderer.h).

#define RV_NO_LONG32   // this TU includes system headers (EGL/GLES)
#include "gl_renderer.h"
#include "winshim/ddraw.h"

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/log.h>

#define LOG_TAG "revolt-gl"
#define ALOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// on-screen debug overlay (platform/debug_overlay.cpp)
extern "C" void DbgOverlayDraw(int width, int height);
extern "C" void TouchOverlayDraw(int width, int height);
extern "C" void DbgCountDraw(int vertexCount);
extern "C" void DbgCountTexUpload(void);

// ---------------------------------------------------------------- state

namespace {

EGLDisplay gDisplay = EGL_NO_DISPLAY;
EGLSurface gSurface = EGL_NO_SURFACE;

int gWidth = 640, gHeight = 480;

GLuint gProgram = 0;
GLuint gVao = 0, gVbo = 0, gIbo = 0;
GLint uScreen = -1, uUseTex = -1, uFogOn = -1, uFogColor = -1, uTex = -1, uCKey = -1;
bool gES3 = true;   // false = GLES2 fallback path (emulators)

// shadowed D3D render state (game sets these through dx.h macros)
struct {
    bool alphaBlend = false;
    D3DBLEND srcBlend = D3DBLEND_ONE;
    D3DBLEND dstBlend = D3DBLEND_ZERO;
    bool zEnable = true;
    bool zWrite = true;
    D3DCMPFUNC zFunc = D3DCMP_LESSEQUAL;
    bool fog = false;
    DWORD fogColor = 0;
    bool colorKey = false;   // D3DRENDERSTATE_COLORKEYENABLE: discard keyed texels
    D3DCULL cull = D3DCULL_NONE;
    IDirect3DTexture2 *texture0 = nullptr;
    bool dirty = true;
} gState;

// shader body shared between GLES3 and GLES2 via prelude macros
const char *kVertSrc3 = "#version 300 es\n#define ATTR in\n#define VARY out\n";
const char *kVertSrc2 = "#define ATTR attribute\n#define VARY varying\n";
const char *kVertBody = R"(
precision highp float;
ATTR vec4 aPosRHW;   // sx, sy, sz, rhw (screen space)
ATTR vec4 aColor;    // diffuse (BGRA as normalized)
ATTR vec4 aSpecular; // specular rgb + fog factor in a
ATTR vec2 aUV;
uniform vec2 uScreen;
VARY vec4 vColor;
VARY vec4 vSpec;
VARY vec2 vUV;
void main() {
    float ndcX = aPosRHW.x / uScreen.x * 2.0 - 1.0;
    float ndcY = 1.0 - aPosRHW.y / uScreen.y * 2.0;
    // clamp: D3D TL verts are drawn with D3DDP_DONOTCLIP and some game paths
    // (DrawPanelSprite) leave sz stale — never let depth clip the primitive
    float ndcZ = clamp(aPosRHW.z, 0.0, 0.99995) * 2.0 - 1.0;
    // restore perspective-correct interpolation: w = 1/rhw
    float w = (aPosRHW.w > 0.0001) ? (1.0 / aPosRHW.w) : 1.0;
    gl_Position = vec4(ndcX * w, ndcY * w, ndcZ * w, w);
    // D3DCOLOR memory order is B,G,R,A — swizzle to RGBA here
    vColor = aColor.bgra;
    vSpec = aSpecular.bgra;
    vUV = aUV;
}
)";

const char *kFragSrc3 =
    "#version 300 es\n#define VARY in\n#define TEX2D texture\n"
    "out vec4 rvFragColor;\n#define OUT_COLOR rvFragColor\n";
const char *kFragSrc2 =
    "#define VARY varying\n#define TEX2D texture2D\n"
    "#define OUT_COLOR gl_FragColor\n";
const char *kFragBody = R"(
precision mediump float;
VARY vec4 vColor;
VARY vec4 vSpec;
VARY vec2 vUV;
uniform sampler2D uTex;
uniform int uUseTex;
uniform int uFogOn;
uniform int uCKey;
uniform vec4 uFogColor;
void main() {
    vec4 base = vColor;
    if (uUseTex != 0) {
        vec4 t = TEX2D(uTex, vUV);
        // D3D color-key: keyed texels (baked to a=0 at upload) are skipped
        if (uCKey != 0 && t.a < 0.5) discard;
        base *= t;
    }
    vec3 rgb = base.rgb + vSpec.rgb;              // D3D additive specular
    if (uFogOn != 0) rgb = mix(uFogColor.rgb, rgb, vSpec.a); // D3D vertex fog
    OUT_COLOR = vec4(rgb, base.a);
}
)";

GLuint Compile(GLenum type, const char *prelude, const char *body)
{
    GLuint sh = glCreateShader(type);
    const char *parts[2] = { prelude, body };
    glShaderSource(sh, 2, parts, nullptr);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(sh, sizeof(log), nullptr, log);
        ALOGE("shader compile failed: %s", log);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

GLenum MapBlend(D3DBLEND b)
{
    switch (b) {
        case D3DBLEND_ZERO:         return GL_ZERO;
        case D3DBLEND_ONE:          return GL_ONE;
        case D3DBLEND_SRCCOLOR:     return GL_SRC_COLOR;
        case D3DBLEND_INVSRCCOLOR:  return GL_ONE_MINUS_SRC_COLOR;
        case D3DBLEND_SRCALPHA:     return GL_SRC_ALPHA;
        case D3DBLEND_INVSRCALPHA:  return GL_ONE_MINUS_SRC_ALPHA;
        case D3DBLEND_DESTALPHA:    return GL_DST_ALPHA;
        case D3DBLEND_INVDESTALPHA: return GL_ONE_MINUS_DST_ALPHA;
        case D3DBLEND_DESTCOLOR:    return GL_DST_COLOR;
        case D3DBLEND_INVDESTCOLOR: return GL_ONE_MINUS_DST_COLOR;
        default:                    return GL_ONE;
    }
}

GLenum MapCmp(D3DCMPFUNC f)
{
    switch (f) {
        case D3DCMP_NEVER:        return GL_NEVER;
        case D3DCMP_LESS:         return GL_LESS;
        case D3DCMP_EQUAL:        return GL_EQUAL;
        case D3DCMP_LESSEQUAL:    return GL_LEQUAL;
        case D3DCMP_GREATER:      return GL_GREATER;
        case D3DCMP_NOTEQUAL:     return GL_NOTEQUAL;
        case D3DCMP_GREATEREQUAL: return GL_GEQUAL;
        default:                  return GL_ALWAYS;
    }
}

// upload a surface (with mip chain) as a GL texture; converts the shim's
// 32-bit ARGB pixel memory to RGBA
void UploadTexture(IDirectDrawSurface4 *surf)
{
    DbgCountTexUpload();
    if (!surf->glTex) glGenTextures(1, &surf->glTex);
    glBindTexture(GL_TEXTURE_2D, surf->glTex);

    int levels = 0;
    for (IDirectDrawSurface4 *s = surf; s; s = s->nextMip, levels++) {
        DWORD w = s->desc.dwWidth, h = s->desc.dwHeight;
        const DWORD *src = (const DWORD *)s->pixels;
        if (!src) continue;
        // ARGB (BGRA byte order) -> RGBA
        DWORD *tmp = (DWORD *)malloc((size_t)w * h * 4);
        if (!tmp) break;
        // color-keyed surface (SetColorKey): keyed texels bake to alpha 0 so
        // the shader can discard them when COLORKEYENABLE is on; the rest is
        // forced opaque (the X channel of XRGB memory is undefined)
        bool keyed = surf->hasColorKey || s->hasColorKey;
        DWORD key = s->desc.ddckCKSrcBlt.dwColorSpaceLowValue & 0x00ffffff;
        for (DWORD i = 0; i < w * h; i++) {
            DWORD c = src[i];
            DWORD out = (c & 0x0000ff00) |         // G stays
                        ((c >> 16) & 0x000000ff) | // R from bits 16-23
                        ((c << 16) & 0x00ff0000);  // B to bits 16-23
            if (keyed && (c & 0x00ffffff) == key)
                tmp[i] = out;                      // alpha 0 = keyed away
            else
                tmp[i] = out | 0xff000000;
        }
        glTexImage2D(GL_TEXTURE_2D, levels, GL_RGBA, (GLsizei)w, (GLsizei)h, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, tmp);
        free(tmp);
        s->dirty = FALSE;
    }

    // ES2 has no TEXTURE_MAX_LEVEL: a partial mip chain samples black, so
    // mip filtering is ES3-only; ES2 always samples the base level.
    bool useMips = gES3 && levels > 1;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    useMips ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (useMips)
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, levels - 1);
}

void ApplyState()
{
    if (!gState.dirty) return;
    gState.dirty = false;

    if (gState.alphaBlend) {
        glEnable(GL_BLEND);
        glBlendFunc(MapBlend(gState.srcBlend), MapBlend(gState.dstBlend));
    } else {
        glDisable(GL_BLEND);
    }

    if (gState.zEnable) {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(MapCmp(gState.zFunc));
    } else {
        glDisable(GL_DEPTH_TEST);
    }
    glDepthMask(gState.zWrite ? GL_TRUE : GL_FALSE);

    if (gState.cull == D3DCULL_NONE) {
        glDisable(GL_CULL_FACE);
    } else {
        glEnable(GL_CULL_FACE);
        // D3D CCW cull == GL front-face CW kept; match handedness
        glCullFace(GL_BACK);
        glFrontFace(gState.cull == D3DCULL_CCW ? GL_CW : GL_CCW);
    }

    glUniform1i(uFogOn, gState.fog ? 1 : 0);
    glUniform1i(uCKey, gState.colorKey ? 1 : 0);
    glUniform4f(uFogColor,
                ((gState.fogColor >> 16) & 0xff) / 255.0f,
                ((gState.fogColor >> 8) & 0xff) / 255.0f,
                (gState.fogColor & 0xff) / 255.0f, 1.0f);

    IDirectDrawSurface4 *surf =
        gState.texture0 ? gState.texture0->surface : nullptr;
    if (surf && surf->pixels) {
        if (surf->dirty || !surf->glTex) UploadTexture(surf);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, surf->glTex);
        glUniform1i(uUseTex, 1);
    } else {
        glUniform1i(uUseTex, 0);
    }
}

}  // namespace

// ---------------------------------------------------------------- lifecycle

bool RendererInit(int screenWidth, int screenHeight, int esMajorVersion)
{
    gWidth = screenWidth;
    gHeight = screenHeight;
    gES3 = esMajorVersion >= 3;

    GLuint vs = Compile(GL_VERTEX_SHADER, gES3 ? kVertSrc3 : kVertSrc2, kVertBody);
    GLuint fs = Compile(GL_FRAGMENT_SHADER, gES3 ? kFragSrc3 : kFragSrc2, kFragBody);
    if (!vs || !fs) return false;

    gProgram = glCreateProgram();
    glAttachShader(gProgram, vs);
    glAttachShader(gProgram, fs);
    // fixed attrib locations (works for both GLSL versions)
    glBindAttribLocation(gProgram, 0, "aPosRHW");
    glBindAttribLocation(gProgram, 1, "aColor");
    glBindAttribLocation(gProgram, 2, "aSpecular");
    glBindAttribLocation(gProgram, 3, "aUV");
    glLinkProgram(gProgram);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = 0;
    glGetProgramiv(gProgram, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(gProgram, sizeof(log), nullptr, log);
        ALOGE("program link failed: %s", log);
        return false;
    }

    uScreen = glGetUniformLocation(gProgram, "uScreen");
    uUseTex = glGetUniformLocation(gProgram, "uUseTex");
    uFogOn = glGetUniformLocation(gProgram, "uFogOn");
    uFogColor = glGetUniformLocation(gProgram, "uFogColor");
    uTex = glGetUniformLocation(gProgram, "uTex");
    uCKey = glGetUniformLocation(gProgram, "uCKey");

    if (gES3) glGenVertexArrays(1, &gVao);   // VAOs are ES3-only
    glGenBuffers(1, &gVbo);
    glGenBuffers(1, &gIbo);

    glUseProgram(gProgram);
    glUniform1i(uTex, 0);
    glUniform2f(uScreen, (float)gWidth, (float)gHeight);

    gState.dirty = true;
    ALOGI("renderer ready (%dx%d, ES%d)", gWidth, gHeight, gES3 ? 3 : 2);
    return true;
}

void RendererShutdown()
{
    if (gProgram) glDeleteProgram(gProgram);
    if (gVao && gES3) glDeleteVertexArrays(1, &gVao);
    if (gVbo) glDeleteBuffers(1, &gVbo);
    if (gIbo) glDeleteBuffers(1, &gIbo);
    gProgram = gVao = gVbo = gIbo = 0;
}

void RendererSetEGL(void *display, void *surface)
{
    gDisplay = (EGLDisplay)display;
    gSurface = (EGLSurface)surface;
}

void RendererResize(int screenWidth, int screenHeight)
{
    gWidth = screenWidth;
    gHeight = screenHeight;
    if (gProgram) {
        glUseProgram(gProgram);
        glUniform2f(uScreen, (float)gWidth, (float)gHeight);
    }
}

// ---------------------------------------------------------------- frame

void RendererClear(int32_t rgb)
{
    glViewport(0, 0, gWidth, gHeight);
    glClearColor(((rgb >> 16) & 0xff) / 255.0f, ((rgb >> 8) & 0xff) / 255.0f,
                 (rgb & 0xff) / 255.0f, 1.0f);
    glClearDepthf(1.0f);
    glDepthMask(GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    gState.dirty = true;   // depth mask touched
}

static int gSwapCount = 0;
int RendererGetSwapCount() { return gSwapCount; }

void RendererSwap()
{
    gSwapCount++;
    static bool inOverlay = false;
    if (!inOverlay) {
        inOverlay = true;
        // the game's dx.h macros shadow render state and skip redundant sets;
        // the overlay must leave gState exactly as found or those shadows lie
        // (symptom: first HUD batch each frame drew with a null texture)
        auto saved = gState;
        TouchOverlayDraw(gWidth, gHeight);
        DbgOverlayDraw(gWidth, gHeight);    // log stays on top of the buttons
        gState = saved;
        gState.dirty = true;
        inOverlay = false;
    }
    if (gDisplay != EGL_NO_DISPLAY && gSurface != EGL_NO_SURFACE)
        eglSwapBuffers(gDisplay, gSurface);
}

// ---------------------------------------------------------------- state

void RendererSetRenderState(D3DRENDERSTATETYPE state, DWORD value)
{
    switch (state) {
        case D3DRENDERSTATE_ALPHABLENDENABLE: gState.alphaBlend = value != 0; break;
        case D3DRENDERSTATE_SRCBLEND:  gState.srcBlend = (D3DBLEND)value; break;
        case D3DRENDERSTATE_DESTBLEND: gState.dstBlend = (D3DBLEND)value; break;
        case D3DRENDERSTATE_ZENABLE:   gState.zEnable = value != D3DZB_FALSE; break;
        case D3DRENDERSTATE_ZWRITEENABLE: gState.zWrite = value != 0; break;
        case D3DRENDERSTATE_ZFUNC:     gState.zFunc = (D3DCMPFUNC)value; break;
        case D3DRENDERSTATE_FOGENABLE: gState.fog = value != 0; break;
        case D3DRENDERSTATE_FOGCOLOR:  gState.fogColor = value; break;
        case D3DRENDERSTATE_COLORKEYENABLE: gState.colorKey = value != 0; break;
        case D3DRENDERSTATE_CULLMODE:  gState.cull = (D3DCULL)value; break;
        default: return;   // filters/dither/etc — not state we shadow yet
    }
    gState.dirty = true;
}

extern "C" void DbgPrintf(const char *fmt, ...);

void RendererSetTexture(DWORD stage, IDirect3DTexture2 *texture)
{
    if (stage != 0) return;   // stage 1 (env maps) comes later
    static bool loggedFirst = false;
    if (texture && !loggedFirst) {
        loggedFirst = true;
        IDirectDrawSurface4 *s = texture->surface;
        DbgPrintf("FIRST SETTEX surf=%p px=%p %dx%d", s,
                  s ? s->pixels : nullptr,
                  s ? (int)s->desc.dwWidth : 0, s ? (int)s->desc.dwHeight : 0);
    }
    gState.texture0 = texture;
    gState.dirty = true;
}

// ---------------------------------------------------------------- draw

void RendererDraw(D3DPRIMITIVETYPE type, DWORD fvf, const void *vertices,
                  DWORD vertexCount, const WORD *indices, DWORD indexCount)
{
    if (!gProgram || !vertices || vertexCount == 0) return;
    DbgCountDraw((int)vertexCount);

    // vertex layout from FVF: XYZRHW(16) + DIFFUSE(4) + SPECULAR(4) + UVs
    if (!(fvf & D3DFVF_XYZRHW)) return;   // game only submits TL vertices
    GLsizei stride = 16;
    GLsizei colorOfs = -1, specOfs = -1, uvOfs = -1;
    if (fvf & D3DFVF_DIFFUSE) { colorOfs = stride; stride += 4; }
    if (fvf & D3DFVF_SPECULAR) { specOfs = stride; stride += 4; }
    DWORD texCount = (fvf >> 8) & 0xf;    // TEX1=1, TEX2=2 in our defines
    if (texCount > 0) { uvOfs = stride; stride += 8 * (GLsizei)texCount; }

    glUseProgram(gProgram);
    if (gES3) glBindVertexArray(gVao);

    glBindBuffer(GL_ARRAY_BUFFER, gVbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)stride * vertexCount, vertices,
                 GL_STREAM_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, stride, (void *)0);

    if (colorOfs >= 0) {
        glEnableVertexAttribArray(1);
        // D3DCOLOR is BGRA in memory; swizzle in the attrib setup
        glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride,
                              (void *)(intptr_t)colorOfs);
    } else {
        glDisableVertexAttribArray(1);
        glVertexAttrib4f(1, 1, 1, 1, 1);
    }

    if (specOfs >= 0) {
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride,
                              (void *)(intptr_t)specOfs);
    } else {
        glDisableVertexAttribArray(2);
        glVertexAttrib4f(2, 0, 0, 0, 1);
    }

    if (uvOfs >= 0) {
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, stride,
                              (void *)(intptr_t)uvOfs);
    } else {
        glDisableVertexAttribArray(3);
        glVertexAttrib2f(3, 0, 0);
    }

    ApplyState();

    GLenum mode;
    GLsizei elemCount = (GLsizei)(indices ? indexCount : vertexCount);
    switch (type) {
        case D3DPT_TRIANGLELIST:  mode = GL_TRIANGLES; break;
        case D3DPT_TRIANGLESTRIP: mode = GL_TRIANGLE_STRIP; break;
        case D3DPT_TRIANGLEFAN:   mode = GL_TRIANGLE_FAN; break;
        case D3DPT_LINELIST:      mode = GL_LINES; break;
        case D3DPT_LINESTRIP:     mode = GL_LINE_STRIP; break;
        case D3DPT_POINTLIST:     mode = GL_POINTS; break;
        default: return;
    }

    if (indices) {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gIbo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)indexCount * 2,
                     indices, GL_STREAM_DRAW);
        glDrawElements(mode, elemCount, GL_UNSIGNED_SHORT, (void *)0);
    } else {
        glDrawArrays(mode, 0, elemCount);
    }
}
