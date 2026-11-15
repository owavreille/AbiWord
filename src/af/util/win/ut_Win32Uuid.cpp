/* AbiSource Program Utilities
 * Copyright (C) 2003-2004 Tomas Frydrych <tomasfrydrych@yahoo.co.uk>
 * 
 * Based on libuuid
 * Copyright (C) 1996, 1997, 1998 Theodore Ts'o.
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

#include "ut_Win32Uuid.h"
#include "ut_assert.h"
#include "ut_debugmsg.h"

HCRYPTPROV UT_Win32UUID::s_hProv = 0;
UT_sint32  UT_Win32UUID::s_iInstCount = 0;

UT_Win32UUID::~UT_Win32UUID ()
{
	UT_ASSERT( s_iInstCount > 0 );
	s_iInstCount--;
	
	if(s_iInstCount <= 0 && s_hProv)
	{
		CryptReleaseContext(s_hProv,0);
		// The handle is invalid after CryptReleaseContext. Leaving it set makes the
		// next UUID call CryptGenRandom on a released provider.
		s_hProv = 0;
	}
}


/*
 * Generate a series of random bytes. 
 */
bool UT_Win32UUID::_getRandomBytes(void *buf, UT_sint32 nbytes)
{
	if (nbytes > 0 && buf)
	{
		if (!s_hProv &&
			!CryptAcquireContextW(&s_hProv, nullptr, nullptr, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
		{
			// Failure does not yield a usable provider handle.
			s_hProv = 0;
		}
		if (s_hProv && CryptGenRandom(s_hProv, static_cast<DWORD>(nbytes), static_cast<BYTE *>(buf)))
			return true;
	}

#ifdef DEBUG
	wchar_t * wide = nullptr;
	char narrow[256];
	narrow[0] = '\0';
	DWORD ecode = GetLastError();
	if (FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
				  nullptr, ecode,
				  MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
				  (LPWSTR) &wide, 0, nullptr) != 0 && wide)
	{
		WideCharToMultiByte(CP_UTF8, 0, wide, -1, narrow, static_cast<int>(sizeof(narrow)), nullptr, nullptr);
		narrow[sizeof(narrow) - 1] = '\0';
		LocalFree(wide);
	}

	UT_DEBUGMSG(("UT_Win32UUID::_getRandomBytes: no Cryptographic services (0x%08lx; %s)\n",
				 static_cast<unsigned long>(ecode), narrow));
#endif
	return UT_UUID::_getRandomBytes(buf, nbytes);
}

