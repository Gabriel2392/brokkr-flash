#pragma once

/* Change this prefix to your project name if needed */
#define XXH_NAMESPACE myproj_

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#  define BROKKR_XXH_X86_DISPATCH 1
#  ifndef BROKKR_XXH_NO_INCLUDE
#    include "xxhash.h"
#    include "xxh_x86dispatch.h"
#  endif
#else
#  define XXH_INLINE_ALL
#  include "xxhash.h"
#endif
