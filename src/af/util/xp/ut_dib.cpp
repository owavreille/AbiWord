/* AbiWord
 * Copyright (C) 2026
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include <cstring>

#include "ut_dib.h"

bool UT_image_is_png(const UT_Byte * data, UT_uint32 size)
{
	return data && size >= 8
		&& data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G';
}

bool UT_image_is_jpeg(const UT_Byte * data, UT_uint32 size)
{
	return data && size >= 3 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF;
}

static void s_poke32(UT_Byte * out, UT_uint32 value)
{
	out[0] = static_cast<UT_Byte>(value & 0xff);
	out[1] = static_cast<UT_Byte>((value >> 8) & 0xff);
	out[2] = static_cast<UT_Byte>((value >> 16) & 0xff);
	out[3] = static_cast<UT_Byte>((value >> 24) & 0xff);
}

static void s_poke16(UT_Byte * out, UT_uint32 value)
{
	out[0] = static_cast<UT_Byte>(value & 0xff);
	out[1] = static_cast<UT_Byte>((value >> 8) & 0xff);
}

bool UT_pack_dib24(const UT_ImagePixels & src, UT_ByteBuf & out)
{
	out.truncate(0);
	if (!src.rows || src.width <= 0 || src.height <= 0)
		return false;
	if (src.width > UT_DIB_MAX_SIDE || src.height > UT_DIB_MAX_SIDE)
		return false;
	if (src.bpp != 3 && src.bpp != 4)
		return false;

	const UT_uint32 srcRow = static_cast<UT_uint32>((src.width * src.bpp + 3) & ~3);
	const UT_uint32 dstRow = static_cast<UT_uint32>((src.width * 3 + 3) & ~3);
	const UT_uint64 total = 40ull + static_cast<UT_uint64>(dstRow) * static_cast<UT_uint64>(src.height);
	if (total == 0 || total > 0x7fffffffu)
		return false;

	UT_Byte header[40];
	memset(header, 0, sizeof(header));
	s_poke32(header + 0, 40);
	s_poke32(header + 4, static_cast<UT_uint32>(src.width));
	s_poke32(header + 8, static_cast<UT_uint32>(src.height));
	s_poke16(header + 12, 1);
	s_poke16(header + 14, 24);
	s_poke32(header + 20, dstRow * static_cast<UT_uint32>(src.height));
	if (!out.append(header, 40))
		return false;

	UT_Byte * row = new UT_Byte[dstRow];
	bool ok = true;
	for (UT_sint32 y = 0; ok && y < src.height; ++y)
	{
		const UT_sint32 srcY = src.topDown ? (src.height - 1 - y) : y;
		const UT_Byte * srow = src.rows + static_cast<size_t>(srcY) * srcRow;
		memset(row, 0, dstRow);
		for (UT_sint32 x = 0; x < src.width; ++x)
		{
			int b = srow[x * src.bpp + 0];
			int g = srow[x * src.bpp + 1];
			int r = srow[x * src.bpp + 2];
			if (src.bpp == 4)
			{
				const int a = srow[x * src.bpp + 3];
				const int inv = 255 - a;
				b = (b * a + 255 * inv + 127) / 255;
				g = (g * a + 255 * inv + 127) / 255;
				r = (r * a + 255 * inv + 127) / 255;
			}
			row[x * 3 + 0] = static_cast<UT_Byte>(b);
			row[x * 3 + 1] = static_cast<UT_Byte>(g);
			row[x * 3 + 2] = static_cast<UT_Byte>(r);
		}
		ok = out.append(row, dstRow);
	}
	delete [] row;
	if (!ok)
		out.truncate(0);
	return ok;
}

bool UT_dib_pixels_fit(UT_uint32 size, UT_uint32 headerBytes,
	UT_sint32 width, UT_sint32 height, UT_uint32 bitCount,
	UT_uint32 compression, UT_uint32 clrUsed, UT_uint32 & pixelOffset)
{
	pixelOffset = headerBytes;
	if (size < 40 || headerBytes < 40 || headerBytes > size
		|| width <= 0 || width > UT_DIB_MAX_SIDE || height == 0)
		return false;
	const UT_sint32 absHeight = height < 0 ? -height : height;
	if (absHeight > UT_DIB_MAX_SIDE)
		return false;

	UT_uint32 extra = 0;
	if (clrUsed)
	{
		if (clrUsed > 256)
			return false;
		extra = clrUsed * 4;
	}
	else if (bitCount > 0 && bitCount <= 8)
		extra = (1u << bitCount) * 4;
	else if (compression == UT_DIB_BI_BITFIELDS && headerBytes == 40)
		extra = 12;

	if (extra > size - pixelOffset)
		return false;
	pixelOffset += extra;
	return true;
}

bool UT_dib_storage_bytes(UT_sint32 width, UT_sint32 height, UT_uint32 bitCount, UT_uint32 & bytes)
{
	bytes = 0;
	if (width <= 0 || height <= 0 || width > UT_DIB_MAX_SIDE || height > UT_DIB_MAX_SIDE)
		return false;
	if (bitCount != 1 && bitCount != 4 && bitCount != 8
		&& bitCount != 16 && bitCount != 24 && bitCount != 32)
		return false;
	const UT_uint64 stride = (((static_cast<UT_uint64>(width) * bitCount) + 31) / 32) * 4;
	const UT_uint64 total = stride * static_cast<UT_uint64>(height);
	if (total == 0 || total > 0x7fffffffu)
		return false;
	bytes = static_cast<UT_uint32>(total);
	return true;
}
