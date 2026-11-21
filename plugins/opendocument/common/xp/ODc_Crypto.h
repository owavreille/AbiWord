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

#pragma once

#include <string>

#include <gsf/gsf.h>

#include "ut_types.h"

// Shared with the manifest parser. A missing or hostile size must not reach g_malloc.
inline constexpr UT_uint32 ODC_MAX_DECRYPTED_BYTES = 256u * 1024u * 1024u;
inline constexpr UT_uint32 ODC_MAX_PBKDF2_ITERATIONS = 1000000u;

class ODc_CryptoInfo {
public:
	// stream information
	UT_uint32		m_decryptedSize;

	// algorithm information
	std::string		m_algorithm;
	std::string		m_initVector;
	std::string		m_checksum;
	std::string		m_checksumType;

	// key information
	std::string		m_keyType;
	UT_uint32		m_iterCount;
	std::string		m_salt;
	UT_uint32		m_derivedKeySize = 0;
	UT_uint32		m_startKeySize = 0;
	std::string		m_startKeyName;
};

class ODc_Crypto {
public:
	static UT_Error decrypt(GsfInput* pStream, const ODc_CryptoInfo& cryptInfo,
							const std::string& password, GsfInput** pDecryptedInput);

private:
	static UT_Error performDecrypt(GsfInput* pStream,	unsigned char* salt, UT_uint32 salt_length, UT_uint32 iter_count,
                                   unsigned char* ivec, gsize ivec_length, const std::string& password, UT_uint32 decrypted_size,
                                   const std::string& checksum, const std::string& checksumType, bool aes,
                                   UT_uint32 derived_key_size, UT_uint32 start_key_size, const std::string& start_key_name,
                                   GsfInput** pDecryptedInput);
};
