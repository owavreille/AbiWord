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

#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <glib.h>
#include <glib/gstdio.h>

#include <gsf/gsf-outfile.h>
#include <gsf/gsf-outfile-msole.h>
#include <gsf/gsf-output-memory.h>
#include <gsf/gsf-output-stdio.h>
#include <gsf/gsf-output.h>

#include "ut_go_file.h"

#include "ie_exp_MsWord_97.h"
#include "pd_Document.h"
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

static std::string character_grpprl(const PP_AttrProp * ap)
{
	std::string grpprl;
	if (!ap)
		return grpprl;
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

static std::vector<Paragraph> collect_paragraphs(PD_Document * doc, std::vector<Picture> & pics)
{
	std::vector<Paragraph> paragraphs;
	Paragraph current;
	bool accept = true;
	bool in_paragraph = false;
	int frame_depth = 0;
	int skip_depth = 0;
	pt_PieceTable * table = doc->getPieceTable();
	if (!table)
		return paragraphs;

	for (pf_Frag * frag = table->getFragments().getFirst(); frag; frag = frag->getNext())
	{
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
		run.grpprl = character_grpprl(props);
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

static void append_run(Paragraph & current, const PP_AttrProp * props, const UT_UCS4Char * chars, UT_uint32 length)
{
	Run run;
	run.grpprl = character_grpprl(props);
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

static Headers collect_headers(PD_Document * doc, std::vector<Picture> & pics)
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

	for (pf_Frag * frag = table->getFragments().getFirst(); frag; frag = frag->getNext())
	{
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
				DocSection section;
				for (int slot = 0; slot < 6; ++slot)
				{
					const char * id = frag_attribute(doc, frag, HDD_SLOT[slot]);
					if (id)
						section.slot[slot] = id;
				}
				out.sections.push_back(section);
				continue;
			}
			if (kind == PTX_SectionHdrFtr)
			{
				skip_depth = 0;
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
				if (in_story && skip_depth == 0)
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
				if (in_story)
					++skip_depth;
				continue;
			}
			if (skipped_section(kind))
			{
				if (in_story)
					++skip_depth;
				continue;
			}
			if (skip_depth && (kind == PTX_EndFootnote || kind == PTX_EndEndnote ||
				kind == PTX_EndFrame || kind == PTX_EndTOC || kind == PTX_EndAnnotation ||
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
		append_run(current, props, doc->getPointer(text->getBufIndex()), text->getLength());
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
	std::vector<uint16_t> & units, std::vector<ChRun> & runs, std::vector<unsigned> & bounds)
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
		units.push_back(0x000D);
		bounds.push_back(FC_MIN + static_cast<unsigned>(units.size() * 2));
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

static const unsigned PAPX_BYTES = 6;
static const unsigned PAPX_MAX = 0x1D;

static bool papx_fits(unsigned count)
{
	if (count == 0 || count > PAPX_MAX)
		return false;
	unsigned at = (count + 1) * 4 + count * 13;
	if (at & 1)
		++at;
	return at + PAPX_BYTES < PAGE;
}

// PapxInFkp: cb = 3, so the following GrpPrlAndIstd is 2*cb-1 = 5 bytes.
// Those bytes are the paragraph style (istd 0, Normal) and sprmPJc80 = left.
static Buf make_papx(const std::vector<unsigned> & bounds, size_t from, size_t until)
{
	Buf page(PAGE, 0);
	const unsigned count = static_cast<unsigned>(until - from);
	unsigned pos = 0;
	for (size_t i = from; i <= until; ++i)
	{
		poke_u32(page, pos, bounds[i]);
		pos += 4;
	}
	unsigned papx = pos + count * 13;
	if (papx & 1)
		++papx;
	page[papx] = 3;
	page[papx + 1] = 0;
	page[papx + 2] = 0;
	page[papx + 3] = 0x03;
	page[papx + 4] = 0x24;
	page[papx + 5] = 0x00;
	const unsigned word = papx / 2;
	for (unsigned i = 0; i < count; ++i)
		page[pos + i * 13] = static_cast<unsigned char>(word);
	page[511] = static_cast<unsigned char>(count);
	return page;
}

static std::vector<FkpPage> pack_papx(const std::vector<unsigned> & bounds)
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
			if (papx_fits(static_cast<unsigned>(j - i)))
				best = j;
			else
				break;
		}
		FkpPage page;
		page.start = bounds[i];
		page.limit = bounds[best];
		page.page = make_papx(bounds, i, best);
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

static Buf font_table()
{
	// SttbfFfn is a non-extended STTB. cData 0xFFFF would mark it extended
	// and Word then rejects the file. The string is one FFN: Roman TrueType
	// Times New Roman, weight 400, no alternate name.
	const char * name = "Times New Roman";
	Buf ffn;
	ffn.push_back(static_cast<unsigned char>(0x02 | 0x04 | (0x01 << 4)));
	append_u16(ffn, 400);
	ffn.push_back(0);
	ffn.push_back(0);
	ffn.insert(ffn.end(), 10, 0);
	ffn.insert(ffn.end(), 24, 0);
	for (const char * p = name; *p; ++p)
		append_u16(ffn, static_cast<unsigned char>(*p));
	append_u16(ffn, 0);

	Buf out;
	append_u16(out, 1);
	append_u16(out, 0);
	out.push_back(static_cast<unsigned char>(ffn.size()));
	out.insert(out.end(), ffn.begin(), ffn.end());
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
	if (doc)
	{
		const double w_in = doc->m_docPageSize.Width(DIM_IN);
		const double h_in = doc->m_docPageSize.Height(DIM_IN);
		if (w_in > 0.0)
			page_w = static_cast<unsigned>(w_in * 1440.0 + 0.5);
		if (h_in > 0.0)
			page_h = static_cast<unsigned>(h_in * 1440.0 + 0.5);
	}
	// Sepx. Every section must state its page size and its four margins.
	const unsigned sprm[][2] = {
		{ 0xB01F, page_w },
		{ 0xB020, page_h },
		{ 0xB021, 1440 },
		{ 0xB022, 1440 },
		{ 0x9023, 1440 },
		{ 0x9024, 1440 },
		{ 0xB025, 0 },
		{ 0xB017, 720 },
		{ 0xB018, 720 }
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

static bool write_ole(GsfOutput * destination, const Buf & word, const Buf & table, const Buf & data)
{
	GsfOutput * memory = gsf_output_memory_new();
	if (!memory)
		return false;
	GsfOutfile * ole = gsf_outfile_msole_new(memory);
	if (!ole)
	{
		g_object_unref(memory);
		return false;
	}

	bool ok = true;
	GsfOutput * word_stream = gsf_outfile_new_child(ole, "WordDocument", FALSE);
	GsfOutput * table_stream = gsf_outfile_new_child(ole, "1Table", FALSE);
	GsfOutput * data_stream = nullptr;
	if (!data.empty())
		data_stream = gsf_outfile_new_child(ole, "Data", FALSE);
	if (!word_stream || !table_stream || (!data.empty() && !data_stream))
		ok = false;
	if (ok && !word.empty())
		ok = gsf_output_write(word_stream, word.size(), &word[0]) && ok;
	if (ok && !table.empty())
		ok = gsf_output_write(table_stream, table.size(), &table[0]) && ok;
	if (ok && data_stream && !data.empty())
		ok = gsf_output_write(data_stream, data.size(), &data[0]) && ok;
	if (word_stream)
	{
		ok = gsf_output_close(word_stream) && ok;
		g_object_unref(word_stream);
	}
	if (table_stream)
	{
		ok = gsf_output_close(table_stream) && ok;
		g_object_unref(table_stream);
	}
	if (data_stream)
	{
		ok = gsf_output_close(data_stream) && ok;
		g_object_unref(data_stream);
	}
	ok = gsf_output_close(GSF_OUTPUT(ole)) && ok;
	g_object_unref(ole);
	if (ok)
	{
		const guint8 * bytes = gsf_output_memory_get_bytes(GSF_OUTPUT_MEMORY(memory));
		const gsf_off_t size = gsf_output_size(memory);
		ok = bytes && size > 0 &&
			gsf_output_write(destination, static_cast<size_t>(size), bytes);
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

GsfOutput * IE_Exp_MsWord_97::_openFile(const char * szFilename)
{
	// The usual saver writes .gsf-save-XXXXXX and renames it when the
	// file is closed. Windows refuses that rename for a .doc name, so
	// the export reports that it could not write. Open the destination.
	char * local = UT_go_filename_from_uri(szFilename);
	const char * path = (local && *local) ? local : szFilename;
	FILE * file = path ? g_fopen(path, "wb") : nullptr;
	GsfOutput * output = nullptr;
	if (file)
		output = gsf_output_stdio_new_FILE(path ? path : szFilename, file, FALSE);
	g_free(local);
	return output;
}

UT_Error IE_Exp_MsWord_97::_writeDocument(void)
{
	std::vector<Picture> pictures;
	std::vector<Paragraph> paragraphs = collect_paragraphs(getDoc(), pictures);
	Headers headers = collect_headers(getDoc(), pictures);
	const Buf data = layout_pictures(pictures);
	seal_pictures(paragraphs, pictures);
	for (std::map<std::string, Story>::iterator it = headers.stories.begin(); it != headers.stories.end(); ++it)
		seal_pictures(it->second.paragraphs, pictures);

	std::vector<uint16_t> units;
	std::vector<ChRun> runs;
	std::vector<unsigned> bounds;
	bounds.push_back(FC_MIN);
	append_paragraphs(paragraphs, units, runs, bounds);
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
			{
				if (slot >= 4)
					append_paragraphs(std::vector<Paragraph>(2), units, runs, bounds);
				continue;
			}
			append_paragraphs(slot_text[slot], units, runs, bounds);
			append_paragraphs(std::vector<Paragraph>(1), units, runs, bounds);
		}
		hdd_cp.push_back(static_cast<unsigned>(units.size()) - ccp_text);
		ccp_hdd = static_cast<unsigned>(units.size()) - ccp_text;
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
	const std::vector<FkpPage> pap_pages = pack_papx(bounds);
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
	const Buf fonts = font_table();
	const unsigned font_at = static_cast<unsigned>(table.size());
	table.insert(table.end(), fonts.begin(), fonts.end());

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
	append_u32(plc, ccp_text + ccp_hdd);
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
	place(fib, 15, font_at, static_cast<unsigned>(fonts.size()));
	place(fib, 31, dop_at, 500);
	place(fib, 33, clx_at, clx_len);
	memcpy(&word[0], &fib[0], FIB_BYTES);

	if (!getFp() || !write_ole(getFp(), word, table, data))
		return UT_IE_COULDNOTWRITE;
	return UT_OK;
}
