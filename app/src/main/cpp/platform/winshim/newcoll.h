// Case-sensitivity shim: game code includes "newcoll.h" but the real file
// is game/inc/NewColl.h. Harmless on Windows, fatal on Linux; winshim/ is first
// on the include path so this forwards without touching game/.
#include "NewColl.h"
