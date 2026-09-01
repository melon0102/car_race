// Case-sensitivity shim: game code includes "Registry.h" but the real file
// is game/inc/registry.h. Harmless on Windows, fatal on Linux; winshim/ is first
// on the include path so this forwards without touching game/.
#include "registry.h"
