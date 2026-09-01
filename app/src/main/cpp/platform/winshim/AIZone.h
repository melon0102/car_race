// Case-sensitivity shim: game code includes "AIZone.h" but the real file
// is game/inc/aizone.h. Harmless on Windows, fatal on Linux; winshim/ is first
// on the include path so this forwards without touching game/.
#include "aizone.h"
