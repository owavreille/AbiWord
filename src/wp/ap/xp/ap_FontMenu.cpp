/* AbiWord
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include <algorithm>
#include <cstring>
#include <set>

#include <glib.h>

#include "ap_FontMenu.h"
#include "pd_Document.h"
#include "pf_Frag_Strux.h"
#include "pp_Property.h"
#include "pt_PieceTable.h"
#include "xap_Frame.h"
#include "xad_Document.h"

bool AP_fontMenuIsSeparator(const char * name)
{
	return name && strcmp(name, AP_FONT_MENU_SEPARATOR) == 0;
}

PD_Document * AP_documentFromFrame(XAP_Frame * frame)
{
	if (!frame)
		return nullptr;
	AD_Document * doc = frame->getCurrentDoc();
	if (!doc || doc->getType() != ADDOCUMENT_ABIWORD)
		return nullptr;
	return static_cast<PD_Document *>(doc);
}

static bool fontNameLess(const std::string & left, const std::string & right)
{
	return g_utf8_collate(left.c_str(), right.c_str()) < 0;
}

static void rememberFont(const char * name, std::set<std::string> & seen, std::vector<std::string> & fonts)
{
	if (!name || !*name || AP_fontMenuIsSeparator(name))
		return;
	if (seen.insert(name).second)
		fonts.push_back(name);
}

void AP_collectUsedFonts(PD_Document * doc, std::vector<std::string> & fonts)
{
	fonts.clear();
	pt_PieceTable * table = doc ? doc->getPieceTable() : nullptr;
	if (!table)
		return;

	struct UsedFontCache
	{
		const PD_Document * doc;
		const pf_Frag * stamp;
		UT_sint32 cr;
		std::vector<std::string> fonts;
	};
	static UsedFontCache cache = { nullptr, nullptr, -1, {} };
	const pf_Frag * stamp = table->getFragments().getFirst();
	const UT_sint32 cr = doc->getCRNumber();
	if (cache.doc == doc && cache.stamp == stamp && cache.cr == cr)
	{
		fonts = cache.fonts;
		return;
	}

	std::set<std::string> seen;
	const PP_AttrProp * sectionAP = nullptr;
	const PP_AttrProp * blockAP = nullptr;

	for (pf_Frag * frag = table->getFragments().getFirst(); frag; frag = frag->getNext())
	{
		const PP_AttrProp * ap = nullptr;
		doc->getAttrProp(frag->getIndexAP(), &ap);

		if (frag->getType() == pf_Frag::PFT_Strux)
		{
			pf_Frag_Strux * strux = static_cast<pf_Frag_Strux *>(frag);
			switch (strux->getStruxType())
			{
			case PTX_Block:
				blockAP = ap;
				rememberFont(PP_evalProperty("font-family", nullptr, blockAP, sectionAP, doc, true),
					     seen, fonts);
				break;
			case PTX_Section:
			case PTX_SectionHdrFtr:
			case PTX_SectionEndnote:
			case PTX_SectionFootnote:
			case PTX_SectionMarginnote:
			case PTX_SectionAnnotation:
			case PTX_SectionFrame:
			case PTX_SectionTOC:
				sectionAP = ap;
				blockAP = nullptr;
				break;
			default:
				break;
			}
			continue;
		}

		if (frag->getType() == pf_Frag::PFT_Text ||
		    frag->getType() == pf_Frag::PFT_FmtMark ||
		    frag->getType() == pf_Frag::PFT_Object)
		{
			rememberFont(PP_evalProperty("font-family", ap, blockAP, sectionAP, doc, true),
				     seen, fonts);
		}
	}

	std::sort(fonts.begin(), fonts.end(), fontNameLess);
	cache.doc = doc;
	cache.stamp = stamp;
	cache.cr = cr;
	cache.fonts = fonts;
}

void AP_buildFontMenu(PD_Document * doc,
		      const std::vector<std::string> & installed,
		      std::vector<std::string> & menu)
{
	std::vector<std::string> all;
	all.reserve(installed.size());
	for (const std::string & name : installed)
	{
		if (!name.empty() && !AP_fontMenuIsSeparator(name.c_str()))
			all.push_back(name);
	}
	std::sort(all.begin(), all.end(), fontNameLess);
	all.erase(std::unique(all.begin(), all.end()), all.end());

	std::vector<std::string> used;
	AP_collectUsedFonts(doc, used);

	menu.clear();
	if (!used.empty())
	{
		menu.swap(used);
		menu.push_back(AP_FONT_MENU_SEPARATOR);
	}
	menu.insert(menu.end(), all.begin(), all.end());
}
