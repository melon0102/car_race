// Case-sensitivity shim: game code includes "Light.h" but the real file
// is game/inc/light.h. Harmless on Windows, fatal on Linux; winshim/ is first
// on the include path so this forwards without touching game/.
#include "light.h"
