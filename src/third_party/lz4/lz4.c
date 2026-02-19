/*
   LZ4 - Fast LZ compression algorithm
   Copyright (c) Yann Collet. All rights reserved.

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

#include "lz4.h"

#include <limits.h>
#if defined(__cplusplus) || (defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 199901L))
  #include <stdint.h>
#endif

#if !LZ4_FREESTANDING
  #include <string.h>
#endif

/*-************************************
 *  Compiler options / inlining / branch hints
 **************************************/
#ifndef LZ4_FORCE_INLINE
  #if defined(_MSC_VER) && !defined(__clang__)
    #define LZ4_FORCE_INLINE static __forceinline
  #else
    #if defined(__cplusplus) || (defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 199901L))
      #if defined(__GNUC__) || defined(__clang__)
        #define LZ4_FORCE_INLINE static inline __attribute__((always_inline))
      #else
        #define LZ4_FORCE_INLINE static inline
      #endif
    #else
      #define LZ4_FORCE_INLINE static
    #endif
  #endif
#endif

#if defined(__PPC64__) && defined(__LITTLE_ENDIAN__) && defined(__GNUC__) && !defined(__clang__)
  #define LZ4_FORCE_O2 __attribute__((optimize("O2")))
  #undef LZ4_FORCE_INLINE
  #define LZ4_FORCE_INLINE static __inline __attribute__((optimize("O2"), always_inline))
#else
  #define LZ4_FORCE_O2
#endif

#if (defined(__GNUC__) && (__GNUC__ >= 3)) || defined(__clang__) ||                                                    \
    (defined(__INTEL_COMPILER) && (__INTEL_COMPILER >= 800))
  #define LZ4_expect(expr, value) (__builtin_expect((expr), (value)))
#else
  #define LZ4_expect(expr, value) (expr)
#endif
#ifndef likely
  #define likely(expr) LZ4_expect(!!(expr), 1)
#endif
#ifndef unlikely
  #define unlikely(expr) LZ4_expect(!!(expr), 0)
#endif

/*-************************************
 *  libc wrappers (freestanding-friendly)
 **************************************/
#ifndef LZ4_memcpy
  #if defined(__GNUC__) && (__GNUC__ >= 4)
    #define LZ4_memcpy(dst, src, size) __builtin_memcpy((dst), (src), (size))
  #else
    #define LZ4_memcpy(dst, src, size) memcpy((dst), (src), (size))
  #endif
#endif
#ifndef LZ4_memmove
  #if defined(__GNUC__) && (__GNUC__ >= 4)
    #define LZ4_memmove(dst, src, size) __builtin_memmove((dst), (src), (size))
  #else
    #define LZ4_memmove(dst, src, size) memmove((dst), (src), (size))
  #endif
#endif
#ifndef LZ4_memset
  #define LZ4_memset(p, v, s) memset((p), (v), (s))
#endif

/*-************************************
 *  Types
 **************************************/
#if defined(__cplusplus) || (defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 199901L))
typedef uint8_t BYTE;
typedef uint16_t U16;
typedef uint32_t U32;
typedef uint64_t U64;
typedef uintptr_t uptrval;
#else
  #if UINT_MAX != 4294967295UL
    #error "This minimal lz4.c requires 32-bit unsigned int when not C99/C++."
  #endif
typedef unsigned char BYTE;
typedef unsigned short U16;
typedef unsigned int U32;
typedef unsigned long long U64;
typedef size_t uptrval;
#endif

/*-************************************
 *  Endianness
 **************************************/
#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && defined(__ORDER_BIG_ENDIAN__)
  #define LZ4_IS_LITTLE_ENDIAN (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
#else
LZ4_FORCE_INLINE unsigned LZ4_isLittleEndian(void) {
  const union {
    U32 u;
    BYTE c[4];
  } one = {1};
  return one.c[0];
}
  #define LZ4_IS_LITTLE_ENDIAN (LZ4_isLittleEndian())
#endif

/*-************************************
 *  Unaligned memory access
 **************************************/
#ifndef LZ4_FORCE_MEMORY_ACCESS
  #if defined(__GNUC__) &&                                                                                             \
      (defined(__ARM_ARCH_6__) || defined(__ARM_ARCH_6J__) || defined(__ARM_ARCH_6K__) || defined(__ARM_ARCH_6Z__) ||  \
       defined(__ARM_ARCH_6ZK__) || defined(__ARM_ARCH_6T2__) || (defined(__riscv) && defined(__riscv_zicclsm)))
    #define LZ4_FORCE_MEMORY_ACCESS 2
  #elif (defined(__INTEL_COMPILER) && !defined(_WIN32)) || defined(__GNUC__) || defined(_MSC_VER)
    #define LZ4_FORCE_MEMORY_ACCESS 1
  #endif
#endif

#if defined(__GNUC__) || defined(__INTEL_COMPILER)
  #define LZ4_PACK(decl) decl __attribute__((__packed__))
#elif defined(_MSC_VER)
  #define LZ4_PACK(decl) __pragma(pack(push, 1)) decl __pragma(pack(pop))
#else
  #define LZ4_PACK(decl) decl
#endif

#if defined(LZ4_FORCE_MEMORY_ACCESS) && (LZ4_FORCE_MEMORY_ACCESS == 2)
LZ4_FORCE_INLINE U16 LZ4_read16(const void* p) { return *(const U16*)p; }
LZ4_FORCE_INLINE void LZ4_write32(void* p, U32 v) { *(U32*)p = v; }
#elif defined(LZ4_FORCE_MEMORY_ACCESS) && (LZ4_FORCE_MEMORY_ACCESS == 1)
LZ4_PACK(typedef struct { U16 u16; }) LZ4_unalign16;
LZ4_PACK(typedef struct { U32 u32; }) LZ4_unalign32;
LZ4_FORCE_INLINE U16 LZ4_read16(const void* p) { return ((const LZ4_unalign16*)p)->u16; }
LZ4_FORCE_INLINE void LZ4_write32(void* p, U32 v) { ((LZ4_unalign32*)p)->u32 = v; }
#else
LZ4_FORCE_INLINE U16 LZ4_read16(const void* p) {
  U16 v;
  LZ4_memcpy(&v, p, sizeof(v));
  return v;
}
LZ4_FORCE_INLINE void LZ4_write32(void* p, U32 v) { LZ4_memcpy(p, &v, sizeof(v)); }
#endif

LZ4_FORCE_INLINE U16 LZ4_readLE16(const void* p) {
  if (LZ4_IS_LITTLE_ENDIAN) return LZ4_read16(p);
  {
    const BYTE* b = (const BYTE*)p;
    return (U16)((U16)b[0] | (U16)(b[1] << 8));
  }
}

/*-************************************
 *  Constants (only what decompressor needs)
 **************************************/
#define MINMATCH 4
#define WILDCOPYLENGTH 8
#define LASTLITERALS 5
#define MFLIMIT 12
#define MATCH_SAFEGUARD_DISTANCE ((2 * WILDCOPYLENGTH) - MINMATCH) /* 12 */
#define FASTLOOP_SAFE_DISTANCE 64

#define ML_BITS 4
#define ML_MASK ((1U << ML_BITS) - 1)
#define RUN_BITS (8 - ML_BITS)
#define RUN_MASK ((1U << RUN_BITS) - 1)

#define LZ4_STATIC_ASSERT(c)                                                                                           \
  {                                                                                                                    \
    enum { LZ4_static_assert = 1 / (int)(!!(c)) };                                                                     \
  }

static const unsigned inc32table[8] = {0, 1, 2, 1, 0, 4, 4, 4};
static const int dec64table[8] = {0, 0, 0, -1, -4, 1, 2, 3};

/*-************************************
 *  Wild copies (allow controlled overwrite past end)
 **************************************/
LZ4_FORCE_INLINE void LZ4_wildCopy8(void* dst, const void* src, void* dstEnd) {
  BYTE* d = (BYTE*)dst;
  const BYTE* s = (const BYTE*)src;
  BYTE* const e = (BYTE*)dstEnd;
  do {
    LZ4_memcpy(d, s, 8);
    d += 8;
    s += 8;
  } while (d < e);
}

#ifndef LZ4_FAST_DEC_LOOP
  #if defined(__i386__) || defined(_M_IX86) || defined(__x86_64__) || defined(_M_X64)
    #define LZ4_FAST_DEC_LOOP 1
  #elif defined(__aarch64__)
    #if defined(__clang__) && defined(__ANDROID__)
      #define LZ4_FAST_DEC_LOOP 0
    #else
      #define LZ4_FAST_DEC_LOOP 1
    #endif
  #else
    #define LZ4_FAST_DEC_LOOP 0
  #endif
#endif

#if LZ4_FAST_DEC_LOOP
LZ4_FORCE_INLINE void LZ4_wildCopy32(void* dst, const void* src, void* dstEnd) {
  BYTE* d = (BYTE*)dst;
  const BYTE* s = (const BYTE*)src;
  BYTE* const e = (BYTE*)dstEnd;
  do {
    LZ4_memcpy(d, s, 16);
    LZ4_memcpy(d + 16, s + 16, 16);
    d += 32;
    s += 32;
  } while (d < e);
}

LZ4_FORCE_INLINE void LZ4_memcpy_using_offset_base(BYTE* dst, const BYTE* src, BYTE* dstEnd, size_t offset) {
  (void)offset; /* offset only used for assertions in full source; keep interface */
  if (offset < 8) {
    LZ4_write32(dst, 0); /* silence msan when offset==0 */
    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
    dst[3] = src[3];
    src += inc32table[offset];
    LZ4_memcpy(dst + 4, src, 4);
    src -= dec64table[offset];
    dst += 8;
  } else {
    LZ4_memcpy(dst, src, 8);
    dst += 8;
    src += 8;
  }
  LZ4_wildCopy8(dst, src, dstEnd);
}

LZ4_FORCE_INLINE void LZ4_memcpy_using_offset(BYTE* dst, const BYTE* src, BYTE* dstEnd, size_t offset) {
  BYTE v[8];
  switch (offset) {
    case 1: LZ4_memset(v, *src, 8); break;
    case 2:
      LZ4_memcpy(v, src, 2);
      LZ4_memcpy(v + 2, src, 2);
      LZ4_memcpy(v + 4, v, 4);
      break;
    case 4:
      LZ4_memcpy(v, src, 4);
      LZ4_memcpy(v + 4, src, 4);
      break;
    default: LZ4_memcpy_using_offset_base(dst, src, dstEnd, offset); return;
  }
  LZ4_memcpy(dst, v, 8);
  dst += 8;
  while (dst < dstEnd) {
    LZ4_memcpy(dst, v, 8);
    dst += 8;
  }
}
#endif /* LZ4_FAST_DEC_LOOP */

/*-************************************
 *  Variable-length decoding (safe)
 **************************************/
typedef size_t Rvl_t;
static const Rvl_t rvl_error = (Rvl_t)(-1);

LZ4_FORCE_INLINE Rvl_t read_variable_length(const BYTE** ip, const BYTE* ilimit, int initial_check) {
  Rvl_t s, length = 0;
  if (initial_check && unlikely(*ip >= ilimit)) return rvl_error;
  s = **ip;
  (*ip)++;
  length += s;
  if (unlikely(*ip > ilimit)) return rvl_error;
  if ((sizeof(length) < 8) && unlikely(length > ((Rvl_t)(-1) / 2))) return rvl_error;
  if (likely(s != 255)) return length;

  do {
    s = **ip;
    (*ip)++;
    length += s;
    if (unlikely(*ip > ilimit)) return rvl_error;
    if ((sizeof(length) < 8) && unlikely(length > ((Rvl_t)(-1) / 2))) return rvl_error;
  } while (s == 255);

  return length;
}

/*-************************************
 *  Public API: LZ4_decompress_safe only
 **************************************/
LZ4_FORCE_O2
LZ4LIB_API int LZ4_decompress_safe(const char* src, char* dst, int compressedSize, int dstCapacity) {
  const BYTE* const istart = (const BYTE*)src;
  BYTE* const ostart = (BYTE*)dst;

  const BYTE* ip;
  const BYTE* iend;
  BYTE* op;
  BYTE* oend;

  const BYTE* shortiend;
  BYTE* shortoend;

  if (!src || !dst) return -1;
  if (compressedSize < 0 || dstCapacity < 0) return -1;

  ip = (const BYTE*)src;
  iend = ip + (size_t)compressedSize;
  op = (BYTE*)dst;
  oend = op + (size_t)dstCapacity;

  /* special cases */
  if (unlikely(dstCapacity == 0)) {
    if (compressedSize == 1 && *ip == 0) return 0;
    return -1;
  }
  if (unlikely(compressedSize == 0)) return -1;

  /* safe shortcut end pointers (avoid forming pointers before start) */
  shortiend = (compressedSize >= 16) ? (iend - 16) : istart;
  shortoend = (dstCapacity >= 32) ? (oend - 32) : ostart;

  for (;;) {
    unsigned token;
    size_t length;
    size_t offset;
    const BYTE* match;
    BYTE* cpy;

#if LZ4_FAST_DEC_LOOP
    /* enter fast loop only when buffers are big enough */
    if ((size_t)(oend - op) >= FASTLOOP_SAFE_DISTANCE && (size_t)(iend - ip) >= 32) {
      const BYTE* const fastIend = iend;
      BYTE* const fastOend = oend;

      /* Fast loop */
      while ((size_t)(fastOend - op) >= FASTLOOP_SAFE_DISTANCE) {
        token = *ip++;
        length = token >> ML_BITS;

        /* literals */
        if (length == RUN_MASK) {
          const BYTE* const ilimit = ((size_t)(fastIend - istart) >= RUN_MASK) ? (fastIend - RUN_MASK) : istart;
          size_t const addl = read_variable_length(&ip, ilimit, 1);
          if (addl == rvl_error) goto _output_error;
          length += addl;
          if (unlikely((uptrval)op + length < (uptrval)op)) goto _output_error;
          if (unlikely((uptrval)ip + length < (uptrval)ip)) goto _output_error;

          LZ4_STATIC_ASSERT(MFLIMIT >= WILDCOPYLENGTH);
          if ((op + length > fastOend - 32) || (ip + length > fastIend - 32)) goto safe_literal_copy;
          LZ4_wildCopy32(op, ip, op + length);
          ip += length;
          op += length;
        } else if (ip <= fastIend - (16 + 1)) {
          LZ4_memcpy(op, ip, 16);
          ip += length;
          op += length;
        } else {
          goto safe_literal_copy;
        }

        /* offset */
        offset = LZ4_readLE16(ip);
        ip += 2;
        match = op - offset;

        if (unlikely(match < (const BYTE*)ostart)) goto _output_error;

        /* match length */
        length = token & ML_MASK;
        if (length == ML_MASK) {
          const BYTE* const ilimit =
              ((size_t)(fastIend - istart) >= (LASTLITERALS - 1)) ? (fastIend - (LASTLITERALS - 1)) : istart;
          size_t const addl = read_variable_length(&ip, ilimit, 0);
          if (addl == rvl_error) goto _output_error;
          length += addl;
          length += MINMATCH;
          if (unlikely((uptrval)op + length < (uptrval)op)) goto _output_error;
          if (op + length >= fastOend - FASTLOOP_SAFE_DISTANCE) goto safe_match_copy;
        } else {
          length += MINMATCH;
          if (op + length >= fastOend - FASTLOOP_SAFE_DISTANCE) goto safe_match_copy;

          /* fastpath for common non-overlap cases */
          if (offset >= 8) {
            LZ4_memcpy(op, match, 8);
            LZ4_memcpy(op + 8, match + 8, 8);
            LZ4_memcpy(op + 16, match + 16, 2);
            op += length;
            continue;
          }
        }

        /* general match copy */
        cpy = op + length;
        if (unlikely(offset < 16))
          LZ4_memcpy_using_offset(op, match, cpy, offset);
        else
          LZ4_wildCopy32(op, match, cpy);
        op = cpy;
      }
    }
/* drop to safe decode */
#endif /* LZ4_FAST_DEC_LOOP */

    /* Safe loop */
    for (;;) {
      token = *ip++;
      length = token >> ML_BITS;

      /* shortcut */
      if ((length != RUN_MASK) && likely((ip < shortiend) & (op <= shortoend))) {
        LZ4_memcpy(op, ip, 16);
        op += length;
        ip += length;

        length = token & ML_MASK;
        offset = LZ4_readLE16(ip);
        ip += 2;
        match = op - offset;

        if (unlikely(match < (const BYTE*)ostart)) goto _output_error;

        if ((length != ML_MASK) && (offset >= 8)) {
          LZ4_memcpy(op + 0, match + 0, 8);
          LZ4_memcpy(op + 8, match + 8, 8);
          LZ4_memcpy(op + 16, match + 16, 2);
          op += length + MINMATCH;
          continue;
        }
        goto _copy_match;
      }

      /* literal length */
      if (length == RUN_MASK) {
        const BYTE* const ilimit = ((size_t)(iend - istart) >= RUN_MASK) ? (iend - RUN_MASK) : istart;
        size_t const addl = read_variable_length(&ip, ilimit, 1);
        if (addl == rvl_error) goto _output_error;
        length += addl;
        if (unlikely((uptrval)op + length < (uptrval)op)) goto _output_error;
        if (unlikely((uptrval)ip + length < (uptrval)ip)) goto _output_error;
      }

#if LZ4_FAST_DEC_LOOP
    safe_literal_copy:
#endif
      /* copy literals */
      cpy = op + length;

      LZ4_STATIC_ASSERT(MFLIMIT >= WILDCOPYLENGTH);

      /* output parsing restriction (need room for match copy) or input parsing restriction */
      {
        const BYTE* const iend_minus =
            ((size_t)(iend - istart) >= (2 + 1 + LASTLITERALS)) ? (iend - (2 + 1 + LASTLITERALS)) : istart;
        BYTE* const oend_minus = ((size_t)(oend - ostart) >= MFLIMIT) ? (oend - MFLIMIT) : ostart;

        if ((cpy > oend_minus) || (ip + length > iend_minus)) {
          /* must be last literals run */
          if ((ip + length != iend) || (cpy > oend)) goto _output_error;
          LZ4_memmove(op, ip, length);
          ip += length;
          op += length;
          return (int)(op - ostart);
        }
      }

      LZ4_wildCopy8(op, ip, cpy);
      ip += length;
      op = cpy;

      /* offset */
      offset = LZ4_readLE16(ip);
      ip += 2;
      match = op - offset;
      if (unlikely(match < (const BYTE*)ostart)) goto _output_error;

      /* match length */
      length = token & ML_MASK;

    _copy_match:
      if (length == ML_MASK) {
        const BYTE* const ilimit = ((size_t)(iend - istart) >= (LASTLITERALS - 1)) ? (iend - (LASTLITERALS - 1))
                                                                                   : istart;
        size_t const addl = read_variable_length(&ip, ilimit, 0);
        if (addl == rvl_error) goto _output_error;
        length += addl;
        if (unlikely((uptrval)op + length < (uptrval)op)) goto _output_error;
      }
      length += MINMATCH;

#if LZ4_FAST_DEC_LOOP
    safe_match_copy:
#endif
      cpy = op + length;

      if (unlikely(cpy > oend - MATCH_SAFEGUARD_DISTANCE)) {
        BYTE* const oCopyLimit = oend - (WILDCOPYLENGTH - 1);
        if (cpy > oend - LASTLITERALS) goto _output_error;

        /* first 8 bytes */
        if (unlikely(offset < 8)) {
          LZ4_write32(op, 0);
          op[0] = match[0];
          op[1] = match[1];
          op[2] = match[2];
          op[3] = match[3];
          match += inc32table[offset];
          LZ4_memcpy(op + 4, match, 4);
          match -= dec64table[offset];
        } else {
          LZ4_memcpy(op, match, 8);
          match += 8;
        }
        op += 8;

        if (op < oCopyLimit) {
          LZ4_wildCopy8(op, match, oCopyLimit);
          match += (size_t)(oCopyLimit - op);
          op = oCopyLimit;
        }
        while (op < cpy) {
          *op++ = *match++;
        }
        op = cpy;
        continue;
      }

      /* match copy (normal) */
      if (unlikely(offset < 8)) {
        LZ4_write32(op, 0);
        op[0] = match[0];
        op[1] = match[1];
        op[2] = match[2];
        op[3] = match[3];
        match += inc32table[offset];
        LZ4_memcpy(op + 4, match, 4);
        match -= dec64table[offset];
      } else {
        LZ4_memcpy(op, match, 8);
        match += 8;
      }
      op += 8;

      LZ4_memcpy(op, match, 8);
      if (length > 16) LZ4_wildCopy8(op + 8, match + 8, cpy);
      op = cpy;
    }
  }

_output_error:
  /* match upstream error convention: negative position in input, minus 1 */
  return (int)(-((ptrdiff_t)((const char*)ip - src)) - 1);
}
