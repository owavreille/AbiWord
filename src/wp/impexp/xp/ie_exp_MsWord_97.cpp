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
 * text at fcMin, formatted-disk pages after the text, and the piece table,
 * stylesheet, section table and document properties in the 1Table stream.
 */

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <gsf/gsf-outfile.h>
#include <gsf/gsf-outfile-msole.h>
#include <gsf/gsf-output-memory.h>
#include <gsf/gsf-output.h>

#include "ie_exp_MsWord_97.h"
#include "pd_Document.h"
#include "pt_PieceTable.h"
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
	grpprl.push_back(static_cast<char>(operand & 0xff));
	if (width == 2)
		grpprl.push_back(static_cast<char>((operand >> 8) & 0xff));
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

static std::vector<Paragraph> collect_paragraphs(PD_Document * doc)
{
	std::vector<Paragraph> paragraphs;
	Paragraph current;
	bool accept = true;
	bool in_paragraph = false;
	pt_PieceTable * table = doc->getPieceTable();
	if (!table)
		return paragraphs;

	for (pf_Frag * frag = table->getFragments().getFirst(); frag; frag = frag->getNext())
	{
		if (frag->getType() == pf_Frag::PFT_Strux)
		{
			const PTStruxType kind = static_cast<pf_Frag_Strux *>(frag)->getStruxType();
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
			if (kind == PTX_Section)
				accept = true;
			else if (skipped_section(kind))
				accept = false;
			continue;
		}
		if (!accept || frag->getType() != pf_Frag::PFT_Text)
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
		if (!current.runs.empty() && current.runs.back().grpprl == run.grpprl)
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
	return (count + 1) * 4 + count + grp + 1 <= PAGE;
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

static bool papx_fits(unsigned count)
{
	return (count + 1) * 4 + count * 13 + 1 <= PAGE;
}

static Buf make_papx(const std::vector<unsigned> & bounds, size_t from, size_t until)
{
	Buf page(PAGE, 0);
	unsigned pos = 0;
	for (size_t i = from; i <= until; ++i)
	{
		poke_u32(page, pos, bounds[i]);
		pos += 4;
	}
	page[511] = static_cast<unsigned char>(until - from);
	return page;
}

static std::vector<FkpPage> pack_papx(const std::vector<unsigned> & bounds)
{
	std::vector<FkpPage> pages;
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
	Buf std;
	append_u16(std, 0);
	append_u16(std, 1 | (0x0FFF << 4));
	append_u16(std, 2);
	append_u16(std, 0);
	append_u16(std, 0);
	append_u16(std, 6);
	const char * name = "Normal";
	for (const char * p = name; *p; ++p)
		append_u16(std, static_cast<unsigned char>(*p));
	append_u16(std, 0);
	append_u16(std, 2);
	append_u16(std, 0);
	append_u16(std, 0);

	Buf stshi;
	append_u16(stshi, 1);
	append_u16(stshi, 10);
	append_u16(stshi, 1);
	append_u16(stshi, 0);
	append_u16(stshi, 0);
	append_u16(stshi, 0);
	append_u16(stshi, 0);
	append_u16(stshi, 0);
	append_u16(stshi, 0);

	Buf out;
	append_u16(out, static_cast<unsigned>(stshi.size()));
	out.insert(out.end(), stshi.begin(), stshi.end());
	append_u16(out, static_cast<unsigned>(std.size()));
	out.insert(out.end(), std.begin(), std.end());
	return out;
}

static Buf font_table()
{
	const char * name = "Times New Roman";
	Buf body;
	body.push_back(0);
	body.push_back(static_cast<unsigned char>(2 | 4 | (1 << 4)));
	append_u16(body, 400);
	body.push_back(0);
	body.push_back(0);
	body.insert(body.end(), 10, 0);
	body.insert(body.end(), 24, 0);
	for (const char * p = name; *p; ++p)
		append_u16(body, static_cast<unsigned char>(*p));
	append_u16(body, 0);
	body[0] = static_cast<unsigned char>(body.size() - 1);

	Buf out;
	append_u16(out, 0xFFFF);
	append_u16(out, 1);
	append_u16(out, 0);
	out.insert(out.end(), body.begin(), body.end());
	return out;
}

static Buf bin_table(const std::vector<FkpPage> & pages, const std::vector<unsigned> & pns)
{
	Buf out;
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

static bool write_ole(GsfOutput * destination, const Buf & word, const Buf & table)
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
	if (!word_stream || !table_stream)
		ok = false;
	if (ok && !word.empty())
		ok = gsf_output_write(word_stream, word.size(), &word[0]) && ok;
	if (ok && !table.empty())
		ok = gsf_output_write(table_stream, table.size(), &table[0]) && ok;
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

UT_Error IE_Exp_MsWord_97::_writeDocument(void)
{
	const std::vector<Paragraph> paragraphs = collect_paragraphs(getDoc());
	std::vector<uint16_t> units;
	std::vector<ChRun> runs;
	std::vector<unsigned> bounds;
	bounds.push_back(FC_MIN);

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
	if (runs.empty())
	{
		ChRun run;
		run.fc = FC_MIN;
		runs.push_back(run);
	}

	const unsigned fc_mac = FC_MIN + static_cast<unsigned>(units.size() * 2);
	const unsigned ccp = static_cast<unsigned>(units.size());
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

	const unsigned sed_at = static_cast<unsigned>(table.size());
	append_u32(table, 0);
	append_u32(table, ccp);
	append_u16(table, 0);
	append_u32(table, 0xFFFFFFFF);
	append_u16(table, 0);
	append_u32(table, 0);
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

	const unsigned dop_at = static_cast<unsigned>(table.size());
	table.insert(table.end(), 500, 0);
	table[dop_at] = 0x02;
	poke_u16(table, dop_at + 10, 720);

	const unsigned clx_at = static_cast<unsigned>(table.size());
	Buf plc;
	append_u32(plc, 0);
	append_u32(plc, ccp);
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
	poke_u16(fib, 0x0A, 0x1200);
	poke_u16(fib, 0x0C, 0x00BF);
	fib[0x13] = 0x10;
	poke_u32(fib, 0x18, FC_MIN);
	poke_u32(fib, 0x1C, fc_mac);
	poke_u16(fib, 0x20, 0x000E);
	poke_u16(fib, 0x3E, 0x0016);
	poke_u32(fib, 0x40, static_cast<unsigned>(word.size()));
	poke_u32(fib, 0x4C, ccp);
	poke_u16(fib, 0x98, 93);
	place(fib, 1, stsh_at, static_cast<unsigned>(stsh.size()));
	place(fib, 6, sed_at, sed_len);
	place(fib, 12, ch_at, static_cast<unsigned>(ch_bin.size()));
	place(fib, 13, pap_at, static_cast<unsigned>(pap_bin.size()));
	place(fib, 15, font_at, static_cast<unsigned>(fonts.size()));
	place(fib, 31, dop_at, 500);
	place(fib, 33, clx_at, clx_len);
	memcpy(&word[0], &fib[0], FIB_BYTES);

	if (!getFp() || !write_ole(getFp(), word, table))
		return UT_IE_COULDNOTWRITE;
	return UT_OK;
}
