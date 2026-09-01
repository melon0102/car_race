// Case-sensitivity shim: game code includes "Visibox.h" but the real file
// is game/inc/visibox.h. Harmless on Windows, fatal on Linux; winshim/ is first
// on the include path so this forwards without touching game/.
#include "visibox.h"
