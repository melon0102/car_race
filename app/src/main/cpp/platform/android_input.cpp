// android_input.cpp — replaces game/Input.cpp for the Android port.
//
// The game polls a DirectInput-style key array (Keys[256], DIK_* scancodes)
// every frame via ReadKeyboard(). Here that array is fed from Android input:
//   - touch buttons (landscape): drawn + hit-tested by touch_overlay.cpp
//     (steer bottom-left, accel/brake right edge, flip-recover, fire)
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

// x, y normalized to [0,1] in landscape screen space.
//
// The zone rectangles live in touch_overlay.cpp next to the button drawing,
// so the visible controls and the hit areas are one definition:
//   steer left/right arrows  bottom-left
//   accel/brake arrows       right edge, stacked
//   flip-recover car button  above them (DIK_END -> MOV_RightCar)
//   fire                     over the HUD pickup box on the left
//
// Restart-at-start-line is deliberately NOT on touch (it used to fire when
// reaching for the pickup); it remains on gamepad Y.
extern int TouchOverlayHit(float x, float y);   // touch_overlay.cpp

void AInput_TouchPoint(float x, float y)
{
    int dik = TouchOverlayHit(x, y);
    if (dik) sTouchKeys[dik] = 1;
}

// pressed-state feedback for the drawn buttons
int AInput_TouchHeld(int dik)
{
    return sTouchKeys[dik & 0xff];
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
