
#include "revolt.h"
#include "panel.h"
#include "ctrlread.h"
#include "object.h"
#include "player.h"
#include "timing.h"
#include "geom.h"
#include "trigger.h"
#include "text.h"
#include "input.h"
#include "camera.h"
#include "model.h"
#include "main.h"
#include "posnode.h"	// ANDROID_PORT: PosTotalDist for rival distances

// globals

#if USE_DEBUG_ROUTINES
extern VEC DEBUG_DownForce;
#endif


long SpeedUnits = SPEED_MPH;

static long TrackDirType;
static long TrackDirCount = 0;
static long DisplaySplitTime;
static float DisplaySplitCount;
static long RevLit[REV_LIT_MAX];
static long WrongWayFlag;
static float WrongWayTimer;

char *SpeedUnitText[SPEED_NTYPES] = {  // ANDROID_PORT: was 'static' — panel.h declares it extern (clang rejects the mismatch MSVC allowed)
	"mph",
	"fpm",
	"kph"
};
REAL SpeedUnitScale[SPEED_NTYPES] = {  // ANDROID_PORT: was 'static' — see above
	OGU2MPH_SPEED,
	OGU2FPM_SPEED,
	OGU2KPH_SPEED
};

static MAP_INFO MapInfo[] = {
	{"MARKET1", 24.0f, 400.0f, 0.01f, 0.01f, 0.0f, 128.0f, 64.0f, 64.0f},

	{""},
};

// track dir UV's

static float TrackDirUV[] = {
	128, 0, -64, 64,	// chicane left
	64, 0, -64, 64,		// 180 left
	128, 64, -64, 62,	// 90 left
	256, 0, -64, 64,	// 45 left
	64, 0, 64, 64,		// chicane right
	0, 0, 64, 64,		// 180 right
	64, 64, 64, 62,		// 90 right
	192, 0, 64, 64,		// 45 right
	0, 64, 64, 62,		// ahead
	128, 0, 64, 64,		// danger
	192, 64, 64, 62,	// fork
};

// rev light UV's

static float RevsUV[] = {
	143, 128, 15, 14,
	119, 128, 16, 16,
	95, 128, 19, 18,
	71, 128, 19, 19,
	47, 128, 21, 21,
	23, 128, 22, 22,
	0, 128, 22, 24,
};

// rev light positions

static float RevsPositions[] = {
	492, 451,
	502, 435,
	517, 422,
	536, 412,
	558, 405,
	581, 400,
	606, 399,
};

// pickup UV's

static float PickupUV[] = {
	0, 224,		// shockwave
	64, 224,	// firework
	96, 224,	// firework pack
	32, 224,	// putty bomb
	128, 224,	// water bomb
	224, 224,	// electro pulse
	192, 224,	// oil slick
	0, 192,		// chrome ball
	32, 192,	// turbo
};

///////////////////
// split trigger //
///////////////////

void TriggerSplit(PLAYER *player, long flag, long n, VEC *vec)
{
	CAR	*car;

// ignore if wrong car

	if (player != PLR_LocalPlayer)
		return;

	car = &player->car;

// special end of lap split?

	if (n == -1)
	{
		DisplaySplitCount = SPLIT_COUNT;
		DisplaySplitTime = car->CurrentLapTime - TrackRecords.RecordLap[0].Time;
		car->NextSplit++;
	}

// ignore if gay number

	if (n >= MAX_SPLIT_TIMES)
		return;

// ignore if wrong split

	if (n != car->NextSplit)
		return;

// set car split time

	car->SplitTime[n] = car->CurrentLapTime;

// display split time

	DisplaySplitCount = SPLIT_COUNT;
	DisplaySplitTime = car->SplitTime[n] - TrackRecords.SplitTime[n];
	car->NextSplit++;
}

///////////////////////
// trigger track dir //
///////////////////////

// ANDROID_PORT: rewritten to the retail logic (Xbox/Src/panel.cpp). The level
// data packs each trigger's Flag as (sequence << 16 | arrow type); the 1999
// code used the whole packed value as the UV table index, so with retail .tri
// files the lookup landed way out of bounds and no arrow ever appeared. The
// sequence gate (car->NextTrackDir, reset per lap in timing.cpp) makes the
// arrows fire in track order every lap; type 11 means "advance, no arrow".

void TriggerTrackDir(PLAYER *player, long flag, long n, VEC *vec)
{
	CAR *car = &player->car;

// fuck off everyone else

	if (player != PLR_LocalPlayer)
		return;

// ignore if wrong track dir

	if ((n >> 16) != car->NextTrackDir)
		return;

// set track dir flags

	if ((n & 0xffff) < 11)
	{
		TrackDirType = (n & 0xffff);
		TrackDirCount = TRACK_DIR_COUNT;
	}

// inc next track dir count

	car->NextTrackDir++;
}

////////////////////////////////////////////////////////////////
//
// ANDROID_PORT: retail 3D countdown (rvsource/Xbox/Src/panel.cpp) — the
// red/yellow "3, 2, 1, GO!" digit models (models/go*.m, untextured
// env-shaded), drawn camera-relative with the retail spin flourish and
// slide-in/out. Replaces the 1999 white DumpBigText digit. Adaptations:
// no AllPlayersReady flag in this tree (countdown starting is enough),
// and the models load per level like the other static models.
//
////////////////////////////////////////////////////////////////

static MODEL CountdownModels[4];
static VEC CountdownOffset = {0, -32.0f, 256.0f};

void LoadCountdownModels(void)
{
	long i;

	i  = LoadModel((char *)"models\\go3.m", &CountdownModels[0], -1, 1, LOADMODEL_FORCE_TPAGE, 100);
	i += LoadModel((char *)"models\\go2.m", &CountdownModels[1], -1, 1, LOADMODEL_FORCE_TPAGE, 100);
	i += LoadModel((char *)"models\\go1.m", &CountdownModels[2], -1, 1, LOADMODEL_FORCE_TPAGE, 100);
	i += LoadModel((char *)"models\\gogo.m", &CountdownModels[3], -1, 1, LOADMODEL_FORCE_TPAGE, 100);

	if (i < 4)
	{
		Box(NULL, (char *)"Can't load countdown models!", MB_OK);
	}
}

void FreeCountdownModels(void)
{
	FreeModel(&CountdownModels[0], 1);
	FreeModel(&CountdownModels[1], 1);
	FreeModel(&CountdownModels[2], 1);
	FreeModel(&CountdownModels[3], 1);
}

void DrawCountdown(void)
{
	long count, spin;
	REAL f;
	MAT wmat;
	VEC wpos;

// skip if not ready

	if ((!CountdownTime && TotalRaceTime > 1000) || CountdownTime >= 3000)
		return;

// get counter

	if (CountdownTime) count = CountdownTime + 1000;
	else count = 1000 - TotalRaceTime;

// set mat + pos

	if (count > 3500 || count < 500) spin = 500;
	else spin = count % 1000;

	if (spin < 100) f = (float)(spin - 100) / 400.0f;
	else if (spin > 900) f = (float)(spin - 900) / 400.0f;
	else f = 0.0f;

	RotMatrixY(&wmat, f);

	CopyVec(&CountdownOffset, &wpos);
	if (count > 3800) wpos.v[Z] += (3800 - count);
	else if (count < 200) wpos.v[Z] -= (200 - count);

// draw camera-relative: identity view, fixed 640 perspective

	SetViewport(Camera[CameraCount].X, Camera[CameraCount].Y, Camera[CameraCount].Xsize, Camera[CameraCount].Ysize, 640.0f);
	SetCameraView(&Identity, &ZeroVector, 0.0f);

	SetEnvStatic(&wpos, &wmat, 0x404040, 0.0f, 0.0f, 1.0f);
	DrawModel(&CountdownModels[3 - (count / 1000)], &wmat, &wpos, MODEL_ENV);

// restore the game camera's viewport + view

	SetViewport(Camera[CameraCount].X, Camera[CameraCount].Y, Camera[CameraCount].Xsize, Camera[CameraCount].Ysize, BaseGeomPers + Camera[CameraCount].Lens);
	SetCameraView(&Camera[CameraCount].WMatrix, &Camera[CameraCount].WPos, Camera[CameraCount].Shake);
}

/////////////////////
// revs pos editor //
/////////////////////

static void EditRevsPos(void)
{
	long i;
	char buf[128];
	static long edit = 0;

	if (Keys[DIK_MINUS] && !LastKeys[DIK_MINUS] && edit > 0) edit--;
	if (Keys[DIK_EQUALS] && !LastKeys[DIK_EQUALS] && edit < 6) edit++;

	if (Keys[DIK_LEFT] && !LastKeys[DIK_LEFT]) RevsPositions[edit * 2]--;
	if (Keys[DIK_RIGHT] && !LastKeys[DIK_RIGHT]) RevsPositions[edit * 2]++;

	if (Keys[DIK_UP] && !LastKeys[DIK_UP]) RevsPositions[edit * 2 + 1]--;
	if (Keys[DIK_DOWN] && !LastKeys[DIK_DOWN]) RevsPositions[edit * 2 + 1]++;

	SET_TPAGE(TPAGE_FONT);

	for (i = 0 ; i < 7 ; i++)
	{
		wsprintf(buf, "%d = %d, %d", i, (long)RevsPositions[i * 2], (long)RevsPositions[i * 2 + 1]);
		DumpText(0, i * 32, 24, 32, 0xffffffff, buf);
	}

	SET_TPAGE(TPAGE_FX2);
}

////////////////////////
// draw control panel //
////////////////////////

void DrawControlPanel(void)
{
	float x, y, xsize, ysize, tu, tv, twidth, theight, *p, frevs, dist;
	long i, col, speed, revs, revdest, revper, revadd, pickup, pickup2;
	char buf[128];
	PLAYER *player;

// skip if editing

	if (CAM_MainCamera->Type == CAM_EDIT)
		return;

// set misc render states

	ZBUFFER_OFF();

	ALPHA_ON();
	ALPHA_SRC(D3DBLEND_SRCALPHA);
	ALPHA_DEST(D3DBLEND_INVSRCALPHA);

// fx 1

	SET_TPAGE(TPAGE_FX1);

// position ring

	DrawPanelSprite(16, 368, 96, 96, 1.0f / 256.0f, 129.0f / 256.0f, 62.0f / 256.0f, 62.0f / 256.0f, 0xc0ffffff);
	for (player = PLR_PlayerHead ; player ; player = player->next)
	{
		dist = player->CarAI.FinishDistPanel * RAD;

		x = -(float)sin(dist) * 46.0f + 58.0f;
		y = -(float)cos(dist) * 46.0f + 412.0f;

		tu = 208.0f / 256.0f;
		tv = 216.0f / 256.0f;

		DrawPanelSprite(x, y, 10.0f, 10.0f, tu, tv, 8.0f / 256.0f, 8.0f / 256.0f, 0xc0ffffff);
	}

// fx 2

	SET_TPAGE(TPAGE_FX2);

// wrong way?

	if (WrongWayFlag != PLR_LocalPlayer->CarAI.WrongWay)
		WrongWayTimer += TimeStep;
	else
		WrongWayTimer = 0.0f;

	if (WrongWayTimer >= WRONG_WAY_TOLERANCE) WrongWayFlag = !WrongWayFlag;

	if (WrongWayFlag && TIME2MS(TimerCurrent) & 256)
	{
		DrawPanelSprite(256, 0, 128, 128, 128.0f / 256.0f, 64.0f / 256.0f, 64.0f / 256.0f, 64.0f / 256.0f, 0xc0ffffff);
	}

// track dir?

	if (TrackDirCount)
	{
		if (!WrongWayFlag)
		{
			p = &TrackDirUV[TrackDirType * 4];
			tu = *p++ / 256.0f;
			tv = *p++ / 256.0f;
			twidth = *p++ / 256.0f;
			theight = *p++ / 256.0f;

			if (TrackDirCount > TRACK_DIR_COUNT - TRACK_DIR_FADE_COUNT) col = (TRACK_DIR_COUNT - TrackDirCount) << 25;
			else if (TrackDirCount < TRACK_DIR_FADE_COUNT) col = (TrackDirCount) << 25;
			else col = 255 << 24;
			col |= 0xffffff;

			DrawPanelSprite(256, 0, 128, 128, tu, tv, twidth, theight, col);
		}

		if ((TrackDirCount -= (long)(TimeFactor * 10)) < 0) TrackDirCount = 0;
	}

// revs

	frevs = abs(PLR_LocalPlayer->car.Revs / PLR_LocalPlayer->car.MaxRevs);
	FTOL(frevs, revs);
	FTOL((frevs - (float)revs) * REV_LIT_MAX, revper);
	FTOL(TimeStep * 1024, revadd);

	for (i = 0 ; i < REV_LIT_NUM ; i++)
	{
		if (revs > i) revdest = REV_LIT_MAX;
		else if (revs < i) revdest = 0;
		else revdest = revper;

		if (RevLit[i] < revdest)
		{
			RevLit[i] += revadd * 4;
			if (RevLit[i] > REV_LIT_MAX) RevLit[i] = REV_LIT_MAX;
		}
		else if (RevLit[i] > revdest)
		{
			RevLit[i] -= revadd;
			if (RevLit[i] < 0) RevLit[i] = 0;
		}
	}

	p = RevsUV;

	for (i = 0 ; i < REV_LIT_NUM ; i++)
	{
		tu = *p++ / 256.0f;
		tv = *p++ / 256.0f;
		twidth = *p++ / 256.0f;
		theight = *p++ / 256.0f;

		x = RevsPositions[i * 2];
		y = RevsPositions[i * 2 + 1];

		xsize = twidth * 256;
		ysize = theight * 256;

		if (!RevLit[i])
			DrawPanelSprite(x, y, xsize, ysize, tu, tv, twidth, theight, 0xc0ffffff);
		else if (RevLit[i] == REV_LIT_MAX)
			DrawPanelSprite(x, y, xsize, ysize, tu, tv + (25.0f / 256.0f), twidth, theight, 0xc0ffffff);
		else
		{
			col = RevLit[i] * 0xc0 / REV_LIT_MAX;
			DrawPanelSprite(x, y, xsize, ysize, tu, tv, twidth, theight, ((0xc0 - col) << 24) | 0xffffff);
			DrawPanelSprite(x, y, xsize, ysize, tu, tv + (25.0f / 256.0f), twidth, theight, (col << 24) | 0xffffff);
		}
	}

// pickup
// ANDROID_PORT: the whole pickup cluster (box + icon + count) sits 114 lower
// than the 1999 layout (56 -> 170): mid-left instead of the top corner, where
// it doubles as the touch fire button within thumb reach (touch_overlay.cpp's
// fire zone covers this position)

	DrawPanelSprite(4, 170, 64, 64, 190.0f / 256.0f, 157.0f / 256.0f, 64.0f / 256.0f, 64.0f / 256.0f, 0xe0ffffff);

	if (PLR_LocalPlayer->PickupCycleSpeed)
	{
		pickup = (long)PLR_LocalPlayer->PickupCycleType;
		pickup2 = (pickup + 1) % PICKUP_NUM;

		FTOL((PLR_LocalPlayer->PickupCycleType - (float)pickup) * 255.0f, col);
		col |= (col << 8) | (col << 16);
	}
	else if (PLR_LocalPlayer->PickupNum)
	{
		pickup = PLR_LocalPlayer->PickupType;
		col = 0;
	}
	else
	{
		pickup = -1;
	}

	if (pickup != -1)
	{
		ALPHA_SRC(D3DBLEND_ONE);
		ALPHA_DEST(D3DBLEND_ONE);

		tu = (PickupUV[pickup * 2]) / 256.0f;
		tv = (PickupUV[pickup * 2 + 1]) / 256.0f;
		DrawPanelSprite(20, 186, 32, 32, tu, tv, 32.0f / 256.0f, 32.0f / 256.0f, 0xffffff - col);

		if (col)
		{
			tu = (PickupUV[pickup2 * 2]) / 256.0f;
			tv = (PickupUV[pickup2 * 2 + 1]) / 256.0f;
			DrawPanelSprite(20, 186, 32, 32, tu, tv, 32.0f / 256.0f, 32.0f / 256.0f, col);
		}

		ALPHA_SRC(D3DBLEND_SRCALPHA);
		ALPHA_DEST(D3DBLEND_INVSRCALPHA);
	}

// countdown: drawn as the retail 3D digit models by DrawCountdown()
// (gameloop) — the 1999 white DumpBigText digit is gone (ANDROID_PORT)

// normal font

	SET_TPAGE(TPAGE_FONT);

// pickup count
// ANDROID_PORT: the 1999 build drew the pickup icon but never its count, so a
// 3-shot firework pack looked identical to a single rocket. Ported from the
// retail panel (rvsource/Xbox/Src/panel.cpp): same position, size and yellow
// for the multi-shot pickups. Retail additionally hides it in trial/practice/
// training; kept visible in every mode here since time trial is a normal way
// to play this port.

	if (PLR_LocalPlayer->PickupNum &&
	    (PLR_LocalPlayer->PickupType == PICKUP_FIREWORKPACK ||
	     PLR_LocalPlayer->PickupType == PICKUP_WATERBOMB))
	{
		wsprintf(buf, "%d", (long)PLR_LocalPlayer->PickupNum);
		DumpText(50, 222, 6, 8, 0xc0ffff00, buf);
	}

// split time?

	if (DisplaySplitCount)
	{
		DisplaySplitCount -= TimeStep;
		if (DisplaySplitCount < 0) DisplaySplitCount = 0;

		DumpText(290, 128, 12, 16, 0xc000ffff, "split");
		if (DisplaySplitTime > 0) wsprintf(buf, "+%02d:%02d:%03d", MINUTES(DisplaySplitTime), SECONDS(DisplaySplitTime), THOUSANDTHS(DisplaySplitTime));
		else wsprintf(buf, "-%02d:%02d:%03d", MINUTES(-DisplaySplitTime), SECONDS(-DisplaySplitTime), THOUSANDTHS(-DisplaySplitTime));
		DumpText(260, 144, 12, 16, 0xc0ffffff, buf);
	}

// lap time + race time (time-trial layout only — the race HUD matches
// retail: lap counter + best lap on the right, position on the left)

	if (GameSettings.GameType == GAMETYPE_TRIAL)
	{
		DumpText(588, 96, 12, 16, 0xc000ffff, "lap");
		wsprintf(buf, "%02d:%02d:%03d", MINUTES(PLR_LocalPlayer->car.CurrentLapTime), SECONDS(PLR_LocalPlayer->car.CurrentLapTime), THOUSANDTHS(PLR_LocalPlayer->car.CurrentLapTime));
		DumpText(516, 112, 12, 16, 0xc0ffffff, buf);

		DumpText(576, 136, 12, 16, 0xc000ffff, "race");
		wsprintf(buf, "%02d:%02d:%03d", MINUTES(TotalRaceTime), SECONDS(TotalRaceTime), THOUSANDTHS(TotalRaceTime));
		DumpText(516, 152, 12, 16, 0xc0ffffff, buf);
	}

// speed

	DumpText(588, 448, 12, 16, 0xc000ffff, SpeedUnitText[SpeedUnits]);
	speed = (long)(SpeedUnitScale[SpeedUnits] * VecLen(&PLR_LocalPlayer->car.Body->Centre.Vel));
	wsprintf(buf, "%4d", speed);
	DumpText(528, 448, 12, 16, 0xc0ffffff, buf);

// best acc
//	wsprintf(buf, "0-15: %3dms", (PLR_LocalPlayer->car.Best0to15 > ZERO)? (int)(1000 * PLR_LocalPlayer->car.Best0to15): 0);
//	DumpText(100, 50, 12, 16, 0xc0ffffff, buf);
//	wsprintf(buf, "0-25: %3dms", (PLR_LocalPlayer->car.Best0to25 > ZERO)? (int)(1000 * PLR_LocalPlayer->car.Best0to25): 0);
//	DumpText(100, 70, 12, 16, 0xc0ffffff, buf);
//	wsprintf(buf, "ValidNode: %d (%d)", PLR_LocalPlayer->ValidRailCamNode, (PLR_LocalPlayer->ValidRailCamNode == -1)? -1: CAM_CameraNode[PLR_LocalPlayer->ValidRailCamNode].ID);
//	DumpText(100, 50, 12, 16, 0xffff0000, buf);
//	wsprintf(buf, "BangMag: %d", (int)(100 * PLR_LocalPlayer->car.Body->BangMag));
//	DumpText(100, 50, 12, 16, 0xffff0000, buf);
//	wsprintf(buf, "Down: %8d %8d %8d (%8d)", (int)(100 * DEBUG_DownForce.v[X]), (int)(100 * DEBUG_DownForce.v[Y]), (int)(100 * DEBUG_DownForce.v[Z]), (int)(100 * PLR_LocalPlayer->car.DownForceMod));
//	DumpText(100, 50, 8, 12, 0xffff0000, buf);

// laps
// ANDROID_PORT: retail behavior — "N/M" while racing, "finished" after the
// last lap (the 1999 code kept counting: "4/3"). Race mode puts the counter
// top-right like the reference layout (position owns the top-left); time
// trial keeps it top-left and shows just the running lap number

	if (GameSettings.GameType == GAMETYPE_TRIAL)
	{
		DumpText(16, 16, 12, 16, 0xc000ffff, "lap");
		wsprintf(buf, "%d", PLR_LocalPlayer->car.Laps + 1);
		DumpText(16, 32, 12, 16, 0xc0ffffff, buf);
	}
	else
	{
		DumpText(588, 16, 12, 16, 0xc000ffff, "lap");
		if (PLR_LocalPlayer->car.Laps < GameSettings.NumberOfLaps)
			wsprintf(buf, "%d/%d", PLR_LocalPlayer->car.Laps + 1, GameSettings.NumberOfLaps);
		else
			wsprintf(buf, "finished");
		DumpText(624 - 12 * (long)strlen(buf), 32, 12, 16, 0xc0ffffff, buf);
	}

// position + race HUD
// ANDROID_PORT: cloned from the retail panel (Xbox/Src/panel.cpp): live
// position ordinal top-left (white number, cyan suffix); nearest rivals
// with signed distances in a bordered box bottom-left (retail drew them at
// 122,396/416); after finishing, the big position sprite pops in at top
// centre (font-page digit sprites, retail's shrink-and-fade), the race
// results table lists the finish order with the local row pulsing alpha
// (retail: sin(t/200)*64+192), and a continue hint shows at the bottom.

	if (GameSettings.GameType != GAMETYPE_TRIAL)
	{
		static const char *sOrdinal[] = {"st", "nd", "rd", "th"};
		static float sPosScale = 32.0f;
		static char sWasFinished = 0;
		PLAYER *ahead = NULL, *behind = NULL;
		float d, localdist, aheadDist = 0.0f, behindDist = 0.0f;
		long pos, n, row, alpha;

		localdist = (float)PLR_LocalPlayer->car.Laps - PLR_LocalPlayer->CarAI.FinishDistPanel
		          - (float)PLR_LocalPlayer->CarAI.BackTracking;

		pos = 1;
		for (player = PLR_PlayerHead ; player ; player = player->next)
		{
			if (player == PLR_LocalPlayer || player->type != PLAYER_CPU)
				continue;
			d = (float)player->car.Laps - player->CarAI.FinishDistPanel
			  - (float)player->CarAI.BackTracking;
			if (d > localdist)
			{
				pos++;
				if (!ahead || d - localdist < aheadDist) { ahead = player; aheadDist = d - localdist; }
			}
			else
			{
				if (!behind || localdist - d < behindDist) { behind = player; behindDist = localdist - d; }
			}
		}
		if (PLR_LocalPlayer->RaceFinishTime)
			pos = PLR_LocalPlayer->RaceFinishPos + 1;

// ordinal top-left

		wsprintf(buf, "%d", pos);
		DumpText(16, 16, 24, 32, 0xc0ffffff, buf);
		DumpText(16 + 26 * (long)strlen(buf), 16, 12, 16, 0xc000ffff,
		         (char *)sOrdinal[(pos >= 1 && pos <= 3) ? pos - 1 : 3]);

// nearest rivals box bottom-left

		if (ahead || behind)
		{
			SET_TPAGE(-1);
			DrawPanelSprite(12, 386, 236, 60, 0, 0, 0, 0, 0xb0181818);
			DrawPanelSprite(12, 386, 236, 2, 0, 0, 0, 0, 0xd04060ff);
			DrawPanelSprite(12, 444, 236, 2, 0, 0, 0, 0, 0xd04060ff);
			DrawPanelSprite(12, 386, 2, 60, 0, 0, 0, 0, 0xd04060ff);
			DrawPanelSprite(246, 386, 2, 60, 0, 0, 0, 0, 0xd04060ff);
			SET_TPAGE(TPAGE_FONT);

			if (ahead)
			{
				n = (long)(aheadDist * PosTotalDist / 200.0f);
				if (n > 999) n = 999;
				wsprintf(buf, "-%dm", n);
				DumpText(96 - 12 * (long)strlen(buf), 392, 12, 16, 0xc000ff40, buf);
				DumpText(100, 392, 12, 16, 0xc0ffffff, CarInfo[ahead->cartype].Name);
			}
			if (behind)
			{
				n = (long)(behindDist * PosTotalDist / 200.0f);
				if (n > 999) n = 999;
				wsprintf(buf, "+%dm", n);
				DumpText(96 - 12 * (long)strlen(buf), 418, 12, 16, 0xc0ffff40, buf);
				DumpText(100, 418, 12, 16, 0xc0ffffff, CarInfo[behind->cartype].Name);
			}
		}

// finished: big position pop + results table + continue hint

		if (PLR_LocalPlayer->RaceFinishTime)
		{
			if (!sWasFinished)
			{
				sWasFinished = 1;
				sPosScale = 300.0f;   // retail LocalRacePosScale start
			}

			sPosScale -= TimeStep * 500.0f;
			if (sPosScale < 32.0f) sPosScale = 32.0f;

			if (sPosScale < 300.0f - 256.0f) col = 0xff000000 | 0xffffff;
			else col = ((long)(300.0f - sPosScale) << 24) | 0xffffff;

			{
				float xs = sPosScale, ys = sPosScale * 2.0f;
				float xx = 320.0f - xs * 0.5f, yy = 36.0f;

				if (pos >= 10)
				{
					xx -= xs * 0.5f;
					DrawPanelSprite(xx, yy, xs, ys,
					                ((float)(pos / 10) * 32.0f + 1.0f) / 256.0f, 129.0f / 256.0f,
					                30.0f / 256.0f, 62.0f / 256.0f, col);
					xx += xs;
				}
				DrawPanelSprite(xx, yy, xs, ys,
				                (((pos % 10) % 8) * 32.0f + 1.0f) / 256.0f,
				                (((pos % 10) / 8) * 64.0f + 129.0f) / 256.0f,
				                30.0f / 256.0f, 62.0f / 256.0f, col);
				xx += xs;
				DumpText(xx - 2.0f, yy + ys * 0.08f, xs * 0.4f, xs * 0.55f, col,
				         (char *)sOrdinal[(pos >= 1 && pos <= 3) ? pos - 1 : 3]);
			}

// results table (box + title + blinking local row)

			row = 0;
			for (n = 0 ; n < MAX_NUM_PLAYERS ; n++)
				if (FinishTable[n].Time) row++;

			SET_TPAGE(-1);
			DrawPanelSprite(187, 150, 266, 48.0f + 20.0f * row, 0, 0, 0, 0, 0xb0181818);
			DrawPanelSprite(187, 150, 266, 2, 0, 0, 0, 0, 0xd04060ff);
			DrawPanelSprite(187, 196.0f + 20.0f * row, 266, 2, 0, 0, 0, 0, 0xd04060ff);
			DrawPanelSprite(187, 150, 2, 48.0f + 20.0f * row, 0, 0, 0, 0, 0xd04060ff);
			DrawPanelSprite(451, 150, 2, 48.0f + 20.0f * row, 0, 0, 0, 0, 0xd04060ff);
			SET_TPAGE(TPAGE_FONT);

			DumpText(248, 158, 12, 16, 0xc000ffff, "race results");

			row = 0;
			for (n = 0 ; n < MAX_NUM_PLAYERS ; n++)
			{
				char name[12];

				if (!FinishTable[n].Time)
					continue;
				player = FinishTable[n].Player;

				if (player == PLR_LocalPlayer)
					alpha = (long)(sin((float)TIME2MS(TimerCurrent) / 200.0f) * 64.0f + 192.0f) << 24;
				else
					alpha = 0xe0000000;

				wsprintf(buf, "%02d", n + 1);
				DumpText(196, 190 + 20 * row, 12, 16, alpha | 0xffffff, buf);
				memcpy(name, (player == PLR_LocalPlayer) ? "player" : CarInfo[player->cartype].Name, 11);
				name[(player == PLR_LocalPlayer) ? 6 : 11] = 0;
				DumpText(228, 190 + 20 * row, 12, 16, alpha | 0x00ffff, name);
				wsprintf(buf, "%02d:%02d:%03d", MINUTES(FinishTable[n].Time), SECONDS(FinishTable[n].Time), THOUSANDTHS(FinishTable[n].Time));
				DumpText(340, 190 + 20 * row, 12, 16, alpha | 0xffffff, buf);
				row++;
			}

			DumpText(188, 452, 12, 16, 0xc0ff8000, "press back to continue");

			AllPlayersFinished = TRUE;
			for (player = PLR_PlayerHead ; player ; player = player->next)
				if (player->type == PLAYER_LOCAL || player->type == PLAYER_CPU)
					if (!player->RaceFinishTime)
						AllPlayersFinished = FALSE;
		}
		else
		{
			sWasFinished = 0;
		}
	}

// last lap (time-trial layout only — in race mode the lap counter owns
// this corner, matching the reference: Lap + Best Lap)

	if (GameSettings.GameType == GAMETYPE_TRIAL)
	{
		DumpText(528, 16, 12, 16, 0xc000ffff, "last lap");
		wsprintf(buf, "%02d:%02d:%03d", MINUTES(PLR_LocalPlayer->car.LastLapTime), SECONDS(PLR_LocalPlayer->car.LastLapTime), THOUSANDTHS(PLR_LocalPlayer->car.LastLapTime));
		DumpText(516, 32, 12, 16, 0xc0ffffff, buf);
	}

// best lap

	DumpText(528, 56, 12, 16, 0xc000ffff, "best lap");
	wsprintf(buf, "%02d:%02d:%03d", MINUTES(PLR_LocalPlayer->car.BestLapTime), SECONDS(PLR_LocalPlayer->car.BestLapTime), THOUSANDTHS(PLR_LocalPlayer->car.BestLapTime));
	DumpText(516, 72, 12, 16, 0xc0ffffff, buf);

// bump test

return;
	VEC vec;
	static VEC light = {0, 0, -128};
	static VEC tl = {-64, -64, 0};
	static VEC tr = {64, -64, 0};
	static VEC br = {64, 64, 0};
	static VEC bl = {-64, 64, 0};
	static float tu0, tv0, tu1, tv1, tu2, tv2, tu3, tv3;

	if (Keys[DIK_LEFT]) light.v[X]--;
	if (Keys[DIK_RIGHT]) light.v[X]++;
	if (Keys[DIK_UP]) light.v[Y]--;
	if (Keys[DIK_DOWN]) light.v[Y]++;

	SubVector(&tl, &light, &vec);
	NormalizeVector(&vec);
	tu0 = 0.0f / 256.0f - vec.v[X] / 64.0f;
	tv0 = 0.0f / 256.0f - vec.v[Y] / 64.0f;

	SubVector(&tr, &light, &vec);
	NormalizeVector(&vec);
	tu1 = 64.0f / 256.0f - vec.v[X] / 64.0f;
	tv1 = 0.0f / 256.0f - vec.v[Y] / 64.0f;

	SubVector(&br, &light, &vec);
	NormalizeVector(&vec);
	tu2 = 64.0f / 256.0f - vec.v[X] / 64.0f;
	tv2 = 64.0f / 256.0f - vec.v[Y] / 64.0f;

	SubVector(&bl, &light, &vec);
	NormalizeVector(&vec);
	tu3 = 0.0f / 256.0f - vec.v[X] / 64.0f;
	tv3 = 64.0f / 256.0f - vec.v[Y] / 64.0f;

	DrawVertsTEX2[0].sx = 128;
	DrawVertsTEX2[0].sy = 128;
	DrawVertsTEX2[0].sz = 0.01f;
	DrawVertsTEX2[0].rhw = 1;
	DrawVertsTEX2[0].color = 0x808080;
	DrawVertsTEX2[0].tu = 0.0f / 256.0f;
	DrawVertsTEX2[0].tv = 0.0f / 256.0f;

	DrawVertsTEX2[1].sx = 256;
	DrawVertsTEX2[1].sy = 128;
	DrawVertsTEX2[1].sz = 0.01f;
	DrawVertsTEX2[1].rhw = 1;
	DrawVertsTEX2[1].color = 0x808080;
	DrawVertsTEX2[1].tu = 64.0f / 256.0f;
	DrawVertsTEX2[1].tv = 0.0f / 256.0f;

	DrawVertsTEX2[2].sx = 256;
	DrawVertsTEX2[2].sy = 256;
	DrawVertsTEX2[2].sz = 0.01f;
	DrawVertsTEX2[2].rhw = 1;
	DrawVertsTEX2[2].color = 0x808080;
	DrawVertsTEX2[2].tu = 64.0f / 256.0f;
	DrawVertsTEX2[2].tv = 64.0f / 256.0f;

	DrawVertsTEX2[3].sx = 128;
	DrawVertsTEX2[3].sy = 256;
	DrawVertsTEX2[3].sz = 0.01f;
	DrawVertsTEX2[3].rhw = 1;
	DrawVertsTEX2[3].color = 0x808080;
	DrawVertsTEX2[3].tu = 0.0f / 256.0f;
	DrawVertsTEX2[3].tv = 64.0f / 256.0f;

	DrawVertsTEX2[0].tu2 = tu0;
	DrawVertsTEX2[0].tv2 = tv0;

	DrawVertsTEX2[1].tu2 = tu1;
	DrawVertsTEX2[1].tv2 = tv1;

	DrawVertsTEX2[2].tu2 = tu2;
	DrawVertsTEX2[2].tv2 = tv2;

	DrawVertsTEX2[3].tu2 = tu3;
	DrawVertsTEX2[3].tv2 = tv3;

	ALPHA_OFF();
	SET_TPAGE(TPAGE_FX1);
	SET_TPAGE2(TPAGE_FX1);

	SET_STAGE_STATE(1, D3DTSS_TEXCOORDINDEX, 1);
	SET_STAGE_STATE(1, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	SET_STAGE_STATE(1, D3DTSS_COLORARG2, D3DTA_CURRENT);
	SET_STAGE_STATE(1, D3DTSS_COLOROP, D3DTOP_SUBTRACT);

	D3Ddevice->DrawPrimitive(D3DPT_TRIANGLEFAN, FVF_TEX2, DrawVertsTEX2, 4, D3DDP_DONOTUPDATEEXTENTS);

	SET_STAGE_STATE(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
}

/////////////////////////////////
// draw a control panel sprite //
/////////////////////////////////

void DrawPanelSprite(float x, float y, float width, float height, float tu, float tv, float twidth, float theight, long rgba)
{
	long i;
	float xstart, ystart, xsize, ysize;

// scale

	xstart = x * RenderSettings.GeomScaleX + ScreenLeftClip;
	ystart = y * RenderSettings.GeomScaleY + ScreenTopClip;

	xsize = width * RenderSettings.GeomScaleX;
	ysize = height * RenderSettings.GeomScaleY;

// init vert misc

	for (i = 0 ; i < 4 ; i++)
	{
		DrawVertsTEX1[i].color = rgba;
		DrawVertsTEX1[i].rhw = 1;
	}

// set screen coors

	DrawVertsTEX1[0].sx = DrawVertsTEX1[3].sx = xstart;
	DrawVertsTEX1[1].sx = DrawVertsTEX1[2].sx = xstart + xsize;
	DrawVertsTEX1[0].sy = DrawVertsTEX1[1].sy = ystart;
	DrawVertsTEX1[2].sy = DrawVertsTEX1[3].sy = ystart + ysize;

// set uv's

	DrawVertsTEX1[0].tu = DrawVertsTEX1[3].tu = tu;
	DrawVertsTEX1[1].tu = DrawVertsTEX1[2].tu = tu + twidth;
	DrawVertsTEX1[0].tv = DrawVertsTEX1[1].tv = tv;
	DrawVertsTEX1[2].tv = DrawVertsTEX1[3].tv = tv + theight;

// draw

	D3Ddevice->DrawPrimitive(D3DPT_TRIANGLEFAN, FVF_TEX1, DrawVertsTEX1, 4, D3DDP_DONOTUPDATEEXTENTS | D3DDP_DONOTCLIP);
}
