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
#include <string.h>
#include "ut_dib.h"
#include "ut_go_file.h"
#include "ut_bytebuf.h"
#include "ut_mbtowc.h"
#include "ut_units.h"

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

	TFPASS(UT_ui_px_at_dpi(22, 96) == 22);
	TFPASS(UT_ui_px_at_dpi(22, 192) == 44);
	TFPASS(UT_ui_px_at_dpi(22, 144) == 33);
	TFPASS(UT_ui_px_at_dpi(6, 192) == 12);
	TFPASS(UT_ui_px_at_dpi(4, 192) == 8);
	TFPASS(UT_ui_px_at_dpi(20, 48) == 20);
	TFPASS(UT_ui_px_at_dpi(0, 192) == 0);
	TFPASS(UT_status_fallback_width(8, 96) == 80);
	TFPASS(UT_status_fallback_width(8, 192) == 160);
	TFPASS(UT_status_fallback_width(0, 144) == 0);
	TFPASS(UT_status_pane_width(60, 96) == 76);
	TFPASS(UT_status_pane_width(120, 192) == 152);
	TFPASS(UT_status_pane_width(0, 96) == 32);

	char bounded[4];
	UT_copy_utf8_bounded(bounded, 4, "abcd");
	TFPASS(strcmp(bounded, "abc") == 0);
	const char accent[] = "\xc3\xa9";
	UT_copy_utf8_bounded(bounded, 2, accent);
	TFPASS(bounded[0] == 0);
	UT_copy_utf8_bounded(bounded, 3, accent);
	TFPASS(strcmp(bounded, accent) == 0);
	UT_copy_utf8_bounded(bounded, 0, "a");
	UT_copy_utf8_bounded(nullptr, 4, "a");
	UT_copy_utf8_bounded(bounded, 4, nullptr);
	TFPASS(bounded[0] == 0);
}

TFTEST_MAIN("An emoji keystroke stays inside 16 bits")
{
	TFPASS(UT_keystroke_char('a') == static_cast<UT_UCS4Char>('a'));
	TFPASS(UT_keystroke_char(0xE9) == 0xE9);
	TFPASS(UT_keystroke_char(0xFFFF) == 0xFFFF);
	TFPASS(UT_keystroke_char(0x1F600) == static_cast<UT_UCS4Char>('a'));
	TFPASS(UT_keystroke_char(0) == 0);
}

TFTEST_MAIN("UTF-8 paste keeps a partial character")
{
	UT_UCS4_mbtowc conv("UTF-8");
	UT_UCS4Char wc = 0;
	TFPASS(conv.mbtowc(wc, static_cast<char>(0xC3)) == 0);
	TFPASS(conv.mbtowc(wc, static_cast<char>(0xA9)) == 1);
	TFPASS(wc == 0xE9);

	wc = 0;
	TFPASS(conv.mbtowc(wc, static_cast<char>(0xF0)) == 0);
	TFPASS(conv.mbtowc(wc, static_cast<char>(0x9F)) == 0);
	TFPASS(conv.mbtowc(wc, static_cast<char>(0x98)) == 0);
	TFPASS(conv.mbtowc(wc, static_cast<char>(0x80)) == 1);
	TFPASS(wc == 0x1F600);

	wc = 0x1111;
	TFPASS(conv.mbtowc(wc, 'A') == 1);
	TFPASS(wc == static_cast<UT_UCS4Char>('A'));
}

TFTEST_MAIN("A print range is clamped and a whole document is every page")
{
	int first = 0;
	int last = 0;
	UT_print_page_bounds(false, 4, 9, 12, first, last);
	TFPASS(first == 1);
	TFPASS(last == 12);

	UT_print_page_bounds(true, 0, 40, 12, first, last);
	TFPASS(first == 1);
	TFPASS(last == 12);

	UT_print_page_bounds(true, 3, 5, 12, first, last);
	TFPASS(first == 3);
	TFPASS(last == 5);
}

TFTEST_MAIN("A margin drag may leave the ruler and a cell stops at the table edge")
{
	TFPASS(UT_ruler_drag_slack(false, 40) == 0);
	TFPASS(UT_ruler_drag_slack(true, 40) == 80);
	TFPASS(UT_ruler_drag_slack(true, 0) == 0);

	TFPASS(UT_table_span_continues(3, 4));
	TFPASS(!UT_table_span_continues(4, 4));
	TFPASS(!UT_table_span_continues(3, 3));

	TFPASS(UT_cell_inside_row_span(2, 3, 2, 4));
	TFPASS(!UT_cell_inside_row_span(1, 3, 2, 4));
	TFPASS(!UT_cell_inside_row_span(2, 5, 2, 4));
}

TFTEST_MAIN("A page scale stays in range and an up arrow steps down")
{
	TFPASS(UT_updown_step(1) == -1);
	TFPASS(UT_updown_step(-3) == 3);
	TFPASS(UT_page_scale_step(100, 1) == 101);
	TFPASS(UT_page_scale_step(1, -5) == 1);
	TFPASS(UT_page_scale_step(1000, 5) == 1000);
	TFPASS(UT_page_scale_step(0, 0) == 1);
}

TFTEST_MAIN("A progress fraction stays between zero and one")
{
	TFPASS(UT_progress_fraction(0, 100, 25) == 0.25);
	TFPASS(UT_progress_fraction(10, 20, 10) == 0.0);
	TFPASS(UT_progress_fraction(10, 20, 25) == 1.0);
	TFPASS(UT_progress_fraction(10, 20, 5) == 0.0);
	TFPASS(UT_progress_fraction(5, 5, 9) == 0.0);
	TFPASS(!UT_progress_changed(3, 3));
	TFPASS(UT_progress_changed(3, 4));
}

TFTEST_MAIN("Save As replaces another suffix and a drive path is rooted")
{
	TFPASS(!UT_save_suffix_mismatch(".doc", ".doc"));
	TFPASS(!UT_save_suffix_mismatch(".DOC", ".doc"));
	TFPASS(UT_save_suffix_mismatch(".rtf", ".doc"));
	TFPASS(UT_save_suffix_mismatch("", ".doc"));
	TFPASS(UT_save_suffix_mismatch(nullptr, ".doc"));
	TFPASS(!UT_save_suffix_mismatch(".rtf", ""));
	TFPASS(!UT_save_suffix_mismatch(".rtf", nullptr));

	TFPASS(!UT_win_path_is_rooted(nullptr));
	TFPASS(!UT_win_path_is_rooted(""));
	TFPASS(!UT_win_path_is_rooted("notes.abw"));
	TFPASS(UT_win_path_is_rooted("/tmp/notes.abw"));
	TFPASS(UT_win_path_is_rooted("\\\\server\\share"));
	TFPASS(UT_win_path_is_rooted("C:\\docs"));
	TFPASS(UT_win_path_is_rooted("c:/docs"));
	TFPASS(!UT_win_path_is_rooted("1:\\no"));
}

TFTEST_MAIN("A list notice is a selection or a double click")
{
	TFPASS(UT_is_list_choice(1, 1, 2));
	TFPASS(UT_is_list_choice(2, 1, 2));
	TFPASS(!UT_is_list_choice(0, 1, 2));
	TFPASS(!UT_is_list_choice(3, 1, 2));
}

TFTEST_MAIN("A zero special indent is half an inch and margins must fit")
{
	bool useDefault = false;
	TFPASS(UT_special_indent_inches(0.0, useDefault) == 0.5);
	TFPASS(useDefault);
	TFPASS(UT_special_indent_inches(1.25, useDefault) == 1.25);
	TFPASS(!useDefault);

	const char * room = UT_indent_room_string(DIM_CM, 8.0, 2.54);
	TFPASS(room && strcmp(room, "17.78cm") == 0);

	TFPASS(UT_page_margins_fit(1.0, 1.0, 1.0, 1.0, 8.5, 11.0));
	TFPASS(!UT_page_margins_fit(5.0, 4.0, 1.0, 1.0, 8.5, 11.0));
	TFPASS(!UT_page_margins_fit(1.0, 1.0, 6.0, 6.0, 8.5, 11.0));
	TFPASS(!UT_page_margins_fit(4.0, 4.0, 1.0, 1.0, 8.0, 11.0));
}

TFTEST_MAIN("An RTF character above 255 is a unicode escape and a pixel image is inches")
{
	char escaped[64];
	TFPASS(UT_rtf_unicode_escape(0xE9, escaped, 64) == 0);
	TFPASS(UT_rtf_unicode_escape(0x100, escaped, 64) > 0);
	TFPASS(strcmp(escaped, "\\u256?") == 0);
	TFPASS(UT_rtf_unicode_escape(0x1F600, escaped, 64) > 0);
	TFPASS(strcmp(escaped, "\\u-10179?\\u-8704?") == 0);
	TFPASS(UT_rtf_unicode_escape(0x10FFFF, escaped, 64) > 0);
	TFPASS(strcmp(escaped, "\\u-9217?\\u-8193?") == 0);
	TFPASS(UT_rtf_unicode_escape(0x110000, escaped, 64) == 0);

	TFPASS(UT_rtf_scaled_inches(100, 1440, 96, true) == 1.0);
	TFPASS(UT_rtf_scaled_inches(200, 720, 96, true) == 1.0);
	TFPASS(UT_rtf_scaled_inches(100, 0, 96, false) == 1.0);
	TFPASS(UT_rtf_scaled_inches(50, 0, 96, false) == 0.5);
}

TFTEST_MAIN("ShellExecute failure is a result at or below 32")
{
	TFPASS(UT_go_shell_launch_failed(0));
	TFPASS(UT_go_shell_launch_failed(32));
	TFPASS(!UT_go_shell_launch_failed(33));
	TFPASS(UT_go_shell_launch_failed(-1));
}
