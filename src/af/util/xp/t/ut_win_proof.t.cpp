/* AbiWord
 * Copyright (C) 2026
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "tf_test.h"
#include "ut_unicode.h"
#include "ut_dib.h"
#include "ut_go_file.h"
#include "ut_bytebuf.h"

#define TFSUITE "core.af.util.winproof"

static UT_uint32 le32(const UT_Byte * p)
{
	return (UT_uint32)p[0] | ((UT_uint32)p[1] << 8) | ((UT_uint32)p[2] << 16) | ((UT_uint32)p[3] << 24);
}

TFTEST_MAIN("UTF-16 keeps a surrogate pair and drops an ignorable")
{
	UT_UCS2Char units[2] = { 0, 0 };
	TFPASS(UT_Unicode::UCS4_to_UTF16(0x1F600, units, true) == 2);
	TFPASS(units[0] == 0xD83D);
	TFPASS(units[1] == 0xDE00);
	TFPASS(UT_utf16_unit_advance(0, 12) == 12);
	TFPASS(UT_utf16_unit_advance(1, 12) == 0);

	TFPASS(UT_Unicode::UCS4_to_UTF16(0x200B, units, true) == 0);
	TFPASS(UT_Unicode::UCS4_to_UTF16(0xFEFF, units, true) == 0);
	TFPASS(UT_Unicode::UCS4_to_UTF16(0x200B, units, false) == 1);
	TFPASS(units[0] == 0x200B);

	TFPASS(UT_Unicode::UCS4_to_UTF16(0xD800, units, false) == 1);
	TFPASS(units[0] == 0xFFFD);
	TFPASS(UT_Unicode::UCS4_to_UTF16(0x110000, units, true) == 1);
	TFPASS(units[0] == 0xFFFD);
	TFPASS(UT_Unicode::UCS4_to_UTF16('A', units, true) == 1);
	TFPASS(units[0] == 'A');
}

TFTEST_MAIN("clipboard DIB flattens alpha on white and refuses a huge side")
{
	const UT_Byte png[] = { 0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a };
	const UT_Byte jpeg[] = { 0xFF, 0xD8, 0xFF, 0xE0 };
	TFPASS(UT_image_is_png(png, sizeof png));
	TFPASS(!UT_image_is_png(jpeg, sizeof jpeg));
	TFPASS(UT_image_is_jpeg(jpeg, sizeof jpeg));
	TFPASS(!UT_image_is_png(nullptr, 8));

	// Two top-down pixels, BGRA. Transparent red becomes white. Opaque blue stays.
	const UT_Byte pixels[] = {
		0, 0, 255, 0,
		255, 0, 0, 255
	};
	UT_ImagePixels src;
	src.rows = pixels;
	src.width = 1;
	src.height = 2;
	src.bpp = 4;
	src.topDown = true;
	UT_ByteBuf out;
	TFPASS(UT_pack_dib24(src, out));
	TFPASS(out.getLength() == 40 + 8);
	const UT_Byte * dib = out.getPointer(0);
	TFPASS(le32(dib) == 40);
	TFPASS(le32(dib + 4) == 1);
	TFPASS(le32(dib + 8) == 2);
	TFPASS(dib[14] == 24);
	// Bottom-up: the first stored row is the last source row, opaque blue.
	TFPASS(dib[40] == 255 && dib[41] == 0 && dib[42] == 0);
	TFPASS(dib[44] == 255 && dib[45] == 255 && dib[46] == 255);

	// Half-transparent BGRA (10, 20, 30, 128) flattens on white to (132, 137, 142).
	const UT_Byte half[] = { 10, 20, 30, 128 };
	src.rows = half;
	src.width = 1;
	src.height = 1;
	src.bpp = 4;
	src.topDown = true;
	TFPASS(UT_pack_dib24(src, out));
	TFPASS(out.getLength() == 44);
	dib = out.getPointer(0);
	TFPASS(dib[40] == 132 && dib[41] == 137 && dib[42] == 142);

	const UT_Byte plain[] = { 1, 2, 3 };
	src.rows = plain;
	src.bpp = 3;
	TFPASS(UT_pack_dib24(src, out));
	dib = out.getPointer(0);
	TFPASS(dib[40] == 1 && dib[41] == 2 && dib[42] == 3);

	src.width = UT_DIB_MAX_SIDE + 1;
	TFPASS(!UT_pack_dib24(src, out));
	TFPASS(out.getLength() == 0);

	UT_uint32 offset = 0;
	TFPASS(UT_dib_pixels_fit(40, 40, 8, 8, 24, 0, 0, offset) && offset == 40);
	TFPASS(UT_dib_pixels_fit(40 + 256 * 4, 40, 8, 8, 8, 0, 256, offset) && offset == 40 + 256 * 4);
	TFPASS(!UT_dib_pixels_fit(40, 40, 8, 8, 8, 0, 257, offset));
	TFPASS(!UT_dib_pixels_fit(40, 40, UT_DIB_MAX_SIDE + 1, 1, 24, 0, 0, offset));
	TFPASS(UT_dib_pixels_fit(52, 40, 8, -8, 32, UT_DIB_BI_BITFIELDS, 0, offset) && offset == 52);
	TFPASS(!UT_dib_pixels_fit(50, 40, 8, 8, 32, UT_DIB_BI_BITFIELDS, 0, offset));

	TFPASS(UT_image_page_y(5000) == 5000);
	TFPASS(UT_image_page_y(0) == 0);

	UT_uint32 bytes = 1;
	TFPASS(UT_dib_storage_bytes(8, 1, 24, bytes) && bytes == 24);
	TFPASS(UT_dib_storage_bytes(1, 1, 32, bytes) && bytes == 4);
	TFPASS(!UT_dib_storage_bytes(UT_DIB_MAX_SIDE + 1, 1, 24, bytes));
	TFPASS(!UT_dib_storage_bytes(8, 1, 12, bytes));
	TFPASS(bytes == 0);
}

TFTEST_MAIN("DPI scale matches MulDiv")
{
	TFPASS(UT_scale_by_dpi(-13, 96, 192) == -26);
	TFPASS(UT_scale_by_dpi(10, 96, 144) == 15);
	TFPASS(UT_scale_by_dpi(-11, 96, 144) == -17);
	TFPASS(UT_scale_by_dpi(12, 96, 96) == 12);
	TFPASS(UT_scale_by_dpi(12, 0, 192) == 12);
}

TFTEST_MAIN("ShellExecute failure is a result at or below 32")
{
	TFPASS(UT_go_shell_launch_failed(0));
	TFPASS(UT_go_shell_launch_failed(32));
	TFPASS(!UT_go_shell_launch_failed(33));
	TFPASS(UT_go_shell_launch_failed(-1));
}
