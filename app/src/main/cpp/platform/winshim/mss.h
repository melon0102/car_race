// mss.h — Miles Sound System shim (Re-Volt Android port).
// Replaces the real (proprietary, non-portable) MSS.H. Covers exactly the
// API surface the game uses (29 calls). Currently backed by no-op stubs in
// platform/mss_stub.cpp; Phase 3 reimplements them on Oboe.
#pragma once
#include "windows.h"
#include "mmsystem.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------- types
typedef int32_t  S32;
typedef uint32_t U32;
typedef float    F32;

typedef struct _DIG_DRIVER DIG_DRIVER;
typedef struct _SAMPLE     *HSAMPLE;
typedef struct _STREAM     *HSTREAM;
typedef struct _REDBOOK    *HREDBOOK;
typedef struct _DIG_DRIVER *HDIGDRIVER;
typedef struct _TIMER      *HTIMER;

// ---------------------------------------------------------------- constants
// sample status (AIL_sample_status)
#define SMP_FREE                0x0001
#define SMP_DONE                0x0002
#define SMP_PLAYING             0x0004
#define SMP_STOPPED             0x0008
#define SMP_PLAYINGBUTRELEASED  0x0010

// stream status mirrors sample status in this shim

// preferences (AIL_set_preference) — ids only matter internally
#define DIG_MIXER_CHANNELS      1
#define DIG_DEFAULT_VOLUME      2
#define DIG_RESAMPLING_TOLERANCE 3
#define DIG_USE_STEREO          4
#define DIG_USE_16_BITS         5
#define DIG_HARDWARE_SAMPLE_RATE 6
#define MDI_SERVICE_RATE        7
#define DIG_USE_WAVEOUT         8
#define AIL_QUIET               0
#ifndef YES
#define YES 1
#endif
#ifndef NO
#define NO 0
#endif
#define FILE_READ_WITH_SIZE     ((void *)(intptr_t)-1)

#define WAVE_MAPPER             ((U32)-1)

// redbook (CD audio) status
#define REDBOOK_PLAYING         1
#define REDBOOK_PAUSED          2
#define REDBOOK_STOPPED         3
#define REDBOOK_ERROR           4

// ---------------------------------------------------------------- system
S32  AIL_startup(void);
void AIL_shutdown(void);
void AIL_set_preference(U32 number, S32 value);
S32  AIL_waveOutOpen(HDIGDRIVER *drvr, void *reserved, S32 wDeviceID,
                     WAVEFORMAT *lpFormat);

// ---------------------------------------------------------------- samples
HSAMPLE AIL_allocate_sample_handle(HDIGDRIVER dig);
void AIL_release_sample_handle(HSAMPLE S);
void AIL_init_sample(HSAMPLE S);
S32  AIL_set_sample_file(HSAMPLE S, const void *file_image, S32 block);
void AIL_set_sample_loop_count(HSAMPLE S, S32 loops);
void AIL_set_sample_pan(HSAMPLE S, S32 pan);          // 0..127, 64 = centre
void AIL_set_sample_playback_rate(HSAMPLE S, S32 hz);
void AIL_set_sample_volume(HSAMPLE S, S32 volume);    // 0..127
void AIL_start_sample(HSAMPLE S);
void AIL_stop_sample(HSAMPLE S);
void AIL_resume_sample(HSAMPLE S);
void AIL_end_sample(HSAMPLE S);
U32  AIL_sample_status(HSAMPLE S);

// ---------------------------------------------------------------- streams
HSTREAM AIL_open_stream(HDIGDRIVER dig, const char *filename, S32 stream_mem);
void AIL_close_stream(HSTREAM stream);
void AIL_start_stream(HSTREAM stream);
void AIL_set_stream_loop_count(HSTREAM stream, S32 count);
void AIL_set_stream_volume(HSTREAM stream, S32 volume);
void AIL_set_stream_playback_rate(HSTREAM stream, S32 rate);

// ---------------------------------------------------------------- redbook
HREDBOOK AIL_redbook_open(U32 which);
void AIL_redbook_play(HREDBOOK hand, U32 track);
void AIL_redbook_track_info(HREDBOOK hand, U32 track,
                            U32 *start_msec, U32 *end_msec);

// ---------------------------------------------------------------- file utils
U32   AIL_file_size(const char *filename);
void *AIL_file_read(const char *filename, void *dest);
void  AIL_mem_free_lock(void *ptr);

#ifdef __cplusplus
}
#endif
