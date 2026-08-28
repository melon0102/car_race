// windows.h — Win32 shim for the Re-Volt Android port.
// Provides just enough of the Win32 surface for the game core to compile
// under NDK clang. Grows incrementally: when a game file fails to build on a
// missing type/constant, add it HERE (never patch the game source for this).
#pragma once

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <ctype.h>
#include <limits.h>
#include <float.h>
#include <stddef.h>

// ---------------------------------------------------------------- MSVC-isms
#define __int64        long long
#define __cdecl
#define __stdcall
#define __fastcall
#define __forceinline  inline
#define _inline        inline
#define __inline       inline

#define CALLBACK
#define WINAPI
#define APIENTRY
#define PASCAL
#define FAR
#define NEAR
#define CONST const
#define VOID  void

// CRT name differences
#define stricmp   strcasecmp
#define strnicmp  strncasecmp
#define _stricmp  strcasecmp
#define _strnicmp strncasecmp

#ifndef min
#define min(a, b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef max
#define max(a, b) (((a) > (b)) ? (a) : (b))
#endif

// ---------------------------------------------------------------- base types
// NOTE: Win32 LONG/DWORD are 32-bit. On arm64 a bare C 'long' is 64-bit —
// the game's own use of 'long' in file-format structs is a separate porting
// task (Phase 1); these typedefs at least keep the Win32 API surface correct.
typedef int32_t        LONG;
typedef uint32_t       ULONG;
typedef uint32_t       DWORD;
typedef uint16_t       WORD;
typedef uint8_t        BYTE;
typedef int            BOOL;
typedef int            INT;
typedef unsigned int   UINT;
typedef int16_t        SHORT;
typedef uint16_t       USHORT;
typedef char           CHAR;
typedef unsigned char  UCHAR;
typedef float          FLOAT;
typedef int64_t        LONGLONG;
typedef uint64_t       ULONGLONG;

typedef char          *LPSTR, *PSTR;
typedef const char    *LPCSTR, *PCSTR;
typedef void          *LPVOID, *PVOID;
typedef const void    *LPCVOID;
typedef BYTE          *LPBYTE, *PBYTE;
typedef WORD          *LPWORD;
typedef DWORD         *LPDWORD;
typedef LONG          *LPLONG, *PLONG;
typedef BOOL          *LPBOOL;

typedef intptr_t       LONG_PTR, LRESULT, LPARAM;
typedef uintptr_t      ULONG_PTR, WPARAM, DWORD_PTR;

typedef LONG           HRESULT;

// Handles: opaque pointers on Android. HBITMAP is integer-based because the
// game casts bitmap handles to BOOL (a hard error for pointers on LP64).
typedef void *HANDLE, *HWND, *HINSTANCE, *HMODULE, *HDC, *HKEY,
    *HMENU, *HICON, *HCURSOR, *HGDIOBJ, *HPALETTE, *HBRUSH, *HFONT, *HRGN,
    *HGLOBAL, *HRSRC, *HACCEL, *HMONITOR;
typedef intptr_t HBITMAP;
typedef HKEY *PHKEY;
typedef HICON HCURSOR_t;

#ifndef TRUE
#define TRUE  1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#define MAX_PATH 260
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)

// ---------------------------------------------------------------- HRESULTs
#define S_OK           ((HRESULT)0)
#define E_FAIL         ((HRESULT)0x80004005L)
#define SUCCEEDED(hr)  ((HRESULT)(hr) >= 0)
#define FAILED(hr)     ((HRESULT)(hr) < 0)

// ---------------------------------------------------------------- GUID
typedef struct _GUID {
    DWORD Data1;
    WORD  Data2;
    WORD  Data3;
    BYTE  Data4[8];
} GUID, *LPGUID;
typedef const GUID *LPCGUID;
typedef const GUID &REFGUID;
typedef const GUID &REFIID;
typedef const GUID &REFCLSID;
typedef GUID IID, CLSID;
#define DEFINE_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
    static const GUID name = {l, w1, w2, {b1, b2, b3, b4, b5, b6, b7, b8}}
#define IsEqualGUID(a, b) (memcmp(&(a), &(b), sizeof(GUID)) == 0)
typedef GUID IID_dummy_tag;  // (REFIID defined above as const GUID&)

// ---------------------------------------------------------------- structs
typedef union _LARGE_INTEGER {
    struct {
        DWORD LowPart;
        LONG  HighPart;
    };
    LONGLONG QuadPart;
} LARGE_INTEGER, *PLARGE_INTEGER;

typedef struct tagPOINT {
    LONG x, y;
} POINT, *LPPOINT;

typedef struct tagRECT {
    LONG left, top, right, bottom;
} RECT, *LPRECT;

typedef struct tagSIZE {
    LONG cx, cy;
} SIZE, *LPSIZE;

typedef struct tagPALETTEENTRY {
    BYTE peRed, peGreen, peBlue, peFlags;
} PALETTEENTRY, *LPPALETTEENTRY;

typedef struct tagRGBQUAD {
    BYTE rgbBlue, rgbGreen, rgbRed, rgbReserved;
} RGBQUAD;

#pragma pack(push, 2)
typedef struct tagBITMAPFILEHEADER {
    WORD  bfType;
    DWORD bfSize;
    WORD  bfReserved1, bfReserved2;
    DWORD bfOffBits;
} BITMAPFILEHEADER;
#pragma pack(pop)

typedef struct tagBITMAPINFOHEADER {
    DWORD biSize;
    LONG  biWidth, biHeight;
    WORD  biPlanes, biBitCount;
    DWORD biCompression, biSizeImage;
    LONG  biXPelsPerMeter, biYPelsPerMeter;
    DWORD biClrUsed, biClrImportant;
} BITMAPINFOHEADER;

typedef struct tagBITMAPINFO {
    BITMAPINFOHEADER bmiHeader;
    RGBQUAD          bmiColors[1];
} BITMAPINFO, *LPBITMAPINFO;

typedef struct tagMSG {
    HWND   hwnd;
    UINT   message;
    WPARAM wParam;
    LPARAM lParam;
    DWORD  time;
    POINT  pt;
} MSG, *LPMSG;

typedef LRESULT (*WNDPROC)(HWND, UINT, WPARAM, LPARAM);

typedef struct tagWNDCLASS {
    UINT      style;
    WNDPROC   lpfnWndProc;
    int       cbClsExtra, cbWndExtra;
    HINSTANCE hInstance;
    HICON     hIcon;
    HCURSOR   hCursor;
    HBRUSH    hbrBackground;
    LPCSTR    lpszMenuName, lpszClassName;
} WNDCLASS;

// ---------------------------------------------------------------- macros
#define LOWORD(l)        ((WORD)((DWORD_PTR)(l) & 0xffff))
#define HIWORD(l)        ((WORD)(((DWORD_PTR)(l) >> 16) & 0xffff))
#define LOBYTE(w)        ((BYTE)((DWORD_PTR)(w) & 0xff))
#define HIBYTE(w)        ((BYTE)(((DWORD_PTR)(w) >> 8) & 0xff))
#define MAKELONG(a, b)   ((LONG)(((WORD)(a)) | ((DWORD)((WORD)(b))) << 16))
#define MAKEWORD(a, b)   ((WORD)(((BYTE)(a)) | ((WORD)((BYTE)(b))) << 8))
#define RGB(r, g, b)     ((DWORD)(((BYTE)(r)) | ((WORD)((BYTE)(g)) << 8) | (((DWORD)(BYTE)(b)) << 16)))

// overlap-safe: game code sprintf's a buffer into itself (MSVC's wsprintf
// tolerated that; libc sprintf is UB and garbles the text)
#ifdef __cplusplus
extern "C" int rv_wsprintf(char *dst, const char *fmt, ...);
#else
int rv_wsprintf(char *dst, const char *fmt, ...);
#endif
#define wsprintf  rv_wsprintf
#define wvsprintf vsprintf
#define lstrlen   strlen
#define lstrcpy   strcpy
#define lstrcat   strcat
#define lstrcmpi  strcasecmp
#define ZeroMemory(p, n)      memset((p), 0, (n))
#define CopyMemory(d, s, n)   memcpy((d), (s), (n))
#define FillMemory(p, n, v)   memset((p), (v), (n))

// ---------------------------------------------------------------- constants
// window messages (only ones the game references; extend as needed)
#define WM_CREATE        0x0001
#define WM_DESTROY       0x0002
#define WM_MOVE          0x0003
#define WM_SIZE          0x0005
#define WM_ACTIVATE      0x0006
#define WM_SETFOCUS      0x0007
#define WM_KILLFOCUS     0x0008
#define WM_PAINT         0x000F
#define WM_CLOSE         0x0010
#define WM_QUIT          0x0012
#define WM_ACTIVATEAPP   0x001C
#define WM_KEYDOWN       0x0100
#define WM_KEYUP         0x0101
#define WM_CHAR          0x0102
#define WM_SYSKEYDOWN    0x0104
#define WM_SYSKEYUP      0x0105
#define WM_COMMAND       0x0111
#define WM_SYSCOMMAND    0x0112

#define MB_OK               0x0000
#define MB_OKCANCEL         0x0001
#define MB_YESNO            0x0004
#define MB_ICONERROR        0x0010
#define MB_ICONSTOP         0x0010
#define MB_ICONHAND         0x0010
#define MB_ICONEXCLAMATION  0x0030
#define MB_ICONWARNING      0x0030
#define MB_ICONINFORMATION  0x0040
#define MB_DEFBUTTON1       0x0000
#define MB_DEFBUTTON2       0x0100
#define MB_ICONQUESTION     0x0020
#define IDOK     1
#define IDCANCEL 2
#define IDYES    6
#define IDNO     7

#define VK_ESCAPE  0x1B
#define VK_SPACE   0x20
#define VK_LEFT    0x25
#define VK_UP      0x26
#define VK_RIGHT   0x27
#define VK_DOWN    0x28
#define VK_RETURN  0x0D
#define VK_SHIFT   0x10
#define VK_CONTROL 0x11
#define VK_TAB     0x09
#define VK_F1      0x70
#define VK_F2      0x71
#define VK_F3      0x72
#define VK_F4      0x73
#define VK_F5      0x74
#define VK_F6      0x75
#define VK_F7      0x76
#define VK_F8      0x77
#define VK_F9      0x78
#define VK_F10     0x79
#define VK_F11     0x7A
#define VK_F12     0x7B
#define VK_SCROLL  0x91

#define HIGH_PRIORITY_CLASS   0x00000080
#define NORMAL_PRIORITY_CLASS 0x00000020
#define IDLE_PRIORITY_CLASS   0x00000040

// registry (registry.cpp is replaced, but headers may reference these)
#define HKEY_CLASSES_ROOT   ((HKEY)(uintptr_t)0x80000000)
#define HKEY_CURRENT_USER   ((HKEY)(uintptr_t)0x80000001)
#define HKEY_LOCAL_MACHINE  ((HKEY)(uintptr_t)0x80000002)
#define ERROR_SUCCESS       0L
#define KEY_ALL_ACCESS      0xF003F
#define REG_SZ              1
#define REG_BINARY          3
#define REG_DWORD           4
#define REG_OPTION_NON_VOLATILE 0
typedef DWORD REGSAM;

// ---------------------------------------------------------------- functions
// implemented in platform/winshim_impl.cpp
#ifdef __cplusplus
extern "C" {
#endif

// fopen wrapper: converts Windows-style paths ("gfx\\font1.bmp") to POSIX
// and falls back to a lowercase name (game data is staged all-lowercase;
// Android filesystems are case-sensitive).
FILE *rv_fopen(const char *path, const char *mode);
#ifndef RV_NO_FOPEN_REDIRECT
#define fopen rv_fopen
#endif

// GDI (graceful-fail stubs: bitmap loading is replaced by the GL texture
// loader; these exist so legacy paths compile and no-op at runtime)
typedef struct tagBITMAP {
    LONG   bmType;
    LONG   bmWidth, bmHeight;
    LONG   bmWidthBytes;
    WORD   bmPlanes, bmBitsPixel;
    LPVOID bmBits;
} BITMAP;
#define IMAGE_BITMAP        0
#define LR_LOADFROMFILE     0x0010
#define LR_CREATEDIBSECTION 0x2000
#define SRCCOPY             0x00CC0020
#define BI_RGB              0
typedef DWORD COLORREF;
#define TRANSPARENT 1
#define OPAQUE      2
#define GetRValue(rgb) ((BYTE)(rgb))
#define GetGValue(rgb) ((BYTE)(((WORD)(rgb)) >> 8))
#define GetBValue(rgb) ((BYTE)((rgb) >> 16))
UINT GetDIBColorTable(HDC dc, UINT start, UINT count, RGBQUAD *out);
COLORREF SetBkColor(HDC dc, COLORREF color);
COLORREF SetTextColor(HDC dc, COLORREF color);
BOOL     TextOut(HDC dc, int x, int y, LPCSTR text, int len);
HANDLE LoadImage(HINSTANCE inst, LPCSTR name, UINT type, int cx, int cy, UINT flags);
BOOL   DeleteObject(HBITMAP obj);
int    GetObject(HBITMAP obj, int len, LPVOID out);
HDC    CreateCompatibleDC(HDC dc);
HBITMAP SelectObject(HDC dc, HBITMAP obj);
BOOL   StretchBlt(HDC dst, int x, int y, int w, int h, HDC src,
                  int sx, int sy, int sw, int sh, DWORD rop);
BOOL   DeleteDC(HDC dc);
#define LoadImageA LoadImage

// file enumeration (maps to opendir/readdir)
#define FILE_ATTRIBUTE_DIRECTORY 0x00000010
#define FILE_ATTRIBUTE_NORMAL    0x00000080
typedef struct _WIN32_FIND_DATAA {
    DWORD dwFileAttributes;
    DWORD ftCreationTime[2];
    DWORD ftLastAccessTime[2];
    DWORD ftLastWriteTime[2];
    DWORD nFileSizeHigh;
    DWORD nFileSizeLow;
    DWORD dwReserved0, dwReserved1;
    CHAR  cFileName[MAX_PATH];
    CHAR  cAlternateFileName[14];
} WIN32_FIND_DATAA, WIN32_FIND_DATA, *LPWIN32_FIND_DATA;
HANDLE FindFirstFile(LPCSTR pattern, LPWIN32_FIND_DATA data);
BOOL   FindNextFile(HANDLE find, LPWIN32_FIND_DATA data);
BOOL   FindClose(HANDLE find);
#define FindFirstFileA FindFirstFile
#define FindNextFileA FindNextFile

// local heap (maps to malloc/free)
typedef HANDLE HLOCAL;
#define LMEM_FIXED    0x0000
#define LMEM_ZEROINIT 0x0040
#define LPTR          (LMEM_FIXED | LMEM_ZEROINIT)
HLOCAL LocalAlloc(UINT flags, size_t bytes);
HLOCAL LocalFree(HLOCAL mem);

BOOL  QueryPerformanceCounter(LARGE_INTEGER *count);
BOOL  QueryPerformanceFrequency(LARGE_INTEGER *freq);
DWORD GetTickCount(void);
void  Sleep(DWORD ms);
int   MessageBox(HWND hwnd, LPCSTR text, LPCSTR caption, UINT type);
#define MessageBoxA MessageBox
void  OutputDebugString(LPCSTR s);
#define OutputDebugStringA OutputDebugString
SHORT GetAsyncKeyState(int vKey);
SHORT GetKeyState(int vKey);
BOOL  SetPriorityClass(HANDLE process, DWORD priorityClass);
HANDLE GetCurrentProcess(void);
DWORD GetCurrentDirectory(DWORD len, LPSTR buf);
BOOL  SetCurrentDirectory(LPCSTR path);
void  PostQuitMessage(int code);
char *_strupr(char *s);
char *_strlwr(char *s);
char *itoa(int value, char *str, int base);
#define _itoa itoa

LONG RegCreateKeyEx(HKEY key, LPCSTR sub, DWORD res, LPSTR cls, DWORD opts,
                    REGSAM sam, void *sec, PHKEY out, LPDWORD disp);
LONG RegOpenKeyEx(HKEY key, LPCSTR sub, DWORD opts, REGSAM sam, PHKEY out);
LONG RegCloseKey(HKEY key);
LONG RegSetValueEx(HKEY key, LPCSTR name, DWORD res, DWORD type,
                   const BYTE *data, DWORD len);
LONG RegQueryValueEx(HKEY key, LPCSTR name, LPDWORD res, LPDWORD type,
                     LPBYTE data, LPDWORD len);
LONG RegDeleteKey(HKEY key, LPCSTR sub);

#ifdef __cplusplus
}
#endif

// ---------------------------------------------------------------------------
// 64-bit port: the 1999 codebase assumes `long` is 32-bit EVERYWHERE — file
// format structs, fread(&x, sizeof(long)), pointer-free math. On LP64
// Android (arm64) a bare long is 64-bit, which silently corrupts every
// binary loader. Rather than hand-editing thousands of declarations, game
// translation units keep the original contract via this define.
//
// Platform TUs that include system headers AFTER this header (EGL, GLES,
// AAudio, unistd, ...) must `#define RV_NO_LONG32` before including it, and
// must only exchange fixed-width types with game code (they do: DWORD,
// int32_t, float, pointers). This block sits at the END of the header so
// none of the declarations above are affected.
// ---------------------------------------------------------------------------
#if defined(__LP64__) && !defined(RV_NO_LONG32)
#define long int
#endif
