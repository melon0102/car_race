// Case-sensitivity shim: game code includes "aerial.h" but the real file
// is game/inc/Aerial.h. Harmless on Windows, fatal on Linux; winshim/ is first
// on the include path so this forwards without touching game/.
#include "Aerial.h"
