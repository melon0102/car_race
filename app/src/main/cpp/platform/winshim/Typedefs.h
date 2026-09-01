// Case-sensitivity shim: some game headers (revolt.h, Spark.h) include
// "Typedefs.h" but the file in game/inc is spelled TypeDefs.h. Windows
// filesystems don't care; Linux/NDK builds do. winshim/ is first on the
// include path, so this forwards to the real header without touching game/.
#include "TypeDefs.h"
