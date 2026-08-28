// dplobby.h — DirectPlay lobby shim (Re-Volt Android port).
// Networking (play.cpp) is dropped in Phase 0-4 and rebuilt on sockets later;
// these are header-level placeholders only.
#pragma once
#include "windows.h"

typedef DWORD DPID, *LPDPID;

// opaque COM interfaces — forward-declared so `IDirectPlay4A *x` also works
struct IDirectPlay;        struct IDirectPlay2;
struct IDirectPlay3;       struct IDirectPlay4;
struct IDirectPlay2A;      struct IDirectPlay3A;
struct IDirectPlay4A;      struct IDirectPlayLobby;
struct IDirectPlayLobby2;  struct IDirectPlayLobby2A;
struct IDirectPlayLobby3;  struct IDirectPlayLobby3A;

typedef struct IDirectPlay         *LPDIRECTPLAY;
typedef struct IDirectPlay2        *LPDIRECTPLAY2;
typedef struct IDirectPlay3        *LPDIRECTPLAY3;
typedef struct IDirectPlay4        *LPDIRECTPLAY4;
typedef struct IDirectPlay2A       *LPDIRECTPLAY2A;
typedef struct IDirectPlay3A       *LPDIRECTPLAY3A;
typedef struct IDirectPlay4A       *LPDIRECTPLAY4A;
typedef struct IDirectPlayLobby    *LPDIRECTPLAYLOBBY;
typedef struct IDirectPlayLobby2   *LPDIRECTPLAYLOBBY2;
typedef struct IDirectPlayLobby2A  *LPDIRECTPLAYLOBBY2A;
typedef struct IDirectPlayLobby3   *LPDIRECTPLAYLOBBY3;
typedef struct IDirectPlayLobby3A  *LPDIRECTPLAYLOBBY3A;

typedef struct DPCAPS {
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwMaxBufferSize;
    DWORD dwMaxQueueSize;
    DWORD dwMaxPlayers;
    DWORD dwHundredBaud;
    DWORD dwLatency;
    DWORD dwMaxLocalPlayers;
    DWORD dwHeaderLength;
    DWORD dwTimeout;
} DPCAPS, *LPDPCAPS;

typedef struct DPSESSIONDESC2 {
    DWORD  dwSize;
    DWORD  dwFlags;
    GUID   guidInstance;
    GUID   guidApplication;
    DWORD  dwMaxPlayers;
    DWORD  dwCurrentPlayers;
    union { LPSTR lpszSessionName; LPSTR lpszSessionNameA; };
    union { LPSTR lpszPassword;    LPSTR lpszPasswordA; };
    DWORD_PTR dwReserved1, dwReserved2;
    DWORD_PTR dwUser1, dwUser2, dwUser3, dwUser4;
} DPSESSIONDESC2, *LPDPSESSIONDESC2;

typedef struct DPNAME {
    DWORD dwSize;
    DWORD dwFlags;
    union { LPSTR lpszShortName; LPSTR lpszShortNameA; };
    union { LPSTR lpszLongName;  LPSTR lpszLongNameA; };
} DPNAME, *LPDPNAME;

typedef const DPNAME         *LPCDPNAME;
typedef const DPSESSIONDESC2 *LPCDPSESSIONDESC2;

#define DPID_SYSMSG        0
#define DPID_ALLPLAYERS    0
#define DP_OK              S_OK
#define DPSYS_CREATEPLAYERORGROUP 0x0003
#define DPSYS_DESTROYPLAYERORGROUP 0x0005
#define DPSYS_HOST         0x0101
#define DPSYS_SESSIONLOST  0x0031
