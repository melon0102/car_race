// Case-sensitivity shim: game code includes "Readinit.h" but the real file
// is game/inc/ReadInit.h. Harmless on Windows, fatal on Linux; winshim/ is first
// on the include path so this forwards without touching game/.
#include "ReadInit.h"
