/* AbiWord
 * Copyright (C) 2026
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#pragma once

#include "ut_types.h"
#include "ut_bytebuf.h"

// BI_BITFIELDS. A 24-bit or 32-bit DIB with this compression stores 12 mask bytes.
static const UT_uint32 UT_DIB_BI_BITFIELDS = 3;
static const UT_sint32 UT_DIB_MAX_SIDE = 16384;

struct UT_ImagePixels
{
	const UT_Byte * rows = nullptr;
	UT_sint32 width = 0;
	UT_sint32 height = 0;
	UT_sint32 bpp = 0;
	bool topDown = false;
};

ABI_EXPORT bool UT_image_is_png(const UT_Byte * data, UT_uint32 size);
ABI_EXPORT bool UT_image_is_jpeg(const UT_Byte * data, UT_uint32 size);

// 24-bit bottom-up DIB. Alpha is flattened on white. A side above
// UT_DIB_MAX_SIDE is refused.
ABI_EXPORT bool UT_pack_dib24(const UT_ImagePixels & src, UT_ByteBuf & out);

// Byte offset of the pixels inside a packed DIB. False when the header,
// the side, or the palette would walk off the block.
ABI_EXPORT bool UT_dib_pixels_fit(UT_uint32 size, UT_uint32 headerBytes,
	UT_sint32 width, UT_sint32 height, UT_uint32 bitCount,
	UT_uint32 compression, UT_uint32 clrUsed, UT_uint32 & pixelOffset);

// DWORD-aligned byte count of a BI_RGB bitmap. A side above
// UT_DIB_MAX_SIDE, an unknown depth, or a product past 2 GiB is refused.
ABI_EXPORT bool UT_dib_storage_bytes(UT_sint32 width, UT_sint32 height,
	UT_uint32 bitCount, UT_uint32 & bytes);
