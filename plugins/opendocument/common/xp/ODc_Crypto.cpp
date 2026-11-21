/* Copyright (C) 2010 Marc Maurer <uwog@uwog.net>
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  
 * 02110-1301 USA.
 */

#include <stdlib.h>
#include <string.h>

#include <zlib.h>
#include <glib.h>

#include <gsf/gsf.h>

#include "sha1.h"
#include "gc-pbkdf2-sha1.h"

#include "ut_assert.h"
#include "ut_debugmsg.h"
#include "ODc_Crypto.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_GCRYPT
#include "gcrypt.h"
#endif

#define PASSWORD_HASH_LEN 20
#define PBKDF2_KEYLEN 16

// A missing manifest:size used to become 2^32-1 and was passed to g_malloc.
// 256 MiB covers a large content.xml or an embedded picture and rejects the rest.
static const UT_uint32 ODC_MAX_SALT = 1024u;
static const UT_uint32 ODC_MAX_IV = 64u;

static bool abi_odf_checksum_ok(const unsigned char* data, UT_uint32 size,
                                 const std::string& type, const std::string& b64)
{
	if (type.empty() || b64.empty() || !data)
		return false;

	gsize expect_len = 0;
	unsigned char* expect = g_base64_decode(b64.c_str(), &expect_len);
	if (!expect)
		return false;

	const UT_uint32 prefix = size < 1024u ? size : 1024u;
	unsigned char digest[32];
	size_t digest_len = 0;
	bool ok = false;

	if (g_ascii_strcasecmp(type.c_str(), "SHA1/1K") == 0 ||
	    g_ascii_strcasecmp(type.c_str(), "urn:oasis:names:tc:opendocument:xmlns:manifest:1.0#sha1-1k") == 0)
	{
		sha1_buffer(reinterpret_cast<const char*>(data), prefix, digest);
		digest_len = 20;
	}
#ifdef HAVE_GCRYPT
	else if (g_ascii_strcasecmp(type.c_str(), "urn:oasis:names:tc:opendocument:xmlns:manifest:1.0#sha256-1k") == 0)
	{
		gcry_md_hash_buffer(GCRY_MD_SHA256, digest, data, prefix);
		digest_len = 32;
	}
#endif

	if (digest_len != 0 && expect_len == digest_len)
		ok = memcmp(expect, digest, digest_len) == 0;
	g_free(expect);
	return ok;
}

UT_Error ODc_Crypto::performDecrypt(GsfInput* pStream, 
                                    unsigned char* salt, UT_uint32 salt_length, UT_uint32 iter_count,
                                    unsigned char* ivec, gsize ivec_length, const std::string& password, UT_uint32 decrypted_size,
                                    const std::string& checksum, const std::string& checksumType,
                                    GsfInput** pDecryptedInput)
{
	if (!salt || salt_length == 0 || salt_length > ODC_MAX_SALT)
		return UT_ERROR;
	if (!ivec || ivec_length == 0 || ivec_length > ODC_MAX_IV)
		return UT_ERROR;
	if (iter_count == 0 || iter_count > ODC_MAX_PBKDF2_ITERATIONS)
		return UT_ERROR;
	if (decrypted_size == 0 || decrypted_size > ODC_MAX_DECRYPTED_BYTES)
		return UT_ERROR;
	if (checksum.empty() || checksumType.empty())
		return UT_ERROR;

	unsigned char sha1_password[PASSWORD_HASH_LEN];
	char key[PBKDF2_KEYLEN];

	// get the sha1 sum of the password
	sha1_buffer(password.data(), password.size(), sha1_password);

	// create a PBKDF2 key from the sha1 sum
	int k = pbkdf2_sha1 ((const char*)sha1_password, PASSWORD_HASH_LEN, (const char*)salt, salt_length, iter_count, key, PBKDF2_KEYLEN);
	if (k != 0)
		return UT_ERROR;

    // Get the encrypted content ready
	const gsf_off_t raw_size = gsf_input_size(pStream);
	if (raw_size <= 0 || static_cast<gsf_off_t>(ODC_MAX_DECRYPTED_BYTES) < raw_size)
		return UT_ERROR;
	const UT_uint32 content_size = static_cast<UT_uint32>(raw_size);
	const unsigned char* content = gsf_input_read(pStream, content_size, nullptr);
	if (!content)
		return UT_ERROR;

	unsigned char* content_decrypted = static_cast<unsigned char*>(g_try_malloc(content_size));
	if (!content_decrypted)
		return UT_OUTOFMEM;
    
	// perform the actual decryption
#ifdef HAVE_GCRYPT

    UT_DEBUGMSG(("ODc_Crypto::performDecrypt() using gcrypt\n" ));

    gcry_cipher_hd_t h;
    gcry_error_t ge = gcry_cipher_open(&h, GCRY_CIPHER_BLOWFISH, GCRY_CIPHER_MODE_CFB, 0);
    if (ge != 0)
    {
        g_free(content_decrypted);
        return UT_ERROR;
    }
    ge = gcry_cipher_setkey(h, key, PBKDF2_KEYLEN);
    if (ge == 0)
        ge = gcry_cipher_setiv(h, ivec, ivec_length);
    if (ge == 0)
        ge = gcry_cipher_decrypt(h, content_decrypted, content_size, content, content_size);
    gcry_cipher_close(h);
    if (ge != 0)
    {
        g_free(content_decrypted);
        return UT_ERROR;
    }
    
    
#else

    // removed old blowfish code
    g_free(content_decrypted);
    return UT_ERROR;

#endif
    
	// deflate the decrypted content
	z_stream zs;
	zs.zalloc = nullptr;
	zs.zfree = nullptr;
	zs.opaque = nullptr;
	zs.avail_in = 0;
	zs.next_in = nullptr;

	int err;
	err = inflateInit2(&zs, -MAX_WBITS);
	if (err != Z_OK)
	{
		g_free(content_decrypted);
		return UT_ERROR;
	}

	unsigned char* decrypted = static_cast<unsigned char*>(g_try_malloc(decrypted_size));
	if (!decrypted)
	{
		inflateEnd(&zs);
		g_free(content_decrypted);
		return UT_OUTOFMEM;
	}
	zs.avail_in = content_size;
	zs.avail_out = decrypted_size;
	zs.next_in = content_decrypted;
	zs.next_out = decrypted;

	err = inflate(&zs, Z_FINISH);
	FREEP(content_decrypted);
	
	if (err != Z_STREAM_END)
	{
		inflateEnd(&zs);
		FREEP(decrypted);
		return UT_ERROR;
	}

	inflateEnd(&zs);

	// A declared size larger than the inflated stream would leave the tail
	// of the buffer uninitialized and would fail the checksum for the wrong reason.
	if (zs.total_out != decrypted_size)
	{
		FREEP(decrypted);
		return UT_ERROR;
	}

	if (!abi_odf_checksum_ok(decrypted, decrypted_size, checksumType, checksum))
	{
		FREEP(decrypted);
		return UT_ERROR;
	}

	*pDecryptedInput = gsf_input_memory_new(decrypted, decrypted_size, TRUE);
	
	return UT_OK;
}

UT_Error ODc_Crypto::decrypt(GsfInput* pStream, const ODc_CryptoInfo& cryptInfo,
    const std::string& password, GsfInput** pDecryptedInput)
{
	UT_return_val_if_fail(pStream, UT_ERROR);
	UT_return_val_if_fail(pDecryptedInput, UT_ERROR);
	
	// check if we support the requested decryption method
	UT_return_val_if_fail(g_ascii_strcasecmp(cryptInfo.m_algorithm.c_str(), "Blowfish CFB") == 0, UT_ERROR);
	UT_return_val_if_fail(g_ascii_strcasecmp(cryptInfo.m_keyType.c_str(), "PBKDF2") == 0, UT_ERROR);
	
	// base64 decode the salt
	gsize salt_length = 0;
	unsigned char* salt = g_base64_decode(cryptInfo.m_salt.c_str(), &salt_length);
	if (!salt)
		return UT_ERROR;

	// base64 decode the initialization vector
	gsize ivec_length = 0;
	unsigned char* ivec = g_base64_decode(cryptInfo.m_initVector.c_str(), &ivec_length);
	if (!ivec)
	{
		FREEP(salt);
		return UT_ERROR;
	}
	if (salt_length == 0 || salt_length > ODC_MAX_SALT || ivec_length == 0 || ivec_length > ODC_MAX_IV)
	{
		FREEP(salt);
		FREEP(ivec);
		return UT_ERROR;
	}

	// decrypt the content
	UT_Error result = performDecrypt(pStream, salt, static_cast<UT_uint32>(salt_length), cryptInfo.m_iterCount,  
                                     ivec, ivec_length, password, cryptInfo.m_decryptedSize,
                                     cryptInfo.m_checksum, cryptInfo.m_checksumType, pDecryptedInput);

	// cleanup
	FREEP(salt);
	FREEP(ivec);

	return result;
}
