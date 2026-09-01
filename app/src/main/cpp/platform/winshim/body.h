// Case-sensitivity shim: game code includes "body.h" but the real file
// is game/inc/Body.h. Harmless on Windows, fatal on Linux; winshim/ is first
// on the include path so this forwards without touching game/.
#include "Body.h"
