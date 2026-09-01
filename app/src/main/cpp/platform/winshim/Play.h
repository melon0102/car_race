// Case-sensitivity shim: game code includes "Play.h" but the real file
// is game/inc/play.h. Harmless on Windows, fatal on Linux; winshim/ is first
// on the include path so this forwards without touching game/.
#include "play.h"
