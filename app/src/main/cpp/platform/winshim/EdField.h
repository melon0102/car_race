// Case-sensitivity shim: game code includes "EdField.h" but the real file
// is game/inc/edfield.h. Harmless on Windows, fatal on Linux; winshim/ is first
// on the include path so this forwards without touching game/.
#include "edfield.h"
