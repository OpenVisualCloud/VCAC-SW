#include "version.h"

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

const char BUILD_VERSION[] = STR(BUILDNO) " build on " STR(BUILDDATE);
