/* AbiWord
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "tf_test.h"
#include "ap_FontMenu.h"
#include "pd_Document.h"
#include "pf_Frag_Strux.h"
#include "pt_PieceTable.h"

#define TFSUITE "core.wp.ap.fontmenu"

static PT_DocPosition font_menu_block_content(PD_Document * doc)
{
	pt_PieceTable * table = doc ? doc->getPieceTable() : nullptr;
	if (!table)
		return 2;
	for (pf_Frag * frag = table->getFragments().getFirst(); frag; frag = frag->getNext())
	{
		if (frag->getType() != pf_Frag::PFT_Strux)
			continue;
		if (static_cast<pf_Frag_Strux *>(frag)->getStruxType() == PTX_Block)
			return frag->getPos() + frag->getLength();
	}
	return 2;
}

TFTEST_MAIN("Font menu keeps document faces above the installed list")
{
	TFPASS(AP_fontMenuIsSeparator(AP_FONT_MENU_SEPARATOR));
	TFPASS(!AP_fontMenuIsSeparator("Times New Roman"));
	TFPASS(!AP_fontMenuIsSeparator(nullptr));

	const std::vector<std::string> installed = {"Zebra", "Arial", "Courier New"};
	std::vector<std::string> bare;
	AP_buildFontMenu(nullptr, installed, bare);
	TFPASS(bare.size() == 3);
	TFPASS(bare[0] == "Arial");
	TFPASS(bare[1] == "Courier New");
	TFPASS(bare[2] == "Zebra");

	PD_Document * doc = new PD_Document();
	if (!TFPASS(doc->newDocument() == UT_OK))
	{
		doc->unref();
		return;
	}

	const PT_DocPosition at = font_menu_block_content(doc);
	const std::string words = "Bonjour";
	if (!TFPASS(doc->insertSpan(at, words)))
	{
		doc->unref();
		return;
	}
	const PP_PropertyVector props = {"font-family", "Courier New"};
	TFPASS(doc->changeSpanFmt(PTC_AddFmt, at, at + static_cast<PT_DocPosition>(words.size()),
		PP_NOPROPS, props));

	const std::string more = "X";
	const PT_DocPosition at2 = at + static_cast<PT_DocPosition>(words.size());
	if (!TFPASS(doc->insertSpan(at2, more)))
	{
		doc->unref();
		return;
	}
	const PP_PropertyVector quoted = {"font-family", "'Arial', sans-serif"};
	TFPASS(doc->changeSpanFmt(PTC_AddFmt, at2, at2 + static_cast<PT_DocPosition>(more.size()),
		PP_NOPROPS, quoted));

	std::vector<std::string> used;
	AP_collectUsedFonts(doc, used);
	bool sawCourier = false;
	bool sawArial = false;
	bool sawGeneric = false;
	for (const std::string & name : used)
	{
		if (name == "Courier New")
			sawCourier = true;
		if (name == "Arial")
			sawArial = true;
		if (name == "sans-serif" || name == "'Arial', sans-serif")
			sawGeneric = true;
	}
	TFPASS(sawCourier);
	TFPASS(sawArial);
	TFPASS(!sawGeneric);

	std::vector<std::string> menu;
	AP_buildFontMenu(doc, installed, menu);
	const std::vector<std::string>::iterator separator =
		std::find(menu.begin(), menu.end(), AP_FONT_MENU_SEPARATOR);
	TFPASS(separator != menu.end());
	TFPASS(std::find(menu.begin(), separator, std::string("Courier New")) != separator);
	TFPASS(std::find(menu.begin(), separator, std::string("Arial")) != separator);
	TFPASS(std::find(menu.begin(), separator, std::string("sans-serif")) == separator);

	const std::vector<std::string> tail(separator + 1, menu.end());
	TFPASS(tail.size() == 3);
	TFPASS(tail[0] == "Arial");
	TFPASS(tail[1] == "Courier New");
	TFPASS(tail[2] == "Zebra");
	doc->unref();
}
