/* -*- mode: C++; tab-width: 4; c-basic-offset: 4; -*- */

// UT_Unicode.h

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

#pragma once
#include "ut_types.h"

/** contain various unicode function. Just a namespace */
class ABI_EXPORT UT_Unicode
{
public:
/** scans a buffer for the next valid UTF-8 sequence and returns the corresponding
 * UCS-4 value for that sequence; the pointer and length-remaining are incremented
 * and decremented respectively; returns 0 if no valid UTF-8 sequence found by the
 * end of the string
 */
	static UT_UCS4Char UTF8_to_UCS4 (const char *& buffer, size_t & length);


/** Returns -1 if ucs4 is not valid UCS-4, 0 if ucs4 is 0, 1-6 otherwise
 */
	static int UTF8_ByteLength (UT_UCS4Char ucs4);

/** appends to the buffer the UTF-8 sequence corresponding to the UCS-4 value;
 * the pointer and length-remaining are incremented and decremented respectively;
 * returns false if not valid UCS-4 or if (length < UTF8_ByteLength (ucs4))
 */
	static bool UCS4_to_UTF8 (char *& buffer, size_t & length, UT_UCS4Char ucs4);

	// UTF-16 for the Windows text drawers. dropIgnorable skips U+200B and
	// U+FEFF. A lone surrogate or a value past U+10FFFF becomes U+FFFD.
	// Returns 0, 1 or 2 units written to out.
	static int UCS4_to_UTF16 (UT_UCS4Char ucs4, UT_UCS2Char out[2], bool dropIgnorable);
};

// The second UTF-16 unit of a surrogate pair has no extra advance.
inline int UT_utf16_unit_advance (int unitIndex, int cellAdvance)
{
	return unitIndex == 0 ? cellAdvance : 0;
}

// Device Y stays page-local. Folding it with the physical page height
// sent a picture past the paper edge back to the top.
inline UT_sint32 UT_image_page_y (UT_sint32 yDest)
{
	return yDest;
}

// Same rounding as MulDiv: add half the divisor, then divide toward zero.
// A dpi of zero, or an overflow, leaves the value unchanged.
inline int UT_scale_by_dpi (int value, int fromDpi, int toDpi)
{
	if (fromDpi <= 0 || toDpi <= 0 || fromDpi == toDpi)
		return value;
	const long long divisor = fromDpi;
	long long scaled = static_cast<long long>(value) * static_cast<long long>(toDpi);
	if (scaled >= 0)
		scaled += divisor / 2;
	else
		scaled -= divisor / 2;
	scaled /= divisor;
	if (scaled > 2147483647LL || scaled < -2147483647LL)
		return value;
	return static_cast<int>(scaled);
}

// A size chosen at 96 DPI. Below 96 the toolbar keeps the 96 size.
inline int UT_ui_px_at_dpi (int pxAt96, int dpi)
{
	if (dpi < 96)
		dpi = 96;
	const int scaled = UT_scale_by_dpi(pxAt96, 96, dpi);
	if (pxAt96 > 0 && scaled < 1)
		return 1;
	return scaled;
}

// Copy whole UTF-8 characters that fit. capacity is the byte size of dest,
// including the NUL that is always written when capacity is positive.
inline void UT_copy_utf8_bounded (char * dest, int capacity, const char * utf8)
{
	if (!dest || capacity <= 0)
		return;
	if (!utf8)
	{
		dest[0] = 0;
		return;
	}

	int n = 0;
	const int max = capacity - 1;
	while (utf8[n] && n < max)
	{
		const unsigned char c = static_cast<unsigned char>(utf8[n]);
		int clen = 1;
		if ((c & 0x80) == 0)
			clen = 1;
		else if ((c & 0xe0) == 0xc0)
			clen = 2;
		else if ((c & 0xf0) == 0xe0)
			clen = 3;
		else if ((c & 0xf8) == 0xf0)
			clen = 4;
		else
			break;

		if (n + clen > max)
			break;

		bool complete = true;
		for (int i = 1; i < clen; ++i)
		{
			if (utf8[n + i] == 0)
			{
				complete = false;
				break;
			}
		}
		if (!complete)
			break;

		n += clen;
	}

	for (int i = 0; i < n; ++i)
		dest[i] = utf8[i];
	dest[n] = 0;
}
