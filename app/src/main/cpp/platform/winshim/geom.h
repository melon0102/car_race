// Case-sensitivity shim: game code includes "geom.h" but the real file
// is game/inc/Geom.h. Harmless on Windows, fatal on Linux; winshim/ is first
// on the include path so this forwards without touching game/.
#include "Geom.h"
