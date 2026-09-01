// Case-sensitivity shim: game code includes "particle.h" but the real file
// is game/inc/Particle.h. Harmless on Windows, fatal on Linux; winshim/ is first
// on the include path so this forwards without touching game/.
#include "Particle.h"
