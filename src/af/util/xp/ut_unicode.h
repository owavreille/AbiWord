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

// A status pane used to be ten pixels per character. That width is the
// 96 DPI size. Measured text adds a gap so the glyphs are not clipped.
inline int UT_status_fallback_width (int chars, int dpi)
{
	if (chars < 0)
		chars = 0;
	return chars * UT_ui_px_at_dpi(10, dpi);
}

inline int UT_status_pane_width (int textPx, int dpi)
{
	if (textPx < 0)
		textPx = 0;
	const int width = textPx + UT_ui_px_at_dpi(16, dpi);
	const int floor = UT_ui_px_at_dpi(32, dpi);
	return width < floor ? floor : width;
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

// A keystroke id is 16 bits. An emoji above U+FFFF must not be stored in it.
inline UT_UCS4Char UT_keystroke_char (UT_UCS4Char ch)
{
	return ch > 0xFFFF ? static_cast<UT_UCS4Char>('a') : ch;
}

// A cell edge is exclusive. The neighbor exists while the edge is still
// inside the table, including the last column or row.
inline bool UT_table_span_continues (int edge, int count)
{
	return edge < count;
}

// A row deletion removes a cell only when the cell sits entirely in the
// deleted span. A cell that starts above that span is adjusted, not removed.
inline bool UT_cell_inside_row_span (int top, int bot, int spanTop, int spanEnd)
{
	return top >= spanTop && bot <= spanEnd;
}

// Without a page range, every page is printed. A range is clamped to the
// document. The first page is never below 1.
inline void UT_print_page_bounds (bool rangeSelected, int first, int last,
		int pageCount, int & outFirst, int & outLast)
{
	if (!rangeSelected)
	{
		outFirst = 1;
		outLast = pageCount;
		return;
	}
	outFirst = first < 1 ? 1 : first;
	outLast = last > pageCount ? pageCount : last;
}

// Dragging a vertical margin may leave the ruler by two widths. Any other
// drag stops at the ruler edge.
inline int UT_ruler_drag_slack (bool draggingMargin, int width)
{
	if (!draggingMargin || width <= 0)
		return 0;
	return width * 2;
}

// Windows reports a positive iDelta for the up arrow. The spin value moves
// the other way, so the applied step is the negation of that report.
inline int UT_updown_step (int reportedDelta)
{
	return -reportedDelta;
}

// A page scale is a percentage. The spin cannot leave the range 1..1000.
inline int UT_page_scale_step (int current, int delta)
{
	int next = current + delta;
	if (next < 1)
		return 1;
	if (next > 1000)
		return 1000;
	return next;
}

// The fraction is the distance from the start to the value, over the span.
// An empty span, or a value outside it, stays on 0 or 1.
inline double UT_progress_fraction (int start, int end, int value)
{
	const double denom = static_cast<double>(end) - static_cast<double>(start);
	if (denom <= 0.0001)
		return 0.0;
	double frac = (static_cast<double>(value) - static_cast<double>(start)) / denom;
	if (frac < 0.0)
		return 0.0;
	if (frac > 1.0)
		return 1.0;
	return frac;
}

inline bool UT_progress_changed (int previous, int value)
{
	return previous != value;
}

// A rooted Windows path starts with a slash or a drive letter. Anything else
// is relative to the AbiWord directory.
inline bool UT_win_path_is_rooted (const char * value)
{
	if (!value || !value[0])
		return false;
	if (value[0] == '/' || value[0] == '\\')
		return true;
	const unsigned char c = static_cast<unsigned char>(value[0]);
	const bool letter = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
	return letter && value[1] == ':';
}

// Save As rewrites the name when the chosen exporter wants another suffix.
// The comparison ignores ASCII case. An empty wanted suffix leaves the name.
inline bool UT_save_suffix_mismatch (const char * suffix, const char * wanted)
{
	if (!wanted || !wanted[0])
		return false;
	if (!suffix || !suffix[0])
		return true;
	while (*suffix && *wanted)
	{
		unsigned char a = static_cast<unsigned char>(*suffix);
		unsigned char b = static_cast<unsigned char>(*wanted);
		if (a >= 'A' && a <= 'Z')
			a = static_cast<unsigned char>(a - 'A' + 'a');
		if (b >= 'A' && b <= 'Z')
			b = static_cast<unsigned char>(b - 'A' + 'a');
		if (a != b)
			return true;
		++suffix;
		++wanted;
	}
	return *suffix != *wanted;
}

// A list box only fills the hyperlink field for a selection or a double click.
inline bool UT_is_list_choice (int notifyCode, int selectionChange, int doubleClick)
{
	return notifyCode == selectionChange || notifyCode == doubleClick;
}

// Choosing a first-line or hanging indent on a zero value starts at half an
// inch. A value already set is kept, and the dialog does not overwrite it.
inline double UT_special_indent_inches (double current, bool & useDefault)
{
	if (current == 0.0)
	{
		useDefault = true;
		return 0.5;
	}
	useDefault = false;
	return current;
}

// Margins that meet or pass the page edge do not fit.
inline bool UT_page_margins_fit (double left, double right, double top, double bottom,
		double width, double height)
{
	if (left + right >= width)
		return false;
	if (top + bottom >= height)
		return false;
	return true;
}

// An RTF character above U+00FF is \uN? with a signed 16-bit N. A value past
// the BMP is a surrogate pair. One ANSI byte has no unicode escape.
// The digits are written by hand: Windows headers turn snprintf into _snprintf.
inline int UT_rtf_append_int (char * dest, int capacity, int at, int value)
{
	char digits[16];
	int n = 0;
	const bool neg = value < 0;
	unsigned int mag = neg ? static_cast<unsigned int>(-value) : static_cast<unsigned int>(value);
	do
	{
		digits[n++] = static_cast<char>('0' + (mag % 10u));
		mag /= 10u;
	} while (mag && n < static_cast<int>(sizeof digits));
	if (neg && n < static_cast<int>(sizeof digits))
		digits[n++] = '-';
	if (at + n >= capacity)
		return -1;
	while (n > 0)
		dest[at++] = digits[--n];
	dest[at] = 0;
	return at;
}

inline int UT_rtf_append_text (char * dest, int capacity, int at, const char * text)
{
	if (!text)
		return at;
	while (*text)
	{
		if (at + 1 >= capacity)
			return -1;
		dest[at++] = *text++;
	}
	dest[at] = 0;
	return at;
}

inline int UT_rtf_unicode_escape (UT_UCS4Char wc, char * dest, int capacity)
{
	if (!dest || capacity < 8)
		return 0;
	dest[0] = 0;
	if (wc <= 0x00ff || wc > 0x10ffff)
		return 0;
	int at = UT_rtf_append_text(dest, capacity, 0, "\\u");
	if (wc <= 0xffff)
	{
		const int tmp = static_cast<int>(static_cast<signed short>(static_cast<unsigned short>(wc)));
		at = UT_rtf_append_int(dest, capacity, at, tmp);
		at = UT_rtf_append_text(dest, capacity, at, "?");
	}
	else
	{
		const UT_UCS4Char value = wc - 0x10000;
		const int high = static_cast<int>(static_cast<signed short>(
			static_cast<unsigned short>(0xD800 + (value >> 10))));
		const int low = static_cast<int>(static_cast<signed short>(
			static_cast<unsigned short>(0xDC00 + (value & 0x3FF))));
		at = UT_rtf_append_int(dest, capacity, at, high);
		at = UT_rtf_append_text(dest, capacity, at, "?\\u");
		at = UT_rtf_append_int(dest, capacity, at, low);
		at = UT_rtf_append_text(dest, capacity, at, "?");
	}
	if (at < 0)
	{
		dest[0] = 0;
		return 0;
	}
	return at;
}

// A scaled RTF image is a fraction of the goal in twips, or of the pixel
// size at 96 DPI when the goal is absent. scalePercent is 100 for the
// natural size.
inline double UT_rtf_scaled_inches (int scalePercent, int goalTwips, int pixels, bool useGoal)
{
	const double scale = static_cast<double>(scalePercent) / 100.0;
	if (useGoal)
		return scale * (static_cast<double>(goalTwips) / 1440.0);
	return scale * (static_cast<double>(pixels) / 96.0);
}
