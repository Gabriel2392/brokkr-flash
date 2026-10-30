#define BROKKR_XXH_NO_INCLUDE
#include "xxhash_vendor.h"

#if defined(BROKKR_XXH_X86_DISPATCH)
#  define XXH_STATIC_LINKING_ONLY
#  define XXH_IMPLEMENTATION
#  include "xxhash.h"
#endif
