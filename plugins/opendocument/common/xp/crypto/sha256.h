#ifndef ABI_SHA256_H
#define ABI_SHA256_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void abi_sha256_buffer(const void *data, size_t length, unsigned char digest[32]);

#ifdef __cplusplus
}
#endif

#endif
