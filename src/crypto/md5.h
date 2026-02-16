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
