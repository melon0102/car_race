// game_boot.cpp — the Android replacement for main.cpp's WinMain flow.
//
// Carries the real Go/GoFront/SetupGame/InitPlayers* functions (ported from
// main.cpp with the Win32 window/COM/lobby steps removed) plus:
//   Revolt_Boot(dataDir)  — one-time init, equivalent to WinMain's prologue
//   Revolt_Frame()        — one iteration of the WinMain message loop (Event())
//
// Boot goes straight into a time-trial race (SetupGame) instead of the 1999
// menu flow; the Android frontend UI takes over menu duties in Phase 4.

// system headers BEFORE the game headers — windows.h (via revolt.h) defines
// long=int for this TU on LP64, which must not touch system declarations
#include <unistd.h>
#include <android/log.h>

#include "revolt.h"
#include "main.h"
#include "dx.h"
#include "geom.h"
#include "model.h"
#include "texture.h"
#include "particle.h"
#include "aerial.h"
#include "play.h"
#include "NewColl.h"
#include "Body.h"
#include "car.h"
#include "input.h"
#include "sfx.h"
#include "text.h"
#include "shadow.h"
#include "camera.h"
#include "light.h"
#include "world.h"
#include "draw.h"
#include "DrawObj.h"
#include "visibox.h"
#include "editobj.h"
#include "level.h"
#include "ReadInit.h"
#include "gameloop.h"
#include "gaussian.h"
#include "timing.h"
#include "registry.h"
#include "ctrlread.h"
#include "object.h"
#include "control.h"
#include "player.h"
#include "Ghost.h"
#include "grid.h"
#include "TitleScreen.h"

extern "C" void DbgPrintf(const char *fmt, ...);

// boot logs go to logcat AND the on-screen overlay (emulator debugging)
#define LOG_TAG "revolt-boot"
#define ALOGI(...) do { __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__); DbgPrintf(__VA_ARGS__); } while (0)
#define ALOGE(...) do { __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__); DbgPrintf(__VA_ARGS__); } while (0)

static bool gBooted = false;

extern char *CarInfoFile;   // defined in game_bridge.cpp (was main.cpp)

// ---------------------------------------------------------------------------
// race config from the Java frontend — must be set before Revolt_Boot
// ---------------------------------------------------------------------------

static char gCfgLevelDir[MAX_LEVEL_DIR_NAME] = "NHOOD1";
static int  gCfgCarId = 0;
static int  gCfgGameType = GAMETYPE_TRIAL;
static int  gCfgReversed = FALSE;
static int  gCfgMirrored = FALSE;
static int  gCfgNumCPUs = 7;   // opponents in GAMETYPE_SINGLE — retail-style 8-car grid (grid holds 12)
static int  gCfgNumLaps = 3;   // race length; the 1999 build hardcoded 5

extern "C" void Revolt_Configure(const char *levelDir, int carId, int gameType,
                                 int reversed, int mirrored, int numCpuCars,
                                 int numLaps)
{
    gCfgNumCPUs = numCpuCars;
    if (gCfgNumCPUs < 1) gCfgNumCPUs = 1;
    if (gCfgNumCPUs > MAX_NUM_PLAYERS - 1) gCfgNumCPUs = MAX_NUM_PLAYERS - 1;
    gCfgNumLaps = numLaps;
    if (gCfgNumLaps < 1) gCfgNumLaps = 1;
    if (gCfgNumLaps > 20) gCfgNumLaps = 20;
    if (levelDir && levelDir[0]) {
        strncpy(gCfgLevelDir, levelDir, sizeof(gCfgLevelDir) - 1);
        gCfgLevelDir[sizeof(gCfgLevelDir) - 1] = '\0';
        // FindLevels stores dir names uppercased
        for (char *p = gCfgLevelDir; *p; p++)
            if (*p >= 'a' && *p <= 'z') *p -= 'a' - 'A';
    }
    gCfgCarId    = carId;
    gCfgGameType = (gameType == GAMETYPE_SINGLE) ? GAMETYPE_SINGLE : GAMETYPE_TRIAL;
    gCfgReversed = reversed ? TRUE : FALSE;
    gCfgMirrored = mirrored ? TRUE : FALSE;
    ALOGI("configure: level '%s' car %d type %d rev %d mir %d cpus %d laps %d",
          gCfgLevelDir, carId, gCfgGameType, gCfgReversed, gCfgMirrored, gCfgNumCPUs, gCfgNumLaps);
}

// ---------------------------------------------------------------------------
// boot — WinMain prologue, adapted
// ---------------------------------------------------------------------------

extern "C" int Revolt_Boot(const char *dataDir)
{
    LARGE_INTEGER time;

    if (gBooted) return TRUE;

    // game data directory becomes the working dir (all game paths are relative)
    if (dataDir && chdir(dataDir) != 0) {
        ALOGE("can't chdir to %s", dataDir);
        return FALSE;
    }
    ALOGI("game data dir: %s", dataDir ? dataDir : "(unchanged)");

    // settings (config-file defaults for now) + frontend choices
    GetRegistrySettings();
    memcpy(RegistrySettings.LevelDir, gCfgLevelDir, MAX_LEVEL_DIR_NAME);
    RegistrySettings.CarID = gCfgCarId;

    // find levels
    FindLevels();
    GameSettings.Level = GetLevelNum(RegistrySettings.LevelDir);
    if (GameSettings.Level == -1)
        GameSettings.Level = 0;
    ALOGI("levels found, starting level num %ld", GameSettings.Level);

    // read car info
    if (!ReadAllCarInfo(CarInfoFile)) {
        ALOGE("ReadAllCarInfo failed");
        return FALSE;
    }
    SetAllCarCoMs();
    ALOGI("car info loaded: %ld car types", (long)NCarTypes);

    // create tpage mem
    if (!CreateTPages(TPAGE_NUM)) {
        ALOGE("CreateTPages failed");
        return FALSE;
    }

    // timer freq + rand seed
    QueryPerformanceFrequency(&time);
    TimerFreq = time.LowPart;
    srand(CurrentTimer());

    // draw devices + input + "DirectDraw" + sound
    GetDrawDevices();
    InitInput(nullptr);
    if (!InitDD()) {
        QuitGame = TRUE;
        return FALSE;
    }
    InitSound();

    // set start event: straight into a race, as configured by the frontend
    GameSettings.GameType = gCfgGameType;
    GameSettings.CarID = RegistrySettings.CarID;
    GameSettings.Mirrored = gCfgMirrored;
    GameSettings.Reversed = gCfgReversed;
    GameSettings.AutoBrake = RegistrySettings.AutoBrake;
    GameSettings.Paws = FALSE;
    GameSettings.NumberOfLaps = gCfgNumLaps;

    Event = SetupGame;

    gBooted = true;
    ALOGI("boot complete — entering game");
    return TRUE;
}

// one iteration of the main loop (called per Android frame)
extern "C" int Revolt_Frame(void)
{
    if (!gBooted || QuitGame) return FALSE;
    if (Event) Event();
    return !QuitGame;
}

// ---------------------------------------------------------------------------
// Go / GoFront / SetupGame — ported from main.cpp
// ---------------------------------------------------------------------------

void Go(void)
{
    Event = GoFront;
}

void GoFront(void)
{
    if (!InitD3D(DrawDevices[RegistrySettings.DrawDevice].DisplayMode[DisplayModeCount].Width,
                 DrawDevices[RegistrySettings.DrawDevice].DisplayMode[DisplayModeCount].Height,
                 DrawDevices[RegistrySettings.DrawDevice].DisplayMode[DisplayModeCount].Bpp, 0)) {
        QuitGame = TRUE;
        return;
    }

    GetTextureFormat(RegistrySettings.TextureBpp);
    InitTextures();

    SetupDxState();

    RenderSettings.GeomPers = BaseGeomPers;
    SetNearFar(48.0f, 4096.0f);
    SetViewport(0, 0, (float)ScreenXsize, (float)ScreenYsize, RenderSettings.GeomPers);

    LoadMipTexture((char *)"gfx\\font1.bmp", TPAGE_FONT, 256, 256, 0, 1);
    LoadBitmap((char *)"gfx\\title.bmp", &TitleHbm);

    MenuCount = 0;
    Event = MainMenu;
}

void SetupGame(void)
{
    ALOGI("SetupGame: level %ld ('%s'), car %ld",
          GameSettings.Level, LevelInf[GameSettings.Level].Dir, (long)GameSettings.CarID);

    // kill title bitmap + textures
    FreeBitmap(TitleHbm);
    TitleHbm = 0;
    FreeTextures();

    // init D3D (EGL context already exists — stubs succeed)
    if (!InitD3D(DrawDevices[RegistrySettings.DrawDevice].DisplayMode[DisplayModeCount].Width,
                 DrawDevices[RegistrySettings.DrawDevice].DisplayMode[DisplayModeCount].Height,
                 DrawDevices[RegistrySettings.DrawDevice].DisplayMode[DisplayModeCount].Bpp, 0)) {
        QuitGame = TRUE;
        return;
    }

    GetTextureFormat(RegistrySettings.TextureBpp);
    InitTextures();

    SetupDxState();

    // set geom vars (menu flow normally did this in GoFront)
    RenderSettings.GeomPers = BaseGeomPers;
    SetNearFar(48.0f, 4096.0f);
    SetViewport(0, 0, (float)ScreenXsize, (float)ScreenYsize, RenderSettings.GeomPers);

    // pick texture sets
    if (GameSettings.GameType == GAMETYPE_TRIAL)
        PickTextureSets(2);
    else
        PickTextureSets(MAX_NUM_PLAYERS);

    // initialise object grid system
    if (!GRD_AllocGrids()) {
        QuitGame = TRUE;
    }

    // init level
    LEV_InitLevel();
    ALOGI("level loaded");

    // init players
    if (GameSettings.GameType == GAMETYPE_TRIAL) {
        InitPlayersTrial();
    } else if (GameSettings.GameType == GAMETYPE_SINGLE) {
        InitPlayersSingle();
    } else {
        InitPlayersNetwork();
    }

    // set camera to follow car
    SetCameraFollow(CAM_MainCamera, PLR_LocalPlayer->ownobj, CAM_FOLLOW_BEHIND);

    // go game loop
    TotalRaceTime = 0;
    TimeQueue = 0;
    UpdateTimeFactor();
    Event = GLP_GameLoop;
    ALOGI("entering GLP_GameLoop");
}

// ---------------------------------------------------------------------------
// InitPlayers* — ported verbatim from main.cpp
// ---------------------------------------------------------------------------

void InitPlayersTrial(void)
{
    int playerCarID;
    MAT mat;
    VEC pos;

    if (GameSettings.CarID < (DWORD)NCarTypes)
        playerCarID = GameSettings.CarID;
    else
        playerCarID = 0;

    if (CurrentJoystick != -1) PLR_LocalCtrlType = CTRL_TYPE_JOY;
    else PLR_LocalCtrlType = CTRL_TYPE_KBD;

    GetCarGrid(0, &pos, &mat);

    PLR_LocalPlayer = PLR_CreatePlayer(PLAYER_LOCAL, PLR_LocalCtrlType, playerCarID, &pos, &mat);
    if (PLR_LocalPlayer == NULL) {
        Box(NULL, (char *)"Can't create local player!", MB_OK | MB_ICONERROR);
        QuitGame = TRUE;
        return;
    }

    GHO_GhostExists = LoadGhostData(&LevelInf[GameSettings.Level]);

    if (GHO_GhostExists)
        playerCarID = GHO_BestGhostInfo->CarID;
    else
        playerCarID = 0;

    GetCarGrid(0, &pos, &mat);
    GHO_GhostPlayer = PLR_CreatePlayer(PLAYER_GHOST, CTRL_TYPE_NONE, playerCarID, &pos, &mat);
    if (GHO_GhostPlayer == NULL) {
        Box(NULL, (char *)"Can't create ghost player!", MB_OK | MB_ICONERROR);
        QuitGame = TRUE;
        return;
    }

    if (!GHO_GhostExists)
        ClearBestGhostData();

    InitGhostData(PLR_LocalPlayer);
    InitBestGhostData();
    InitGhostLight();

    CountdownEndTime = CurrentTimer() + MS2TIME(COUNTDOWN_START);
    CountdownTime = TRUE;
}

void InitPlayersSingle(void)
{
    int playerCarID;
    MAT mat;
    VEC pos;

    if (GameSettings.CarID < (DWORD)NCarTypes)
        playerCarID = GameSettings.CarID;
    else
        playerCarID = 0;

    if (CurrentJoystick != -1) PLR_LocalCtrlType = CTRL_TYPE_JOY;
    else PLR_LocalCtrlType = CTRL_TYPE_KBD;

    GetCarGrid(0, &pos, &mat);
    PLR_LocalPlayer = PLR_CreatePlayer(PLAYER_LOCAL, CTRL_TYPE_KBD, playerCarID, &pos, &mat);
    if (PLR_LocalPlayer == NULL) {
        Box(NULL, (char *)"Can't create local player!", MB_OK | MB_ICONERROR);
        QuitGame = TRUE;
        return;
    }

    // ANDROID_PORT: full grid of CPU opponents (main.cpp spawned exactly one),
    // each with a different selectable car, walking away from the player's pick
    {
        extern int NextValidCarID(int currentID);
        int i, cpuCar = playerCarID;

        for (i = 1; i <= gCfgNumCPUs; i++) {
            cpuCar = NextValidCarID(cpuCar);
            GetCarGrid(i, &pos, &mat);
            if (!PLR_CreatePlayer(PLAYER_CPU, CTRL_TYPE_CPU, cpuCar, &pos, &mat)) {
                Box(NULL, (char *)"Can't create computer player!", MB_OK | MB_ICONERROR);
                break;   // keep whatever opponents we managed to create
            }
        }
        ALOGI("race grid: player + %d CPU cars", (int)(NumPlayers - 1));
    }

    GHO_GhostExists = FALSE;
    GHO_GhostPlayer = NULL;

    CountdownEndTime = CurrentTimer() + MS2TIME(COUNTDOWN_START);
    CountdownTime = TRUE;
}

void InitPlayersNetwork(void)
{
    int i;
    PLAYER *player;
    MAT mat;
    VEC pos;
    char buf[256];

    if (CurrentJoystick != -1) PLR_LocalCtrlType = CTRL_TYPE_JOY;
    else PLR_LocalCtrlType = CTRL_TYPE_KBD;

    for (i = 0; i < StartData.PlayerNum; i++) {
        GetCarGrid(StartData.PlayerData[i].GridNum, &pos, &mat);

        if (StartData.PlayerData[i].PlayerID == LocalPlayerID)
            PLR_LocalPlayer = player = PLR_CreatePlayer(PLAYER_LOCAL, PLR_LocalCtrlType, StartData.PlayerData[i].CarID, &pos, &mat);
        else
            player = PLR_CreatePlayer(PLAYER_REMOTE, CTRL_TYPE_NONE, StartData.PlayerData[i].CarID, &pos, &mat);

        if (!player) {
            wsprintf(buf, "Can't create player %s", StartData.PlayerData[i].Name);
            Box(NULL, buf, MB_OK);
            QuitGame = TRUE;
            return;
        }

        player->PlayerID = StartData.PlayerData[i].PlayerID;
        strncpy(player->PlayerName, StartData.PlayerData[i].Name, MAX_PLAYER_NAME);
    }

    GHO_GhostExists = FALSE;
    GHO_GhostPlayer = NULL;

    CountdownEndTime = CurrentTimer() + MS2TIME(COUNTDOWN_START);
    CountdownTime = TRUE;
}

void InitPlayeresFullArray(void)
{
    // dev helper from main.cpp — not used by the Android boot path
}
