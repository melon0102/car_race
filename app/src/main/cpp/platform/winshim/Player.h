// Case-sensitivity shim: game code includes "Player.h" but the real file
// is game/inc/player.h. Harmless on Windows, fatal on Linux; winshim/ is first
// on the include path so this forwards without touching game/.
#include "player.h"
