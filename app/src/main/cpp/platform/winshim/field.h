// Case-sensitivity shim: game code includes "field.h" but the real file
// is game/inc/Field.h. Harmless on Windows, fatal on Linux; winshim/ is first
// on the include path so this forwards without touching game/.
#include "Field.h"
