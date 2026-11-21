#ifndef ABI_AES256_H
#define ABI_AES256_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* AES-256-CBC decryption with PKCS#7 padding removed.
 * out may alias in. *out_len receives the unpadded length.
 * Returns 0 on success.
 */
int abi_aes256_cbc_decrypt(const unsigned char key[32], const unsigned char iv[16],
	const unsigned char *in, size_t in_len, unsigned char *out, size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif
