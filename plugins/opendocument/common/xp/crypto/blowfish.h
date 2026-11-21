#ifndef ABI_BLOWFISH_H
#define ABI_BLOWFISH_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct abi_blowfish_key {
	uint32_t P[18];
	uint32_t S[4][256];
} abi_blowfish_key;

int abi_blowfish_set_key(abi_blowfish_key *key, const unsigned char *data, size_t length);
void abi_blowfish_encrypt_block(const abi_blowfish_key *key,
	const unsigned char in[8], unsigned char out[8]);
void abi_blowfish_cfb8_decrypt(const abi_blowfish_key *key, const unsigned char iv[8],
	const unsigned char *in, unsigned char *out, size_t length);

#ifdef __cplusplus
}
#endif

#endif
