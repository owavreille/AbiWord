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
#include "sha256.h"
#include "blowfish.h"
#include "aes256.h"
#include "gc-pbkdf2-sha1.h"

#include "ut_assert.h"
#include "ut_debugmsg.h"
#include "ODc_Crypto.h"

#define PASSWORD_HASH_LEN 20
#define PBKDF2_KEYLEN 16

// A missing manifest:size used to become 2^32-1 and was passed to g_malloc.
// 256 MiB covers a large content.xml or an embedded picture and rejects the rest.
static const UT_uint32 ODC_MAX_SALT = 1024u;

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
	else if (g_ascii_strcasecmp(type.c_str(), "urn:oasis:names:tc:opendocument:xmlns:manifest:1.0#sha256-1k") == 0)
	{
		abi_sha256_buffer(data, prefix, digest);
		digest_len = 32;
	}

	if (digest_len != 0 && expect_len == digest_len)
		ok = memcmp(expect, digest, digest_len) == 0;
	g_free(expect);
	return ok;
}

UT_Error ODc_Crypto::performDecrypt(GsfInput* pStream, 
                                    unsigned char* salt, UT_uint32 salt_length, UT_uint32 iter_count,
                                    unsigned char* ivec, gsize ivec_length, const std::string& password, UT_uint32 decrypted_size,
                                    const std::string& checksum, const std::string& checksumType, bool aes,
                                    UT_uint32 derived_key_size, UT_uint32 start_key_size, const std::string& start_key_name,
                                    GsfInput** pDecryptedInput)
{
	const gsize expect_iv = aes ? 16 : 8;
	if (!salt || salt_length == 0 || salt_length > ODC_MAX_SALT)
		return UT_ERROR;
	if (!ivec || ivec_length != expect_iv)
		return UT_ERROR;
	if (iter_count == 0 || iter_count > ODC_MAX_PBKDF2_ITERATIONS)
		return UT_ERROR;
	if (decrypted_size == 0 || decrypted_size > ODC_MAX_DECRYPTED_BYTES)
		return UT_ERROR;
	if (checksum.empty() || checksumType.empty())
		return UT_ERROR;

	const bool sha256_start =
		start_key_name.find("sha256") != std::string::npos ||
		g_ascii_strcasecmp(start_key_name.c_str(), "SHA256") == 0;
	unsigned char start_key[32];
	size_t start_len = 0;
	if (sha256_start)
	{
		abi_sha256_buffer(password.data(), password.size(), start_key);
		start_len = 32;
	}
	else
	{
		sha1_buffer(password.data(), password.size(), start_key);
		start_len = PASSWORD_HASH_LEN;
	}
	if (start_key_size > 0 && start_key_size < start_len)
		start_len = start_key_size;

	UT_uint32 key_len = derived_key_size;
	if (key_len == 0)
		key_len = aes ? 32u : static_cast<UT_uint32>(PBKDF2_KEYLEN);
	if ((aes && key_len != 32) || (!aes && key_len != PBKDF2_KEYLEN))
		return UT_ERROR;

	unsigned char key[32];
	int k = pbkdf2_sha1(reinterpret_cast<const char*>(start_key), start_len,
		reinterpret_cast<const char*>(salt), salt_length, iter_count,
		reinterpret_cast<char*>(key), key_len);
	if (k != 0)
		return UT_ERROR;

    // Get the encrypted content ready
	const gsf_off_t raw_size = gsf_input_size(pStream);
	if (raw_size <= 0 || static_cast<gsf_off_t>(ODC_MAX_DECRYPTED_BYTES) < raw_size)
	{
		memset(key, 0, sizeof key);
		memset(start_key, 0, sizeof start_key);
		return UT_ERROR;
	}
	UT_uint32 content_size = static_cast<UT_uint32>(raw_size);
	const unsigned char* content = gsf_input_read(pStream, content_size, nullptr);
	if (!content)
	{
		memset(key, 0, sizeof key);
		memset(start_key, 0, sizeof start_key);
		return UT_ERROR;
	}

	unsigned char* content_decrypted = static_cast<unsigned char*>(g_try_malloc(content_size));
	if (!content_decrypted)
	{
		memset(key, 0, sizeof key);
		memset(start_key, 0, sizeof start_key);
		return UT_OUTOFMEM;
	}
    
	if (aes)
	{
		size_t plain_len = 0;
		if (abi_aes256_cbc_decrypt(key, ivec, content, content_size, content_decrypted, &plain_len) != 0)
		{
			memset(key, 0, sizeof key);
			memset(start_key, 0, sizeof start_key);
			g_free(content_decrypted);
			return UT_ERROR;
		}
		content_size = static_cast<UT_uint32>(plain_len);
	}
	else
	{
		abi_blowfish_key blowfish;
		if (abi_blowfish_set_key(&blowfish, key, PBKDF2_KEYLEN) != 0)
		{
			memset(key, 0, sizeof key);
			memset(start_key, 0, sizeof start_key);
			g_free(content_decrypted);
			return UT_ERROR;
		}
		abi_blowfish_cfb8_decrypt(&blowfish, ivec, content, content_decrypted, content_size);
		memset(&blowfish, 0, sizeof blowfish);
	}
	memset(key, 0, sizeof key);
	memset(start_key, 0, sizeof start_key);

	// ODF 1.3 §4.16.3: the digest covers the compressed plaintext, before inflate.
	if (!abi_odf_checksum_ok(content_decrypted, content_size, checksumType, checksum))
	{
		g_free(content_decrypted);
		return UT_ERROR;
	}

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

	*pDecryptedInput = gsf_input_memory_new(decrypted, decrypted_size, TRUE);
	
	return UT_OK;
}

UT_Error ODc_Crypto::decrypt(GsfInput* pStream, const ODc_CryptoInfo& cryptInfo,
    const std::string& password, GsfInput** pDecryptedInput)
{
	UT_return_val_if_fail(pStream, UT_ERROR);
	UT_return_val_if_fail(pDecryptedInput, UT_ERROR);
	
	const bool blowfish =
		g_ascii_strcasecmp(cryptInfo.m_algorithm.c_str(), "Blowfish CFB") == 0 ||
		g_ascii_strcasecmp(cryptInfo.m_algorithm.c_str(),
			"urn:oasis:names:tc:opendocument:xmlns:manifest:1.0#blowfish") == 0;
	const bool aes =
		g_ascii_strcasecmp(cryptInfo.m_algorithm.c_str(), "http://www.w3.org/2001/04/xmlenc#aes256-cbc") == 0;
	const bool pbkdf2 =
		g_ascii_strcasecmp(cryptInfo.m_keyType.c_str(), "PBKDF2") == 0 ||
		g_ascii_strcasecmp(cryptInfo.m_keyType.c_str(),
			"urn:oasis:names:tc:opendocument:xmlns:manifest:1.0#pbkdf2") == 0;
	if ((!blowfish && !aes) || !pbkdf2)
		return UT_ERROR;
	
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
	if (salt_length == 0 || salt_length > ODC_MAX_SALT || ivec_length != (aes ? 16 : 8))
	{
		FREEP(salt);
		FREEP(ivec);
		return UT_ERROR;
	}

	// decrypt the content
	UT_Error result = performDecrypt(pStream, salt, static_cast<UT_uint32>(salt_length), cryptInfo.m_iterCount,  
                                     ivec, ivec_length, password, cryptInfo.m_decryptedSize,
                                     cryptInfo.m_checksum, cryptInfo.m_checksumType, aes,
                                     cryptInfo.m_derivedKeySize, cryptInfo.m_startKeySize, cryptInfo.m_startKeyName,
                                     pDecryptedInput);

	// cleanup
	FREEP(salt);
	FREEP(ivec);

	return result;
}
