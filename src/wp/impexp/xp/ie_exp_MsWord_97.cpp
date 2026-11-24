/* -*- mode: C++; tab-width: 4; c-basic-offset: 4; -*- */

/* AbiWord
 * Copyright (C) 1998 AbiSource, Inc.
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

/*
 * Word 97 binary document, as specified by [MS-DOC] and written by
 * LibreOffice's ww8 exporter: a FIB at the start of WordDocument, Unicode
 * text at fcMin, formatted-disk pages after the text, a Sepx for the single
 * section, and the piece table, stylesheet, section table and document
 * properties in the 1Table stream. Headers and footers, when the document
 * has them, follow the main text: six stories per MS-DOC PlcfHdd, and
 * ccpHdd stays 0 when there is none, as LibreOffice writes it.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <glib.h>
#include <glib/gstdio.h>

#ifdef G_OS_WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#ifdef interface
#undef interface
#endif
#endif

#include <gsf/gsf-outfile.h>
#include <gsf/gsf-outfile-msole.h>
#include <gsf/gsf-output-memory.h>
#include <gsf/gsf-output.h>

#include "ut_go_file.h"

#include "ie_exp_MsWord_97.h"
#include "pd_Document.h"
#include "pd_Style.h"
#include "pt_PieceTable.h"
#include "pf_Frag_Object.h"
#include "pf_Frag_Strux.h"
#include "pf_Frag_Text.h"
#include "pp_AttrProp.h"
#include "ut_units.h"

namespace {

const unsigned PAGE = 512;
const unsigned FC_MIN = 0x800;
const unsigned FIB_BYTES = 900;

typedef std::vector<unsigned char> Buf;

struct Run
{
	std::u16string text;
	std::string grpprl;
	int picture = -1;
};

struct Picture
{
	std::string bytes;
	unsigned twips_w;
	unsigned twips_h;
	unsigned fc;
};

struct Paragraph
{
	std::vector<Run> runs;
	std::string pap_grpprl;
	const PP_AttrProp * block_ap = nullptr;
	uint16_t end_mark = 0x000D;
};

struct FontCatalog
{
	std::vector<std::string> names;
	FontCatalog()
	{
		names.push_back("Times New Roman");
	}
	unsigned index_of(const char * raw)
	{
		if (!raw || !*raw)
			return 0;
		const char * start = raw;
		while (*start == ' ' || *start == '\t' || *start == '"' || *start == '\'')
			++start;
		const char * end = start;
		while (*end && *end != ',')
			++end;
		while (end > start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '"' || end[-1] == '\''))
			--end;
		if (end <= start)
			return 0;
		std::string name(start, static_cast<size_t>(end - start));
		if (name.size() > 64)
			name.resize(64);
		for (size_t i = 0; i < names.size(); ++i)
		{
			if (!g_ascii_strcasecmp(names[i].c_str(), name.c_str()))
				return static_cast<unsigned>(i);
		}
		if (names.size() >= 64)
			return 0;
		names.push_back(name);
		return static_cast<unsigned>(names.size() - 1);
	}
};

struct ChRun
{
	unsigned fc;
	std::string grpprl;
};

static void append_u16(Buf & out, unsigned value)
{
	out.push_back(static_cast<unsigned char>(value & 0xff));
	out.push_back(static_cast<unsigned char>((value >> 8) & 0xff));
}

static void append_u32(Buf & out, unsigned value)
{
	out.push_back(static_cast<unsigned char>(value & 0xff));
	out.push_back(static_cast<unsigned char>((value >> 8) & 0xff));
	out.push_back(static_cast<unsigned char>((value >> 16) & 0xff));
	out.push_back(static_cast<unsigned char>((value >> 24) & 0xff));
}

static void poke_u16(Buf & out, unsigned at, unsigned value)
{
	out[at] = static_cast<unsigned char>(value & 0xff);
	out[at + 1] = static_cast<unsigned char>((value >> 8) & 0xff);
}

static void poke_u32(Buf & out, unsigned at, unsigned value)
{
	out[at] = static_cast<unsigned char>(value & 0xff);
	out[at + 1] = static_cast<unsigned char>((value >> 8) & 0xff);
	out[at + 2] = static_cast<unsigned char>((value >> 16) & 0xff);
	out[at + 3] = static_cast<unsigned char>((value >> 24) & 0xff);
}

static void append_sprm(std::string & grpprl, unsigned opcode, unsigned operand, unsigned width)
{
	grpprl.push_back(static_cast<char>(opcode & 0xff));
	grpprl.push_back(static_cast<char>((opcode >> 8) & 0xff));
	for (unsigned i = 0; i < width && i < 4; ++i)
		grpprl.push_back(static_cast<char>((operand >> (8 * i)) & 0xff));
}

static bool property_is(const PP_AttrProp * ap, const char * name, const char * wanted)
{
	const gchar * value = nullptr;
	return ap && ap->getProperty(name, value) && value && !strcmp(value, wanted);
}

static const gchar * block_property(PD_Document * doc, const PP_AttrProp * ap, const char * name);

static std::string character_grpprl(PD_Document * doc, FontCatalog & fonts, const PP_AttrProp * span, const PP_AttrProp * block)
{
	std::string grpprl;
	const PP_AttrProp * ap = span ? span : block;
	if (ap)
	{
		const gchar * weight = nullptr;
		if (ap->getProperty("font-weight", weight) && weight)
		{
			const int numeric = atoi(weight);
			if (!strcmp(weight, "bold") || numeric >= 700)
				append_sprm(grpprl, 0x0835, 1, 1);
		}
		if (property_is(ap, "font-style", "italic"))
			append_sprm(grpprl, 0x0836, 1, 1);
		const gchar * decoration = nullptr;
		if (ap->getProperty("text-decoration", decoration) && decoration &&
			strstr(decoration, "underline"))
			append_sprm(grpprl, 0x2A3E, 1, 1);
		const gchar * size = nullptr;
		if (ap->getProperty("font-size", size) && size && *size)
		{
			const double points = UT_convertToPoints(size);
			if (points > 0.0 && points < 1000.0)
				append_sprm(grpprl, 0x4A43, static_cast<unsigned>(points * 2.0 + 0.5), 2);
		}
	}
	const gchar * family = block_property(doc, span, "font-family");
	if (!family || !*family)
		family = block_property(doc, block, "font-family");
	if (family && *family)
	{
		const unsigned font = fonts.index_of(family);
		append_sprm(grpprl, 0x4A4F, font, 2); // sprmCRgFtc0
		append_sprm(grpprl, 0x4A50, font, 2); // sprmCRgFtc1
		append_sprm(grpprl, 0x4A51, font, 2); // sprmCRgFtc2
	}
	return grpprl;
}

static const gchar * block_property(PD_Document * doc, const PP_AttrProp * ap, const char * name)
{
	const gchar * value = nullptr;
	if (ap && ap->getProperty(name, value) && value && *value)
		return value;
	if (doc && ap)
	{
		const gchar * styleName = nullptr;
		if (ap->getAttribute("style", styleName) && styleName && *styleName)
		{
			PD_Style * style = nullptr;
			if (doc->getStyle(styleName, &style) && style && style->getProperty(name, value) && value && *value)
				return value;
		}
	}
	return nullptr;
}

static bool parse_signed_twips(const gchar * text, int & out)
{
	if (!text || !*text)
		return false;
	const double inches = UT_convertToInches(text);
	const double twips = inches * 1440.0;
	if (twips < -31680.0 || twips > 31680.0)
		return false;
	out = static_cast<int>(twips >= 0.0 ? twips + 0.5 : twips - 0.5);
	return true;
}

static std::string paragraph_grpprl(PD_Document * doc, const PP_AttrProp * ap)
{
	std::string grpprl;
	unsigned jc = 0;
	if (const gchar * align = block_property(doc, ap, "text-align"))
	{
		if (!strcmp(align, "center"))
			jc = 1;
		else if (!strcmp(align, "right"))
			jc = 2;
		else if (!strcmp(align, "justify"))
			jc = 3;
	}
	// sprmPJc80 (0x2403) and sprmPJc (0x2461) for Word 97 and modern Word/LibreOffice.
	append_sprm(grpprl, 0x2403, jc, 1);
	if (jc)
		append_sprm(grpprl, 0x2461, jc, 1);

	int twips = 0;
	if (parse_signed_twips(block_property(doc, ap, "margin-left"), twips) && twips != 0)
	{
		const unsigned u = static_cast<unsigned>(twips) & 0xFFFFu;
		append_sprm(grpprl, 0x840F, u, 2); // sprmPDxaLeft80
		append_sprm(grpprl, 0x845E, u, 2); // sprmPDxaLeft
	}
	if (parse_signed_twips(block_property(doc, ap, "margin-right"), twips) && twips != 0)
	{
		const unsigned u = static_cast<unsigned>(twips) & 0xFFFFu;
		append_sprm(grpprl, 0x840E, u, 2); // sprmPDxaRight80
		append_sprm(grpprl, 0x845D, u, 2); // sprmPDxaRight
	}
	if (parse_signed_twips(block_property(doc, ap, "text-indent"), twips) && twips != 0)
	{
		const unsigned u = static_cast<unsigned>(twips) & 0xFFFFu;
		append_sprm(grpprl, 0x8411, u, 2); // sprmPDxaLeft180
		append_sprm(grpprl, 0x8460, u, 2); // sprmPDxaLeft1
	}
	if (parse_signed_twips(block_property(doc, ap, "margin-top"), twips) && twips > 0)
		append_sprm(grpprl, 0xA413, static_cast<unsigned>(twips) & 0xFFFFu, 2); // sprmPDyaBefore
	if (parse_signed_twips(block_property(doc, ap, "margin-bottom"), twips) && twips > 0)
		append_sprm(grpprl, 0xA414, static_cast<unsigned>(twips) & 0xFFFFu, 2); // sprmPDyaAfter

	if (const gchar * tabstops = block_property(doc, ap, "tabstops"))
	{
		struct TabStop { int pos; unsigned char tbd; };
		std::vector<TabStop> tabs;
		const char * p = tabstops;
		while (*p && tabs.size() < 64)
		{
			while (*p == ',' || *p == ' ' || *p == '\t')
				++p;
			if (!*p)
				break;
			const char * end = p;
			while (*end && *end != ',')
				++end;
			std::string entry(p, static_cast<size_t>(end - p));
			p = end;
			const size_t slash = entry.find('/');
			if (slash == std::string::npos || slash == 0)
				continue;
			const std::string posStr = entry.substr(0, slash);
			int posTwips = 0;
			if (!parse_signed_twips(posStr.c_str(), posTwips) || posTwips <= 0)
				continue;
			unsigned jcTab = 0;
			unsigned leader = 0;
			if (slash + 1 < entry.size())
			{
				switch (entry[slash + 1])
				{
				case 'C': case 'c': jcTab = 1; break;
				case 'R': case 'r': jcTab = 2; break;
				case 'D': case 'd': jcTab = 3; break;
				case 'B': case 'b': jcTab = 4; break;
				default: jcTab = 0; break;
				}
			}
			if (slash + 2 < entry.size() && entry[slash + 2] >= '0' && entry[slash + 2] <= '4')
				leader = static_cast<unsigned>(entry[slash + 2] - '0');
			TabStop ts = { posTwips, static_cast<unsigned char>((jcTab & 0x07u) | ((leader & 0x07u) << 3)) };
			tabs.push_back(ts);
		}
		if (!tabs.empty())
		{
			for (size_t i = 1; i < tabs.size(); ++i)
			{
				TabStop key = tabs[i];
				size_t j = i;
				while (j > 0 && tabs[j - 1].pos > key.pos)
				{
					tabs[j] = tabs[j - 1];
					--j;
				}
				tabs[j] = key;
			}
			const unsigned n = static_cast<unsigned>(tabs.size());
			const unsigned cb = 2 + n * 3;
			if (cb <= 255)
			{
				// sprmPChgTabsPapx (0xC60D)
				grpprl.push_back(static_cast<char>(0x0D));
				grpprl.push_back(static_cast<char>(0xC6));
				grpprl.push_back(static_cast<char>(cb));
				grpprl.push_back(0); // itbdDelMax = 0
				grpprl.push_back(static_cast<char>(n));
				for (size_t i = 0; i < tabs.size(); ++i)
				{
					const unsigned u = static_cast<unsigned>(tabs[i].pos) & 0xFFFFu;
					grpprl.push_back(static_cast<char>(u & 0xff));
					grpprl.push_back(static_cast<char>((u >> 8) & 0xff));
				}
				for (size_t i = 0; i < tabs.size(); ++i)
					grpprl.push_back(static_cast<char>(tabs[i].tbd));
			}
		}
	}
	return grpprl;
}

static void append_code_unit(std::u16string & text, UT_UCS4Char ch)
{
	if (ch == UCS_LF || ch == 0x000D)
		ch = 0x000B;
	else if (ch == UCS_FF)
		ch = 0x000C;
	else if (ch == UCS_VTAB)
		ch = 0x000E;
	if (ch > 0x10FFFF || (ch >= 0xD800 && ch <= 0xDFFF))
		ch = 0xFFFD;
	if (ch <= 0xFFFF)
	{
		text.push_back(static_cast<char16_t>(ch));
		return;
	}
	ch -= 0x10000;
	text.push_back(static_cast<char16_t>(0xD800 + (ch >> 10)));
	text.push_back(static_cast<char16_t>(0xDC00 + (ch & 0x3FF)));
}

static bool skipped_section(PTStruxType type)
{
	switch (type)
	{
	case PTX_SectionHdrFtr:
	case PTX_SectionEndnote:
	case PTX_SectionFootnote:
	case PTX_SectionMarginnote:
	case PTX_SectionAnnotation:
	case PTX_SectionFrame:
	case PTX_SectionTOC:
		return true;
	default:
		return false;
	}
}

static bool ended_skipped_section(PTStruxType kind)
{
	switch (kind)
	{
	case PTX_EndFootnote:
	case PTX_EndEndnote:
	case PTX_EndAnnotation:
	case PTX_EndMarginnote:
	case PTX_EndTOC:
		return true;
	default:
		return false;
	}
}

static bool append_image(PD_Document * doc, Paragraph & current, const PP_AttrProp * ap, std::vector<Picture> & pics);

struct TableCell
{
	int left = 0;
	int right = 0;
	int top = 0;
	int bot = 0;
	std::vector<Paragraph> paragraphs;
};

struct OpenTable
{
	int depth = 0;
	const PP_AttrProp * ap = nullptr;
	std::vector<TableCell> cells;
	int current = -1;
	bool in_paragraph = false;
	Paragraph paragraph;
};

static bool feed_table(OpenTable & table, PD_Document * doc, FontCatalog & fonts,
	pf_Frag * frag, std::vector<Picture> & pics,
	bool & in_paragraph, Paragraph & current, std::vector<Paragraph> & dest);

static std::vector<Paragraph> collect_paragraphs(PD_Document * doc, std::vector<Picture> & pics, FontCatalog & fonts)
{
	std::vector<Paragraph> paragraphs;
	Paragraph current;
	bool accept = true;
	bool in_paragraph = false;
	int frame_depth = 0;
	int skip_depth = 0;
	OpenTable open_table;
	pt_PieceTable * table = doc->getPieceTable();
	if (!table)
		return paragraphs;

	for (pf_Frag * frag = table->getFragments().getFirst(); frag; frag = frag->getNext())
	{
		if (accept && !frame_depth &&
			feed_table(open_table, doc, fonts, frag, pics, in_paragraph, current, paragraphs))
			continue;
		if (frag->getType() == pf_Frag::PFT_Strux)
		{
			const PTStruxType kind = static_cast<pf_Frag_Strux *>(frag)->getStruxType();
			if (frame_depth)
			{
				if (kind == PTX_SectionFrame)
					++frame_depth;
				else if (kind == PTX_EndFrame)
				{
					--frame_depth;
					accept = (frame_depth == 0 && skip_depth == 0);
				}
				continue;
			}
			if (kind == PTX_Block)
			{
				if (accept)
				{
					if (in_paragraph)
						paragraphs.push_back(current);
					current = Paragraph();
					const PP_AttrProp * blockAP = nullptr;
					doc->getAttrProp(frag->getIndexAP(), &blockAP);
					current.block_ap = blockAP;
					current.pap_grpprl = paragraph_grpprl(doc, blockAP);
					in_paragraph = true;
				}
				else if (in_paragraph)
				{
					paragraphs.push_back(current);
					current = Paragraph();
					in_paragraph = false;
				}
				continue;
			}
			if (in_paragraph)
			{
				paragraphs.push_back(current);
				current = Paragraph();
				in_paragraph = false;
			}
			if (kind == PTX_SectionFrame)
			{
				// A positioned image is a frame, not a PFT_Object. Closing
				// the section here used to drop every fragment after it.
				if (accept)
				{
					Paragraph image;
					const PP_AttrProp * ap = nullptr;
					doc->getAttrProp(frag->getIndexAP(), &ap);
					if (append_image(doc, image, ap, pics))
					{
						current = image;
						in_paragraph = true;
					}
				}
				frame_depth = 1;
				accept = false;
				continue;
			}
			if (kind == PTX_Section)
			{
				skip_depth = 0;
				accept = (frame_depth == 0 && skip_depth == 0);
			}
			else if (skipped_section(kind))
			{
				++skip_depth;
				accept = false;
			}
			else if (ended_skipped_section(kind))
			{
				if (skip_depth > 0)
					--skip_depth;
				accept = (frame_depth == 0 && skip_depth == 0);
			}
			continue;
		}
		if (frame_depth || !accept)
			continue;
		if (frag->getType() == pf_Frag::PFT_Object)
		{
			const pf_Frag_Object * object = static_cast<const pf_Frag_Object *>(frag);
			if (object->getObjectType() != PTO_Image)
				continue;
			if (!in_paragraph)
			{
				current = Paragraph();
				in_paragraph = true;
			}
			const PP_AttrProp * ap = nullptr;
			doc->getAttrProp(object->getIndexAP(), &ap);
			append_image(doc, current, ap, pics);
			continue;
		}
		if (frag->getType() != pf_Frag::PFT_Text)
			continue;
		if (!in_paragraph)
		{
			current = Paragraph();
			in_paragraph = true;
		}
		const pf_Frag_Text * text = static_cast<const pf_Frag_Text *>(frag);
		const UT_UCS4Char * chars = doc->getPointer(text->getBufIndex());
		const PP_AttrProp * props = nullptr;
		doc->getAttrProp(text->getIndexAP(), &props);
		Run run;
		run.grpprl = character_grpprl(doc, fonts, props, current.block_ap);
		for (UT_uint32 i = 0; chars && i < text->getLength(); ++i)
			append_code_unit(run.text, chars[i]);
		if (!current.runs.empty() && current.runs.back().picture < 0 && current.runs.back().grpprl == run.grpprl)
			current.runs.back().text += run.text;
		else
			current.runs.push_back(run);
	}
	if (in_paragraph)
		paragraphs.push_back(current);
	if (paragraphs.empty())
		paragraphs.push_back(Paragraph());
	return paragraphs;
}

// MS-DOC 2.8.22 PlcfHdd: six stories per section, in this order. Each story
// ends with a paragraph mark, including the empty ones. "header" and "footer"
// are the odd-page stories Word uses when even pages are not distinct.
static const char * const HDD_SLOT[6] = {
	"header-even",
	"header",
	"footer-even",
	"footer",
	"header-first",
	"footer-first"
};

struct Story
{
	std::string type;
	std::vector<Paragraph> paragraphs;
};

struct DocSection
{
	std::string slot[6];
};

struct Headers
{
	std::vector<DocSection> sections;
	std::map<std::string, Story> stories;
};

static const char * frag_attribute(PD_Document * doc, pf_Frag * frag, const char * name)
{
	const PP_AttrProp * ap = nullptr;
	const gchar * value = nullptr;
	if (!doc->getAttrProp(frag->getIndexAP(), &ap) || !ap)
		return nullptr;
	if (ap->getAttribute(name, value) && value && *value)
		return value;
	// A section can store the header id as a property (pp_Property lists
	// "header" at section level) as well as an attribute.
	if (ap->getProperty(name, value) && value && *value)
		return value;
	return nullptr;
}

static int slot_for_type(const char * type)
{
	if (!type)
		return -1;
	for (int i = 0; i < 6; ++i)
	{
		if (!strcmp(type, HDD_SLOT[i]))
			return i;
	}
	if (!strcmp(type, "header-last"))
		return 1;
	if (!strcmp(type, "footer-last"))
		return 3;
	return -1;
}

static void append_run(Paragraph & current, PD_Document * doc, FontCatalog & fonts, const PP_AttrProp * props, const UT_UCS4Char * chars, UT_uint32 length)
{
	Run run;
	run.grpprl = character_grpprl(doc, fonts, props, current.block_ap);
	for (UT_uint32 i = 0; chars && i < length; ++i)
		append_code_unit(run.text, chars[i]);
	if (!current.runs.empty() && current.runs.back().picture < 0 && current.runs.back().grpprl == run.grpprl)
		current.runs.back().text += run.text;
	else
		current.runs.push_back(run);
}

static const char * image_id_of(const PP_AttrProp * ap)
{
	const gchar * value = nullptr;
	if (!ap)
		return nullptr;
	if (ap->getAttribute("dataid", value) && value && *value)
		return value;
	if (ap->getAttribute("strux-image-dataid", value) && value && *value)
		return value;
	return nullptr;
}

static unsigned twips_property(const PP_AttrProp * ap, const char * name, unsigned fallback)
{
	const gchar * value = nullptr;
	if (!ap || !ap->getProperty(name, value) || !value || !*value)
		return fallback;
	const double inches = UT_convertToInches(value);
	if (inches <= 0.0 || inches > 50.0)
		return fallback;
	return static_cast<unsigned>(inches * 1440.0 + 0.5);
}

static bool image_bytes(PD_Document * doc, const char * id, std::string & bytes)
{
	if (!id || !*id)
		return false;
	UT_ConstByteBufPtr buf;
	std::string mime;
	if (!doc->getDataItemDataByName(id, buf, &mime, nullptr) || !buf || !buf->getLength())
		return false;
	const unsigned char * raw = buf->getPointer(0);
	const bool png = buf->getLength() >= 8 && raw[0] == 137 && raw[1] == 'P' && raw[2] == 'N' && raw[3] == 'G';
	const bool jpeg = buf->getLength() >= 2 && raw[0] == 0xFF && raw[1] == 0xD8;
	// The Data stream labels the blip as PNG or JPEG. Another mime would be
	// stored under the wrong type and Word would drop the picture.
	if (!png && !jpeg)
		return false;
	if (!mime.empty() && mime.compare(0, 6, "image/") != 0)
		return false;
	bytes.assign(reinterpret_cast<const char *>(raw), buf->getLength());
	return true;
}

static bool append_image(PD_Document * doc, Paragraph & current, const PP_AttrProp * ap, std::vector<Picture> & pics)
{
	std::string bytes;
	if (!image_bytes(doc, image_id_of(ap), bytes))
		return false;
	Picture pic;
	pic.bytes = bytes;
	pic.twips_w = twips_property(ap, "width", 0);
	if (!pic.twips_w)
		pic.twips_w = twips_property(ap, "frame-width", 1440);
	pic.twips_h = twips_property(ap, "height", 0);
	if (!pic.twips_h)
		pic.twips_h = twips_property(ap, "frame-height", 1440);
	pic.fc = 0;
	pics.push_back(pic);
	Run run;
	run.picture = static_cast<int>(pics.size() - 1);
	run.text.push_back(0x0001);
	current.runs.push_back(run);
	return true;
}

static int property_int(const PP_AttrProp * ap, const char * name)
{
	const gchar * value = nullptr;
	if (!ap || !ap->getProperty(name, value) || !value || !*value)
		return 0;
	return atoi(value);
}

static void close_table_paragraph(OpenTable & table)
{
	if (!table.in_paragraph)
		return;
	if (table.current >= 0 && table.current < static_cast<int>(table.cells.size()))
		table.cells[static_cast<size_t>(table.current)].paragraphs.push_back(table.paragraph);
	table.paragraph = Paragraph();
	table.in_paragraph = false;
}

static std::string in_table_grpprl(const std::string & grp)
{
	// make_papx writes the style index itself. The grpprl is only sprms.
	std::string marked;
	append_sprm(marked, 0x2416, 1, 1); // sprmPFInTable
	marked += grp;
	return marked;
}

static std::vector<unsigned> table_column_widths(const PP_AttrProp * ap, unsigned columns)
{
	std::vector<unsigned> widths;
	const gchar * props = nullptr;
	if (ap && ap->getProperty("table-column-props", props) && props)
	{
		const char * p = props;
		while (*p && widths.size() < 39)
		{
			while (*p == '/' || *p == ' ' || *p == '\t')
				++p;
			if (!*p)
				break;
			const char * end = p;
			while (*end && *end != '/')
				++end;
			std::string token(p, static_cast<size_t>(end - p));
			int twips = 0;
			if (parse_signed_twips(token.c_str(), twips) && twips > 0)
				widths.push_back(static_cast<unsigned>(twips));
			p = end;
		}
	}
	const unsigned fill = widths.empty() ? 1440u : widths.back();
	while (widths.size() < columns && widths.size() < 39)
		widths.push_back(fill);
	if (columns > 0 && widths.size() > columns)
		widths.resize(columns);
	return widths;
}

static std::string row_mark_grpprl(const std::vector<unsigned> & widths)
{
	std::string grp;
	append_sprm(grp, 0x2416, 1, 1);
	append_sprm(grp, 0x2417, 1, 1); // sprmPFTtp, the row mark
	append_sprm(grp, 0x9602, 108, 2); // sprmTDxaGapHalf
	const unsigned n = static_cast<unsigned>(widths.size());
	if (n < 1 || n > 39)
		return grp;
	std::string operand;
	operand.push_back(static_cast<char>(n));
	int x = 0;
	auto put16 = [&operand](int value)
	{
		const unsigned u = static_cast<unsigned>(value) & 0xFFFFu;
		operand.push_back(static_cast<char>(u & 0xff));
		operand.push_back(static_cast<char>((u >> 8) & 0xff));
	};
	put16(0);
	for (unsigned i = 0; i < n; ++i)
	{
		x += static_cast<int>(widths[i]);
		put16(x);
	}
	for (unsigned i = 0; i < n; ++i)
	{
		operand.push_back(0);
		operand.push_back(0);
		for (int border = 0; border < 4; ++border)
		{
			operand.push_back(0x08);
			operand.push_back(0x01);
		}
	}
	grp.push_back(0x08);
	grp.push_back(static_cast<char>(0xD6)); // sprmTDefTable
	const unsigned cb = static_cast<unsigned>(operand.size());
	grp.push_back(static_cast<char>(cb & 0xff));
	grp.push_back(static_cast<char>((cb >> 8) & 0xff));
	grp += operand;
	return grp;
}

static Paragraph mark_paragraph(uint16_t mark, const std::string & pap)
{
	Paragraph paragraph;
	paragraph.end_mark = mark;
	paragraph.pap_grpprl = pap;
	return paragraph;
}

static std::vector<Paragraph> build_table(OpenTable & table)
{
	std::vector<Paragraph> out;
	close_table_paragraph(table);
	if (table.cells.empty())
		return out;

	bool grid = false;
	int columns = 0;
	for (size_t i = 0; i < table.cells.size(); ++i)
	{
		const TableCell & cell = table.cells[i];
		if (cell.right > cell.left || cell.bot > cell.top)
			grid = true;
		if (cell.right > columns)
			columns = cell.right;
	}
	if (!grid)
		columns = static_cast<int>(table.cells.size());
	if (columns < 1)
		columns = 1;
	if (columns > 39)
		columns = 39;

	std::vector<std::vector<int>> rows;
	if (!grid)
	{
		std::vector<int> row;
		for (int i = 0; i < columns && i < static_cast<int>(table.cells.size()); ++i)
			row.push_back(i);
		rows.push_back(row);
	}
	else
	{
		std::vector<int> tops;
		for (size_t i = 0; i < table.cells.size(); ++i)
		{
			const int top = table.cells[i].top;
			bool seen = false;
			for (size_t t = 0; t < tops.size(); ++t)
				if (tops[t] == top)
					seen = true;
			if (!seen)
				tops.push_back(top);
		}
		for (size_t a = 1; a < tops.size(); ++a)
		{
			const int key = tops[a];
			size_t b = a;
			while (b > 0 && tops[b - 1] > key)
			{
				tops[b] = tops[b - 1];
				--b;
			}
			tops[b] = key;
		}
		for (size_t t = 0; t < tops.size(); ++t)
		{
			std::vector<int> row;
			for (size_t i = 0; i < table.cells.size(); ++i)
				if (table.cells[i].top == tops[t])
					row.push_back(static_cast<int>(i));
			for (size_t a = 1; a < row.size(); ++a)
			{
				const int key = row[a];
				size_t b = a;
				while (b > 0 && table.cells[static_cast<size_t>(row[b - 1])].left > table.cells[static_cast<size_t>(key)].left)
				{
					row[b] = row[b - 1];
					--b;
				}
				row[b] = key;
			}
			rows.push_back(row);
		}
	}

	const std::vector<unsigned> widths = table_column_widths(table.ap, static_cast<unsigned>(columns));
	const std::string row_pap = row_mark_grpprl(widths);
	for (size_t r = 0; r < rows.size(); ++r)
	{
		for (int c = 0; c < columns; ++c)
		{
			int found = -1;
			if (!grid)
			{
				if (c < static_cast<int>(rows[r].size()))
					found = rows[r][static_cast<size_t>(c)];
			}
			else
			{
				for (size_t i = 0; i < rows[r].size(); ++i)
				{
					const TableCell & cell = table.cells[static_cast<size_t>(rows[r][i])];
					if (cell.left == c)
						found = rows[r][i];
				}
			}
			if (found >= 0)
			{
				const std::vector<Paragraph> & paras = table.cells[static_cast<size_t>(found)].paragraphs;
				if (paras.empty())
					out.push_back(mark_paragraph(0x000D, in_table_grpprl(std::string())));
				for (size_t p = 0; p < paras.size(); ++p)
				{
					Paragraph paragraph = paras[p];
					paragraph.pap_grpprl = in_table_grpprl(paragraph.pap_grpprl);
					out.push_back(paragraph);
				}
			}
			else
			{
				out.push_back(mark_paragraph(0x000D, in_table_grpprl(std::string())));
			}
			out.push_back(mark_paragraph(0x0007, in_table_grpprl(std::string())));
		}
		out.push_back(mark_paragraph(0x0007, row_pap));
	}
	return out;
}

static bool feed_table(OpenTable & table, PD_Document * doc, FontCatalog & fonts,
	pf_Frag * frag, std::vector<Picture> & pics,
	bool & in_paragraph, Paragraph & current, std::vector<Paragraph> & dest)
{
	if (frag->getType() == pf_Frag::PFT_Strux)
	{
		const PTStruxType kind = static_cast<pf_Frag_Strux *>(frag)->getStruxType();
		if (table.depth == 0)
		{
			if (kind != PTX_SectionTable)
				return false;
			if (in_paragraph)
			{
				dest.push_back(current);
				current = Paragraph();
				in_paragraph = false;
			}
			table = OpenTable();
			table.depth = 1;
			doc->getAttrProp(frag->getIndexAP(), &table.ap);
			return true;
		}
		if (kind == PTX_SectionTable)
		{
			++table.depth;
			return true;
		}
		if (kind == PTX_EndTable)
		{
			if (table.depth > 0)
				--table.depth;
			if (table.depth == 0)
			{
				const std::vector<Paragraph> built = build_table(table);
				dest.insert(dest.end(), built.begin(), built.end());
				table = OpenTable();
			}
			return true;
		}
		if (table.depth > 1)
			return true;
		if (kind == PTX_SectionCell)
		{
			close_table_paragraph(table);
			const PP_AttrProp * ap = nullptr;
			doc->getAttrProp(frag->getIndexAP(), &ap);
			TableCell cell;
			cell.left = property_int(ap, "left-attach");
			cell.right = property_int(ap, "right-attach");
			cell.top = property_int(ap, "top-attach");
			cell.bot = property_int(ap, "bot-attach");
			table.cells.push_back(cell);
			table.current = static_cast<int>(table.cells.size() - 1);
			return true;
		}
		if (kind == PTX_EndCell)
		{
			close_table_paragraph(table);
			table.current = -1;
			return true;
		}
		if (kind == PTX_Block)
		{
			close_table_paragraph(table);
			if (table.current < 0)
			{
				table.cells.push_back(TableCell());
				table.current = static_cast<int>(table.cells.size() - 1);
			}
			const PP_AttrProp * ap = nullptr;
			doc->getAttrProp(frag->getIndexAP(), &ap);
			table.paragraph = Paragraph();
			table.paragraph.block_ap = ap;
			table.paragraph.pap_grpprl = paragraph_grpprl(doc, ap);
			table.in_paragraph = true;
			return true;
		}
		return true;
	}
	if (table.depth == 0)
		return false;
	if (table.depth > 1)
		return true;
	if (!table.in_paragraph)
	{
		if (table.current < 0)
		{
			table.cells.push_back(TableCell());
			table.current = static_cast<int>(table.cells.size() - 1);
		}
		table.paragraph = Paragraph();
		table.in_paragraph = true;
	}
	if (frag->getType() == pf_Frag::PFT_Object)
	{
		const pf_Frag_Object * object = static_cast<const pf_Frag_Object *>(frag);
		if (object->getObjectType() == PTO_Image)
		{
			const PP_AttrProp * ap = nullptr;
			doc->getAttrProp(object->getIndexAP(), &ap);
			append_image(doc, table.paragraph, ap, pics);
		}
		return true;
	}
	if (frag->getType() == pf_Frag::PFT_Text)
	{
		const pf_Frag_Text * text = static_cast<const pf_Frag_Text *>(frag);
		const PP_AttrProp * props = nullptr;
		doc->getAttrProp(text->getIndexAP(), &props);
		append_run(table.paragraph, doc, fonts, props, doc->getPointer(text->getBufIndex()), text->getLength());
	}
	return true;
}

static Headers collect_headers(PD_Document * doc, std::vector<Picture> & pics, FontCatalog & fonts)
{
	Headers out;
	pt_PieceTable * table = doc->getPieceTable();
	if (!table)
		return out;

	bool in_story = false;
	int skip_depth = 0;
	bool in_paragraph = false;
	std::string current_id;
	Paragraph current;
	std::vector<bool> frame_skipped;
	OpenTable open_table;

	for (pf_Frag * frag = table->getFragments().getFirst(); frag; frag = frag->getNext())
	{
		if (in_story && skip_depth == 0 &&
			feed_table(open_table, doc, fonts, frag, pics, in_paragraph, current, out.stories[current_id].paragraphs))
			continue;
		if (frag->getType() == pf_Frag::PFT_Strux)
		{
			const PTStruxType kind = static_cast<pf_Frag_Strux *>(frag)->getStruxType();
			if (kind == PTX_Block)
			{
				if (in_story && skip_depth == 0)
				{
					if (in_paragraph)
						out.stories[current_id].paragraphs.push_back(current);
					current = Paragraph();
					const PP_AttrProp * blockAP = nullptr;
					doc->getAttrProp(frag->getIndexAP(), &blockAP);
					current.block_ap = blockAP;
					current.pap_grpprl = paragraph_grpprl(doc, blockAP);
					in_paragraph = true;
				}
				continue;
			}
			if (in_story && skip_depth == 0 && in_paragraph)
			{
				out.stories[current_id].paragraphs.push_back(current);
				current = Paragraph();
				in_paragraph = false;
			}
			if (kind == PTX_Section)
			{
				in_story = false;
				skip_depth = 0;
				frame_skipped.clear();
				DocSection section;
				for (int slot = 0; slot < 6; ++slot)
				{
					const char * id = frag_attribute(doc, frag, HDD_SLOT[slot]);
					if (id)
						section.slot[slot] = id;
				}
				if (section.slot[1].empty())
				{
					const char * id = frag_attribute(doc, frag, "header-last");
					if (id)
						section.slot[1] = id;
				}
				if (section.slot[3].empty())
				{
					const char * id = frag_attribute(doc, frag, "footer-last");
					if (id)
						section.slot[3] = id;
				}
				out.sections.push_back(section);
				continue;
			}
			if (kind == PTX_SectionHdrFtr)
			{
				skip_depth = 0;
				frame_skipped.clear();
				const char * id = frag_attribute(doc, frag, "id");
				const char * type = frag_attribute(doc, frag, "type");
				if (id)
				{
					in_story = true;
					current_id = id;
					out.stories[current_id].type = type ? type : "";
				}
				else
					in_story = false;
				continue;
			}
			if (kind == PTX_SectionFrame)
			{
				bool skip_frame = false;
				if (in_story && skip_depth == 0)
				{
					Paragraph image;
					const PP_AttrProp * ap = nullptr;
					doc->getAttrProp(frag->getIndexAP(), &ap);
					if (append_image(doc, image, ap, pics))
					{
						current = image;
						in_paragraph = true;
						skip_frame = true;
					}
				}
				if (in_story)
				{
					frame_skipped.push_back(skip_frame);
					if (skip_frame)
						++skip_depth;
				}
				continue;
			}
			if (kind == PTX_EndFrame)
			{
				if (in_story && !frame_skipped.empty())
				{
					if (frame_skipped.back() && skip_depth > 0)
						--skip_depth;
					frame_skipped.pop_back();
				}
				continue;
			}
			if (skipped_section(kind))
			{
				if (in_story)
					++skip_depth;
				continue;
			}
			if (skip_depth && (kind == PTX_EndFootnote || kind == PTX_EndEndnote ||
				kind == PTX_EndTOC || kind == PTX_EndAnnotation ||
				kind == PTX_EndMarginnote))
				--skip_depth;
			continue;
		}
		if (!in_story || skip_depth)
			continue;
		if (frag->getType() == pf_Frag::PFT_Object)
		{
			const pf_Frag_Object * object = static_cast<const pf_Frag_Object *>(frag);
			if (object->getObjectType() != PTO_Image)
				continue;
			if (!in_paragraph)
			{
				current = Paragraph();
				in_paragraph = true;
			}
			const PP_AttrProp * ap = nullptr;
			doc->getAttrProp(object->getIndexAP(), &ap);
			append_image(doc, current, ap, pics);
			continue;
		}
		if (frag->getType() != pf_Frag::PFT_Text)
			continue;
		if (!in_paragraph)
		{
			current = Paragraph();
			in_paragraph = true;
		}
		const pf_Frag_Text * text = static_cast<const pf_Frag_Text *>(frag);
		const PP_AttrProp * props = nullptr;
		doc->getAttrProp(text->getIndexAP(), &props);
		append_run(current, doc, fonts, props, doc->getPointer(text->getBufIndex()), text->getLength());
	}
	if (in_story && skip_depth == 0 && in_paragraph)
		out.stories[current_id].paragraphs.push_back(current);

	std::set<std::string> used;
	for (size_t s = 0; s < out.sections.size(); ++s)
		for (int slot = 0; slot < 6; ++slot)
			if (!out.sections[s].slot[slot].empty())
				used.insert(out.sections[s].slot[slot]);

	std::vector<std::string> extras;
	for (std::map<std::string, Story>::const_iterator it = out.stories.begin(); it != out.stories.end(); ++it)
	{
		if (!used.count(it->first) && !it->second.paragraphs.empty())
			extras.push_back(it->first);
	}
	if (!extras.empty() && out.sections.empty())
		out.sections.push_back(DocSection());
	if (!out.sections.empty())
	{
		DocSection & last = out.sections.back();
		for (size_t i = 0; i < extras.size(); ++i)
		{
			const std::string type = out.stories[extras[i]].type;
			const std::vector<Paragraph> extra_paragraphs = out.stories[extras[i]].paragraphs;
			int slot = slot_for_type(type.c_str());
			if (slot < 0)
				slot = 1;
			if (last.slot[slot].empty())
				last.slot[slot] = extras[i];
			else
			{
				Story & dest = out.stories[last.slot[slot]];
				dest.paragraphs.insert(dest.paragraphs.end(), extra_paragraphs.begin(), extra_paragraphs.end());
			}
		}
	}
	return out;
}

static bool needs_header_text(const Headers & headers)
{
	for (size_t s = 0; s < headers.sections.size(); ++s)
		for (int slot = 0; slot < 6; ++slot)
			if (!headers.sections[s].slot[slot].empty())
				return true;
	for (std::map<std::string, Story>::const_iterator it = headers.stories.begin(); it != headers.stories.end(); ++it)
		if (!it->second.paragraphs.empty())
			return true;
	return false;
}

static void append_paragraphs(const std::vector<Paragraph> & paragraphs,
	std::vector<uint16_t> & units, std::vector<ChRun> & runs,
	std::vector<unsigned> & bounds, std::vector<std::string> & pap_grpprls)
{
	for (size_t p = 0; p < paragraphs.size(); ++p)
	{
		const std::vector<Run> & source = paragraphs[p].runs;
		if (source.empty())
		{
			const unsigned fc = FC_MIN + static_cast<unsigned>(units.size() * 2);
			if (runs.empty() || !runs.back().grpprl.empty())
			{
				ChRun run;
				run.fc = fc;
				runs.push_back(run);
			}
		}
		for (size_t r = 0; r < source.size(); ++r)
		{
			const unsigned fc = FC_MIN + static_cast<unsigned>(units.size() * 2);
			if (runs.empty() || runs.back().grpprl != source[r].grpprl)
			{
				ChRun run;
				run.fc = fc;
				run.grpprl = source[r].grpprl;
				runs.push_back(run);
			}
			for (size_t c = 0; c < source[r].text.size(); ++c)
				units.push_back(static_cast<uint16_t>(source[r].text[c]));
		}
		units.push_back(paragraphs[p].end_mark);
		bounds.push_back(FC_MIN + static_cast<unsigned>(units.size() * 2));
		pap_grpprls.push_back(paragraphs[p].pap_grpprl);
	}
}

static bool chpx_fits(const std::vector<ChRun> & runs, size_t from, size_t until)
{
	const unsigned count = static_cast<unsigned>(until - from);
	unsigned grp = 0;
	for (size_t i = from; i < until; ++i)
	{
		if (runs[i].grpprl.empty())
			continue;
		unsigned size = static_cast<unsigned>(1 + runs[i].grpprl.size());
		if (size & 1)
			++size;
		grp += size;
	}
	return (count + 1) * 4 + count + grp + 2 <= PAGE;
}

static Buf make_chpx(const std::vector<ChRun> & runs, size_t from, size_t until, unsigned limit)
{
	Buf page(PAGE, 0);
	const unsigned count = static_cast<unsigned>(until - from);
	unsigned pos = 0;
	for (size_t i = from; i < until; ++i)
	{
		poke_u32(page, pos, runs[i].fc);
		pos += 4;
	}
	poke_u32(page, pos, limit);
	pos += 4;

	unsigned end = 510;
	std::vector<unsigned> offsets;
	for (size_t n = until; n > from; --n)
	{
		const std::string & grp = runs[n - 1].grpprl;
		if (grp.empty())
		{
			offsets.push_back(0);
			continue;
		}
		unsigned size = static_cast<unsigned>(1 + grp.size());
		if (size & 1)
			++size;
		end -= size;
		if (end & 1)
			--end;
		page[end] = static_cast<unsigned char>(grp.size());
		memcpy(&page[end + 1], grp.data(), grp.size());
		offsets.push_back(end / 2);
	}
	for (size_t i = offsets.size(); i > 0; --i)
		page[pos++] = static_cast<unsigned char>(offsets[i - 1]);
	page[511] = static_cast<unsigned char>(count);
	return page;
}

struct FkpPage
{
	unsigned start;
	unsigned limit;
	Buf page;
};

static std::vector<FkpPage> pack_chpx(const std::vector<ChRun> & runs, unsigned fc_end)
{
	std::vector<FkpPage> pages;
	size_t i = 0;
	while (i < runs.size())
	{
		size_t best = i + 1;
		for (size_t j = i + 1; j <= runs.size(); ++j)
		{
			if (chpx_fits(runs, i, j))
				best = j;
			else
				break;
		}
		const unsigned limit = best < runs.size() ? runs[best].fc : fc_end;
		FkpPage page;
		page.start = runs[i].fc;
		page.limit = limit;
		page.page = make_chpx(runs, i, best, limit);
		pages.push_back(page);
		i = best;
	}
	return pages;
}

static const unsigned PAPX_MAX = 0x1D;

static std::string effective_pap_grpprl(const std::string & grp)
{
	if (!grp.empty())
		return grp;
	std::string def;
	append_sprm(def, 0x2403, 0, 1);
	return def;
}

static unsigned papx_record_size(const std::string & grp)
{
	const unsigned total = 2 + static_cast<unsigned>(grp.size());
	return (total & 1) ? (1 + total) : (2 + total);
}

static bool papx_fits(const std::vector<std::string> & pap_grpprls, size_t from, size_t until)
{
	const unsigned count = static_cast<unsigned>(until - from);
	if (count == 0 || count > PAPX_MAX)
		return false;
	unsigned at = (count + 1) * 4 + count * 13;
	if (at & 1)
		++at;
	std::set<std::string> unique;
	unsigned data_bytes = 0;
	for (size_t i = from; i < until; ++i)
	{
		const std::string grp = effective_pap_grpprl(pap_grpprls[i]);
		if (unique.insert(grp).second)
			data_bytes += papx_record_size(grp);
	}
	return at + data_bytes <= 510;
}

// MS-DOC 2.9.174 PapxInFkp: each unique GrpPrlAndIstd is stored from the
// end of the 512-byte FKP page at an even offset; BX[0] holds offset / 2.
static Buf make_papx(const std::vector<unsigned> & bounds,
	const std::vector<std::string> & pap_grpprls, size_t from, size_t until)
{
	Buf page(PAGE, 0);
	const unsigned count = static_cast<unsigned>(until - from);
	unsigned pos = 0;
	for (size_t i = from; i <= until; ++i)
	{
		poke_u32(page, pos, bounds[i]);
		pos += 4;
	}
	unsigned end = 510;
	std::map<std::string, unsigned char> placed;
	for (size_t i = from; i < until; ++i)
	{
		const std::string grp = effective_pap_grpprl(pap_grpprls[i]);
		std::map<std::string, unsigned char>::const_iterator it = placed.find(grp);
		unsigned char word = 0;
		if (it != placed.end())
		{
			word = it->second;
		}
		else
		{
			const unsigned size = papx_record_size(grp);
			end -= size;
			const unsigned total = 2 + static_cast<unsigned>(grp.size());
			if (total & 1)
			{
				page[end] = static_cast<unsigned char>((total + 1) / 2);
				page[end + 1] = 0;
				page[end + 2] = 0;
				memcpy(&page[end + 3], grp.data(), grp.size());
			}
			else
			{
				page[end] = 0;
				page[end + 1] = static_cast<unsigned char>(total / 2);
				page[end + 2] = 0;
				page[end + 3] = 0;
				memcpy(&page[end + 4], grp.data(), grp.size());
			}
			word = static_cast<unsigned char>(end / 2);
			placed[grp] = word;
		}
		page[pos + (i - from) * 13] = word;
	}
	page[511] = static_cast<unsigned char>(count);
	return page;
}

static std::vector<FkpPage> pack_papx(const std::vector<unsigned> & bounds,
	const std::vector<std::string> & pap_grpprls)
{
	std::vector<FkpPage> pages;
	if (bounds.size() < 2)
		return pages;
	size_t i = 0;
	const size_t last = bounds.size() - 1;
	while (i < last)
	{
		size_t best = i + 1;
		for (size_t j = i + 1; j <= last; ++j)
		{
			if (papx_fits(pap_grpprls, i, j))
				best = j;
			else
				break;
		}
		FkpPage page;
		page.start = bounds[i];
		page.limit = bounds[best];
		page.page = make_papx(bounds, pap_grpprls, i, best);
		pages.push_back(page);
		i = best;
	}
	return pages;
}

static Buf stylesheet()
{
	// StdfBase, then the Unicode name, then the two UPX records of a
	// paragraph style (paragraph properties, then character properties).
	// bchUpe must equal the STD size. Slots 1..14 exist and are empty:
	// the fixed-index styles are required, and 13 and 14 must be empty.
	Buf normal;
	append_u16(normal, 0);
	append_u16(normal, 1 | (0x0FFF << 4));
	append_u16(normal, 2);
	append_u16(normal, 0);
	append_u16(normal, 0);
	append_u16(normal, 6);
	const char * name = "Normal";
	for (const char * p = name; *p; ++p)
		append_u16(normal, static_cast<unsigned char>(*p));
	append_u16(normal, 0);
	append_u16(normal, 2);
	append_u16(normal, 0);
	append_u16(normal, 0);
	poke_u16(normal, 6, static_cast<unsigned>(normal.size()));

	Buf stshi;
	append_u16(stshi, 15);
	append_u16(stshi, 10);
	append_u16(stshi, 1);
	append_u16(stshi, 15);
	append_u16(stshi, 15);
	append_u16(stshi, 0);
	append_u16(stshi, 0);
	append_u16(stshi, 0);
	append_u16(stshi, 0);

	Buf out;
	append_u16(out, static_cast<unsigned>(stshi.size()));
	out.insert(out.end(), stshi.begin(), stshi.end());
	append_u16(out, static_cast<unsigned>(normal.size()));
	out.insert(out.end(), normal.begin(), normal.end());
	for (int slot = 1; slot < 15; ++slot)
		append_u16(out, 0);
	return out;
}

static Buf font_ffn(const std::string & name)
{
	// FFN: Roman TrueType, weight 400, empty panose and signature.
	// The STTB length byte must stay under 256, so the name is capped.
	Buf ffn;
	ffn.push_back(static_cast<unsigned char>(0x02 | 0x04 | (0x01 << 4)));
	append_u16(ffn, 400);
	ffn.push_back(0);
	ffn.push_back(0);
	ffn.insert(ffn.end(), 10, 0);
	ffn.insert(ffn.end(), 24, 0);
	size_t i = 0;
	unsigned chars = 0;
	while (i < name.size() && chars < 64)
	{
		const unsigned char c = static_cast<unsigned char>(name[i]);
		unsigned cp = c;
		size_t step = 1;
		if ((c & 0x80) == 0)
			cp = c;
		else if ((c & 0xE0) == 0xC0 && i + 1 < name.size())
		{
			cp = ((c & 0x1F) << 6) | (static_cast<unsigned char>(name[i + 1]) & 0x3F);
			step = 2;
		}
		else if ((c & 0xF0) == 0xE0 && i + 2 < name.size())
		{
			cp = ((c & 0x0F) << 12) | ((static_cast<unsigned char>(name[i + 1]) & 0x3F) << 6) |
				(static_cast<unsigned char>(name[i + 2]) & 0x3F);
			step = 3;
		}
		if (cp > 0xFFFF)
			cp = '?';
		append_u16(ffn, cp);
		i += step;
		++chars;
		if (ffn.size() > 250)
			break;
	}
	append_u16(ffn, 0);
	return ffn;
}

static Buf font_table(const FontCatalog & fonts)
{
	// SttbfFfn is a non-extended STTB. cData 0xFFFF would mark it extended
	// and Word then rejects the file. Slot 0 stays Times New Roman.
	Buf out;
	const unsigned count = static_cast<unsigned>(fonts.names.empty() ? 1 : fonts.names.size());
	append_u16(out, count);
	append_u16(out, 0);
	const std::vector<std::string> & names = fonts.names;
	for (unsigned i = 0; i < count; ++i)
	{
		const std::string & name = (i < names.size() && !names[i].empty()) ? names[i] : std::string("Times New Roman");
		Buf ffn = font_ffn(name);
		if (ffn.size() > 255)
			ffn.resize(255);
		out.push_back(static_cast<unsigned char>(ffn.size()));
		out.insert(out.end(), ffn.begin(), ffn.end());
	}
	return out;
}

static unsigned clamp_u16(unsigned value)
{
	return value > 65535 ? 65535 : value;
}

static unsigned hundredths_mm(unsigned twips)
{
	unsigned value = (twips * 5 + 36) / 72;
	if (!value)
		value = 1;
	return clamp_u16(value);
}

static bool is_jpeg(const std::string & bytes)
{
	return bytes.size() >= 2 &&
		static_cast<unsigned char>(bytes[0]) == 0xFF &&
		static_cast<unsigned char>(bytes[1]) == 0xD8;
}

// Inline picture as LibreOffice writes it in the Data stream and as
// SvxMSDffManager::ImportGraphic reads it: PICF mm 0x64, one SpContainer,
// then an FBSE whose blip follows the 36-byte fixed payload.
static Buf picture_record(const Picture & pic, unsigned index)
{
	const bool jpeg = is_jpeg(pic.bytes);
	const unsigned bt = jpeg ? 5 : 6;
	const unsigned blip_type = 0xF018 + bt;
	const unsigned blip_instance = jpeg ? 0x46A : 0x6E0;
	const unsigned image_size = static_cast<unsigned>(pic.bytes.size());
	const unsigned blip_payload = 17 + image_size;
	const unsigned blip_total = 8 + blip_payload;

	Buf shape;
	append_u16(shape, 0x000F);
	append_u16(shape, 0xF004);
	append_u32(shape, 0);
	append_u16(shape, (75u << 4) | 2);
	append_u16(shape, 0xF00A);
	append_u32(shape, 8);
	append_u32(shape, 0x401);
	append_u32(shape, 0x0A00);
	append_u16(shape, (4u << 4) | 3);
	append_u16(shape, 0xF00B);
	append_u32(shape, 24);
	append_u16(shape, 0x4104);
	append_u32(shape, 1);
	append_u16(shape, 262);
	append_u32(shape, 0);
	append_u16(shape, 319);
	append_u32(shape, 0);
	append_u16(shape, 511);
	append_u32(shape, 0x80000);
	append_u16(shape, 0);
	append_u16(shape, 0xF010);
	append_u32(shape, 4);
	append_u32(shape, 0x80000000);
	poke_u32(shape, 4, static_cast<unsigned>(shape.size() - 8));

	const unsigned uid[4] = {
		0xA11B0001u ^ (index * 0x01000193u),
		image_size,
		pic.twips_w,
		pic.twips_h
	};
	Buf record;
	append_u16(record, (bt << 4) | 2);
	append_u16(record, 0xF007);
	append_u32(record, 36 + blip_total);
	record.push_back(static_cast<unsigned char>(bt));
	record.push_back(static_cast<unsigned char>(bt));
	for (int i = 0; i < 4; ++i)
		append_u32(record, uid[i]);
	append_u16(record, 0);
	append_u32(record, blip_total);
	append_u32(record, 1);
	append_u32(record, 0);
	append_u32(record, 0);
	append_u16(record, blip_instance << 4);
	append_u16(record, blip_type);
	append_u32(record, blip_payload);
	for (int i = 0; i < 4; ++i)
		append_u32(record, uid[i]);
	record.push_back(0xFF);
	record.insert(record.end(), pic.bytes.begin(), pic.bytes.end());

	const unsigned tw = pic.twips_w ? pic.twips_w : 1440;
	const unsigned th = pic.twips_h ? pic.twips_h : 1440;
	Buf out(0x44, 0);
	poke_u32(out, 0, 0x44 + static_cast<unsigned>(shape.size() + record.size()));
	poke_u16(out, 4, 0x44);
	poke_u16(out, 6, 0x64);
	poke_u16(out, 8, hundredths_mm(tw));
	poke_u16(out, 10, hundredths_mm(th));
	poke_u16(out, 0x1C, clamp_u16(tw));
	poke_u16(out, 0x1E, clamp_u16(th));
	poke_u16(out, 0x20, 1000);
	poke_u16(out, 0x22, 1000);
	out.insert(out.end(), shape.begin(), shape.end());
	out.insert(out.end(), record.begin(), record.end());
	return out;
}

static Buf layout_pictures(std::vector<Picture> & pics)
{
	Buf data;
	for (size_t i = 0; i < pics.size(); ++i)
	{
		while (data.size() & 3)
			data.push_back(0);
		pics[i].fc = static_cast<unsigned>(data.size());
		const Buf record = picture_record(pics[i], static_cast<unsigned>(i));
		data.insert(data.end(), record.begin(), record.end());
	}
	return data;
}

static std::string picture_grpprl(unsigned fc)
{
	std::string grpprl;
	append_sprm(grpprl, 0x0855, 1, 1);
	append_sprm(grpprl, 0x6A03, fc, 4);
	return grpprl;
}

static void seal_pictures(std::vector<Paragraph> & paragraphs, const std::vector<Picture> & pics)
{
	for (size_t p = 0; p < paragraphs.size(); ++p)
	{
		for (size_t r = 0; r < paragraphs[p].runs.size(); ++r)
		{
			const int index = paragraphs[p].runs[r].picture;
			if (index < 0 || static_cast<size_t>(index) >= pics.size())
				continue;
			paragraphs[p].runs[r].grpprl = picture_grpprl(pics[index].fc);
		}
	}
}

static unsigned story_characters(const std::vector<Paragraph> & paragraphs)
{
	unsigned count = 0;
	for (size_t p = 0; p < paragraphs.size(); ++p)
	{
		for (size_t r = 0; r < paragraphs[p].runs.size(); ++r)
			count += static_cast<unsigned>(paragraphs[p].runs[r].text.size());
		++count;
	}
	if (!paragraphs.empty())
		++count;
	return count;
}

static unsigned header_flags(const std::vector<Paragraph> slot_text[6])
{
	const unsigned bit[6] = { 0x01, 0x02, 0x04, 0x08, 0x10, 0x20 };
	unsigned flags = 0;
	for (int slot = 0; slot < 6; ++slot)
	{
		if (story_characters(slot_text[slot]) >= 2)
			flags |= bit[slot];
	}
	return flags;
}

static Buf section_properties(PD_Document * doc, unsigned hf_flags)
{
	unsigned page_w = 12240;
	unsigned page_h = 15840;
	unsigned margin_left = 1440;
	unsigned margin_right = 1440;
	unsigned margin_top = 1440;
	unsigned margin_bottom = 1440;
	unsigned margin_header = 720;
	unsigned margin_footer = 720;
	if (doc)
	{
		const double w_in = doc->m_docPageSize.Width(DIM_IN);
		const double h_in = doc->m_docPageSize.Height(DIM_IN);
		if (w_in > 0.0)
			page_w = static_cast<unsigned>(w_in * 1440.0 + 0.5);
		if (h_in > 0.0)
			page_h = static_cast<unsigned>(h_in * 1440.0 + 0.5);
		pt_PieceTable * table = doc->getPieceTable();
		for (pf_Frag * frag = table ? table->getFragments().getFirst() : nullptr; frag; frag = frag->getNext())
		{
			if (frag->getType() == pf_Frag::PFT_Strux &&
			    static_cast<pf_Frag_Strux *>(frag)->getStruxType() == PTX_Section)
			{
				const PP_AttrProp * ap = nullptr;
				if (doc->getAttrProp(frag->getIndexAP(), &ap) && ap)
				{
					margin_left = twips_property(ap, "page-margin-left", margin_left);
					margin_right = twips_property(ap, "page-margin-right", margin_right);
					margin_top = twips_property(ap, "page-margin-top", margin_top);
					margin_bottom = twips_property(ap, "page-margin-bottom", margin_bottom);
					margin_header = twips_property(ap, "page-margin-header", margin_header);
					margin_footer = twips_property(ap, "page-margin-footer", margin_footer);
				}
				break;
			}
		}
	}
	// Sepx. Every section must state its page size and its four margins.
	const unsigned sprm[][2] = {
		{ 0xB01F, page_w },
		{ 0xB020, page_h },
		{ 0xB021, margin_left },
		{ 0xB022, margin_right },
		{ 0x9023, margin_top },
		{ 0x9024, margin_bottom },
		{ 0xB025, 0 },
		{ 0xB017, margin_header },
		{ 0xB018, margin_footer }
	};
	Buf grp;
	for (unsigned i = 0; i < sizeof(sprm) / sizeof(sprm[0]); ++i)
	{
		append_u16(grp, sprm[i][0]);
		append_u16(grp, sprm[i][1]);
	}
	// sprmSGprfIhdt. Word and LibreOffice skip the header subdocument when
	// this section flag is zero, even if PlcfHdd and ccpHdd are present.
	if (hf_flags)
	{
		append_u16(grp, 0x3014);
		grp.push_back(static_cast<unsigned char>(hf_flags));
		if (hf_flags & 0x30)
		{
			append_u16(grp, 0x300A);
			grp.push_back(1);
		}
	}
	Buf out;
	append_u16(out, static_cast<unsigned>(grp.size()));
	out.insert(out.end(), grp.begin(), grp.end());
	return out;
}

static Buf bin_table(const std::vector<FkpPage> & pages, const std::vector<unsigned> & pns)
{
	Buf out;
	if (pages.empty() || pns.empty())
		return out;
	append_u32(out, pages.front().start);
	for (size_t i = 0; i < pages.size(); ++i)
		append_u32(out, pages[i].limit);
	for (size_t i = 0; i < pns.size(); ++i)
		append_u32(out, pns[i]);
	return out;
}

static void place(Buf & fib, unsigned index, unsigned offset, unsigned length)
{
	const unsigned at = 0x9A + index * 8;
	poke_u32(fib, at, offset);
	poke_u32(fib, at + 4, length);
}

static bool write_ole_stream(GsfOutfile * ole, const char * name, const Buf & data, std::string & reason)
{
	GsfOutput * stream = gsf_outfile_new_child(ole, name, FALSE);
	if (!stream)
	{
		reason = std::string("ole new_child failed: ") + name;
		return false;
	}
	bool ok = true;
	if (!data.empty())
		ok = gsf_output_write(stream, data.size(), &data[0]) != FALSE;
	const bool closed = gsf_output_close(stream) != FALSE;
	g_object_unref(stream);
	if (!ok || !closed)
	{
		reason = std::string("ole stream write/close failed: ") + name;
		return false;
	}
	return true;
}

static bool build_ole_bytes(const Buf & word, const Buf & table, const Buf & data, Buf & out, std::string & reason)
{
	out.clear();
	GsfOutput * memory = gsf_output_memory_new();
	if (!memory)
	{
		reason = "gsf_output_memory_new failed";
		return false;
	}
	GsfOutfile * ole = gsf_outfile_msole_new(memory);
	if (!ole)
	{
		g_object_unref(memory);
		reason = "gsf_outfile_msole_new failed";
		return false;
	}

	bool ok = write_ole_stream(ole, "WordDocument", word, reason) &&
	          write_ole_stream(ole, "1Table", table, reason);
	if (ok && !data.empty())
		ok = write_ole_stream(ole, "Data", data, reason);

	const bool ole_closed = gsf_output_close(GSF_OUTPUT(ole)) != FALSE;
	g_object_unref(ole);
	if (ok && !ole_closed)
	{
		reason = "ole close failed";
		ok = false;
	}
	if (ok)
	{
		const guint8 * bytes = gsf_output_memory_get_bytes(GSF_OUTPUT_MEMORY(memory));
		const gsf_off_t size = gsf_output_size(memory);
		if (!bytes || size <= 0)
		{
			reason = "ole memory buffer empty";
			ok = false;
		}
		else
		{
			out.assign(bytes, bytes + static_cast<size_t>(size));
		}
	}
	g_object_unref(memory);
	return ok;
}

}

IE_Exp_MsWord_97::IE_Exp_MsWord_97(PD_Document * pDocument)
	: IE_Exp(pDocument)
{
}

IE_Exp_MsWord_97::~IE_Exp_MsWord_97()
{
}

// file:///C:/Dir/name.doc and /C:/Dir/name.doc both have to become
// C:\Dir\name.doc. g_filename_from_uri sometimes keeps the leading slash,
// and CreateFile rejects that path.
static void
normalize_windows_path(char * path)
{
#ifdef G_OS_WIN32
	if (!path || !path[0])
		return;
	if (path[0] == '/' &&
	    g_ascii_isalpha(static_cast<unsigned char>(path[1])) &&
	    (path[2] == ':' || path[2] == '|'))
	{
		if (path[2] == '|')
			path[2] = ':';
		memmove(path, path + 1, strlen(path + 1) + 1);
	}
	for (char * p = path; *p; ++p)
		if (*p == '/')
			*p = '\\';
#else
	(void)path;
#endif
}

// The dialog hands back a file URI. A raw path is kept as-is.
static char *
local_path_for_save(const char * name)
{
	if (!name || !*name)
		return nullptr;

	char * local = nullptr;
	if (!UT_go_path_is_uri(name) && g_ascii_strncasecmp(name, "file:", 5) != 0)
		local = g_strdup(name);
	else
	{
		local = UT_go_filename_from_uri(name);
		if (!local || !*local)
		{
			g_free(local);
			local = nullptr;
			if (g_ascii_strncasecmp(name, "file:", 5) != 0)
				return nullptr;
			const char * path = name + 5;
			if (path[0] == '/' && path[1] == '/' && path[2] == '/')
				path += 2;
			else if (path[0] == '/' && path[1] == '/')
			{
				path += 2;
				const char * slash = strchr(path, '/');
				if (!slash)
					return nullptr;
				path = slash;
			}
			local = g_uri_unescape_string(path, nullptr);
			if (!local || !*local)
			{
				g_free(local);
				local = g_strdup(path);
			}
		}
	}
	if (!local || !*local)
	{
		g_free(local);
		return nullptr;
	}
	normalize_windows_path(local);
	return local;
}

#ifdef G_OS_WIN32
// g_fopen and g_file_set_contents_full() both go through the C runtime.
// The direct-write flag still passes O_NOFOLLOW to _wopen, which rejects
// it, so the export reports a write error without creating the .doc.
// CreateFileW is the call those wrappers are supposed to make.
static bool
write_word_file(const char * path, const guint8 * bytes, size_t size, std::string & reason)
{
	wchar_t * wide = reinterpret_cast<wchar_t *>(g_utf8_to_utf16(path, -1, nullptr, nullptr, nullptr));
	if (!wide)
	{
		reason = "utf8_to_utf16 failed";
		return false;
	}

	HANDLE file = INVALID_HANDLE_VALUE;
	DWORD lastErr = 0;
	for (int attempt = 0; attempt < 6 && file == INVALID_HANDLE_VALUE; ++attempt)
	{
		if (attempt)
			Sleep(50);
		file = CreateFileW(wide,
			GENERIC_WRITE,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			nullptr,
			CREATE_ALWAYS,
			FILE_ATTRIBUTE_NORMAL,
			nullptr);
		lastErr = (file == INVALID_HANDLE_VALUE) ? GetLastError() : 0;
		if (file == INVALID_HANDLE_VALUE && lastErr != ERROR_SHARING_VIOLATION)
			break;
	}
	if (file == INVALID_HANDLE_VALUE &&
	    g_ascii_isalpha(static_cast<unsigned char>(path[0])) &&
	    path[1] == ':' && path[2] == '\\')
	{
		const size_t chars = wcslen(wide);
		wchar_t * extended = g_new(wchar_t, chars + 5);
		memcpy(extended, L"\\\\?\\", 4 * sizeof(wchar_t));
		memcpy(extended + 4, wide, (chars + 1) * sizeof(wchar_t));
		file = CreateFileW(extended,
			GENERIC_WRITE,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			nullptr,
			CREATE_ALWAYS,
			FILE_ATTRIBUTE_NORMAL,
			nullptr);
		if (file == INVALID_HANDLE_VALUE)
			lastErr = GetLastError();
		g_free(extended);
	}
	if (file == INVALID_HANDLE_VALUE)
	{
		char msg[128];
		snprintf(msg, sizeof(msg), "CreateFileW err=%lu", static_cast<unsigned long>(lastErr));
		reason = msg;
		g_free(wide);
		return false;
	}

	const guint8 * cursor = bytes;
	size_t left = size;
	bool wrote_all = true;
	while (left > 0)
	{
		const DWORD chunk = left > 1048576u ? 1048576u : static_cast<DWORD>(left);
		DWORD wrote = 0;
		if (!WriteFile(file, cursor, chunk, &wrote, nullptr) || wrote != chunk)
		{
			char msg[128];
			snprintf(msg, sizeof(msg), "WriteFile err=%lu", static_cast<unsigned long>(GetLastError()));
			reason = msg;
			wrote_all = false;
			break;
		}
		cursor += wrote;
		left -= wrote;
	}
	CloseHandle(file);
	if (!wrote_all)
		DeleteFileW(wide);
	g_free(wide);
	return wrote_all;
}
#else
static bool
write_word_file(const char * path, const guint8 * bytes, size_t size, std::string & reason)
{
	FILE * file = g_fopen(path, "wb");
	if (!file)
	{
		reason = "g_fopen failed";
		return false;
	}
	const size_t wrote = fwrite(bytes, 1, size, file);
	const int flushed = fflush(file);
	const int closed = fclose(file);
	const bool ok = wrote == size && flushed == 0 && closed == 0;
	if (!ok)
		reason = "fwrite/fclose failed";
	if (wrote != size)
		g_unlink(path);
	return ok;
}
#endif

static bool
save_word_bytes(const char * name, const guint8 * bytes, size_t size, std::string & reason)
{
	if (!name || !*name || !bytes || size == 0)
	{
		reason = "invalid output arguments";
		return false;
	}

	char * path = local_path_for_save(name);
	if (path && *path)
	{
		if (write_word_file(path, bytes, size, reason))
		{
			g_free(path);
			return true;
		}
	}
	else
	{
		reason = "local_path_for_save failed";
	}
	g_free(path);

	// Fallback to the standard UT_go_file_create pipeline used by other
	// exporters (e.g. .docx, .odt, .abw) when direct file opening fails.
	GError * err = nullptr;
	GsfOutput * out = UT_go_file_create(name, &err);
	if (out)
	{
		const gboolean wrote = gsf_output_write(out, size, bytes);
		const gboolean closed = gsf_output_close(out);
		g_object_unref(out);
		if (wrote && closed)
			return true;
		reason += "; UT_go_file_create write/close failed";
	}
	else if (err)
	{
		reason += std::string("; UT_go_file_create: ") + (err->message ? err->message : "error");
		g_error_free(err);
	}
	return false;
}

GsfOutput * IE_Exp_MsWord_97::_openFile(const char * /*szFilename*/)
{
	// The OLE file is assembled in this buffer, then copied to disk in
	// _writeDocument. Closing the buffer does not rename anything.
	return gsf_output_memory_new();
}

UT_Error IE_Exp_MsWord_97::_writeDocument(void)
{
	std::vector<Picture> pictures;
	FontCatalog fonts;
	std::vector<Paragraph> paragraphs = collect_paragraphs(getDoc(), pictures, fonts);
	Headers headers = collect_headers(getDoc(), pictures, fonts);
	const Buf data = layout_pictures(pictures);
	seal_pictures(paragraphs, pictures);
	for (std::map<std::string, Story>::iterator it = headers.stories.begin(); it != headers.stories.end(); ++it)
		seal_pictures(it->second.paragraphs, pictures);

	std::vector<uint16_t> units;
	std::vector<ChRun> runs;
	std::vector<unsigned> bounds;
	std::vector<std::string> pap_grpprls;
	bounds.push_back(FC_MIN);
	append_paragraphs(paragraphs, units, runs, bounds, pap_grpprls);
	const unsigned ccp_text = static_cast<unsigned>(units.size());

	// LibreOffice leaves ccpHdd at 0 when the document has no header. A
	// document that has one gets the six MS-DOC stories after the main text.
	unsigned hf_flags = 0;
	std::vector<unsigned> hdd_cp;
	unsigned ccp_hdd = 0;
	if (needs_header_text(headers))
	{
		// MS-DOC PlcfHdd starts with six empty footnote and endnote separator
		// stories, then the six header and footer stories. An empty story is
		// two equal character positions. A story that has text ends with its
		// own paragraph mark and one extra guard mark, which is what
		// LibreOffice writes and what Word expects before it shows the header.
		std::vector<Paragraph> slot_text[6];
		std::set<std::string> seen[6];
		for (size_t s = 0; s < headers.sections.size(); ++s)
		{
			for (int slot = 0; slot < 6; ++slot)
			{
				const std::string & id = headers.sections[s].slot[slot];
				if (id.empty() || !seen[slot].insert(id).second)
					continue;
				std::map<std::string, Story>::const_iterator story = headers.stories.find(id);
				if (story == headers.stories.end())
					continue;
				slot_text[slot].insert(slot_text[slot].end(),
					story->second.paragraphs.begin(), story->second.paragraphs.end());
			}
		}
		hf_flags = header_flags(slot_text);
		for (int separator = 0; separator < 6; ++separator)
			hdd_cp.push_back(static_cast<unsigned>(units.size()) - ccp_text);
		for (int slot = 0; slot < 6; ++slot)
		{
			hdd_cp.push_back(static_cast<unsigned>(units.size()) - ccp_text);
			if (slot_text[slot].empty())
				continue;
			append_paragraphs(slot_text[slot], units, runs, bounds, pap_grpprls);
			append_paragraphs(std::vector<Paragraph>(1), units, runs, bounds, pap_grpprls);
		}
		ccp_hdd = static_cast<unsigned>(units.size()) - ccp_text;
		if (ccp_hdd)
		{
			// MS-DOC 2.8.22 PlcfHdd has n + 2 CPs (14 for 12 stories):
			// the second-to-last CP equals ccpHdd and the last CP is the
			// ignored guard position. MS-DOC 2.8.35 PlcPcd requires one
			// extra trailing paragraph mark after non-empty subdocuments
			// (last CP = ccpText + ccpHdd + 1).
			hdd_cp.push_back(ccp_hdd);
			hdd_cp.push_back(ccp_hdd + 1);
			append_paragraphs(std::vector<Paragraph>(1), units, runs, bounds, pap_grpprls);
		}
		else
		{
			hdd_cp.clear();
		}
	}
	if (runs.empty())
	{
		ChRun run;
		run.fc = FC_MIN;
		runs.push_back(run);
	}

	const unsigned fc_mac = FC_MIN + static_cast<unsigned>(units.size() * 2);
	const unsigned ccp = ccp_text;
	if (fc_mac < FC_MIN)
		return UT_IE_COULDNOTWRITE;

	const std::vector<FkpPage> ch_pages = pack_chpx(runs, fc_mac);
	const std::vector<FkpPage> pap_pages = pack_papx(bounds, pap_grpprls);
	unsigned fkp_at = (fc_mac + PAGE - 1) & ~(PAGE - 1);
	Buf word(fkp_at, 0);
	for (size_t i = 0; i < units.size(); ++i)
	{
		word[FC_MIN + i * 2] = static_cast<unsigned char>(units[i] & 0xff);
		word[FC_MIN + i * 2 + 1] = static_cast<unsigned char>((units[i] >> 8) & 0xff);
	}

	unsigned pn = fkp_at / PAGE;
	std::vector<unsigned> ch_pns;
	for (size_t i = 0; i < ch_pages.size(); ++i)
	{
		word.insert(word.end(), ch_pages[i].page.begin(), ch_pages[i].page.end());
		ch_pns.push_back(pn++);
	}
	std::vector<unsigned> pap_pns;
	for (size_t i = 0; i < pap_pages.size(); ++i)
	{
		word.insert(word.end(), pap_pages[i].page.begin(), pap_pages[i].page.end());
		pap_pns.push_back(pn++);
	}

	Buf table;
	const Buf stsh = stylesheet();
	const unsigned stsh_at = static_cast<unsigned>(table.size());
	table.insert(table.end(), stsh.begin(), stsh.end());

	const Buf sepx = section_properties(getDoc(), hf_flags);
	const unsigned sepx_at = static_cast<unsigned>(word.size());
	word.insert(word.end(), sepx.begin(), sepx.end());

	const unsigned sed_at = static_cast<unsigned>(table.size());
	append_u32(table, 0);
	append_u32(table, ccp);
	append_u16(table, 0);
	append_u32(table, sepx_at);
	append_u16(table, 0);
	append_u32(table, 0xFFFFFFFF);
	const unsigned sed_len = static_cast<unsigned>(table.size() - sed_at);

	const Buf ch_bin = bin_table(ch_pages, ch_pns);
	const unsigned ch_at = static_cast<unsigned>(table.size());
	table.insert(table.end(), ch_bin.begin(), ch_bin.end());
	const Buf pap_bin = bin_table(pap_pages, pap_pns);
	const unsigned pap_at = static_cast<unsigned>(table.size());
	table.insert(table.end(), pap_bin.begin(), pap_bin.end());
	const Buf fontBytes = font_table(fonts);
	const unsigned font_at = static_cast<unsigned>(table.size());
	table.insert(table.end(), fontBytes.begin(), fontBytes.end());

	// FibRgFcLcb97 index 11 is fcPlcfHdd / lcbPlcfHdd. The CPs are relative
	// to the header subdocument and the last one equals ccpHdd.
	Buf hdd;
	unsigned hdd_at = 0;
	if (ccp_hdd)
	{
		for (size_t i = 0; i < hdd_cp.size(); ++i)
			append_u32(hdd, hdd_cp[i]);
		hdd_at = static_cast<unsigned>(table.size());
		table.insert(table.end(), hdd.begin(), hdd.end());
	}

	const unsigned dop_at = static_cast<unsigned>(table.size());
	table.insert(table.end(), 500, 0);
	table[dop_at] = 0x02;
	if (hf_flags & 0x05)
		table[dop_at] |= 0x01;
	poke_u16(table, dop_at + 10, 720);

	const unsigned clx_at = static_cast<unsigned>(table.size());
	Buf plc;
	append_u32(plc, 0);
	append_u32(plc, static_cast<unsigned>(units.size()));
	plc.push_back(0);
	plc.push_back(0);
	append_u32(plc, FC_MIN);
	append_u16(plc, 0);
	table.push_back(0x02);
	append_u32(table, static_cast<unsigned>(plc.size()));
	table.insert(table.end(), plc.begin(), plc.end());
	const unsigned clx_len = static_cast<unsigned>(table.size() - clx_at);

	Buf fib(FIB_BYTES, 0);
	poke_u16(fib, 0x00, 0xA5EC);
	poke_u16(fib, 0x02, 0x00C1);
	poke_u16(fib, 0x06, 0x0409);
	// Bit 3 of FibBase is fHasPic. Word skips the picture character when it is clear.
	poke_u16(fib, 0x0A, data.empty() ? 0x1200 : 0x1208);
	poke_u16(fib, 0x0C, 0x00BF);
	fib[0x13] = 0x10;
	poke_u32(fib, 0x18, FC_MIN);
	poke_u32(fib, 0x1C, fc_mac);
	poke_u16(fib, 0x20, 0x000E);
	poke_u16(fib, 0x3E, 0x0016);
	poke_u32(fib, 0x40, static_cast<unsigned>(word.size()));
	poke_u32(fib, 0x4C, ccp);
	poke_u32(fib, 0x54, ccp_hdd);
	poke_u16(fib, 0x98, 93);
	place(fib, 1, stsh_at, static_cast<unsigned>(stsh.size()));
	place(fib, 6, sed_at, sed_len);
	if (ccp_hdd)
		place(fib, 11, hdd_at, static_cast<unsigned>(hdd.size()));
	place(fib, 12, ch_at, static_cast<unsigned>(ch_bin.size()));
	place(fib, 13, pap_at, static_cast<unsigned>(pap_bin.size()));
	place(fib, 15, font_at, static_cast<unsigned>(fontBytes.size()));
	place(fib, 31, dop_at, 500);
	place(fib, 33, clx_at, clx_len);
	memcpy(&word[0], &fib[0], FIB_BYTES);

	std::string reason;
	Buf ole_bytes;
	if (!getFp())
	{
		IE_Exp::setLastErrorDetail("null output stream");
		return UT_IE_COULDNOTWRITE;
	}
	if (!build_ole_bytes(word, table, data, ole_bytes, reason))
	{
		IE_Exp::setLastErrorDetail(reason);
		return UT_IE_COULDNOTWRITE;
	}
	if (!GSF_IS_OUTPUT_MEMORY(getFp()))
	{
		if (!gsf_output_write(getFp(), ole_bytes.size(), &ole_bytes[0]))
		{
			IE_Exp::setLastErrorDetail("gsf_output_write failed");
			return UT_IE_COULDNOTWRITE;
		}
		return UT_OK;
	}
	if (!save_word_bytes(getFileName(), &ole_bytes[0], ole_bytes.size(), reason))
	{
		IE_Exp::setLastErrorDetail(reason);
		return UT_IE_COULDNOTWRITE;
	}
	return UT_OK;
}
