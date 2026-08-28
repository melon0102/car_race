// android_input.cpp — replaces game/Input.cpp for the Android port.
//
// The game polls a DirectInput-style key array (Keys[256], DIK_* scancodes)
// every frame via ReadKeyboard(). Here that array is fed from Android input:
//   - touch zones (landscape): steer buttons bottom-left, accel/brake
//     bottom-right, fire top-right, restart top-left
//   - gamepads: dpad/left stick steer, A/gas accel, B/brake reverse, X fire
//   - hardware keyboards: arrows etc. pass through directly
//
// android_main.cpp parses the GameActivity input buffers and calls the
// AInput_* functions; ReadKeyboard() latches the state into Keys/LastKeys
// with correct edge detection.

#include <string.h>   // before game headers (long=int define, see windows.h)

#include "revolt.h"
#include "input.h"

// ---------------------------------------------------------------- game globals
// (normally defined in Input.cpp)

char Keys[256];
char LastKeys[256];
DIMOUSESTATE Mouse;
long JoystickNum = 0, CurrentJoystick = -1;   // -1 = use keyboard controls
JOYSTICK Joystick[MAX_JOYSTICKS];
DIJOYSTATE JoystickState;

// ---------------------------------------------------------------- virtual state

static char sTouchKeys[256];    // from touch zones (rebuilt per touch event)
static char sPadKeys[256];      // from gamepad/keyboard events (edge driven)
static float sAxisSteer = 0.0f; // left stick x
static float sAxisGas = 0.0f;   // right trigger / gas
static float sAxisBrake = 0.0f; // left trigger / brake

extern "C" {

// ---- touch: caller rebuilds the active pointer list each event ----

void AInput_TouchReset(void)
{
    memset(sTouchKeys, 0, sizeof(sTouchKeys));
}

// x, y normalized to [0,1] in landscape screen space
void AInput_TouchPoint(float x, float y)
{
    if (y >= 0.50f) {
        // bottom band: driving controls
        if (x < 0.18f)                    sTouchKeys[DIK_LEFT] = 1;
        else if (x < 0.36f)               sTouchKeys[DIK_RIGHT] = 1;
        else if (x >= 0.82f)              sTouchKeys[DIK_UP] = 1;      // accel
        else if (x >= 0.64f)              sTouchKeys[DIK_DOWN] = 1;    // brake/rev
    } else if (y < 0.30f) {
        if (x >= 0.80f)                   sTouchKeys[DIK_LCONTROL] = 1; // fire
        else if (x < 0.20f)               sTouchKeys[DIK_HOME] = 1;     // restart
    }
}

// ---- gamepad / keyboard keys ----

// maps an Android keycode to a DIK scancode (0 = unmapped)
static int MapKeyCode(int32_t keyCode)
{
    switch (keyCode) {
        case 19 /*DPAD_UP*/:      return DIK_UP;
        case 20 /*DPAD_DOWN*/:    return DIK_DOWN;
        case 21 /*DPAD_LEFT*/:    return DIK_LEFT;
        case 22 /*DPAD_RIGHT*/:   return DIK_RIGHT;
        case 96 /*BUTTON_A*/:     return DIK_UP;       // accel
        case 97 /*BUTTON_B*/:     return DIK_DOWN;     // brake/reverse
        case 99 /*BUTTON_X*/:     return DIK_LCONTROL; // fire
        case 100 /*BUTTON_Y*/:    return DIK_HOME;     // restart
        case 108 /*BUTTON_START*/:return DIK_ESCAPE;
        case 4  /*BACK*/:         return DIK_ESCAPE;
        case 66 /*ENTER*/:        return DIK_RETURN;
        case 62 /*SPACE*/:        return DIK_SPACE;
        case 111 /*ESCAPE*/:      return DIK_ESCAPE;
        default:                  return 0;
    }
}

void AInput_Key(int32_t keyCode, int down)
{
    int dik = MapKeyCode(keyCode);
    if (dik) sPadKeys[dik] = down ? 1 : 0;
}

// ---- gamepad axes ----

void AInput_Axes(float steerX, float gas, float brake)
{
    sAxisSteer = steerX;
    sAxisGas = gas;
    sAxisBrake = brake;
}

}  // extern "C"

// ---------------------------------------------------------------- game hooks

long InitInput(HINSTANCE)
{
    memset(Keys, 0, sizeof(Keys));
    memset(LastKeys, 0, sizeof(LastKeys));
    memset(sTouchKeys, 0, sizeof(sTouchKeys));
    memset(sPadKeys, 0, sizeof(sPadKeys));
    return TRUE;
}

void KillInput(void) {}

void ReadKeyboard(void)
{
    memcpy(LastKeys, Keys, sizeof(Keys));

    // merge touch + pad states
    for (int i = 0; i < 256; i++)
        Keys[i] = sTouchKeys[i] | sPadKeys[i];

    // analog stick/triggers as digital keys (deadzone 0.35)
    if (sAxisSteer < -0.35f) Keys[DIK_LEFT] = 1;
    if (sAxisSteer > 0.35f)  Keys[DIK_RIGHT] = 1;
    if (sAxisGas > 0.30f)    Keys[DIK_UP] = 1;
    if (sAxisBrake > 0.30f)  Keys[DIK_DOWN] = 1;
}

void ReadMouse(void) {}
void ReadJoystick(void) {}
void SetMouseExclusive(long) {}

unsigned char GetKeyPress(void)
{
    return 0;   // text entry comes via the Android UI in Phase 4
}
