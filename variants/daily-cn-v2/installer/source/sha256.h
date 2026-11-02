#ifndef MER_SHA256_H
#define MER_SHA256_H
#include <stddef.h>
#include <stdint.h>
struct mer_sha256_ctx {uint32_t state[8];uint64_t bytes;unsigned char pending[64];size_t used;};
void mer_sha256_init(struct mer_sha256_ctx *ctx);
void mer_sha256_update(struct mer_sha256_ctx *ctx,const void *data,size_t size);
void mer_sha256_final(struct mer_sha256_ctx *ctx,char hex[65]);
void mer_sha256(const void *data, size_t size, char hex[65]);
#endif
