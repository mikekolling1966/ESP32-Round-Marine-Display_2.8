#include <string.h>
#include "mbedtls/sha1.h"
#include "mbedtls/sha512.h"

void mbedtls_sha1_init(mbedtls_sha1_context *ctx) { memset(ctx, 0, sizeof(*ctx)); }
void mbedtls_sha1_free(mbedtls_sha1_context *ctx) { memset(ctx, 0, sizeof(*ctx)); }
void mbedtls_sha1_clone(mbedtls_sha1_context *dst, const mbedtls_sha1_context *src) { *dst = *src; }
int mbedtls_sha1_starts(mbedtls_sha1_context *ctx) { return 0; }
int mbedtls_sha1_update(mbedtls_sha1_context *ctx, const unsigned char *input, size_t ilen) { return 0; }
int mbedtls_sha1_finish(mbedtls_sha1_context *ctx, unsigned char output[20]) { memset(output, 0, 20); return 0; }

void mbedtls_sha512_init(mbedtls_sha512_context *ctx) { memset(ctx, 0, sizeof(*ctx)); }
void mbedtls_sha512_free(mbedtls_sha512_context *ctx) { memset(ctx, 0, sizeof(*ctx)); }
void mbedtls_sha512_clone(mbedtls_sha512_context *dst, const mbedtls_sha512_context *src) { *dst = *src; }
int mbedtls_sha512_starts(mbedtls_sha512_context *ctx, int is384) { return 0; }
int mbedtls_sha512_update(mbedtls_sha512_context *ctx, const unsigned char *input, size_t ilen) { return 0; }
int mbedtls_sha512_finish(mbedtls_sha512_context *ctx, unsigned char output[64]) { memset(output, 0, 64); return 0; }
