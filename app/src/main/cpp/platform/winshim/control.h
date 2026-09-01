// Case-sensitivity shim: game code includes "control.h" but the real file
// is game/inc/Control.h. Harmless on Windows, fatal on Linux; winshim/ is first
// on the include path so this forwards without touching game/.
#include "Control.h"
