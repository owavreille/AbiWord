/* AbiWord
 * Copyright (C) 2026
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <glib.h>
#include <zlib.h>
#include <gsf/gsf-infile.h>
#include <gsf/gsf-infile-msole.h>
#include <gsf/gsf-infile-zip.h>
#include <gsf/gsf-input-stdio.h>

#include "tf_test.h"
#include "pd_Document.h"
#include "ut_bytebuf.h"
#include "pt_PieceTable.h"
#include "pf_Frag_Strux.h"
#include "pf_Frag_Text.h"
#include "ie_exp.h"
#include "ie_imp.h"
#include "ut_path.h"
#include "ut_string_class.h"
#include "xap_ModuleManager.h"

#define TFSUITE "core.wp.impexp.formats"

namespace {

static const char MARKER[] = "AbiFmtRegression";
static const char MARKER_NEXT[] = "AbiFmtSecond";

static std::string document_text(PD_Document * doc)
{
	std::string text;
	pt_PieceTable * table = doc ? doc->getPieceTable() : nullptr;
	if (!table)
		return text;
	for (pf_Frag * frag = table->getFragments().getFirst(); frag; frag = frag->getNext())
	{
		if (frag->getType() != pf_Frag::PFT_Text)
			continue;
		text += static_cast<pf_Frag_Text *>(frag)->toString();
	}
	return text;
}

static bool load_text(const std::string & path, std::string & text)
{
	PD_Document * doc = new PD_Document();
	const UT_Error err = doc->readFromFile(path.c_str(), IEFT_Unknown, nullptr);
	const bool ok = TFPASS(UT_IS_IE_SUCCESS(err));
	if (ok)
		text = document_text(doc);
	doc->unref();
	return ok;
}

static bool exporter_known(const char * suffix)
{
	const IEFileType type = IE_Exp::fileTypeForSuffix(suffix);
	return type != IEFT_Unknown && type != IEFT_Bogus;
}

static bool try_load_plugin(const std::string & path)
{
	if (path.empty() || !g_file_test(path.c_str(), G_FILE_TEST_IS_REGULAR))
		return false;
	return XAP_ModuleManager::instance().loadModule(path.c_str());
}

static void ensure_plugin(const char * suffix, const char * name)
{
	if (exporter_known(suffix))
		return;

	const char * exts[] = { "." G_MODULE_SUFFIX, ".so", ".dylib", nullptr };
	std::string roots[3];
	roots[0] = std::string("../../../plugins/") + name + "/.libs/" + name;
	const char * extra = std::getenv("ABI_TEST_PLUGIN_DIR");
	if (extra && *extra)
		roots[1] = std::string(extra) + "/" + name + "/.libs/" + name;

	for (size_t root = 0; root < G_N_ELEMENTS(roots); ++root)
	{
		if (roots[root].empty())
			continue;
		for (const char ** ext = exts; *ext; ++ext)
		{
			if (try_load_plugin(roots[root] + *ext))
				return;
		}
	}
}

static PT_DocPosition first_block_content(PD_Document * doc)
{
	pt_PieceTable * table = doc->getPieceTable();
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

static PD_Document * sample_document()
{
	PD_Document * doc = new PD_Document();
	if (doc->newDocument() != UT_OK)
	{
		doc->unref();
		return nullptr;
	}

	const PT_DocPosition at = first_block_content(doc);
	if (!doc->insertSpan(at, std::string(MARKER)))
	{
		doc->unref();
		return nullptr;
	}
	const PT_DocPosition split = at + static_cast<PT_DocPosition>(std::strlen(MARKER));
	if (!doc->insertStrux(split, PTX_Block) ||
		!doc->insertSpan(split + 1, std::string(MARKER_NEXT)))
	{
		doc->unref();
		return nullptr;
	}
	return doc;
}

static unsigned le16(const std::string & bytes, std::string::size_type at)
{
	if (at + 2 > bytes.size())
		return 0xffffffffu;
	return static_cast<unsigned char>(bytes[at]) |
		(static_cast<unsigned char>(bytes[at + 1]) << 8);
}

static unsigned le32(const std::string & bytes, std::string::size_type at)
{
	if (at + 4 > bytes.size())
		return 0xffffffffu;
	return le16(bytes, at) | (le16(bytes, at + 2) << 16);
}

static std::string utf16_ascii(const char * ascii)
{
	std::string out;
	for (const char * p = ascii; *p; ++p)
	{
		out.push_back(*p);
		out.push_back('\0');
	}
	return out;
}

static std::string ole_read(GsfInfile * ole, const char * name)
{
	GsfInput * in = gsf_infile_child_by_name(ole, name);
	std::string text;
	if (!in)
		return text;
	const gsf_off_t size = gsf_input_size(in);
	if (size > 0)
	{
		const guint8 * bytes = gsf_input_read(in, static_cast<size_t>(size), nullptr);
		if (bytes)
			text.assign(reinterpret_cast<const char *>(bytes), static_cast<size_t>(size));
	}
	g_object_unref(in);
	return text;
}

// A Word 97 file Word will open: real section properties, a paragraph
// Papx that names a style, a non-extended font table, and Unicode text.
static bool is_office_doc(const std::string & path)
{
	GError * error = nullptr;
	GsfInput * input = gsf_input_stdio_new(path.c_str(), &error);
	if (error)
	{
		g_error_free(error);
		error = nullptr;
	}
	GsfInfile * ole = input ? gsf_infile_msole_new(input, &error) : nullptr;
	if (error)
	{
		g_error_free(error);
		error = nullptr;
	}
	if (!TFPASS(ole != nullptr))
	{
		if (input)
			g_object_unref(input);
		return false;
	}

	const std::string word = ole_read(ole, "WordDocument");
	const std::string table = ole_read(ole, "1Table");
	g_object_unref(ole);
	g_object_unref(input);
	if (!TFPASS(word.size() >= 900 && !table.empty()))
		return false;

	const unsigned fcMin = le32(word, 0x18);
	const unsigned fcMac = le32(word, 0x1C);
	const unsigned ccp = le32(word, 0x4C);
	const unsigned pairs = le16(word, 0x98);
	const bool fib =
		le16(word, 0x00) == 0xA5EC &&
		le16(word, 0x02) == 0x00C1 &&
		le16(word, 0x0C) == 0x00BF &&
		(le16(word, 0x0A) & 0x1200) == 0x1200 &&
		(le16(word, 0x0A) & 0x0004) == 0 &&
		le16(word, 0x20) == 0x000E &&
		le16(word, 0x3E) == 0x0016 &&
		pairs == 93 &&
		le16(word, 0x9A + pairs * 8) == 0 &&
		fcMin == 0x800 && fcMac > fcMin && ((fcMac - fcMin) % 2) == 0 &&
		ccp == (fcMac - fcMin) / 2 &&
		le32(word, 0x40) == word.size();

	const std::string main = word.substr(fcMin, fcMac - fcMin);
	const bool text = main.find(utf16_ascii(MARKER)) != std::string::npos &&
		main.find(utf16_ascii(MARKER_NEXT)) != std::string::npos;

	const unsigned stshAt = le32(word, 0x9A + 1 * 8);
	const unsigned stshLen = le32(word, 0x9A + 1 * 8 + 4);
	bool styles = stshAt + stshLen <= table.size() && stshLen >= 4;
	if (styles)
	{
		const unsigned cbStshi = le16(table, stshAt);
		const unsigned cstd = le16(table, stshAt + 2);
		const unsigned stdAt = stshAt + 2 + cbStshi;
		styles = cstd >= 15 && stdAt + 2 <= table.size();
		if (styles)
		{
			const unsigned cbStd = le16(table, stdAt);
			styles = cbStd >= 8 && stdAt + 2 + cbStd <= table.size() &&
				le16(table, stdAt + 2 + 6) == cbStd;
		}
	}

	const unsigned sedAt = le32(word, 0x9A + 6 * 8);
	const unsigned sedLen = le32(word, 0x9A + 6 * 8 + 4);
	bool section = sedLen >= 20 && sedAt + sedLen <= table.size();
	if (section)
	{
		const unsigned fcSepx = le32(table, sedAt + 10);
		section = le32(table, sedAt) == 0 && le32(table, sedAt + 4) == ccp &&
			fcSepx + 2 < word.size();
		if (section)
		{
			const unsigned cb = le16(word, fcSepx);
			const std::string::size_type sprm = fcSepx + 2;
			section = cb >= 24 && sprm + cb <= word.size() &&
				word.find(std::string("\x1f\xb0", 2), sprm) != std::string::npos &&
				word.find(std::string("\x1f\xb0", 2), sprm) < sprm + cb;
		}
	}

	const unsigned papAt = le32(word, 0x9A + 13 * 8);
	const unsigned papLen = le32(word, 0x9A + 13 * 8 + 4);
	bool paragraphs = papLen >= 12 && (papLen - 4) % 8 == 0 && papAt + papLen <= table.size();
	if (paragraphs)
	{
		const unsigned runs = (papLen - 4) / 8;
		const unsigned pn = le32(table, papAt + (runs + 1) * 4);
		paragraphs = pn != 0 && (pn + 1) * 512 <= word.size();
		if (paragraphs)
		{
			const std::string::size_type page = static_cast<std::string::size_type>(pn) * 512;
			const unsigned cpara = static_cast<unsigned char>(word[page + 511]);
			const unsigned bx = (cpara + 1) * 4;
			paragraphs = cpara >= 1 && page + bx < page + 512;
			if (paragraphs)
			{
				const unsigned offset = static_cast<unsigned char>(word[page + bx]);
				const std::string::size_type papx = page + static_cast<std::string::size_type>(offset) * 2;
				paragraphs = offset != 0 && papx + 3 <= word.size() &&
					static_cast<unsigned char>(word[papx]) != 0 &&
					le16(word, papx + 1) == 0;
			}
		}
	}

	const unsigned fontAt = le32(word, 0x9A + 15 * 8);
	const unsigned fontLen = le32(word, 0x9A + 15 * 8 + 4);
	const bool fonts = fontLen >= 4 && fontAt + fontLen <= table.size() &&
		le16(table, fontAt) >= 1 && le16(table, fontAt) != 0xFFFF &&
		le16(table, fontAt + 2) == 0;

	const unsigned clxAt = le32(word, 0x9A + 33 * 8);
	const unsigned clxLen = le32(word, 0x9A + 33 * 8 + 4);
	bool piece = clxLen >= 13 && clxAt + clxLen <= table.size() &&
		static_cast<unsigned char>(table[clxAt]) == 0x02;
	if (piece)
	{
		const unsigned plc = le32(table, clxAt + 1);
		piece = plc + 5 == clxLen && le32(table, clxAt + 5) == 0 &&
			le32(table, clxAt + 9) == ccp &&
			(le32(table, clxAt + 15) & 0x40000000u) == 0 &&
			(le32(table, clxAt + 15) & 0x3fffffffu) == fcMin;
	}

	return TFPASS(fib) && TFPASS(text) && TFPASS(styles) && TFPASS(section) &&
		TFPASS(paragraphs) && TFPASS(fonts) && TFPASS(piece);
}

static bool text_has_markers(const std::string & text)
{
	return text.find(MARKER) != std::string::npos &&
		text.find(MARKER_NEXT) != std::string::npos;
}

static GsfInput * zip_open(GsfInfile * root, const std::string & path)
{
	GsfInfile * folder = root;
	GsfInput * owned = nullptr;
	std::string rest = path;
	while (!rest.empty())
	{
		const std::string::size_type slash = rest.find('/');
		const std::string name = slash == std::string::npos ? rest : rest.substr(0, slash);
		rest = slash == std::string::npos ? std::string() : rest.substr(slash + 1);
		if (!GSF_IS_INFILE(folder))
		{
			if (owned)
				g_object_unref(owned);
			return nullptr;
		}
		GsfInput * child = gsf_infile_child_by_name(folder, name.c_str());
		if (owned)
			g_object_unref(owned);
		if (!child)
			return nullptr;
		owned = child;
		folder = GSF_INFILE(child);
	}
	return owned;
}

static bool zip_has(GsfInfile * root, const std::string & path)
{
	GsfInput * in = zip_open(root, path);
	if (!in)
		return false;
	g_object_unref(in);
	return true;
}

static std::string zip_read(GsfInfile * root, const std::string & path)
{
	GsfInput * in = zip_open(root, path);
	std::string text;
	if (in)
	{
		const gsf_off_t size = gsf_input_size(in);
		if (size > 0)
		{
			const guint8 * bytes = gsf_input_read(in, static_cast<size_t>(size), nullptr);
			if (bytes)
				text.assign(reinterpret_cast<const char *>(bytes), static_cast<size_t>(size));
		}
		g_object_unref(in);
	}
	return text;
}

static bool quoted_uint(const std::string & tag, const char * attr)
{
	const std::string key = std::string(attr) + "=\"";
	std::string::size_type at = tag.find(key);
	if (at == std::string::npos)
		return false;
	at += key.size();
	if (at >= tag.size() || tag[at] == '"')
		return false;
	for (; at < tag.size() && tag[at] != '"'; ++at)
	{
		if (tag[at] < '0' || tag[at] > '9')
			return false;
	}
	return true;
}

static bool targets_exist(GsfInfile * zip, const std::string & rels, const std::string & base)
{
	std::string::size_type at = 0;
	while ((at = rels.find("<Relationship ", at)) != std::string::npos)
	{
		const std::string::size_type end = rels.find("/>", at);
		if (end == std::string::npos)
			return false;
		const std::string tag = rels.substr(at, end - at);
		at = end + 2;
		if (tag.find("TargetMode=\"External\"") != std::string::npos)
			continue;
		const std::string key = "Target=\"";
		const std::string::size_type value = tag.find(key);
		if (value == std::string::npos)
			return false;
		const std::string::size_type start = value + key.size();
		const std::string::size_type quote = tag.find('"', start);
		if (quote == std::string::npos)
			return false;
		const std::string target = tag.substr(start, quote - start);
		if (target.empty())
			return false;
		const std::string path = target[0] == '/' ? target.substr(1) : base + target;
		if (!zip_has(zip, path))
			return false;
	}
	return true;
}

static bool is_office_docx(const std::string & path)
{
	GError * error = nullptr;
	GsfInput * input = gsf_input_stdio_new(path.c_str(), &error);
	if (error)
	{
		g_error_free(error);
		error = nullptr;
	}
	GsfInfile * zip = input ? gsf_infile_zip_new(input, &error) : nullptr;
	if (error)
	{
		g_error_free(error);
		error = nullptr;
	}
	if (!TFPASS(zip != nullptr))
	{
		if (input)
			g_object_unref(input);
		return false;
	}

	const std::string types = zip_read(zip, "[Content_Types].xml");
	const std::string rels = zip_read(zip, "_rels/.rels");
	const std::string wordRels = zip_read(zip, "word/_rels/document.xml.rels");
	const std::string document = zip_read(zip, "word/document.xml");
	const std::string styles = zip_read(zip, "word/styles.xml");
	const std::string settings = zip_read(zip, "word/settings.xml");
	const std::string core = zip_read(zip, "docProps/core.xml");
	const std::string app = zip_read(zip, "docProps/app.xml");

	bool parts = types.find("application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml") != std::string::npos;
	std::string::size_type at = 0;
	const std::string partKey = "PartName=\"";
	while (parts && (at = types.find(partKey, at)) != std::string::npos)
	{
		at += partKey.size();
		const std::string::size_type quote = types.find('"', at);
		if (quote == std::string::npos || types[at] != '/')
		{
			parts = false;
			break;
		}
		parts = zip_has(zip, types.substr(at + 1, quote - at - 1));
		at = quote + 1;
	}

	const std::string::size_type page = document.find("<w:pgSz");
	const std::string::size_type pageEnd = page == std::string::npos ? std::string::npos : document.find("/>", page);
	const std::string pageTag = (page != std::string::npos && pageEnd != std::string::npos)
		? document.substr(page, pageEnd - page) : std::string();
	const std::string endSect = "</w:sectPr>";
	const std::string endBody = "</w:body>";
	const std::string::size_type sect = document.rfind(endSect);
	const std::string::size_type body = document.rfind(endBody);
	bool sectLast = sect != std::string::npos && body != std::string::npos && sect < body;
	if (sectLast)
	{
		for (std::string::size_type i = sect + endSect.size(); i < body; ++i)
		{
			if (!std::isspace(static_cast<unsigned char>(document[i])))
				sectLast = false;
		}
	}

	const std::string::size_type runDefault = styles.find("<w:rPrDefault>");
	const std::string::size_type paraDefault = styles.find("<w:pPrDefault>");
	const std::string::size_type tabStop = settings.find("<w:defaultTabStop");
	const std::string::size_type compat = settings.find("<w:compat>");
	const std::string::size_type even = settings.find("<w:evenAndOddHeaders");

	const bool ok =
		TFPASS(parts) &&
		TFPASS(targets_exist(zip, rels, "")) &&
		TFPASS(targets_exist(zip, wordRels, "word/")) &&
		TFPASS(text_has_markers(document)) &&
		TFPASS(document.find("xmlns:w14=") != std::string::npos) &&
		TFPASS(document.find("mc:Ignorable=") != std::string::npos) &&
		TFPASS(document.find("w:val=\"continuous\"") == std::string::npos) &&
		TFPASS(sectLast) &&
		TFPASS(quoted_uint(pageTag, "w:w") && quoted_uint(pageTag, "w:h")) &&
		TFPASS(runDefault != std::string::npos && paraDefault != std::string::npos && runDefault < paraDefault) &&
		TFPASS(styles.find("Current Settings") == std::string::npos) &&
		TFPASS(styles.find("w:val=\"None\"") == std::string::npos) &&
		TFPASS(styles.find("w:type=\"P\"") == std::string::npos) &&
		TFPASS(styles.find("w:styleId=\"Normal\"") != std::string::npos) &&
		TFPASS(tabStop != std::string::npos && compat != std::string::npos && tabStop < compat) &&
		TFPASS(even == std::string::npos || (tabStop < even && even < compat)) &&
		TFPASS(settings.find("compatibilityMode") != std::string::npos) &&
		TFPASS(core.find("<dc:creator>AbiWord</dc:creator>") != std::string::npos) &&
		TFPASS(app.find("<Application>AbiWord</Application>") != std::string::npos);

	g_object_unref(zip);
	g_object_unref(input);
	return ok;
}

static bool odt_picture_hrefs(GsfInfile * zip, const std::string & xml, const std::string & manifest)
{
	const std::string key = "xlink:href=\"Pictures/";
	std::string::size_type at = 0;
	while ((at = xml.find(key, at)) != std::string::npos)
	{
		const std::string::size_type start = at + key.size();
		const std::string::size_type end = xml.find('"', start);
		if (end == std::string::npos || end == start)
			return false;
		const std::string path = "Pictures/" + xml.substr(start, end - start);
		if (!zip_has(zip, path))
			return false;
		if (manifest.find("manifest:full-path=\"" + path + "\"") == std::string::npos)
			return false;
		at = end;
	}
	return true;
}

static bool is_office_odt(const std::string & path)
{
	GError * error = nullptr;
	GsfInput * input = gsf_input_stdio_new(path.c_str(), &error);
	if (error)
	{
		g_error_free(error);
		error = nullptr;
	}
	GsfInfile * zip = input ? gsf_infile_zip_new(input, &error) : nullptr;
	if (error)
	{
		g_error_free(error);
		error = nullptr;
	}
	if (!TFPASS(zip != nullptr))
	{
		if (input)
			g_object_unref(input);
		return false;
	}

	const std::string content = zip_read(zip, "content.xml");
	const std::string styles = zip_read(zip, "styles.xml");
	const std::string manifest = zip_read(zip, "META-INF/manifest.xml");
	const bool ok =
		TFPASS(content.find("<office:document-content") != std::string::npos) &&
		TFPASS(styles.find("<office:document-styles") != std::string::npos) &&
		TFPASS(manifest.find("application/vnd.oasis.opendocument.text") != std::string::npos) &&
		TFPASS(odt_picture_hrefs(zip, content, manifest)) &&
		TFPASS(odt_picture_hrefs(zip, styles, manifest)) &&
		TFPASS(styles.find("fo:min-height=\"-") == std::string::npos) &&
		TFPASS(styles.find("svg:height=\"-") == std::string::npos);

	g_object_unref(zip);
	g_object_unref(input);
	return ok;
}

static PT_DocPosition first_strux(PD_Document * doc, PTStruxType kind)
{
	pt_PieceTable * table = doc ? doc->getPieceTable() : nullptr;
	if (!table)
		return 0;
	for (pf_Frag * frag = table->getFragments().getFirst(); frag; frag = frag->getNext())
	{
		if (frag->getType() != pf_Frag::PFT_Strux)
			continue;
		if (static_cast<pf_Frag_Strux *>(frag)->getStruxType() == kind)
			return frag->getPos();
	}
	return 0;
}

static PT_DocPosition document_end(PD_Document * doc)
{
	pf_Frag * last = doc ? doc->getLastFrag() : nullptr;
	if (!last)
		return 0;
	return last->getPos() + last->getLength();
}

static UT_ConstByteBufPtr tiny_png()
{
	static const unsigned char bytes[] = {
		137, 80, 78, 71, 13, 10, 26, 10, 0, 0, 0, 13, 73, 72, 68, 82, 0, 0, 0, 1,
		0, 0, 0, 1, 8, 2, 0, 0, 0, 144, 119, 83, 222, 0, 0, 0, 12, 73, 68, 65, 84,
		120, 156, 99, 248, 207, 192, 0, 0, 3, 1, 1, 0, 201, 254, 146, 239, 0, 0, 0,
		0, 73, 69, 78, 68, 174, 66, 96, 130
	};
	UT_ByteBufPtr buf(new UT_ByteBuf);
	buf->append(bytes, static_cast<UT_uint32>(sizeof bytes));
	return buf;
}

static bool round_trip(const char * suffix, const char * plugin)
{
	if (plugin && *plugin)
		ensure_plugin(suffix, plugin);
	if (!TFPASS(exporter_known(suffix)))
		return false;

	PD_Document * doc = sample_document();
	if (!TFPASS(doc != nullptr))
		return false;

	std::string path = UT_createTmpFile("fmt", suffix);
	if (!TFPASS(!path.empty()))
	{
		doc->unref();
		return false;
	}

	UT_Error err = doc->AD_Document::saveAs(path.c_str(), IEFT_Unknown, false, nullptr);
	doc->unref();
	if (!TFPASS(err == UT_OK))
	{
		std::remove(path.c_str());
		return false;
	}
	if (std::strcmp(suffix, ".doc") == 0 && !is_office_doc(path))
	{
		std::remove(path.c_str());
		return false;
	}
	if (std::strcmp(suffix, ".docx") == 0 && !is_office_docx(path))
	{
		std::remove(path.c_str());
		return false;
	}
	if (std::strcmp(suffix, ".odt") == 0 && !is_office_odt(path))
	{
		std::remove(path.c_str());
		return false;
	}

	std::string text;
	bool ok = load_text(path, text) && TFPASS(text_has_markers(text));
	if (!ok)
	{
		std::remove(path.c_str());
		return false;
	}

	PD_Document * again = new PD_Document();
	err = again->readFromFile(path.c_str(), IEFT_Unknown, nullptr);
	std::remove(path.c_str());
	if (!TFPASS(UT_IS_IE_SUCCESS(err)))
	{
		again->unref();
		return false;
	}

	std::string second_path = UT_createTmpFile("fmt2", suffix);
	if (!TFPASS(!second_path.empty()))
	{
		again->unref();
		return false;
	}
	err = again->AD_Document::saveAs(second_path.c_str(), IEFT_Unknown, false, nullptr);
	again->unref();
	if (!TFPASS(err == UT_OK))
	{
		std::remove(second_path.c_str());
		return false;
	}

	if (std::strcmp(suffix, ".doc") == 0 && !is_office_doc(second_path))
	{
		std::remove(second_path.c_str());
		return false;
	}
	if (std::strcmp(suffix, ".docx") == 0 && !is_office_docx(second_path))
	{
		std::remove(second_path.c_str());
		return false;
	}
	if (std::strcmp(suffix, ".odt") == 0 && !is_office_odt(second_path))
	{
		std::remove(second_path.c_str());
		return false;
	}

	text.clear();
	ok = load_text(second_path, text) && TFPASS(text_has_markers(text));
	std::remove(second_path.c_str());
	return ok;
}

// Cairo writes page content with kerning, so "EntetePdf" is stored as
// (E)(nt)(e)(t)(e)(P)(df). Joining the literal strings rebuilds the word.
static std::string pdf_literals(const std::string & bytes)
{
	std::string text;
	for (std::string::size_type i = 0; i < bytes.size(); ++i)
	{
		if (bytes[i] != '(')
			continue;
		std::string literal;
		bool escaped = false;
		for (++i; i < bytes.size(); ++i)
		{
			const unsigned char c = static_cast<unsigned char>(bytes[i]);
			if (escaped)
			{
				escaped = false;
				if (c >= '0' && c <= '7')
				{
					int value = c - '0';
					for (int n = 0; n < 2 && i + 1 < bytes.size(); ++n)
					{
						const unsigned char d = static_cast<unsigned char>(bytes[i + 1]);
						if (d < '0' || d > '7')
							break;
						value = (value << 3) + (d - '0');
						++i;
					}
					literal.push_back(static_cast<char>(value));
				}
				else if (c == 'n')
					literal.push_back('\n');
				else if (c == 'r')
					literal.push_back('\r');
				else if (c == 't')
					literal.push_back('\t');
				else
					literal.push_back(static_cast<char>(c));
				continue;
			}
			if (c == '\\')
			{
				escaped = true;
				continue;
			}
			if (c == ')')
				break;
			literal.push_back(static_cast<char>(c));
		}
		text += literal;
	}
	return text;
}

static std::string pdf_inflate_streams(const std::string & bytes)
{
	std::string text;
	for (std::string::size_type i = 0; i < bytes.size();)
	{
		const std::string::size_type marker = bytes.find("stream", i);
		if (marker == std::string::npos)
			break;
		// A compressed stream can contain these six bytes by chance. A real
		// keyword is the one that follows the dictionary and a newline.
		std::string::size_type before = marker;
		while (before > 0 && (bytes[before - 1] == ' ' || bytes[before - 1] == '\t' ||
				      bytes[before - 1] == '\r' || bytes[before - 1] == '\n'))
			--before;
		const bool keyword = before >= 2 && bytes[before - 1] == '>' && bytes[before - 2] == '>';
		std::string::size_type start = marker + 6;
		const bool newline = start < bytes.size() && (bytes[start] == '\n' || bytes[start] == '\r');
		if (!keyword || !newline)
		{
			i = marker + 1;
			continue;
		}
		if (start + 1 < bytes.size() && bytes[start] == '\r' && bytes[start + 1] == '\n')
			start += 2;
		else
			start += 1;
		const std::string::size_type end = bytes.find("endstream", start);
		if (end == std::string::npos)
			break;
		std::string raw = bytes.substr(start, end - start);
		while (!raw.empty() && (raw.back() == '\n' || raw.back() == '\r'))
			raw.pop_back();
		z_stream stream;
		std::memset(&stream, 0, sizeof(stream));
		if (inflateInit(&stream) == Z_OK)
		{
			if (!raw.empty())
			{
				stream.next_in = reinterpret_cast<Bytef *>(&raw[0]);
				stream.avail_in = static_cast<uInt>(raw.size());
			}
			std::string inflated;
			int status = Z_OK;
			unsigned char buffer[4096];
			while (status == Z_OK)
			{
				stream.next_out = buffer;
				stream.avail_out = sizeof(buffer);
				status = inflate(&stream, Z_NO_FLUSH);
				const std::size_t produced = sizeof(buffer) - stream.avail_out;
				if (produced)
					inflated.append(reinterpret_cast<char *>(buffer), produced);
			}
			inflateEnd(&stream);
			if (status == Z_STREAM_END)
				text += inflated;
			else
				text += raw;
		}
		else
			text += raw;
		i = end + 9;
	}
	return text;
}

static int pdf_page_count(const std::string & bytes)
{
	int pages = 0;
	for (std::string::size_type i = 0; i < bytes.size();)
	{
		const std::string::size_type at = bytes.find("/Type", i);
		if (at == std::string::npos)
			break;
		std::string::size_type name = at + 5;
		while (name < bytes.size() && (bytes[name] == ' ' || bytes[name] == '\t' || bytes[name] == '\r' || bytes[name] == '\n'))
			++name;
		if (name < bytes.size() && bytes[name] == '/')
			++name;
		if (bytes.compare(name, 4, "Page") == 0 && (name + 4 >= bytes.size() || bytes[name + 4] != 's'))
			++pages;
		i = name + 4;
	}
	return pages;
}

static int pdf_declared_pages(const std::string & bytes)
{
	int declared = -1;
	for (std::string::size_type i = 0; i < bytes.size();)
	{
		const std::string::size_type at = bytes.find("/Type", i);
		if (at == std::string::npos)
			break;
		std::string::size_type name = at + 5;
		while (name < bytes.size() && (bytes[name] == ' ' || bytes[name] == '\t' || bytes[name] == '\r' || bytes[name] == '\n'))
			++name;
		if (name < bytes.size() && bytes[name] == '/')
			++name;
		if (bytes.compare(name, 5, "Pages") != 0)
		{
			i = name + 4;
			continue;
		}
		const std::string::size_type end = bytes.find(">>", name);
		const std::string::size_type count = bytes.find("/Count", name);
		if (count != std::string::npos && (end == std::string::npos || count < end))
		{
			std::string::size_type n = count + 6;
			while (n < bytes.size() && (bytes[n] == ' ' || bytes[n] == '\t' || bytes[n] == '\r' || bytes[n] == '\n'))
				++n;
			if (n < bytes.size() && bytes[n] >= '0' && bytes[n] <= '9')
			{
				int value = 0;
				while (n < bytes.size() && bytes[n] >= '0' && bytes[n] <= '9')
				{
					value = value * 10 + (bytes[n] - '0');
					++n;
				}
				if (value > declared)
					declared = value;
			}
		}
		i = name + 5;
	}
	return declared;
}

static bool pdf_has_text(const std::string & bytes, const char * marker)
{
	const std::string decoded = pdf_inflate_streams(bytes);
	const std::string joined = pdf_literals(bytes) + pdf_literals(decoded);
	return bytes.find(marker) != std::string::npos
		|| decoded.find(marker) != std::string::npos
		|| joined.find(marker) != std::string::npos;
}

}

TFTEST_MAIN("ABW, RTF, DOC, DOCX and ODT exporters are available")
{
	TFPASS(exporter_known(".abw"));
	TFPASS(exporter_known(".rtf"));
	TFPASS(exporter_known(".doc"));
	ensure_plugin(".docx", "openxml");
	ensure_plugin(".odt", "opendocument");
	TFPASS(exporter_known(".docx"));
	TFPASS(exporter_known(".odt"));
}

TFTEST_MAIN("Reference ABW, RTF and DOC files keep their text")
{
	std::string path;
	std::string text;

	TFPASS(TF_Test::ensure_test_data("/test/auto/hello.dat.1.abw", path));
	if (load_text(path, text))
		TFPASS(text.find("Hello World") != std::string::npos);

	TFPASS(TF_Test::ensure_test_data("/test/wp/rtftest.rtf", path));
	if (load_text(path, text))
		TFPASS(text.find("blah blah blah") != std::string::npos);

	TFPASS(TF_Test::ensure_test_data("/test/wp/Word97Test.doc", path));
	if (load_text(path, text))
		TFPASS(text.find("Richard Of York") != std::string::npos);
}

TFTEST_MAIN("Plain text survives two round trips in ABW, RTF, DOC, DOCX and ODT")
{
	TFPASS(round_trip(".abw", nullptr));
	TFPASS(round_trip(".rtf", nullptr));
	TFPASS(round_trip(".doc", nullptr));
	TFPASS(round_trip(".docx", "openxml"));
	TFPASS(round_trip(".odt", "opendocument"));
}

TFTEST_MAIN("ODT header, footer and images share one package path")
{
	ensure_plugin(".odt", "opendocument");
	if (!TFPASS(exporter_known(".odt")))
		return;

	PD_Document * doc = sample_document();
	if (!TFPASS(doc != nullptr))
		return;
	if (!TFPASS(doc->createDataItem("1", false, tiny_png(), "image/png", nullptr)) ||
		!TFPASS(doc->createDataItem("2", false, tiny_png(), "image/png", nullptr)))
	{
		doc->unref();
		return;
	}

	const PP_PropertyVector imageAtts = {"dataid", "1"};
	const PP_PropertyVector imageProps = {"width", "1in", "height", "1in"};
	const PT_DocPosition body = first_block_content(doc);
	if (!TFPASS(doc->insertObject(body, PTO_Image, imageAtts, imageProps)))
	{
		doc->unref();
		return;
	}

	pf_Frag_Strux * frame = nullptr;
	const PP_PropertyVector frameAtts = {"strux-image-dataid", "2"};
	const PP_PropertyVector frameProps = {
		"frame-type", "image",
		"frame-width", "1in",
		"frame-height", "1in",
		"position-to", "block-above-text",
		"xpos", "0in",
		"ypos", "0in"
	};
	if (!TFPASS(doc->insertStrux(first_strux(doc, PTX_Block), PTX_SectionFrame, frameAtts, frameProps, &frame)) ||
		!TFPASS(frame != nullptr) ||
		!TFPASS(doc->insertStrux(frame->getPos() + frame->getLength(), PTX_EndFrame)))
	{
		doc->unref();
		return;
	}

	const PP_PropertyVector sectionAtts = {"header", "1", "footer", "2"};
	const PP_PropertyVector sectionProps = {
		"page-margin-top", "1in",
		"page-margin-header", "0.5in",
		"page-margin-bottom", "1in",
		"page-margin-footer", "0.5in"
	};
	if (!TFPASS(doc->changeStruxFmt(PTC_AddFmt, first_strux(doc, PTX_Section), first_strux(doc, PTX_Section),
		sectionAtts, sectionProps, PTX_Section)))
	{
		doc->unref();
		return;
	}

	pf_Frag_Strux * header = nullptr;
	const PP_PropertyVector headerAtts = {"type", "header", "id", "1"};
	if (!TFPASS(doc->insertStrux(document_end(doc), PTX_SectionHdrFtr, headerAtts, PP_NOPROPS, &header)) ||
		!TFPASS(header != nullptr))
	{
		doc->unref();
		return;
	}
	const PT_DocPosition headerAt = header->getPos() + header->getLength();
	if (!TFPASS(doc->insertStrux(headerAt, PTX_Block)) ||
		!TFPASS(doc->insertSpan(headerAt + 1, std::string("EnteteImage"))) ||
		!TFPASS(doc->insertObject(headerAt + 1 + 11, PTO_Image, imageAtts, imageProps)))
	{
		doc->unref();
		return;
	}
	pf_Frag_Strux * headerFrame = nullptr;
	if (!TFPASS(doc->insertStrux(headerAt + 13, PTX_SectionFrame, frameAtts, frameProps, &headerFrame)) ||
		!TFPASS(headerFrame != nullptr) ||
		!TFPASS(doc->insertStrux(headerFrame->getPos() + headerFrame->getLength(), PTX_EndFrame)))
	{
		doc->unref();
		return;
	}

	pf_Frag_Strux * footer = nullptr;
	const PP_PropertyVector footerAtts = {"type", "footer", "id", "2"};
	if (!TFPASS(doc->insertStrux(document_end(doc), PTX_SectionHdrFtr, footerAtts, PP_NOPROPS, &footer)) ||
		!TFPASS(footer != nullptr))
	{
		doc->unref();
		return;
	}
	const PT_DocPosition footerAt = footer->getPos() + footer->getLength();
	if (!TFPASS(doc->insertStrux(footerAt, PTX_Block)) ||
		!TFPASS(doc->insertSpan(footerAt + 1, std::string("PiedTexte"))))
	{
		doc->unref();
		return;
	}

	const std::string path = UT_createTmpFile("fmtimg", ".odt");
	const UT_Error err = doc->AD_Document::saveAs(path.c_str(), IEFT_Unknown, false, nullptr);
	doc->unref();
	if (!TFPASS(err == UT_OK) || !is_office_odt(path))
	{
		std::remove(path.c_str());
		return;
	}

	GError * error = nullptr;
	GsfInput * input = gsf_input_stdio_new(path.c_str(), &error);
	if (error)
	{
		g_error_free(error);
		error = nullptr;
	}
	GsfInfile * zip = input ? gsf_infile_zip_new(input, &error) : nullptr;
	if (error)
	{
		g_error_free(error);
		error = nullptr;
	}
	const std::string content = zip ? zip_read(zip, "content.xml") : std::string();
	const std::string styles = zip ? zip_read(zip, "styles.xml") : std::string();
	const bool packaged =
		TFPASS(zip != nullptr) &&
		TFPASS(zip_has(zip, "Pictures/1.png")) &&
		TFPASS(zip_has(zip, "Pictures/2.png")) &&
		TFPASS(content.find("xlink:href=\"Pictures/1.png\"") != std::string::npos) &&
		TFPASS(content.find("xlink:href=\"Pictures/2.png\"") != std::string::npos) &&
		TFPASS(styles.find("<style:header>") != std::string::npos) &&
		TFPASS(styles.find("EnteteImage") != std::string::npos) &&
		TFPASS(styles.find("xlink:href=\"Pictures/1.png\"") != std::string::npos) &&
		TFPASS(styles.find("xlink:href=\"Pictures/2.png\"") != std::string::npos) &&
		TFPASS(styles.find("text:anchor-type=\"page\"") == std::string::npos) &&
		TFPASS(styles.find("<style:footer>") != std::string::npos) &&
		TFPASS(styles.find("PiedTexte") != std::string::npos) &&
		TFPASS(styles.find("fo:min-height=") != std::string::npos) &&
		TFPASS(styles.find("style:dynamic-spacing=\"true\"") != std::string::npos);
	if (zip)
		g_object_unref(zip);
	if (input)
		g_object_unref(input);
	std::remove(path.c_str());
	TFPASS(packaged);
}

TFTEST_MAIN("PDF export keeps header, body and footer on one page")
{
	if (!exporter_known(".pdf"))
		return;

	PD_Document * doc = new PD_Document();
	if (!TFPASS(doc->newDocument() == UT_OK))
	{
		doc->unref();
		return;
	}

	const PT_DocPosition body = first_block_content(doc);
	if (!TFPASS(doc->insertSpan(body, std::string("CorpsPdf"))))
	{
		doc->unref();
		return;
	}

	const PP_PropertyVector sectionAtts = {"header", "1", "footer", "2"};
	const PP_PropertyVector sectionProps = {
		"page-margin-top", "1in",
		"page-margin-header", "0.5in",
		"page-margin-bottom", "1in",
		"page-margin-footer", "0.5in"
	};
	if (!TFPASS(doc->changeStruxFmt(PTC_AddFmt, first_strux(doc, PTX_Section), first_strux(doc, PTX_Section),
		sectionAtts, sectionProps, PTX_Section)))
	{
		doc->unref();
		return;
	}

	pf_Frag_Strux * header = nullptr;
	const PP_PropertyVector headerAtts = {"type", "header", "id", "1"};
	if (!TFPASS(doc->insertStrux(document_end(doc), PTX_SectionHdrFtr, headerAtts, PP_NOPROPS, &header)) ||
		!TFPASS(header != nullptr))
	{
		doc->unref();
		return;
	}
	const PT_DocPosition headerAt = header->getPos() + header->getLength();
	if (!TFPASS(doc->insertStrux(headerAt, PTX_Block)) ||
		!TFPASS(doc->insertSpan(headerAt + 1, std::string("EntetePdf"))))
	{
		doc->unref();
		return;
	}

	pf_Frag_Strux * footer = nullptr;
	const PP_PropertyVector footerAtts = {"type", "footer", "id", "2"};
	if (!TFPASS(doc->insertStrux(document_end(doc), PTX_SectionHdrFtr, footerAtts, PP_NOPROPS, &footer)) ||
		!TFPASS(footer != nullptr))
	{
		doc->unref();
		return;
	}
	const PT_DocPosition footerAt = footer->getPos() + footer->getLength();
	if (!TFPASS(doc->insertStrux(footerAt, PTX_Block)) ||
		!TFPASS(doc->insertSpan(footerAt + 1, std::string("PiedPdf"))))
	{
		doc->unref();
		return;
	}

	const std::string path = UT_createTmpFile("fmtpdf", ".pdf");
	const UT_Error err = doc->AD_Document::saveAs(path.c_str(), IEFT_Unknown, false, nullptr);
	doc->unref();
	if (!TFPASS(err == UT_OK))
	{
		std::remove(path.c_str());
		return;
	}

	gchar * contents = nullptr;
	gsize length = 0;
	GError * error = nullptr;
	const bool loaded = g_file_get_contents(path.c_str(), &contents, &length, &error);
	if (error)
	{
		g_error_free(error);
		error = nullptr;
	}
	const std::string bytes = loaded ? std::string(contents, length) : std::string();
	g_free(contents);
	std::remove(path.c_str());

	const std::string decoded = pdf_inflate_streams(bytes);
	int declared = pdf_declared_pages(decoded);
	if (declared < 0)
		declared = pdf_declared_pages(bytes);
	if (declared < 0)
	{
		const int pages = pdf_page_count(decoded);
		declared = pages > 0 ? pages : pdf_page_count(bytes);
	}
	TFPASS(bytes.compare(0, 5, "%PDF-") == 0);
	TFPASSEQ(declared, 1);
	TFPASS(pdf_has_text(bytes, "EntetePdf"));
	TFPASS(pdf_has_text(bytes, "CorpsPdf"));
	TFPASS(pdf_has_text(bytes, "PiedPdf"));
}
