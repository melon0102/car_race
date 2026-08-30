// android_native.cpp — SDL-style shell for the Re-Volt Android port.
//
// Architecture (mirrors SDL2/RVGL, proven on emulators):
//   Java MainActivity owns a SurfaceView; its Surface is passed down via
//   JNI (nativeSurfaceChanged). A dedicated game thread turns it into an
//   ANativeWindow, brings up EGL + the renderer, boots the game and runs
//   the frame loop. Input arrives as plain JNI calls from Java.

#define RV_NO_LONG32

#include <jni.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/log.h>
#include <pthread.h>
#include <unistd.h>

#include <cmath>

#include "../platform/gl_renderer.h"

#define LOG_TAG "revolt-main"
#define ALOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

extern "C" int Revolt_SelfTest(void);
extern "C" int Revolt_Boot(const char *dataDir);
extern "C" int Revolt_Frame(void);
extern "C" void Revolt_Configure(const char *levelDir, int carId, int gameType,
                                 int reversed, int mirrored, int numCpuCars);

// input layer (platform/android_input.cpp)
extern "C" void AInput_TouchReset(void);
extern "C" void AInput_TouchPoint(float x, float y);
extern "C" void AInput_Key(int32_t keyCode, int down);
extern "C" void AInput_Axes(float steerX, float gas, float brake);
extern "C" void GLDevice_InvalidateTextures(void);
extern "C" void DbgPrintf(const char *fmt, ...);

// the game's globals (defined in the shim/replacement layer)
extern LPDIRECT3DDEVICE3 D3Ddevice;
extern DWORD ScreenXsize, ScreenYsize;

// ---------------------------------------------------------------- state

namespace {

pthread_mutex_t gLock = PTHREAD_MUTEX_INITIALIZER;
ANativeWindow *gPendingWindow = nullptr;   // set by JNI, consumed by thread
int gPendingW = 0, gPendingH = 0;
bool gWindowChanged = false;
bool gQuit = false;
bool gThreadStarted = false;
char gDataDir[512] = {0};

pthread_t gThread;

struct GfxState {
    ANativeWindow *window = nullptr;
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLContext context = EGL_NO_CONTEXT;
    int esMajor = 3;
    int width = 0, height = 0;
} gGfx;

bool gGameRunning = false;
bool gBootedOnce = false;

// ---------------------------------------------------------------- EGL

bool TryConfig(EGLConfig config)
{
    EGLint visualId = 0, r = 0, g = 0, b = 0, a = 0, d = 0;
    eglGetConfigAttrib(gGfx.display, config, EGL_NATIVE_VISUAL_ID, &visualId);
    eglGetConfigAttrib(gGfx.display, config, EGL_RED_SIZE, &r);
    eglGetConfigAttrib(gGfx.display, config, EGL_GREEN_SIZE, &g);
    eglGetConfigAttrib(gGfx.display, config, EGL_BLUE_SIZE, &b);
    eglGetConfigAttrib(gGfx.display, config, EGL_ALPHA_SIZE, &a);
    eglGetConfigAttrib(gGfx.display, config, EGL_DEPTH_SIZE, &d);

    gGfx.surface = eglCreateWindowSurface(gGfx.display, config, gGfx.window, nullptr);
    if (gGfx.surface == EGL_NO_SURFACE) {
        ALOGI("cfg vid=%d rgba=%d%d%d%d d=%d: surface failed 0x%x",
              visualId, r, g, b, a, d, eglGetError());
        return false;
    }

    for (int ver = 3; ver >= 2; ver--) {
        const EGLint ctxAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, ver, EGL_NONE };
        gGfx.context = eglCreateContext(gGfx.display, config, EGL_NO_CONTEXT, ctxAttribs);
        if (gGfx.context == EGL_NO_CONTEXT) {
            ALOGI("cfg vid=%d: ES%d context create failed 0x%x",
                  visualId, ver, eglGetError());
            continue;
        }
        if (eglMakeCurrent(gGfx.display, gGfx.surface, gGfx.surface, gGfx.context)) {
            gGfx.esMajor = ver;
            ALOGI("BOUND cfg vid=%d rgba=%d%d%d%d d=%d ES%d",
                  visualId, r, g, b, a, d, ver);
            return true;
        }
        ALOGI("cfg vid=%d: ES%d makeCurrent failed 0x%x", visualId, ver, eglGetError());
        eglDestroyContext(gGfx.display, gGfx.context);
        gGfx.context = EGL_NO_CONTEXT;
    }

    eglDestroySurface(gGfx.display, gGfx.surface);
    gGfx.surface = EGL_NO_SURFACE;
    return false;
}

bool InitDisplay()
{
    gGfx.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (gGfx.display == EGL_NO_DISPLAY || !eglInitialize(gGfx.display, nullptr, nullptr)) {
        ALOGE("egl display init failed");
        return false;
    }
    ALOGI("EGL vendor='%s' version='%s'",
          eglQueryString(gGfx.display, EGL_VENDOR),
          eglQueryString(gGfx.display, EGL_VERSION));
    eglBindAPI(EGL_OPENGL_ES_API);

    const EGLint attribs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
        EGL_DEPTH_SIZE, 16,
        EGL_NONE
    };
    EGLConfig configs[64];
    EGLint numConfigs = 0;
    if (!eglChooseConfig(gGfx.display, attribs, configs, 64, &numConfigs) ||
        numConfigs < 1) {
        ALOGE("no usable EGLConfig");
        return false;
    }
    ALOGI("%d candidate EGL configs", numConfigs);

    // prefer alpha-8 configs (matches the RGBA_8888 SurfaceView)
    for (int pass = 0; pass < 2; pass++) {
        for (EGLint i = 0; i < numConfigs; i++) {
            EGLint alpha = 0;
            eglGetConfigAttrib(gGfx.display, configs[i], EGL_ALPHA_SIZE, &alpha);
            if ((pass == 0) != (alpha == 8)) continue;
            if (TryConfig(configs[i])) goto bound;
        }
    }
    ALOGE("no EGL config could bind a context");
    return false;

bound:
    eglQuerySurface(gGfx.display, gGfx.surface, EGL_WIDTH, &gGfx.width);
    eglQuerySurface(gGfx.display, gGfx.surface, EGL_HEIGHT, &gGfx.height);
    eglSwapInterval(gGfx.display, 1);

    ALOGI("GL ready: %s | %s | %dx%d",
          (const char *)glGetString(GL_RENDERER),
          (const char *)glGetString(GL_VERSION), gGfx.width, gGfx.height);

    RendererSetEGL(gGfx.display, gGfx.surface);
    if (!RendererInit(gGfx.width, gGfx.height, gGfx.esMajor)) {
        ALOGE("RendererInit failed");
        return false;
    }
    ScreenXsize = (DWORD)gGfx.width;
    ScreenYsize = (DWORD)gGfx.height;
    return true;
}

void TermDisplay()
{
    GLDevice_InvalidateTextures();
    RendererShutdown();
    RendererSetEGL(EGL_NO_DISPLAY, EGL_NO_SURFACE);
    if (gGfx.display != EGL_NO_DISPLAY) {
        eglMakeCurrent(gGfx.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (gGfx.context != EGL_NO_CONTEXT) eglDestroyContext(gGfx.display, gGfx.context);
        if (gGfx.surface != EGL_NO_SURFACE) eglDestroySurface(gGfx.display, gGfx.surface);
        eglTerminate(gGfx.display);
    }
    gGfx.display = EGL_NO_DISPLAY;
    gGfx.surface = EGL_NO_SURFACE;
    gGfx.context = EGL_NO_CONTEXT;
    if (gGfx.window) {
        ANativeWindow_release(gGfx.window);
        gGfx.window = nullptr;
    }
}

// ---------------------------------------------------------------- game thread

void *GameThread(void *)
{
    ALOGI("game thread up");
    float phase = 0.0f;

    while (true) {
        // pick up window changes from the Java side
        pthread_mutex_lock(&gLock);
        bool quit = gQuit;
        bool changed = gWindowChanged;
        ANativeWindow *newWin = gPendingWindow;
        gWindowChanged = false;
        pthread_mutex_unlock(&gLock);

        if (quit) break;

        if (changed) {
            TermDisplay();
            gGfx.window = newWin;
            if (gGfx.window) {
                if (InitDisplay()) {
                    int ok = Revolt_SelfTest();
                    ALOGI("Re-Volt core self-test: %s", ok ? "PASS" : "FAIL");
                    DbgPrintf("GL %dx%d ES%d selftest %s", gGfx.width, gGfx.height,
                              gGfx.esMajor, ok ? "PASS" : "FAIL");
                    if (!gBootedOnce) {
                        gBootedOnce = true;
                        if (Revolt_Boot(gDataDir)) {
                            gGameRunning = true;
                        } else {
                            ALOGE("Revolt_Boot failed — test scene");
                            DbgPrintf("BOOT FAILED - TEST SCENE");
                        }
                    }
                } else {
                    ALOGE("InitDisplay failed for new window");
                }
            }
        }

        if (gGfx.surface == EGL_NO_SURFACE) {
            usleep(50000);   // no drawable yet
            continue;
        }

        if (gGameRunning) {
            int swapsBefore = RendererGetSwapCount();
            if (!Revolt_Frame()) {
                gGameRunning = false;
                ALOGI("game loop ended");
                DbgPrintf("GAME LOOP ENDED (quit flag)");
            }
            if (RendererGetSwapCount() == swapsBefore) {
                RendererClear(0x000030);
                RendererSwap();
            }
        } else {
            // renderer smoke test: spinning triangle through the D3D path
            phase += 0.02f;
            RendererClear(0x102040);
            float cx = gGfx.width * 0.5f, cy = gGfx.height * 0.5f;
            float r = gGfx.height * 0.3f;
            D3DTLVERTEX tri[3];
            for (int i = 0; i < 3; i++) {
                float ang = phase + (float)i * 2.09439f;
                tri[i].sx = cx + cosf(ang) * r;
                tri[i].sy = cy + sinf(ang) * r;
                tri[i].sz = 0.5f;
                tri[i].rhw = 1.0f;
                tri[i].specular = 0xff000000;
                tri[i].tu = tri[i].tv = 0.0f;
            }
            tri[0].color = 0xffff4020;
            tri[1].color = 0xff40ff20;
            tri[2].color = 0xff2040ff;
            D3Ddevice->SetRenderState(D3DRENDERSTATE_ZENABLE, D3DZB_FALSE);
            D3Ddevice->SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, FALSE);
            D3Ddevice->SetRenderState(D3DRENDERSTATE_CULLMODE, D3DCULL_NONE);
            D3Ddevice->SetTexture(0, nullptr);
            D3Ddevice->DrawPrimitive(D3DPT_TRIANGLELIST, D3DFVF_TLVERTEX, tri, 3, 0);
            RendererSwap();
        }
    }

    TermDisplay();
    ALOGI("game thread down");
    return nullptr;
}

}  // namespace

// ---------------------------------------------------------------- JNI

extern "C" {

JNIEXPORT void JNICALL
Java_com_revolt_game_MainActivity_nativeSetDataDir(JNIEnv *env, jclass, jstring dir)
{
    const char *s = env->GetStringUTFChars(dir, nullptr);
    snprintf(gDataDir, sizeof(gDataDir), "%s", s);
    env->ReleaseStringUTFChars(dir, s);
    ALOGI("data dir: %s", gDataDir);
}

JNIEXPORT void JNICALL
Java_com_revolt_game_MainActivity_nativeConfigure(JNIEnv *env, jclass,
                                                  jstring levelDir, jint carId,
                                                  jint gameType, jboolean reversed,
                                                  jboolean mirrored, jint numCpuCars)
{
    const char *s = levelDir ? env->GetStringUTFChars(levelDir, nullptr) : nullptr;
    Revolt_Configure(s ? s : "", carId, gameType, reversed ? 1 : 0, mirrored ? 1 : 0,
                     numCpuCars);
    if (s) env->ReleaseStringUTFChars(levelDir, s);
}

JNIEXPORT void JNICALL
Java_com_revolt_game_MainActivity_nativeSurfaceChanged(JNIEnv *env, jclass,
                                                       jobject surface, jint w, jint h)
{
    ANativeWindow *win = ANativeWindow_fromSurface(env, surface);
    ALOGI("nativeSurfaceChanged %dx%d win=%p", w, h, win);

    pthread_mutex_lock(&gLock);
    gPendingWindow = win;
    gPendingW = w;
    gPendingH = h;
    gWindowChanged = true;
    if (!gThreadStarted) {
        gThreadStarted = true;
        pthread_create(&gThread, nullptr, GameThread, nullptr);
    }
    pthread_mutex_unlock(&gLock);
}

JNIEXPORT void JNICALL
Java_com_revolt_game_MainActivity_nativeSurfaceDestroyed(JNIEnv *, jclass)
{
    ALOGI("nativeSurfaceDestroyed");
    pthread_mutex_lock(&gLock);
    gPendingWindow = nullptr;
    gWindowChanged = true;
    pthread_mutex_unlock(&gLock);
}

JNIEXPORT void JNICALL
Java_com_revolt_game_MainActivity_nativeTouchReset(JNIEnv *, jclass)
{
    AInput_TouchReset();
}

JNIEXPORT void JNICALL
Java_com_revolt_game_MainActivity_nativeTouchPoint(JNIEnv *, jclass, jfloat x, jfloat y)
{
    AInput_TouchPoint(x, y);
}

JNIEXPORT void JNICALL
Java_com_revolt_game_MainActivity_nativeKey(JNIEnv *, jclass, jint keyCode, jint down)
{
    AInput_Key(keyCode, down);
}

JNIEXPORT void JNICALL
Java_com_revolt_game_MainActivity_nativeAxes(JNIEnv *, jclass, jfloat sx, jfloat gas, jfloat brake)
{
    AInput_Axes(sx, gas, brake);
}

}  // extern "C"
