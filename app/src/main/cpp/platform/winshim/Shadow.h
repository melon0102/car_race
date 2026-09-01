// Case-sensitivity shim: game code includes "Shadow.h" but the real file
// is game/inc/shadow.h. Harmless on Windows, fatal on Linux; winshim/ is first
// on the include path so this forwards without touching game/.
#include "shadow.h"
