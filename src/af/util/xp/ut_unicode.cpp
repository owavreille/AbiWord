/* -*- mode: C++; tab-width: 4; c-basic-offset: 4; -*- */

// UT_Unicode.cpp

// Copyright (C) 2001 Mike Nordell <tamlin@algonet.se>
// Copyright (c) 2007 Hubert Figuiere <hub@figuiere.net>
// 
// This class is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
// 
// This class is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  
// 02110-1301 USA.
//


#include <glib.h>

#include "ut_assert.h"
#include "ut_unicode.h"


/* Returns -1 if ucs4 is not valid UCS-4, 1-6 otherwise
 */
int UT_Unicode::UTF8_ByteLength (UT_UCS4Char u)
{
	return g_unichar_to_utf8 (u, nullptr);
}

/* appends to the buffer the UTF-8 sequence corresponding to the UCS-4 value;
 * the pointer and length-remaining are incremented and decremented respectively;
 * returns false if not valid UCS-4 or if (length < UTF8_ByteLength (ucs4))
 */
bool UT_Unicode::UCS4_to_UTF8 (char *& buffer, size_t & length, UT_UCS4Char ucs4)
{
	gchar buf [6];
	int seql = g_unichar_to_utf8 (ucs4, &buf[0]);
	
	if (length < static_cast<unsigned>(seql)) return false;

	length -= seql;
	for (int i = 0; i < seql; ++i)
	{
		*buffer++ = buf[i];
	}
	
	return true;
}

int UT_Unicode::UCS4_to_UTF16 (UT_UCS4Char ucs4, UT_UCS2Char out[2], bool dropIgnorable)
{
	if (!out)
		return 0;
	if (dropIgnorable && (ucs4 == 0x200B || ucs4 == 0xFEFF))
		return 0;
	if (ucs4 > 0x10FFFF || (ucs4 >= 0xD800 && ucs4 <= 0xDFFF))
		ucs4 = 0xFFFD;
	if (ucs4 <= 0xFFFF)
	{
		out[0] = static_cast<UT_UCS2Char>(ucs4);
		return 1;
	}
	ucs4 -= 0x10000;
	out[0] = static_cast<UT_UCS2Char>(0xD800 + ((ucs4 >> 10) & 0x3FF));
	out[1] = static_cast<UT_UCS2Char>(0xDC00 + (ucs4 & 0x3FF));
	return 2;
}

/** scans a buffer for the next valid UTF-8 sequence and returns the corresponding
 * UCS-4 value for that sequence; the pointer and length-remaining are incremented
 * and decremented respectively; returns 0 if no valid UTF-8 sequence found by the
 * end of the string
 */
UT_UCS4Char UT_Unicode::UTF8_to_UCS4 (const char *& buffer, size_t & length)
{
	if(!buffer || !length)
		return 0;

	UT_UCS4Char ucs4 = g_utf8_get_char_validated (buffer, length);

	if ((UT_sint32)ucs4 == -1 || (UT_sint32)ucs4 == -2)
		return 0;

	const char * p = g_utf8_next_char (buffer);
	UT_uint32 diff = p - buffer;

	buffer += diff;
	length -= diff;

	return ucs4;
}

