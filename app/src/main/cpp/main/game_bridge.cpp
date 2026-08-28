// game_bridge.cpp — the seam between the Android platform layer and the
// original Re-Volt game core.
//
// Two jobs:
//  1. Define globals the enabled game modules reference but whose owning
//     module (main.cpp, dx.cpp, ...) is NOT compiled — those files are being
//     replaced by this platform layer.
//  2. Expose plain C entry points (Revolt_*) for android_main to call.
//
// As more game files are enabled in CMakeLists.txt, definitions here get
// REMOVED when the real owning module starts to compile.

#include <android/log.h>   // before game headers (long=int define, see windows.h)

#include "revolt.h"
#include "main.h"
#include "geom.h"
#include "registry.h"
#include "input.h"

#define LOG_TAG "revolt-core"
#define ALOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

// ---------------------------------------------------------------------------
// 64-bit contract checks: game TUs must see long as 32-bit (see windows.h)
// and VISIMASK as 64-bit, or every binary loader mis-reads its files.
// ---------------------------------------------------------------------------
static_assert(sizeof(long) == 4, "long must be 32-bit in game code (RV long=int contract)");
static_assert(sizeof(VISIMASK) == 8, "VISIMASK must stay 64-bit");
static_assert(sizeof(GAME_SETTINGS) == 32, "GAME_SETTINGS layout drifted");   // 8 x 32-bit fields

// ---------------------------------------------------------------------------
// Globals normally defined in main.cpp (not compiled — replaced by
// android_main.cpp). Initial values copied from main.cpp.
// ---------------------------------------------------------------------------

char Everything = TRUE;
char NoGamma = FALSE;
char AppRestore = FALSE;
char QuitGame = FALSE;
char FullScreen = TRUE;
unsigned long FrameCount, FrameCountLast, FrameTime, FrameTimeLast, FrameRate;
long EditMode = 0;
char DetailMenuTogg = 0;
REAL TimeFactor;
REAL TimeStep;
REAL EditScale = 1.0f;
HWND hwnd = nullptr;
HBITMAP TitleHbm = 0;   // HBITMAP is integer-based in the shim
void (*Event)(void) = nullptr;
GAME_SETTINGS GameSettings;
RENDER_SETTINGS RenderSettings;

// MSVC runtime provides these on Windows; Bionic doesn't.
int __argc = 0;
char **__argv = nullptr;

// Normally defined in main.cpp (replaced by android_main.cpp).
char *CarInfoFile = (char *)"CarInfo.txt";

// Input globals + hooks live in platform/android_input.cpp (real impl).

// Go/GoFront/SetupGame/InitPlayers* live in game_boot.cpp (real ports).
void CheckCheatStrings(void) {}   // cheat-code keyboard buffer — not on Android

// Normally defined in registry.cpp (replaced — Android keeps settings in a
// config file via Get/SetRegistrySettings below in a later phase).
REGISTRY_SETTINGS RegistrySettings;

void GetRegistrySettings(void)
{
    // sensible Android defaults until the config file lands
    memset(&RegistrySettings, 0, sizeof(RegistrySettings));
    RegistrySettings.EnvFlag = TRUE;
    RegistrySettings.ShadowFlag = TRUE;
    RegistrySettings.LightFlag = TRUE;
    RegistrySettings.InstanceFlag = TRUE;
    RegistrySettings.SkidFlag = TRUE;
    RegistrySettings.AutoBrake = TRUE;
    RegistrySettings.CarID = 0;
    RegistrySettings.ScreenWidth = 1280;
    RegistrySettings.ScreenHeight = 720;
    RegistrySettings.ScreenBpp = 32;
    RegistrySettings.TextureBpp = 32;
    RegistrySettings.Brightness = 256;
    RegistrySettings.Contrast = 256;
    RegistrySettings.TextureBpp = 24;   // picks the 24/32-bit format branch
    strcpy(RegistrySettings.PlayerName, "PLAYER");
    strcpy(RegistrySettings.LevelDir, "NHOOD1");   // starting level; FindLevels stores dir names UPPERCASED
}

void SetRegistrySettings(void)
{
    // Phase 4: persist to a config file in the app's data dir
}

// main.cpp's message-box helper: log it (logcat + on-screen), answer "OK".
extern "C" void DbgPrintf(const char *fmt, ...);

long Box(char *title, char *mess, long flag)
{
    __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "[Box] %s: %s",
                        title ? title : "Re-Volt", mess ? mess : "");
    DbgPrintf("BOX %s: %s", title ? title : "", mess ? mess : "");
    (void)flag;
    return IDOK;
}

#if SHOW_PHYSICS_INFO
long DEBUG_CollGrid = 0;
int DEBUG_NCols = 0;
int DEBUG_LastNCols = 0;
int DEBUG_N2Cols = 0;
VEC DEBUG_dR = {0, 0, 0};
VEC DEBUG_Impulse = {0, 0, 0};
VEC DEBUG_AngImpulse = {0, 0, 0};
VEC DEBUG_SNorm[256];
FACING_POLY DEBUG_Faces[256];
#endif

// ---------------------------------------------------------------------------
// Entry points for the platform layer
// ---------------------------------------------------------------------------

extern "C" {

// Prove the original game code is alive: run its matrix/vector math and
// check a known identity (rotating (1,0,0) by 90° about Y must give ~(0,0,-1)
// or ~(0,0,1) depending on handedness — we just check length preservation
// and log the result).
int Revolt_SelfTest(void)
{
    MAT rot;
    VEC in = {{1.0f, 2.0f, 3.0f}};
    VEC out = {{0.0f, 0.0f, 0.0f}};

    RotMatrixY(&rot, 0.25f);        // game convention: 0.25 == 90 degrees
    RotVector(&rot, &in, &out);

    REAL lenIn  = (REAL)sqrt(in.v[X] * in.v[X] + in.v[Y] * in.v[Y] + in.v[Z] * in.v[Z]);
    REAL lenOut = (REAL)sqrt(out.v[X] * out.v[X] + out.v[Y] * out.v[Y] + out.v[Z] * out.v[Z]);

    ALOGI("SelfTest: RotMatrixY(90deg) * (%.1f %.1f %.1f) = (%.3f %.3f %.3f)",
          in.v[X], in.v[Y], in.v[Z], out.v[X], out.v[Y], out.v[Z]);
    ALOGI("SelfTest: |in|=%.4f |out|=%.4f", lenIn, lenOut);

    // rotation must preserve length
    return (fabsf(lenIn - lenOut) < 0.001f) ? 1 : 0;
}

}  // extern "C"
