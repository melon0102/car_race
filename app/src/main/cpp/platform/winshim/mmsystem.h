// mmsystem.h — Win32 multimedia shim (Re-Volt Android port).
#pragma once
#include "windows.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef UINT MMRESULT;
#define TIMERR_NOERROR 0

DWORD timeGetTime(void);                 // implemented in winshim_impl.cpp
MMRESULT timeBeginPeriod(UINT period);   // no-op on Android
MMRESULT timeEndPeriod(UINT period);     // no-op on Android

// wave format (referenced by sound headers)
#pragma pack(push, 1)
typedef struct tWAVEFORMATEX {
    WORD  wFormatTag;
    WORD  nChannels;
    DWORD nSamplesPerSec;
    DWORD nAvgBytesPerSec;
    WORD  nBlockAlign;
    WORD  wBitsPerSample;
    WORD  cbSize;
} WAVEFORMATEX, *LPWAVEFORMATEX;

typedef struct waveformat_tag {
    WORD  wFormatTag;
    WORD  nChannels;
    DWORD nSamplesPerSec;
    DWORD nAvgBytesPerSec;
    WORD  nBlockAlign;
} WAVEFORMAT;

typedef struct pcmwaveformat_tag {
    WAVEFORMAT wf;
    WORD       wBitsPerSample;
} PCMWAVEFORMAT;
#pragma pack(pop)

#define WAVE_FORMAT_PCM 1

#ifdef __cplusplus
}
#endif
