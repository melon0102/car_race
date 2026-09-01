// Case-sensitivity shim: game code includes "Object.h" but the real file
// is game/inc/object.h. Harmless on Windows, fatal on Linux; winshim/ is first
// on the include path so this forwards without touching game/.
#include "object.h"
