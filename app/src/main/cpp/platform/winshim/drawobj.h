// Case-sensitivity shim: game code includes "drawobj.h" but the real file
// is game/inc/DrawObj.h. Harmless on Windows, fatal on Linux; winshim/ is first
// on the include path so this forwards without touching game/.
#include "DrawObj.h"
