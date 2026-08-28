// dsound.h — DirectSound shim (Re-Volt Android port).
// sfx.cpp is REPLACED by an Oboe-based mixer; only header-level types here.
#pragma once
#include "windows.h"
#include "mmsystem.h"

typedef void *LPDIRECTSOUND, *LPDIRECTSOUNDBUFFER, *LPDIRECTSOUND3DBUFFER,
    *LPDIRECTSOUND3DLISTENER, *LPDIRECTSOUNDNOTIFY;

typedef struct _DSBUFFERDESC {
    DWORD           dwSize;
    DWORD           dwFlags;
    DWORD           dwBufferBytes;
    DWORD           dwReserved;
    LPWAVEFORMATEX  lpwfxFormat;
} DSBUFFERDESC, *LPDSBUFFERDESC;

#define DSBPLAY_LOOPING       0x00000001
#define DSBCAPS_CTRL3D        0x00000010
#define DSBCAPS_CTRLFREQUENCY 0x00000020
#define DSBCAPS_CTRLPAN       0x00000040
#define DSBCAPS_CTRLVOLUME    0x00000080
#define DSBCAPS_STATIC        0x00000002
#define DSBSTATUS_PLAYING     0x00000001
#define DSBVOLUME_MAX         0
#define DSBVOLUME_MIN         (-10000)
