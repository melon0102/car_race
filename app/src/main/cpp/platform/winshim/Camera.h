// Case-sensitivity shim: game code includes "Camera.h" but the real file
// is game/inc/camera.h. Harmless on Windows, fatal on Linux; winshim/ is first
// on the include path so this forwards without touching game/.
#include "camera.h"
