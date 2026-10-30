#define BROKKR_XXH_NO_INCLUDE
#include "xxhash_vendor.h"

#if defined(BROKKR_XXH_X86_DISPATCH)
#  undef XXH_NAMESPACE
#  include "xxh_x86dispatch.c"
#endif
