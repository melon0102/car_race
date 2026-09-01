// Case-sensitivity shim: game code includes "spark.h" but the real file
// is game/inc/Spark.h. Harmless on Windows, fatal on Linux; winshim/ is first
// on the include path so this forwards without touching game/.
#include "Spark.h"
