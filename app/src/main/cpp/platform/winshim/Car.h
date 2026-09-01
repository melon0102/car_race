// Case-sensitivity shim: game code includes "Car.h" but the real file
// is game/inc/car.h. Harmless on Windows, fatal on Linux; winshim/ is first
// on the include path so this forwards without touching game/.
#include "car.h"
