// Case-sensitivity shim: game code includes "Level.h" but the real file
// is game/inc/level.h. Harmless on Windows, fatal on Linux; winshim/ is first
// on the include path so this forwards without touching game/.
#include "level.h"
