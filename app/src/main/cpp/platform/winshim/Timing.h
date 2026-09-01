// Case-sensitivity shim: game code includes "Timing.h" but the real file
// is game/inc/timing.h. Harmless on Windows, fatal on Linux; winshim/ is first
// on the include path so this forwards without touching game/.
#include "timing.h"
