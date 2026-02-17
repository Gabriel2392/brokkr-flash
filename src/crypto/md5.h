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

#ifndef MD5_H
#define MD5_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MD5_BLOCK_SIZE 16

typedef unsigned char MD5_BYTE;
typedef unsigned int  MD5_WORD;

typedef struct {
   MD5_BYTE data[64];
   MD5_WORD datalen;
   unsigned long long bitlen;
   MD5_WORD state[4];
} MD5_CTX;

void md5_init(MD5_CTX *ctx);
void md5_update(MD5_CTX *ctx, const MD5_BYTE data[], size_t len);
void md5_final(MD5_CTX *ctx, MD5_BYTE hash[]);

#ifdef __cplusplus
}
#endif

#endif
