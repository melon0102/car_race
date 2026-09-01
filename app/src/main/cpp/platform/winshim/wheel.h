// Case-sensitivity shim: game code includes "wheel.h" but the real file
// is game/inc/Wheel.h. Harmless on Windows, fatal on Linux; winshim/ is first
// on the include path so this forwards without touching game/.
#include "Wheel.h"
