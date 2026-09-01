// Case-sensitivity shim: game code includes "Instance.h" but the real file
// is game/inc/instance.h. Harmless on Windows, fatal on Linux; winshim/ is first
// on the include path so this forwards without touching game/.
#include "instance.h"
