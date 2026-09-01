// Case-sensitivity shim: game code includes "Main.h" but the real file
// is game/inc/main.h. Harmless on Windows, fatal on Linux; winshim/ is first
// on the include path so this forwards without touching game/.
#include "main.h"
