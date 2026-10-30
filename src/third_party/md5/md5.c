/*
 * Copyright (c) 2026 Gabriel2392
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "md5.h"

#include <string.h>

#if defined(_MSC_VER) && !defined(__clang__)
  #include <stdlib.h>
  #define MD5_ROTL(v, n) _rotl((v), (n))
#else
  #define MD5_ROTL(v, n) (((v) << (n)) | ((v) >> (32 - (n))))
#endif

#if defined(__GNUC__) || defined(__clang__)
  #define MD5_PIN(v) __asm__("" : "+r"(v))
#else
  #define MD5_PIN(v) ((void)0)
#endif

#define MD5_STEP_F(a, b, c, d, x, k, s)                                                                                \
  do {                                                                                                                 \
    MD5_WORD t_ = (a) + (x) + (k);                                                                                     \
    MD5_PIN(t_);                                                                                                       \
    (a) = MD5_ROTL(t_ + ((((c) ^ (d)) & (b)) ^ (d)), s) + (b);                                                         \
  } while (0)

#define MD5_STEP_G(a, b, c, d, x, k, s)                                                                                \
  do {                                                                                                                 \
    MD5_WORD t_ = (a) + (x) + (k) + ((c) & ~(d));                                                                      \
    MD5_PIN(t_);                                                                                                       \
    (a) = MD5_ROTL(t_ + ((d) & (b)), s) + (b);                                                                         \
  } while (0)

#define MD5_STEP_H(a, b, c, d, x, k, s)                                                                                \
  do {                                                                                                                 \
    MD5_WORD t_ = (a) + (x) + (k);                                                                                     \
    MD5_PIN(t_);                                                                                                       \
    (a) = MD5_ROTL(t_ + (((c) ^ (d)) ^ (b)), s) + (b);                                                                 \
  } while (0)

#define MD5_STEP_I(a, b, c, d, x, k, s)                                                                                \
  do {                                                                                                                 \
    MD5_WORD t_ = (a) + (x) + (k);                                                                                     \
    MD5_PIN(t_);                                                                                                       \
    (a) = MD5_ROTL(t_ + ((c) ^ ((b) | ~(d))), s) + (b);                                                                \
  } while (0)

typedef void (*md5_blocks_fn)(MD5_WORD state[4], const MD5_BYTE* data, size_t blocks);

#if defined(BROKKR_MD5_X64_ASM)
void md5_blocks_x64(MD5_WORD state[4], const MD5_BYTE* data, size_t blocks);
void md5_blocks_x64_avx512(MD5_WORD state[4], const MD5_BYTE* data, size_t blocks);
  #define md5_blocks_base md5_blocks_x64
#else
static MD5_WORD md5_load(const MD5_BYTE* p) {
  #if defined(BROKKR_BIG_ENDIAN)
  return (MD5_WORD)p[0] | ((MD5_WORD)p[1] << 8) | ((MD5_WORD)p[2] << 16) | ((MD5_WORD)p[3] << 24);
  #else
  MD5_WORD v;
  memcpy(&v, p, sizeof v);
  return v;
  #endif
}

static void md5_blocks_base(MD5_WORD state[4], const MD5_BYTE* data, size_t blocks) {
  MD5_WORD a = state[0], b = state[1], c = state[2], d = state[3];
  MD5_WORD x[16];
  int i;

  while (blocks--) {
    const MD5_WORD aa = a, bb = b, cc = c, dd = d;
    for (i = 0; i < 16; ++i) x[i] = md5_load(data + 4 * i);

    MD5_STEP_F(a, b, c, d, x[0], 0xd76aa478u, 7);
    MD5_STEP_F(d, a, b, c, x[1], 0xe8c7b756u, 12);
    MD5_STEP_F(c, d, a, b, x[2], 0x242070dbu, 17);
    MD5_STEP_F(b, c, d, a, x[3], 0xc1bdceeeu, 22);
    MD5_STEP_F(a, b, c, d, x[4], 0xf57c0fafu, 7);
    MD5_STEP_F(d, a, b, c, x[5], 0x4787c62au, 12);
    MD5_STEP_F(c, d, a, b, x[6], 0xa8304613u, 17);
    MD5_STEP_F(b, c, d, a, x[7], 0xfd469501u, 22);
    MD5_STEP_F(a, b, c, d, x[8], 0x698098d8u, 7);
    MD5_STEP_F(d, a, b, c, x[9], 0x8b44f7afu, 12);
    MD5_STEP_F(c, d, a, b, x[10], 0xffff5bb1u, 17);
    MD5_STEP_F(b, c, d, a, x[11], 0x895cd7beu, 22);
    MD5_STEP_F(a, b, c, d, x[12], 0x6b901122u, 7);
    MD5_STEP_F(d, a, b, c, x[13], 0xfd987193u, 12);
    MD5_STEP_F(c, d, a, b, x[14], 0xa679438eu, 17);
    MD5_STEP_F(b, c, d, a, x[15], 0x49b40821u, 22);
    MD5_STEP_G(a, b, c, d, x[1], 0xf61e2562u, 5);
    MD5_STEP_G(d, a, b, c, x[6], 0xc040b340u, 9);
    MD5_STEP_G(c, d, a, b, x[11], 0x265e5a51u, 14);
    MD5_STEP_G(b, c, d, a, x[0], 0xe9b6c7aau, 20);
    MD5_STEP_G(a, b, c, d, x[5], 0xd62f105du, 5);
    MD5_STEP_G(d, a, b, c, x[10], 0x02441453u, 9);
    MD5_STEP_G(c, d, a, b, x[15], 0xd8a1e681u, 14);
    MD5_STEP_G(b, c, d, a, x[4], 0xe7d3fbc8u, 20);
    MD5_STEP_G(a, b, c, d, x[9], 0x21e1cde6u, 5);
    MD5_STEP_G(d, a, b, c, x[14], 0xc33707d6u, 9);
    MD5_STEP_G(c, d, a, b, x[3], 0xf4d50d87u, 14);
    MD5_STEP_G(b, c, d, a, x[8], 0x455a14edu, 20);
    MD5_STEP_G(a, b, c, d, x[13], 0xa9e3e905u, 5);
    MD5_STEP_G(d, a, b, c, x[2], 0xfcefa3f8u, 9);
    MD5_STEP_G(c, d, a, b, x[7], 0x676f02d9u, 14);
    MD5_STEP_G(b, c, d, a, x[12], 0x8d2a4c8au, 20);
    MD5_STEP_H(a, b, c, d, x[5], 0xfffa3942u, 4);
    MD5_STEP_H(d, a, b, c, x[8], 0x8771f681u, 11);
    MD5_STEP_H(c, d, a, b, x[11], 0x6d9d6122u, 16);
    MD5_STEP_H(b, c, d, a, x[14], 0xfde5380cu, 23);
    MD5_STEP_H(a, b, c, d, x[1], 0xa4beea44u, 4);
    MD5_STEP_H(d, a, b, c, x[4], 0x4bdecfa9u, 11);
    MD5_STEP_H(c, d, a, b, x[7], 0xf6bb4b60u, 16);
    MD5_STEP_H(b, c, d, a, x[10], 0xbebfbc70u, 23);
    MD5_STEP_H(a, b, c, d, x[13], 0x289b7ec6u, 4);
    MD5_STEP_H(d, a, b, c, x[0], 0xeaa127fau, 11);
    MD5_STEP_H(c, d, a, b, x[3], 0xd4ef3085u, 16);
    MD5_STEP_H(b, c, d, a, x[6], 0x04881d05u, 23);
    MD5_STEP_H(a, b, c, d, x[9], 0xd9d4d039u, 4);
    MD5_STEP_H(d, a, b, c, x[12], 0xe6db99e5u, 11);
    MD5_STEP_H(c, d, a, b, x[15], 0x1fa27cf8u, 16);
    MD5_STEP_H(b, c, d, a, x[2], 0xc4ac5665u, 23);
    MD5_STEP_I(a, b, c, d, x[0], 0xf4292244u, 6);
    MD5_STEP_I(d, a, b, c, x[7], 0x432aff97u, 10);
    MD5_STEP_I(c, d, a, b, x[14], 0xab9423a7u, 15);
    MD5_STEP_I(b, c, d, a, x[5], 0xfc93a039u, 21);
    MD5_STEP_I(a, b, c, d, x[12], 0x655b59c3u, 6);
    MD5_STEP_I(d, a, b, c, x[3], 0x8f0ccc92u, 10);
    MD5_STEP_I(c, d, a, b, x[10], 0xffeff47du, 15);
    MD5_STEP_I(b, c, d, a, x[1], 0x85845dd1u, 21);
    MD5_STEP_I(a, b, c, d, x[8], 0x6fa87e4fu, 6);
    MD5_STEP_I(d, a, b, c, x[15], 0xfe2ce6e0u, 10);
    MD5_STEP_I(c, d, a, b, x[6], 0xa3014314u, 15);
    MD5_STEP_I(b, c, d, a, x[13], 0x4e0811a1u, 21);
    MD5_STEP_I(a, b, c, d, x[4], 0xf7537e82u, 6);
    MD5_STEP_I(d, a, b, c, x[11], 0xbd3af235u, 10);
    MD5_STEP_I(c, d, a, b, x[2], 0x2ad7d2bbu, 15);
    MD5_STEP_I(b, c, d, a, x[9], 0xeb86d391u, 21);

    a += aa;
    b += bb;
    c += cc;
    d += dd;
    data += 64;
  }

  state[0] = a;
  state[1] = b;
  state[2] = c;
  state[3] = d;
}
#endif

#if !defined(BROKKR_MD5_X64_ASM) && (defined(__x86_64__) || defined(_M_X64)) &&                                        \
    (defined(__GNUC__) || defined(__clang__))
  #include <cpuid.h>
  #include <immintrin.h>

  #define MD5_HAVE_AVX512 1
  #define MD5_VPIN(v) __asm__("" : "+v"(v))

  #define MD5_STEP_AVX512(a, b, c, d, w, k, imm, s)                                                                    \
    do {                                                                                                               \
      __m128i t_ = _mm_add_epi32((a), _mm_cvtsi32_si128((int)((w) + (k))));                                            \
      MD5_VPIN(t_);                                                                                                    \
      (a) = _mm_add_epi32(_mm_rol_epi32(_mm_add_epi32(t_, _mm_ternarylogic_epi32((b), (c), (d), (imm))), (s)), (b));   \
    } while (0)

__attribute__((target("avx512f,avx512vl"))) static void md5_blocks_avx512(MD5_WORD state[4], const MD5_BYTE* data,
                                                                          size_t blocks) {
  __m128i a = _mm_cvtsi32_si128((int)state[0]);
  __m128i b = _mm_cvtsi32_si128((int)state[1]);
  __m128i c = _mm_cvtsi32_si128((int)state[2]);
  __m128i d = _mm_cvtsi32_si128((int)state[3]);
  MD5_WORD x[16];
  int i;

  while (blocks--) {
    const __m128i aa = a, bb = b, cc = c, dd = d;
    for (i = 0; i < 16; ++i) x[i] = md5_load(data + 4 * i);

    MD5_STEP_AVX512(a, b, c, d, x[0], 0xd76aa478u, 0xca, 7);
    MD5_STEP_AVX512(d, a, b, c, x[1], 0xe8c7b756u, 0xca, 12);
    MD5_STEP_AVX512(c, d, a, b, x[2], 0x242070dbu, 0xca, 17);
    MD5_STEP_AVX512(b, c, d, a, x[3], 0xc1bdceeeu, 0xca, 22);
    MD5_STEP_AVX512(a, b, c, d, x[4], 0xf57c0fafu, 0xca, 7);
    MD5_STEP_AVX512(d, a, b, c, x[5], 0x4787c62au, 0xca, 12);
    MD5_STEP_AVX512(c, d, a, b, x[6], 0xa8304613u, 0xca, 17);
    MD5_STEP_AVX512(b, c, d, a, x[7], 0xfd469501u, 0xca, 22);
    MD5_STEP_AVX512(a, b, c, d, x[8], 0x698098d8u, 0xca, 7);
    MD5_STEP_AVX512(d, a, b, c, x[9], 0x8b44f7afu, 0xca, 12);
    MD5_STEP_AVX512(c, d, a, b, x[10], 0xffff5bb1u, 0xca, 17);
    MD5_STEP_AVX512(b, c, d, a, x[11], 0x895cd7beu, 0xca, 22);
    MD5_STEP_AVX512(a, b, c, d, x[12], 0x6b901122u, 0xca, 7);
    MD5_STEP_AVX512(d, a, b, c, x[13], 0xfd987193u, 0xca, 12);
    MD5_STEP_AVX512(c, d, a, b, x[14], 0xa679438eu, 0xca, 17);
    MD5_STEP_AVX512(b, c, d, a, x[15], 0x49b40821u, 0xca, 22);
    MD5_STEP_AVX512(a, b, c, d, x[1], 0xf61e2562u, 0xe4, 5);
    MD5_STEP_AVX512(d, a, b, c, x[6], 0xc040b340u, 0xe4, 9);
    MD5_STEP_AVX512(c, d, a, b, x[11], 0x265e5a51u, 0xe4, 14);
    MD5_STEP_AVX512(b, c, d, a, x[0], 0xe9b6c7aau, 0xe4, 20);
    MD5_STEP_AVX512(a, b, c, d, x[5], 0xd62f105du, 0xe4, 5);
    MD5_STEP_AVX512(d, a, b, c, x[10], 0x02441453u, 0xe4, 9);
    MD5_STEP_AVX512(c, d, a, b, x[15], 0xd8a1e681u, 0xe4, 14);
    MD5_STEP_AVX512(b, c, d, a, x[4], 0xe7d3fbc8u, 0xe4, 20);
    MD5_STEP_AVX512(a, b, c, d, x[9], 0x21e1cde6u, 0xe4, 5);
    MD5_STEP_AVX512(d, a, b, c, x[14], 0xc33707d6u, 0xe4, 9);
    MD5_STEP_AVX512(c, d, a, b, x[3], 0xf4d50d87u, 0xe4, 14);
    MD5_STEP_AVX512(b, c, d, a, x[8], 0x455a14edu, 0xe4, 20);
    MD5_STEP_AVX512(a, b, c, d, x[13], 0xa9e3e905u, 0xe4, 5);
    MD5_STEP_AVX512(d, a, b, c, x[2], 0xfcefa3f8u, 0xe4, 9);
    MD5_STEP_AVX512(c, d, a, b, x[7], 0x676f02d9u, 0xe4, 14);
    MD5_STEP_AVX512(b, c, d, a, x[12], 0x8d2a4c8au, 0xe4, 20);
    MD5_STEP_AVX512(a, b, c, d, x[5], 0xfffa3942u, 0x96, 4);
    MD5_STEP_AVX512(d, a, b, c, x[8], 0x8771f681u, 0x96, 11);
    MD5_STEP_AVX512(c, d, a, b, x[11], 0x6d9d6122u, 0x96, 16);
    MD5_STEP_AVX512(b, c, d, a, x[14], 0xfde5380cu, 0x96, 23);
    MD5_STEP_AVX512(a, b, c, d, x[1], 0xa4beea44u, 0x96, 4);
    MD5_STEP_AVX512(d, a, b, c, x[4], 0x4bdecfa9u, 0x96, 11);
    MD5_STEP_AVX512(c, d, a, b, x[7], 0xf6bb4b60u, 0x96, 16);
    MD5_STEP_AVX512(b, c, d, a, x[10], 0xbebfbc70u, 0x96, 23);
    MD5_STEP_AVX512(a, b, c, d, x[13], 0x289b7ec6u, 0x96, 4);
    MD5_STEP_AVX512(d, a, b, c, x[0], 0xeaa127fau, 0x96, 11);
    MD5_STEP_AVX512(c, d, a, b, x[3], 0xd4ef3085u, 0x96, 16);
    MD5_STEP_AVX512(b, c, d, a, x[6], 0x04881d05u, 0x96, 23);
    MD5_STEP_AVX512(a, b, c, d, x[9], 0xd9d4d039u, 0x96, 4);
    MD5_STEP_AVX512(d, a, b, c, x[12], 0xe6db99e5u, 0x96, 11);
    MD5_STEP_AVX512(c, d, a, b, x[15], 0x1fa27cf8u, 0x96, 16);
    MD5_STEP_AVX512(b, c, d, a, x[2], 0xc4ac5665u, 0x96, 23);
    MD5_STEP_AVX512(a, b, c, d, x[0], 0xf4292244u, 0x39, 6);
    MD5_STEP_AVX512(d, a, b, c, x[7], 0x432aff97u, 0x39, 10);
    MD5_STEP_AVX512(c, d, a, b, x[14], 0xab9423a7u, 0x39, 15);
    MD5_STEP_AVX512(b, c, d, a, x[5], 0xfc93a039u, 0x39, 21);
    MD5_STEP_AVX512(a, b, c, d, x[12], 0x655b59c3u, 0x39, 6);
    MD5_STEP_AVX512(d, a, b, c, x[3], 0x8f0ccc92u, 0x39, 10);
    MD5_STEP_AVX512(c, d, a, b, x[10], 0xffeff47du, 0x39, 15);
    MD5_STEP_AVX512(b, c, d, a, x[1], 0x85845dd1u, 0x39, 21);
    MD5_STEP_AVX512(a, b, c, d, x[8], 0x6fa87e4fu, 0x39, 6);
    MD5_STEP_AVX512(d, a, b, c, x[15], 0xfe2ce6e0u, 0x39, 10);
    MD5_STEP_AVX512(c, d, a, b, x[6], 0xa3014314u, 0x39, 15);
    MD5_STEP_AVX512(b, c, d, a, x[13], 0x4e0811a1u, 0x39, 21);
    MD5_STEP_AVX512(a, b, c, d, x[4], 0xf7537e82u, 0x39, 6);
    MD5_STEP_AVX512(d, a, b, c, x[11], 0xbd3af235u, 0x39, 10);
    MD5_STEP_AVX512(c, d, a, b, x[2], 0x2ad7d2bbu, 0x39, 15);
    MD5_STEP_AVX512(b, c, d, a, x[9], 0xeb86d391u, 0x39, 21);

    a = _mm_add_epi32(a, aa);
    b = _mm_add_epi32(b, bb);
    c = _mm_add_epi32(c, cc);
    d = _mm_add_epi32(d, dd);
    data += 64;
  }

  state[0] = (MD5_WORD)_mm_cvtsi128_si32(a);
  state[1] = (MD5_WORD)_mm_cvtsi128_si32(b);
  state[2] = (MD5_WORD)_mm_cvtsi128_si32(c);
  state[3] = (MD5_WORD)_mm_cvtsi128_si32(d);
}

static int md5_cpu_has_avx512(void) {
  unsigned int eax, ebx, ecx, edx, xcr0_lo, xcr0_hi;
  if (__get_cpuid_max(0, 0) < 7) return 0;
  __cpuid(1, eax, ebx, ecx, edx);
  if (!(ecx & (1u << 27))) return 0;
  __cpuid_count(7, 0, eax, ebx, ecx, edx);
  if (!(ebx & (1u << 16)) || !(ebx & (1u << 31))) return 0;
  __asm__("xgetbv" : "=a"(xcr0_lo), "=d"(xcr0_hi) : "c"(0));
  (void)xcr0_hi;
  return (xcr0_lo & 0xE6) == 0xE6;
}
#endif

#if defined(BROKKR_MD5_X64_ASM)
  #include <intrin.h>

  #define MD5_HAVE_AVX512 1
  #define md5_blocks_avx512 md5_blocks_x64_avx512

static int md5_cpu_has_avx512(void) {
  int r[4];
  __cpuid(r, 0);
  if (r[0] < 7) return 0;
  __cpuid(r, 1);
  if (!(r[2] & (1 << 27))) return 0;
  __cpuidex(r, 7, 0);
  if (!(r[1] & (1 << 16)) || !(r[1] & (1u << 31))) return 0;
  return (_xgetbv(0) & 0xE6) == 0xE6;
}
#endif

static md5_blocks_fn md5_select_blocks(void) {
#if defined(MD5_HAVE_AVX512)
  if (md5_cpu_has_avx512()) return md5_blocks_avx512;
#endif
  return md5_blocks_base;
}

void md5_init(MD5_CTX* ctx) {
  ctx->datalen = 0;
  ctx->bitlen = 0;
  ctx->state[0] = 0x67452301;
  ctx->state[1] = 0xEFCDAB89;
  ctx->state[2] = 0x98BADCFE;
  ctx->state[3] = 0x10325476;
  ctx->blocks = md5_select_blocks();
}

void md5_update(MD5_CTX* ctx, const MD5_BYTE data[], size_t len) {
  ctx->bitlen += (unsigned long long)len << 3;

  if (ctx->datalen) {
    const size_t fill = 64 - ctx->datalen;
    if (len < fill) {
      memcpy(ctx->data + ctx->datalen, data, len);
      ctx->datalen += (MD5_WORD)len;
      return;
    }
    memcpy(ctx->data + ctx->datalen, data, fill);
    ctx->blocks(ctx->state, ctx->data, 1);
    ctx->datalen = 0;
    data += fill;
    len -= fill;
  }

  if (len >= 64) {
    ctx->blocks(ctx->state, data, len / 64);
    data += len & ~(size_t)63;
    len &= 63;
  }

  if (len) {
    memcpy(ctx->data, data, len);
    ctx->datalen = (MD5_WORD)len;
  }
}

void md5_final(MD5_CTX* ctx, MD5_BYTE hash[]) {
  const unsigned long long bits = ctx->bitlen;
  size_t i = ctx->datalen;

  ctx->data[i++] = 0x80;
  if (i > 56) {
    memset(ctx->data + i, 0, 64 - i);
    ctx->blocks(ctx->state, ctx->data, 1);
    i = 0;
  }
  memset(ctx->data + i, 0, 56 - i);
  for (i = 0; i < 8; ++i) ctx->data[56 + i] = (MD5_BYTE)(bits >> (8 * i));
  ctx->blocks(ctx->state, ctx->data, 1);

  for (i = 0; i < 4; ++i) {
    hash[i] = (MD5_BYTE)(ctx->state[0] >> (8 * i));
    hash[i + 4] = (MD5_BYTE)(ctx->state[1] >> (8 * i));
    hash[i + 8] = (MD5_BYTE)(ctx->state[2] >> (8 * i));
    hash[i + 12] = (MD5_BYTE)(ctx->state[3] >> (8 * i));
  }
}
