// Case-sensitivity shim: game code includes "Ctrlread.h" but the real file
// is game/inc/ctrlread.h. Harmless on Windows, fatal on Linux; winshim/ is first
// on the include path so this forwards without touching game/.
#include "ctrlread.h"
