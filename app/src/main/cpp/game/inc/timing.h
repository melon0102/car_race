
#ifndef TIMING_H
#define TIMING_H

#include "revolt.h"
#include "main.h"
#include "level.h"

// macros

#define FIXED_TIME_STEP FALSE

#define MAKE_TIME(_m, _s, _t) \
	((_m) * 60000 + (_s) * 1000 + (_t))

#ifdef _PC
 #define TIME2MS(_t) \
 	(_t) / (TimerFreq / 1000)

 #define MS2TIME(_t) \
 	(_t) * (TimerFreq / 1000)

 #define RECORDS_FILENAME "times.dat"
 #define RECORDS_FILENAME_MIRRORED "times.tad"
#endif

#ifdef _N64
 #define TIME2MS(_t)  	(_t)
 #define MS2TIME(_t)  	(_t)
#endif

// ANDROID_PORT: the dev build had this stubbed to 1ms (instant start, no
// 3-2-1); restored to the retail value (Xbox/Src/timing.h) — 5s total: the
// grid sweep camera runs first, the big 3-2-1 digits draw once it's < 3000ms
#define COUNTDOWN_START (1000 * 5)

// prototypes

extern unsigned long CurrentTimer(void);
extern void UpdateTimeFactor(void);
extern void UpdateRaceTimers(void);
extern void LoadTrackTimes(LEVELINFO *lev);
extern void SaveTrackTimes(LEVELINFO *lev);
extern void CheckForBestLap(CAR *car);
extern void CheckForBestRace(CAR *car);

// globals

extern unsigned long TimerLast, TimerCurrent, TimerDiff, TimerFreq, TotalRaceTime, TotalRaceStartTime, CountdownTime, CountdownEndTime;
extern long TimeQueue, TimeLoopCount;
extern RECORD_ENTRY TrackRecords;

#endif