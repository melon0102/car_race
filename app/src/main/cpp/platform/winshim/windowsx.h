// windowsx.h — Win32 shim (Re-Volt Android port). Message-cracker macros.
#pragma once
#include "windows.h"

#define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))
