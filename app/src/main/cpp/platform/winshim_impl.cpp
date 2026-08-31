// winshim_impl.cpp — implementations for the Win32 shim (Re-Volt Android port).
// Timing maps to clock_gettime, message boxes / debug output to logcat,
// registry to no-ops (settings will live in a config file instead).

#define RV_NO_FOPEN_REDIRECT   // this file implements rv_fopen with real fopen
#define RV_NO_LONG32           // this TU includes system headers (time, dirent, ...)
#include "winshim/windows.h"
#include "winshim/mmsystem.h"
#include "winshim/ddraw.h"

#include <time.h>
#include <unistd.h>
#include <dirent.h>
#include <fnmatch.h>
#include <sys/stat.h>
#include <android/log.h>

#define LOG_TAG "revolt-shim"
#define ALOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

extern "C" {

// ---------------------------------------------------------------- rv_wsprintf

int rv_wsprintf(char *dst, const char *fmt, ...)
{
    char tmp[1024];   // format into a scratch buffer so dst may overlap args
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    strcpy(dst, tmp);
    return n;
}

// ---------------------------------------------------------------- rv_fopen

FILE *rv_fopen(const char *path, const char *mode)
{
    if (!path) return nullptr;

    char fixed[512];
    strncpy(fixed, path, sizeof(fixed) - 1);
    fixed[sizeof(fixed) - 1] = 0;
    for (char *p = fixed; *p; p++) if (*p == '\\') *p = '/';

    FILE *fp = fopen(fixed, mode);
    if (fp) return fp;

    // game data is staged all-lowercase; retry with a lowercase name
    _strlwr(fixed);
    return fopen(fixed, mode);
}

// ---------------------------------------------------------------- timing

static int64_t NowNs(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

// Microsecond resolution, NOT nanoseconds: the game stores QPC's LowPart in
// 32-bit timers (TimerFreq etc.) — at 1ns a 32-bit timer wraps every 4.3s,
// at 1µs every ~71 minutes, matching the PC-era magnitudes it was tuned for.
BOOL QueryPerformanceCounter(LARGE_INTEGER *count)
{
    count->QuadPart = NowNs() / 1000LL;
    return TRUE;
}

BOOL QueryPerformanceFrequency(LARGE_INTEGER *freq)
{
    freq->QuadPart = 1000000LL;
    return TRUE;
}

DWORD GetTickCount(void)
{
    return (DWORD)(NowNs() / 1000000LL);
}

DWORD timeGetTime(void)
{
    return GetTickCount();
}

MMRESULT timeBeginPeriod(UINT) { return TIMERR_NOERROR; }
MMRESULT timeEndPeriod(UINT)   { return TIMERR_NOERROR; }

void Sleep(DWORD ms)
{
    usleep(ms * 1000);
}

// ---------------------------------------------------------------- mini-GDI
// texture.cpp loads BMPs via LoadImage/StretchBlt into surface DCs, so this
// is a REAL (minimal) implementation: BMP parsing (8/24/32-bit, bottom-up),
// bitmaps held as 32-bit ARGB, StretchBlt point-sampled into shim surfaces.

struct GdiBitmap {
    int w, h;
    DWORD *argb;              // 32-bit ARGB rows, top-down
    RGBQUAD palette[256];     // original palette for 8-bit sources
    int paletteSize;
};

struct GdiDC {
    GdiBitmap *bitmap;                 // memory DC with selected bitmap
    IDirectDrawSurface4 *surface;      // DC handed out by surface GetDC
};

// surface GetDC support: give texture.cpp a DC that targets the surface
static GdiDC g_SurfaceDC;   // one at a time is all the game ever uses

HRESULT IDirectDrawSurface4::GetDC(HDC *dc)
{
    if (!dc) return E_FAIL;
    g_SurfaceDC.surface = this;
    g_SurfaceDC.bitmap = nullptr;
    *dc = (HDC)&g_SurfaceDC;
    dirty = TRUE;
    return DD_OK;
}

extern "C" void DbgPrintf(const char *fmt, ...);

HANDLE LoadImage(HINSTANCE, LPCSTR name, UINT, int, int, UINT)
{
    static int logBudget = 10;
    FILE *fp = rv_fopen(name, "rb");   // handles backslashes + case fallback
    if (!fp) {
        if (logBudget > 0) { logBudget--; DbgPrintf("BMP OPEN FAIL %s", name); }
        return nullptr;
    }

    BITMAPFILEHEADER bfh;
    BITMAPINFOHEADER bih;
    if (fread(&bfh, sizeof(bfh), 1, fp) != 1 || bfh.bfType != 0x4D42 ||
        fread(&bih, sizeof(bih), 1, fp) != 1 || bih.biCompression != 0) {
        if (logBudget > 0) { logBudget--; DbgPrintf("BMP HDR FAIL %s", name); }
        fclose(fp);
        return nullptr;
    }

    int w = bih.biWidth;
    int absH = bih.biHeight < 0 ? -bih.biHeight : bih.biHeight;
    bool bottomUp = bih.biHeight > 0;
    int bpp = bih.biBitCount;
    if (w <= 0 || absH <= 0 || (bpp != 8 && bpp != 24 && bpp != 32)) {
        if (logBudget > 0) { logBudget--; DbgPrintf("BMP UNSUPPORTED %s bpp=%d comp=%d", name, bpp, (int)bih.biCompression); }
        fclose(fp);
        return nullptr;
    }

    GdiBitmap *bm = (GdiBitmap *)calloc(1, sizeof(GdiBitmap));
    bm->w = w;
    bm->h = absH;
    bm->argb = (DWORD *)malloc((size_t)w * absH * 4);

    if (bpp == 8) {
        DWORD used = bih.biClrUsed ? bih.biClrUsed : 256;
        if (used > 256) used = 256;
        fseek(fp, sizeof(bfh) + (long)bih.biSize, SEEK_SET);
        fread(bm->palette, sizeof(RGBQUAD), used, fp);
        bm->paletteSize = (int)used;
    }

    fseek(fp, (long)bfh.bfOffBits, SEEK_SET);
    int srcStride = ((w * bpp / 8) + 3) & ~3;   // BMP rows pad to 4 bytes
    BYTE *row = (BYTE *)malloc((size_t)srcStride);

    for (int y = 0; y < absH; y++) {
        if (fread(row, 1, (size_t)srcStride, fp) != (size_t)srcStride) break;
        DWORD *dst = bm->argb + (size_t)(bottomUp ? (absH - 1 - y) : y) * w;
        if (bpp == 8) {
            for (int x = 0; x < w; x++) {
                const RGBQUAD &q = bm->palette[row[x]];
                dst[x] = 0xff000000u | ((DWORD)q.rgbRed << 16) |
                         ((DWORD)q.rgbGreen << 8) | q.rgbBlue;
            }
        } else if (bpp == 24) {
            for (int x = 0; x < w; x++) {
                dst[x] = 0xff000000u | ((DWORD)row[x * 3 + 2] << 16) |
                         ((DWORD)row[x * 3 + 1] << 8) | row[x * 3];
            }
        } else {   // 32
            memcpy(dst, row, (size_t)w * 4);
        }
    }
    free(row);
    fclose(fp);
    return (HANDLE)bm;
}

BOOL DeleteObject(HBITMAP obj)
{
    GdiBitmap *bm = (GdiBitmap *)obj;
    if (bm) {
        free(bm->argb);
        free(bm);
    }
    return TRUE;
}

int GetObject(HBITMAP obj, int, LPVOID out)
{
    GdiBitmap *bm = (GdiBitmap *)obj;
    if (!bm || !out) return 0;
    BITMAP *info = (BITMAP *)out;
    memset(info, 0, sizeof(*info));
    info->bmWidth = bm->w;
    info->bmHeight = bm->h;
    info->bmBitsPixel = 32;
    info->bmBits = bm->argb;
    return sizeof(BITMAP);
}

HDC CreateCompatibleDC(HDC)
{
    return (HDC)calloc(1, sizeof(GdiDC));
}

HBITMAP SelectObject(HDC dc, HBITMAP obj)
{
    GdiDC *d = (GdiDC *)dc;
    if (!d) return 0;
    HBITMAP old = (HBITMAP)d->bitmap;
    d->bitmap = (GdiBitmap *)obj;
    return old;
}

UINT GetDIBColorTable(HDC dc, UINT start, UINT count, RGBQUAD *out)
{
    GdiDC *d = (GdiDC *)dc;
    if (!d || !d->bitmap || !out) return 0;
    GdiBitmap *bm = d->bitmap;
    UINT n = 0;
    for (; n < count && start + n < (UINT)bm->paletteSize; n++)
        out[n] = bm->palette[start + n];
    return n;
}

// point-sampled stretch from a memory DC (bitmap) into a surface DC
BOOL StretchBlt(HDC dstDC, int dx, int dy, int dw, int dh, HDC srcDC,
                int sx, int sy, int sw, int sh, DWORD)
{
    GdiDC *dst = (GdiDC *)dstDC;
    GdiDC *src = (GdiDC *)srcDC;
    if (!dst || !dst->surface || !src || !src->bitmap) return FALSE;
    IDirectDrawSurface4 *surf = dst->surface;
    GdiBitmap *bm = src->bitmap;
    if (!surf->pixels || dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0) return FALSE;

    DWORD bytes = surf->desc.lPitch / (surf->desc.dwWidth ? surf->desc.dwWidth : 1);
    if (bytes != 4) return FALSE;   // surfaces are 32-bit ARGB in this port

    for (int y = 0; y < dh; y++) {
        int ty = dy + y;
        if (ty < 0 || ty >= (int)surf->desc.dwHeight) continue;
        int by = sy + y * sh / dh;
        if (by < 0 || by >= bm->h) continue;
        DWORD *out = (DWORD *)((BYTE *)surf->pixels + (size_t)ty * surf->desc.lPitch);
        const DWORD *in = bm->argb + (size_t)by * bm->w;
        for (int x = 0; x < dw; x++) {
            int tx = dx + x;
            if (tx < 0 || tx >= (int)surf->desc.dwWidth) continue;
            int bx = sx + x * sw / dw;
            if (bx < 0 || bx >= bm->w) continue;
            out[tx] = in[bx];
        }
    }
    surf->dirty = TRUE;
    return TRUE;
}

BOOL DeleteDC(HDC dc)
{
    if (dc && dc != (HDC)&g_SurfaceDC) free(dc);
    return TRUE;
}

COLORREF SetBkColor(HDC, COLORREF) { return 0; }
COLORREF SetTextColor(HDC, COLORREF) { return 0; }
BOOL     TextOut(HDC, int, int, LPCSTR, int) { return FALSE; }

// ---------------------------------------------------------------- find files
// FindFirstFile("levels\\*", ...) style enumeration over opendir/readdir.
// Case-insensitive matching (the game mixes cases freely; Android FS doesn't).

struct FindState {
    DIR *dir;
    char pattern[MAX_PATH];   // filename part, e.g. "*" or "*.inf"
    char dirpath[MAX_PATH];   // directory part
};

static BOOL FindFill(FindState *fs, LPWIN32_FIND_DATA data)
{
    struct dirent *ent;
    while ((ent = readdir(fs->dir)) != nullptr) {
        if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, "..")) continue;
        if (fnmatch(fs->pattern, ent->d_name, FNM_CASEFOLD) != 0) continue;

        memset(data, 0, sizeof(*data));
        strncpy(data->cFileName, ent->d_name, MAX_PATH - 1);

        char full[MAX_PATH * 2];
        snprintf(full, sizeof(full), "%s/%s", fs->dirpath, ent->d_name);
        struct stat st;
        if (stat(full, &st) == 0) {
            if (S_ISDIR(st.st_mode)) data->dwFileAttributes |= FILE_ATTRIBUTE_DIRECTORY;
            data->nFileSizeLow = (DWORD)st.st_size;
        }
        if (!data->dwFileAttributes) data->dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
        return TRUE;
    }
    return FALSE;
}

HANDLE FindFirstFile(LPCSTR pattern, LPWIN32_FIND_DATA data)
{
    FindState *fs = (FindState *)calloc(1, sizeof(FindState));
    if (!fs) return INVALID_HANDLE_VALUE;

    // split "dir\subdir\*.ext" into dir part + wildcard part
    strncpy(fs->dirpath, pattern, MAX_PATH - 1);
    for (char *p = fs->dirpath; *p; p++) if (*p == '\\') *p = '/';
    char *slash = strrchr(fs->dirpath, '/');
    if (slash) {
        strncpy(fs->pattern, slash + 1, MAX_PATH - 1);
        *slash = 0;
    } else {
        strncpy(fs->pattern, fs->dirpath, MAX_PATH - 1);
        strcpy(fs->dirpath, ".");
    }

    // Windows legacy: "*.*" matches EVERYTHING (even names without a dot);
    // fnmatch would require a literal dot — translate to "*"
    if (strcmp(fs->pattern, "*.*") == 0)
        strcpy(fs->pattern, "*");

    fs->dir = opendir(fs->dirpath);
    if (!fs->dir) {
        // ANDROID_PORT: same fallback rv_fopen uses — the game builds these
        // paths from LevelInf[].Dir, which FindLevels stores UPPERCASED
        // ("levels\NHOOD1\*.prm"), while the staged assets are all lowercase.
        // Without this, LoadInstanceModels found nothing and every level lost
        // its instances: parked cars, trees, lampposts, bins, barriers, ramps.
        _strlwr(fs->dirpath);
        fs->dir = opendir(fs->dirpath);
    }
    if (!fs->dir) { free(fs); return INVALID_HANDLE_VALUE; }

    if (!FindFill(fs, data)) {
        closedir(fs->dir);
        free(fs);
        return INVALID_HANDLE_VALUE;
    }
    return (HANDLE)fs;
}

BOOL FindNextFile(HANDLE find, LPWIN32_FIND_DATA data)
{
    if (find == INVALID_HANDLE_VALUE || !find) return FALSE;
    return FindFill((FindState *)find, data);
}

BOOL FindClose(HANDLE find)
{
    if (find == INVALID_HANDLE_VALUE || !find) return FALSE;
    FindState *fs = (FindState *)find;
    closedir(fs->dir);
    free(fs);
    return TRUE;
}

// ---------------------------------------------------------------- local heap

HLOCAL LocalAlloc(UINT flags, size_t bytes)
{
    void *p = malloc(bytes);
    if (p && (flags & LMEM_ZEROINIT)) memset(p, 0, bytes);
    return (HLOCAL)p;
}

HLOCAL LocalFree(HLOCAL mem)
{
    free(mem);
    return nullptr;
}

// ---------------------------------------------------------------- UI / debug

int MessageBox(HWND, LPCSTR text, LPCSTR caption, UINT)
{
    // The game uses Box() -> MessageBox for fatal errors and questions.
    // Log it; "OK" is the safest generic answer. Real dialogs come later
    // via a JNI callback into the Java layer.
    ALOGE("[%s] %s", caption ? caption : "Re-Volt", text ? text : "");
    return IDOK;
}

void OutputDebugString(LPCSTR s)
{
    ALOGI("%s", s ? s : "");
}

SHORT GetAsyncKeyState(int)
{
    return 0;  // keyboard polling replaced by Android input layer
}

SHORT GetKeyState(int)
{
    return 0;  // toggle states (scroll lock etc.) don't exist on Android
}

// ---------------------------------------------------------------- process

BOOL SetPriorityClass(HANDLE, DWORD) { return TRUE; }
HANDLE GetCurrentProcess(void)       { return (HANDLE)(intptr_t)-1; }
void PostQuitMessage(int)            {}

DWORD GetCurrentDirectory(DWORD len, LPSTR buf)
{
    if (getcwd(buf, len) == nullptr) return 0;
    return (DWORD)strlen(buf);
}

BOOL SetCurrentDirectory(LPCSTR path)
{
    return chdir(path) == 0 ? TRUE : FALSE;
}

// ---------------------------------------------------------------- CRT extras

char *_strupr(char *s)
{
    for (char *p = s; *p; p++) *p = (char)toupper((unsigned char)*p);
    return s;
}

char *_strlwr(char *s)
{
    for (char *p = s; *p; p++) *p = (char)tolower((unsigned char)*p);
    return s;
}

char *itoa(int value, char *str, int base)
{
    switch (base) {
        case 10: sprintf(str, "%d", value); break;
        case 16: sprintf(str, "%x", value); break;
        case 8:  sprintf(str, "%o", value); break;
        default: {
            // generic fallback
            char tmp[36];
            int i = 0;
            unsigned int v = (value < 0 && base == 10) ? (unsigned)-value : (unsigned)value;
            do {
                int d = v % base;
                tmp[i++] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
                v /= base;
            } while (v);
            char *p = str;
            while (i) *p++ = tmp[--i];
            *p = 0;
            break;
        }
    }
    return str;
}

// ---------------------------------------------------------------- registry
// Registry is dead on Android — settings will be a file in the app's data
// dir. These stubs report success so legacy code paths don't bail out;
// queries report "not found" so the game falls back to defaults.

LONG RegCreateKeyEx(HKEY, LPCSTR, DWORD, LPSTR, DWORD, REGSAM, void *,
                    PHKEY out, LPDWORD disp)
{
    if (out) *out = (HKEY)(uintptr_t)1;
    if (disp) *disp = 0;
    return ERROR_SUCCESS;
}

LONG RegOpenKeyEx(HKEY, LPCSTR, DWORD, REGSAM, PHKEY out)
{
    if (out) *out = (HKEY)(uintptr_t)1;
    return ERROR_SUCCESS;
}

LONG RegCloseKey(HKEY)                        { return ERROR_SUCCESS; }
LONG RegSetValueEx(HKEY, LPCSTR, DWORD, DWORD, const BYTE *, DWORD)
                                              { return ERROR_SUCCESS; }
LONG RegQueryValueEx(HKEY, LPCSTR, LPDWORD, LPDWORD, LPBYTE, LPDWORD)
                                              { return 2 /*ERROR_FILE_NOT_FOUND*/; }
LONG RegDeleteKey(HKEY, LPCSTR)               { return ERROR_SUCCESS; }

}  // extern "C"
