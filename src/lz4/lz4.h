/*
 *  LZ4 - Fast LZ compression algorithm
 *  Header File
 *  Copyright (c) Yann Collet. All rights reserved.

   BSD 2-Clause License (http://www.opensource.org/licenses/bsd-license.php)

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are
   met:

       * Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
       * Redistributions in binary form must reproduce the above
   copyright notice, this list of conditions and the following disclaimer
   in the documentation and/or other materials provided with the
   distribution.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   You can contact the author at :
    - LZ4 homepage : http://www.lz4.org
    - LZ4 source repository : https://github.com/lz4/lz4
*/
#ifndef LZ4_H_INCLUDED
#define LZ4_H_INCLUDED

#include <stddef.h>

#if defined(__cplusplus)
extern "C" {
  #endif

  #ifndef LZ4LIB_VISIBILITY
  #  if defined(__GNUC__) && (__GNUC__ >= 4)
  #    define LZ4LIB_VISIBILITY __attribute__ ((visibility ("default")))
  #  else
  #    define LZ4LIB_VISIBILITY
  #  endif
  #endif

  #if defined(_WIN32) && defined(LZ4_DLL_EXPORT) && (LZ4_DLL_EXPORT==1)
  #  define LZ4LIB_API __declspec(dllexport) LZ4LIB_VISIBILITY
  #elif defined(_WIN32) && defined(LZ4_DLL_IMPORT) && (LZ4_DLL_IMPORT==1)
  #  define LZ4LIB_API __declspec(dllimport) LZ4LIB_VISIBILITY
  #else
  #  define LZ4LIB_API LZ4LIB_VISIBILITY
  #endif

  #if defined(LZ4_FREESTANDING) && (LZ4_FREESTANDING == 1)
  #  define LZ4_HEAPMODE 0
  #  define LZ4_STATIC_LINKING_ONLY_DISABLE_MEMORY_ALLOCATION 1
  #  if !defined(LZ4_memcpy) || !defined(LZ4_memset) || !defined(LZ4_memmove)
  #    error "LZ4_FREESTANDING requires LZ4_memcpy/LZ4_memset/LZ4_memmove."
  #  endif
  #elif !defined(LZ4_FREESTANDING)
  #  define LZ4_FREESTANDING 0
  #endif

  #define LZ4_VERSION_MAJOR 1
  #define LZ4_VERSION_MINOR 10
  #define LZ4_VERSION_RELEASE 0
  #define LZ4_VERSION_NUMBER (LZ4_VERSION_MAJOR*100*100 + LZ4_VERSION_MINOR*100 + LZ4_VERSION_RELEASE)

  #ifndef LZ4_MEMORY_USAGE_DEFAULT
  #  define LZ4_MEMORY_USAGE_DEFAULT 14
  #endif
  #ifndef LZ4_MEMORY_USAGE
  #  define LZ4_MEMORY_USAGE LZ4_MEMORY_USAGE_DEFAULT
  #endif
  #define LZ4_MEMORY_USAGE_MIN 10
  #define LZ4_MEMORY_USAGE_MAX 20
  #if (LZ4_MEMORY_USAGE < LZ4_MEMORY_USAGE_MIN)
  #  error "LZ4_MEMORY_USAGE too small"
  #endif
  #if (LZ4_MEMORY_USAGE > LZ4_MEMORY_USAGE_MAX)
  #  error "LZ4_MEMORY_USAGE too large"
  #endif

  #define LZ4_MAX_INPUT_SIZE 0x7E000000

  typedef struct LZ4_stream_t_internal LZ4_stream_t;
  typedef struct LZ4_streamDecode_t_internal LZ4_streamDecode_t;

  LZ4LIB_API int LZ4_decompress_safe(const char* src, char* dst, int compressedSize, int dstCapacity);

  #if defined(__cplusplus)
}
#endif

#endif
