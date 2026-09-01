// Case-sensitivity shim: game code includes "ReVolt.h" but the real file
// is game/inc/revolt.h. Harmless on Windows, fatal on Linux; winshim/ is first
// on the include path so this forwards without touching game/.
#include "revolt.h"
