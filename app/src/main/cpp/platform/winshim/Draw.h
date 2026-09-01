// Case-sensitivity shim: game code includes "Draw.h" but the real file
// is game/inc/draw.h. Harmless on Windows, fatal on Linux; winshim/ is first
// on the include path so this forwards without touching game/.
#include "draw.h"
