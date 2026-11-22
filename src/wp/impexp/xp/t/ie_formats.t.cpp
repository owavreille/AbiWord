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
	const std::string meta = zip_read(zip, "meta.xml");
	const std::string manifest = zip_read(zip, "META-INF/manifest.xml");
	const bool thumbInZip = zip_has(zip, "Thumbnails/thumbnail.png");
	const bool thumbNonEmpty = !thumbInZip || !zip_read(zip, "Thumbnails/thumbnail.png").empty();
	const bool thumbInManifest = manifest.find("Thumbnails/thumbnail.png") != std::string::npos;
	const bool ok =
		TFPASS(content.find("<office:document-content") != std::string::npos) &&
		TFPASS(content.find("<style:page-layout") == std::string::npos) &&
		TFPASS(styles.find("<office:document-styles") != std::string::npos) &&
		TFPASS(meta.find("<office:document-meta") != std::string::npos) &&
		TFPASS(manifest.find("application/vnd.oasis.opendocument.text") != std::string::npos) &&
		TFPASS(thumbNonEmpty) &&
		TFPASS(thumbInZip == thumbInManifest) &&
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

// FibRgLw97 starts at 0x40. ie_exp_MsWord_97.cpp stores ccpText at 0x4C;
// ccpFtn is the next dword and ccpHdd follows ccpFtn.
static const unsigned FMT_CCP_TEXT = 0x4C;
static const unsigned FMT_CCP_HDD = FMT_CCP_TEXT + 8;

static PD_Document * header_footer_document(const char * body, const char * header, const char * footer)
{
	PD_Document * doc = new PD_Document();
	if (doc->newDocument() != UT_OK)
	{
		doc->unref();
		return nullptr;
	}

	const PT_DocPosition bodyAt = first_block_content(doc);
	if (!doc->insertSpan(bodyAt, std::string(body)))
	{
		doc->unref();
		return nullptr;
	}

	const PP_PropertyVector sectionAtts = {"header", "1", "footer", "2"};
	const PP_PropertyVector sectionProps = {
		"page-margin-top", "1in",
		"page-margin-header", "0.5in",
		"page-margin-bottom", "1in",
		"page-margin-footer", "0.5in"
	};
	if (!doc->changeStruxFmt(PTC_AddFmt, first_strux(doc, PTX_Section), first_strux(doc, PTX_Section),
		sectionAtts, sectionProps, PTX_Section))
	{
		doc->unref();
		return nullptr;
	}

	pf_Frag_Strux * hdr = nullptr;
	const PP_PropertyVector headerAtts = {"type", "header", "id", "1"};
	if (!doc->insertStrux(document_end(doc), PTX_SectionHdrFtr, headerAtts, PP_NOPROPS, &hdr) ||
		!hdr)
	{
		doc->unref();
		return nullptr;
	}
	const PT_DocPosition headerAt = hdr->getPos() + hdr->getLength();
	if (!doc->insertStrux(headerAt, PTX_Block) ||
		!doc->insertSpan(headerAt + 1, std::string(header)))
	{
		doc->unref();
		return nullptr;
	}

	pf_Frag_Strux * ftr = nullptr;
	const PP_PropertyVector footerAtts = {"type", "footer", "id", "2"};
	if (!doc->insertStrux(document_end(doc), PTX_SectionHdrFtr, footerAtts, PP_NOPROPS, &ftr) ||
		!ftr)
	{
		doc->unref();
		return nullptr;
	}
	const PT_DocPosition footerAt = ftr->getPos() + ftr->getLength();
	if (!doc->insertStrux(footerAt, PTX_Block) ||
		!doc->insertSpan(footerAt + 1, std::string(footer)))
	{
		doc->unref();
		return nullptr;
	}
	return doc;
}

template <typename Factory>
static GsfInfile * package_open(const std::string & path, GsfInput *& input, Factory factory)
{
	input = nullptr;
	GError * error = nullptr;
	input = gsf_input_stdio_new(path.c_str(), &error);
	if (error)
	{
		g_error_free(error);
		error = nullptr;
	}
	GsfInfile * container = input ? factory(input, &error) : nullptr;
	if (error)
	{
		g_error_free(error);
		error = nullptr;
	}
	if (!container && input)
	{
		g_object_unref(input);
		input = nullptr;
	}
	return container;
}

static void package_close(GsfInput * input, GsfInfile * container)
{
	if (container)
		g_object_unref(container);
	if (input)
		g_object_unref(input);
}

static bool markup_next(const std::string & xml, std::string::size_type from, std::string::size_type limit,
			std::string & name, bool & closing, bool & self_closing,
			std::string::size_type & start, std::string::size_type & end)
{
	std::string::size_type i = from;
	while (i < limit)
	{
		const std::string::size_type lt = xml.find('<', i);
		if (lt == std::string::npos || lt >= limit)
			return false;
		if (xml.compare(lt, 4, "<!--") == 0)
		{
			const std::string::size_type comment = xml.find("-->", lt + 4);
			const std::string::size_type next = comment == std::string::npos ? limit : comment + 3;
			i = next > limit ? limit : next;
			continue;
		}
		if (lt + 1 < xml.size() && xml[lt + 1] == '?')
		{
			const std::string::size_type pi = xml.find("?>", lt + 2);
			const std::string::size_type next = pi == std::string::npos ? limit : pi + 2;
			i = next > limit ? limit : next;
			continue;
		}
		if (xml.compare(lt, 9, "<![CDATA[") == 0)
		{
			const std::string::size_type data = xml.find("]]>", lt + 9);
			const std::string::size_type next = data == std::string::npos ? limit : data + 3;
			i = next > limit ? limit : next;
			continue;
		}
		if (lt + 1 < xml.size() && xml[lt + 1] == '!')
		{
			const std::string::size_type decl = xml.find('>', lt + 2);
			const std::string::size_type next = decl == std::string::npos ? limit : decl + 1;
			i = next > limit ? limit : next;
			continue;
		}

		std::string::size_type cursor = lt + 1;
		closing = false;
		if (cursor < xml.size() && xml[cursor] == '/')
		{
			closing = true;
			++cursor;
		}
		const std::string::size_type name_at = cursor;
		while (cursor < xml.size())
		{
			const unsigned char c = static_cast<unsigned char>(xml[cursor]);
			if (std::isalnum(c) || c == ':' || c == '_' || c == '-' || c == '.')
				++cursor;
			else
				break;
		}
		if (cursor == name_at)
		{
			i = lt + 1;
			continue;
		}

		bool in_quote = false;
		char quote = 0;
		std::string::size_type gt = cursor;
		for (; gt < xml.size(); ++gt)
		{
			const char c = xml[gt];
			if (in_quote)
			{
				if (c == quote)
					in_quote = false;
				continue;
			}
			if (c == '"' || c == '\'')
			{
				in_quote = true;
				quote = c;
				continue;
			}
			if (c == '>')
				break;
		}
		if (gt >= xml.size() || gt >= limit)
			return false;

		name = xml.substr(name_at, cursor - name_at);
		std::string::size_type slash = gt;
		while (slash > cursor && std::isspace(static_cast<unsigned char>(xml[slash - 1])))
			--slash;
		self_closing = !closing && slash > cursor && xml[slash - 1] == '/';
		start = lt;
		end = gt + 1;
		return true;
	}
	return false;
}

static bool markup_inner(const std::string & xml, const char * wanted, std::string::size_type from,
			 std::string & inner, std::string::size_type & after)
{
	std::string::size_type i = from;
	while (i < xml.size())
	{
		std::string name;
		bool closing = false;
		bool self_closing = false;
		std::string::size_type start = 0;
		std::string::size_type end = 0;
		if (!markup_next(xml, i, xml.size(), name, closing, self_closing, start, end))
			return false;
		if (!closing && name == wanted)
		{
			if (self_closing)
			{
				inner.clear();
				after = end;
				return true;
			}
			int depth = 1;
			const std::string::size_type inner_at = end;
			std::string::size_type j = end;
			while (depth > 0)
			{
				if (!markup_next(xml, j, xml.size(), name, closing, self_closing, start, end))
					return false;
				if (name == wanted)
				{
					if (closing)
						--depth;
					else if (!self_closing)
						++depth;
				}
				j = end;
				if (depth == 0)
				{
					inner = xml.substr(inner_at, start - inner_at);
					after = end;
					return true;
				}
			}
		}
		i = end;
	}
	return false;
}

static bool docx_final_body_sectpr(const std::string & document, std::string & markup)
{
	std::string body;
	std::string::size_type after = 0;
	if (!markup_inner(document, "w:body", 0, body, after))
		return false;

	std::string last_name;
	std::string::size_type last_start = 0;
	std::string::size_type last_end = 0;
	bool have = false;
	int depth = 0;
	std::string::size_type i = 0;
	while (i < body.size())
	{
		std::string name;
		bool closing = false;
		bool self_closing = false;
		std::string::size_type start = 0;
		std::string::size_type end = 0;
		if (!markup_next(body, i, body.size(), name, closing, self_closing, start, end))
			break;
		if (!closing && !self_closing)
		{
			if (depth == 0)
			{
				last_name = name;
				last_start = start;
				have = true;
			}
			++depth;
		}
		else if (!closing && self_closing)
		{
			if (depth == 0)
			{
				last_name = name;
				last_start = start;
				last_end = end;
				have = true;
			}
		}
		else if (closing)
		{
			if (depth == 1 && have)
				last_end = end;
			if (depth > 0)
				--depth;
		}
		i = end;
	}
	if (!have || last_name != "w:sectPr" || last_end <= last_start)
		return false;
	markup = body.substr(last_start, last_end - last_start);
	return true;
}

static bool docx_run_before_paragraph_defaults(const std::string & styles)
{
	std::string defaults;
	std::string::size_type after = 0;
	if (!markup_inner(styles, "w:docDefaults", 0, defaults, after))
		return false;
	const std::string::size_type run = defaults.find("<w:rPrDefault");
	const std::string::size_type para = defaults.find("<w:pPrDefault");
	return run != std::string::npos && para != std::string::npos && run < para;
}

static std::string docx_relation_target(const std::string & rels, const char * type_tail)
{
	const std::string tail(type_tail);
	std::string::size_type at = 0;
	while ((at = rels.find("<Relationship ", at)) != std::string::npos)
	{
		const std::string::size_type end = rels.find("/>", at);
		if (end == std::string::npos)
			return std::string();
		const std::string tag = rels.substr(at, end - at);
		at = end + 2;
		if (tag.find("TargetMode=\"External\"") != std::string::npos)
			continue;
		const std::string type_key = "Type=\"";
		const std::string::size_type type_at = tag.find(type_key);
		if (type_at == std::string::npos)
			continue;
		const std::string::size_type type_start = type_at + type_key.size();
		const std::string::size_type type_end = tag.find('"', type_start);
		if (type_end == std::string::npos)
			continue;
		const std::string type = tag.substr(type_start, type_end - type_start);
		if (type.size() < tail.size() || type.compare(type.size() - tail.size(), tail.size(), tail) != 0)
			continue;
		const std::string target_key = "Target=\"";
		const std::string::size_type value = tag.find(target_key);
		if (value == std::string::npos)
			continue;
		const std::string::size_type start = value + target_key.size();
		const std::string::size_type quote = tag.find('"', start);
		if (quote == std::string::npos)
			continue;
		return tag.substr(start, quote - start);
	}
	return std::string();
}

static std::string docx_target_path(const std::string & target)
{
	if (target.empty())
		return target;
	if (target[0] == '/')
		return target.substr(1);
	return std::string("word/") + target;
}

static bool frames_are_in_paragraphs(const std::string & region)
{
	int depth = 0;
	std::string::size_type i = 0;
	while (i < region.size())
	{
		std::string name;
		bool closing = false;
		bool self_closing = false;
		std::string::size_type start = 0;
		std::string::size_type end = 0;
		if (!markup_next(region, i, region.size(), name, closing, self_closing, start, end))
			break;
		if (name == "text:p")
		{
			if (closing)
			{
				if (depth > 0)
					--depth;
			}
			else if (!self_closing)
				++depth;
		}
		else if (!closing && name == "draw:frame" && depth <= 0)
			return false;
		i = end;
	}
	return true;
}

static bool odt_header_footer_frames_in_paragraphs(const std::string & styles)
{
	const char * elements[] = {"style:header", "style:footer", nullptr};
	for (const char ** element = elements; *element; ++element)
	{
		std::string inner;
		std::string::size_type from = 0;
		while (markup_inner(styles, *element, from, inner, from))
		{
			if (!frames_are_in_paragraphs(inner))
				return false;
		}
	}
	return true;
}

static bool markup_has_text(const std::string & xml)
{
	bool in_tag = false;
	for (std::string::size_type i = 0; i < xml.size(); ++i)
	{
		const char c = xml[i];
		if (in_tag)
		{
			if (c == '>')
				in_tag = false;
			continue;
		}
		if (c == '<')
		{
			in_tag = true;
			continue;
		}
		if (!std::isspace(static_cast<unsigned char>(c)))
			return true;
	}
	return false;
}

static bool fo_min_height_non_negative(const std::string & xml)
{
	const std::string key = "fo:min-height=\"";
	std::string::size_type at = 0;
	while ((at = xml.find(key, at)) != std::string::npos)
	{
		at += key.size();
		while (at < xml.size() && std::isspace(static_cast<unsigned char>(xml[at])))
			++at;
		if (at < xml.size() && xml[at] == '-')
			return false;
	}
	return true;
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
		TFPASS(styles.find("style:dynamic-spacing=\"true\"") != std::string::npos) &&
		TFPASS(odt_header_footer_frames_in_paragraphs(styles));
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

TFTEST_MAIN("DOCX export keeps header, body and footer in separate parts")
{
	ensure_plugin(".docx", "openxml");
	if (!exporter_known(".docx"))
		return;

	PD_Document * doc = header_footer_document("CorpsDocx", "EnteteDocx", "PiedDocx");
	if (!TFPASS(doc != nullptr))
		return;

	const std::string path = UT_createTmpFile("fmtdocx", ".docx");
	if (!TFPASS(!path.empty()))
	{
		doc->unref();
		return;
	}
	const UT_Error err = doc->AD_Document::saveAs(path.c_str(), IEFT_Unknown, false, nullptr);
	doc->unref();
	if (!TFPASS(err == UT_OK))
	{
		std::remove(path.c_str());
		return;
	}

	GsfInput * input = nullptr;
	GsfInfile * zip = package_open(path, input, gsf_infile_zip_new);
	if (!TFPASS(zip != nullptr))
	{
		package_close(input, zip);
		std::remove(path.c_str());
		return;
	}

	TFPASS(zip_has(zip, "[Content_Types].xml"));
	TFPASS(zip_has(zip, "word/document.xml"));
	TFPASS(zip_has(zip, "word/styles.xml"));
	TFPASS(zip_has(zip, "word/_rels/document.xml.rels"));

	const std::string document = zip_read(zip, "word/document.xml");
	const std::string styles = zip_read(zip, "word/styles.xml");
	const std::string rels = zip_read(zip, "word/_rels/document.xml.rels");
	std::string sect;
	const bool final_sect = docx_final_body_sectpr(document, sect);
	const std::string header_target = docx_relation_target(rels, "/header");
	const std::string footer_target = docx_relation_target(rels, "/footer");
	const std::string header_path = docx_target_path(header_target);
	const std::string footer_path = docx_target_path(footer_target);

	TFPASS(final_sect);
	TFPASS(final_sect && sect.find("w:val=\"continuous\"") == std::string::npos);
	TFPASS(docx_run_before_paragraph_defaults(styles));
	TFPASS(document.find("CorpsDocx") != std::string::npos);
	TFPASS(!header_target.empty() && zip_has(zip, header_path));
	TFPASS(!footer_target.empty() && zip_has(zip, footer_path));
	TFPASS(!header_path.empty() && zip_read(zip, header_path).find("EnteteDocx") != std::string::npos);
	TFPASS(!footer_path.empty() && zip_read(zip, footer_path).find("PiedDocx") != std::string::npos);

	package_close(input, zip);
	std::remove(path.c_str());
}

TFTEST_MAIN("ODT export keeps header and footer in the styles and the body in content")
{
	ensure_plugin(".odt", "opendocument");
	if (!exporter_known(".odt"))
		return;

	PD_Document * doc = header_footer_document("CorpsOdt", "EnteteOdt", "PiedOdt");
	if (!TFPASS(doc != nullptr))
		return;

	const std::string path = UT_createTmpFile("fmtodt", ".odt");
	if (!TFPASS(!path.empty()))
	{
		doc->unref();
		return;
	}
	const UT_Error err = doc->AD_Document::saveAs(path.c_str(), IEFT_Unknown, false, nullptr);
	doc->unref();
	if (!TFPASS(err == UT_OK))
	{
		std::remove(path.c_str());
		return;
	}

	GsfInput * input = nullptr;
	GsfInfile * zip = package_open(path, input, gsf_infile_zip_new);
	if (!TFPASS(zip != nullptr))
	{
		package_close(input, zip);
		std::remove(path.c_str());
		return;
	}

	const std::string content = zip_read(zip, "content.xml");
	const std::string styles = zip_read(zip, "styles.xml");
	std::string header;
	std::string footer;
	std::string::size_type header_after = 0;
	std::string::size_type footer_after = 0;
	const bool has_header = markup_inner(styles, "style:header", 0, header, header_after);
	const bool has_footer = markup_inner(styles, "style:footer", 0, footer, footer_after);

	TFPASS(zip_has(zip, "mimetype"));
	TFPASS(zip_has(zip, "content.xml"));
	TFPASS(zip_has(zip, "styles.xml"));
	TFPASS(zip_has(zip, "META-INF/manifest.xml"));
	TFPASS(content.find("CorpsOdt") != std::string::npos);
	TFPASS(has_header && header.find("EnteteOdt") != std::string::npos);
	TFPASS(has_footer && footer.find("PiedOdt") != std::string::npos);
	TFPASS(has_header && markup_has_text(header));
	TFPASS(fo_min_height_non_negative(styles));
	TFPASS(odt_header_footer_frames_in_paragraphs(styles));

	package_close(input, zip);
	std::remove(path.c_str());
}

TFTEST_MAIN("DOC export keeps the header text in WordDocument")
{
	if (!exporter_known(".doc"))
		return;

	PD_Document * doc = header_footer_document("CorpsDoc", "EnteteDoc", "PiedDoc");
	if (!TFPASS(doc != nullptr))
		return;

	const std::string path = UT_createTmpFile("fmthdr", ".doc");
	if (!TFPASS(!path.empty()))
	{
		doc->unref();
		return;
	}
	const UT_Error err = doc->AD_Document::saveAs(path.c_str(), IEFT_Unknown, false, nullptr);
	doc->unref();
	if (!TFPASS(err == UT_OK))
	{
		std::remove(path.c_str());
		return;
	}

	GsfInput * input = nullptr;
	GsfInfile * ole = package_open(path, input, gsf_infile_msole_new);
	if (!TFPASS(ole != nullptr))
	{
		package_close(input, ole);
		std::remove(path.c_str());
		return;
	}

	GsfInput * word_stream = gsf_infile_child_by_name(ole, "WordDocument");
	GsfInput * table_stream = gsf_infile_child_by_name(ole, "1Table");
	TFPASS(word_stream != nullptr);
	TFPASS(table_stream != nullptr);
	if (word_stream)
		g_object_unref(word_stream);
	if (table_stream)
		g_object_unref(table_stream);

	const std::string word = ole_read(ole, "WordDocument");
	TFPASS(word.find(utf16_ascii("CorpsDoc")) != std::string::npos);
	TFPASS(word.find(utf16_ascii("EnteteDoc")) != std::string::npos);
	TFPASS(word.size() >= FMT_CCP_HDD + 4 && le32(word, FMT_CCP_HDD) > 0);
	// fComplex is bit 2 of the FibBase flags word at 0x0A.
	TFPASS(word.size() >= 0x0C && (le16(word, 0x0A) & 0x0004u) == 0);

	package_close(input, ole);
	std::remove(path.c_str());
}

TFTEST_MAIN("DOCX export keeps a body image in word/media")
{
	ensure_plugin(".docx", "openxml");
	if (!TFPASS(exporter_known(".docx")))
		return;

	PD_Document * doc = sample_document();
	if (!TFPASS(doc != nullptr))
		return;
	const PP_PropertyVector imageAtts = {"dataid", "1"};
	const PP_PropertyVector imageProps = {"width", "1in", "height", "1in"};
	if (!TFPASS(doc->createDataItem("1", false, tiny_png(), "image/png", nullptr)) ||
		!TFPASS(doc->insertObject(first_block_content(doc), PTO_Image, imageAtts, imageProps)))
	{
		doc->unref();
		return;
	}

	const std::string path = UT_createTmpFile("fmtimg", ".docx");
	if (!TFPASS(!path.empty()))
	{
		doc->unref();
		return;
	}
	const UT_Error err = doc->AD_Document::saveAs(path.c_str(), IEFT_Unknown, false, nullptr);
	doc->unref();
	if (!TFPASS(err == UT_OK))
	{
		std::remove(path.c_str());
		return;
	}

	GsfInput * input = nullptr;
	GsfInfile * zip = package_open(path, input, gsf_infile_zip_new);
	if (!TFPASS(zip != nullptr))
	{
		package_close(input, zip);
		std::remove(path.c_str());
		return;
	}

	const std::string document = zip_read(zip, "word/document.xml");
	const std::string rels = zip_read(zip, "word/_rels/document.xml.rels");
	const std::string target = docx_relation_target(rels, "/image");
	TFPASS(document.find("a:blip") != std::string::npos);
	TFPASS(document.find("r:embed=") != std::string::npos);
	TFPASS(document.find("<a:stretch><a:fillRect/></a:stretch>") != std::string::npos);
	TFPASS(target.compare(0, 6, "media/") == 0);
	TFPASS(zip_has(zip, std::string("word/") + target));
	const std::string media = zip_read(zip, std::string("word/") + target);
	TFPASS(media.size() >= 8 && static_cast<unsigned char>(media[0]) == 137 && media.compare(1, 3, "PNG") == 0);

	package_close(input, zip);
	std::remove(path.c_str());
}

TFTEST_MAIN("DOCX export keeps the position of a body image")
{
	ensure_plugin(".docx", "openxml");
	if (!TFPASS(exporter_known(".docx")))
		return;

	PD_Document * doc = sample_document();
	if (!TFPASS(doc != nullptr))
		return;
	if (!TFPASS(doc->createDataItem("1", false, tiny_png(), "image/png", nullptr)))
	{
		doc->unref();
		return;
	}

	pf_Frag_Strux * columnFrame = nullptr;
	const PP_PropertyVector columnAtts = {"strux-image-dataid", "1"};
	const PP_PropertyVector columnProps = {
		"frame-type", "image",
		"frame-width", "1in",
		"frame-height", "1in",
		"position-to", "column-above-text",
		"frame-col-xpos", "1.5in",
		"frame-col-ypos", "2in",
		"wrap-mode", "wrapped-both"
	};
	pf_Frag_Strux * blockFrame = nullptr;
	const PP_PropertyVector blockProps = {
		"frame-type", "image",
		"frame-width", "1in",
		"frame-height", "1in",
		"position-to", "block-above-text",
		"xpos", "0.5in",
		"ypos", "-0.25in",
		"wrap-mode", "wrapped-both"
	};
	if (!TFPASS(doc->insertStrux(first_strux(doc, PTX_Block), PTX_SectionFrame, columnAtts, columnProps, &columnFrame)) ||
		!TFPASS(columnFrame != nullptr) ||
		!TFPASS(doc->insertStrux(columnFrame->getPos() + columnFrame->getLength(), PTX_EndFrame)) ||
		!TFPASS(doc->insertStrux(first_strux(doc, PTX_Block), PTX_SectionFrame, columnAtts, blockProps, &blockFrame)) ||
		!TFPASS(blockFrame != nullptr) ||
		!TFPASS(doc->insertStrux(blockFrame->getPos() + blockFrame->getLength(), PTX_EndFrame)))
	{
		doc->unref();
		return;
	}

	const std::string path = UT_createTmpFile("fmtpos", ".docx");
	if (!TFPASS(!path.empty()))
	{
		doc->unref();
		return;
	}
	const UT_Error err = doc->AD_Document::saveAs(path.c_str(), IEFT_Unknown, false, nullptr);
	doc->unref();
	if (!TFPASS(err == UT_OK))
	{
		std::remove(path.c_str());
		return;
	}

	GsfInput * input = nullptr;
	GsfInfile * zip = package_open(path, input, gsf_infile_zip_new);
	if (!TFPASS(zip != nullptr))
	{
		package_close(input, zip);
		std::remove(path.c_str());
		return;
	}

	const std::string document = zip_read(zip, "word/document.xml");
	// 1.5in and 2in from the column, and 0.5in / -0.25in from the paragraph.
	TFPASS(document.find("relativeFrom=\"column\"") != std::string::npos);
	TFPASS(document.find("relativeFrom=\"margin\"") != std::string::npos);
	TFPASS(document.find("relativeFrom=\"paragraph\"") != std::string::npos);
	TFPASS(document.find(">1371600<") != std::string::npos);
	TFPASS(document.find(">1828800<") != std::string::npos);
	TFPASS(document.find(">457200<") != std::string::npos);
	TFPASS(document.find(">-228600<") != std::string::npos);

	package_close(input, zip);
	std::remove(path.c_str());
}

TFTEST_MAIN("DOC export keeps the header text and an inline image")
{
	if (!exporter_known(".doc"))
		return;

	PD_Document * doc = header_footer_document("CorpsImage", "EnteteImage", "PiedImage");
	if (!TFPASS(doc != nullptr))
		return;
	if (!TFPASS(doc->createDataItem("imgbody", false, tiny_png(), "image/png", nullptr)) ||
		!TFPASS(doc->createDataItem("imghdr", false, tiny_png(), "image/png", nullptr)))
	{
		doc->unref();
		return;
	}

	PT_DocPosition header_at = 0;
	pt_PieceTable * table = doc->getPieceTable();
	bool seen_header = false;
	for (pf_Frag * frag = table ? table->getFragments().getFirst() : nullptr; frag; frag = frag->getNext())
	{
		if (frag->getType() != pf_Frag::PFT_Strux)
			continue;
		pf_Frag_Strux * strux = static_cast<pf_Frag_Strux *>(frag);
		if (strux->getStruxType() == PTX_SectionHdrFtr)
			seen_header = true;
		else if (seen_header && strux->getStruxType() == PTX_Block)
		{
			header_at = strux->getPos() + strux->getLength();
			break;
		}
	}
	const PP_PropertyVector bodyAtts = {"dataid", "imgbody"};
	const PP_PropertyVector headerAtts = {"dataid", "imghdr"};
	const PP_PropertyVector imageProps = {"width", "1in", "height", "1in"};
	if (!TFPASS(header_at != 0) ||
		!TFPASS(doc->insertObject(header_at, PTO_Image, headerAtts, imageProps)) ||
		!TFPASS(doc->insertObject(first_block_content(doc), PTO_Image, bodyAtts, imageProps)))
	{
		doc->unref();
		return;
	}

	const std::string path = UT_createTmpFile("fmtimg", ".doc");
	if (!TFPASS(!path.empty()))
	{
		doc->unref();
		return;
	}
	const UT_Error err = doc->AD_Document::saveAs(path.c_str(), IEFT_Unknown, false, nullptr);
	doc->unref();
	if (!TFPASS(err == UT_OK))
	{
		std::remove(path.c_str());
		return;
	}

	GsfInput * input = nullptr;
	GsfInfile * ole = package_open(path, input, gsf_infile_msole_new);
	if (!TFPASS(ole != nullptr))
	{
		package_close(input, ole);
		std::remove(path.c_str());
		return;
	}

	const std::string word = ole_read(ole, "WordDocument");
	const std::string table_bytes = ole_read(ole, "1Table");
	const std::string data = ole_read(ole, "Data");
	TFPASS(word.find(utf16_ascii("EnteteImage")) != std::string::npos);
	TFPASS(word.size() >= FMT_CCP_HDD + 4 && le32(word, FMT_CCP_HDD) > 0);
	TFPASS(word.size() >= 0x0C && (le16(word, 0x0A) & 0x0004u) == 0);

	const unsigned ccp_text = le32(word, FMT_CCP_TEXT);
	const unsigned ccp_hdd = le32(word, FMT_CCP_HDD);
	unsigned image_chars = 0;
	if (word.size() >= 0x800 + (ccp_text + ccp_hdd) * 2)
	{
		for (unsigned i = 0; i < ccp_text + ccp_hdd; ++i)
		{
			if (le16(word, 0x800 + i * 2) == 0x0001)
				++image_chars;
		}
	}
	TFPASS(image_chars >= 2);

	const unsigned sed_at = le32(word, 0x9A + 6 * 8);
	const unsigned fc_sepx = le32(table_bytes, sed_at + 10);
	const unsigned sepx_len = le16(word, fc_sepx);
	bool header_flag = false;
	if (fc_sepx + 2 + sepx_len <= word.size())
	{
		for (unsigned i = 0; i + 3 <= sepx_len; ++i)
		{
			if (le16(word, fc_sepx + 2 + i) != 0x3014)
				continue;
			const unsigned flags = static_cast<unsigned char>(word[fc_sepx + 4 + i]);
			header_flag = (flags & 0x0A) == 0x0A;
			break;
		}
	}
	TFPASS(header_flag);

	TFPASS(data.size() >= 0x44 && le16(data, 4) == 0x44 && le16(data, 6) == 0x64);
	const std::string::size_type first_png = data.find("\x89PNG");
	const std::string::size_type second_png = first_png == std::string::npos ? std::string::npos : data.find("\x89PNG", first_png + 4);
	TFPASS(second_png != std::string::npos);

	package_close(input, ole);
	std::remove(path.c_str());
}

TFTEST_MAIN("ODT export writes valid table styles, positioned images, text formatting and ISO dates")
{
	ensure_plugin(".odt", "opendocument");
	if (!TFPASS(exporter_known(".odt")))
		return;

	PD_Document * doc = new PD_Document();
	if (!TFPASS(doc->newDocument() == UT_OK))
	{
		doc->unref();
		return;
	}

	doc->setMetaDataProp(PD_META_KEY_DATE, "Sun Nov 22 17:00:00 2026\n");
	doc->setMetaDataProp(PD_META_KEY_DATE_LAST_CHANGED, "Mon Nov 23 08:15:30 2026");

	if (!TFPASS(doc->createDataItem("img1", false, tiny_png(), "image/png", nullptr)))
	{
		doc->unref();
		return;
	}

	// Floating image before first paragraph (mirrors AbiWord's piece table order).
	pf_Frag_Strux * frame = nullptr;
	const PP_PropertyVector frameAtts = {"strux-image-dataid", "img1"};
	const PP_PropertyVector frameProps = {
		"frame-type", "image",
		"frame-width", "1.5in",
		"frame-height", "1in",
		"position-to", "column-above-text",
		"frame-col-xpos", "1.25in",
		"frame-col-ypos", "0.75in",
		"wrap-mode", "wrapped-both"
	};
	if (!TFPASS(doc->insertStrux(first_strux(doc, PTX_Block), PTX_SectionFrame, frameAtts, frameProps, &frame)) ||
		!TFPASS(frame != nullptr) ||
		!TFPASS(doc->insertStrux(frame->getPos() + frame->getLength(), PTX_EndFrame)))
	{
		doc->unref();
		return;
	}

	const PT_DocPosition body = first_block_content(doc);
	const std::string text = "SurligneSouligne";
	if (!TFPASS(doc->insertSpan(body, text)))
	{
		doc->unref();
		return;
	}
	const PP_PropertyVector spanProps = {
		"bgcolor", "ffff00",
		"text-decoration", "underline line-through"
	};
	if (!TFPASS(doc->changeSpanFmt(PTC_AddFmt, body, body + static_cast<PT_DocPosition>(text.size()),
		PP_NOPROPS, spanProps)))
	{
		doc->unref();
		return;
	}

	// Insert a 2x2 table with explicit column widths and cell background.
	pf_Frag_Strux * table = nullptr;
	const PP_PropertyVector tableProps = {
		"table-column-props", "2in/3in/",
		"table-row-heights", "0.5in/0.5in/"
	};
	if (!TFPASS(doc->insertStrux(document_end(doc), PTX_SectionTable, PP_NOPROPS, tableProps, &table)) ||
		!TFPASS(table != nullptr))
	{
		doc->unref();
		return;
	}
	for (int r = 0; r < 2; ++r)
	{
		for (int c = 0; c < 2; ++c)
		{
			pf_Frag_Strux * cell = nullptr;
			const PP_PropertyVector cellProps = {
				"left-attach", std::to_string(c),
				"right-attach", std::to_string(c + 1),
				"top-attach", std::to_string(r),
				"bot-attach", std::to_string(r + 1),
				"background-color", "eeeeee"
			};
			if (!TFPASS(doc->insertStrux(document_end(doc), PTX_SectionCell, PP_NOPROPS, cellProps, &cell)) ||
				!TFPASS(cell != nullptr) ||
				!TFPASS(doc->insertStrux(document_end(doc), PTX_Block)) ||
				!TFPASS(doc->insertSpan(document_end(doc), std::string("Cell"))) ||
				!TFPASS(doc->insertStrux(document_end(doc), PTX_EndCell)))
			{
				doc->unref();
				return;
			}
		}
	}
	if (!TFPASS(doc->insertStrux(document_end(doc), PTX_EndTable)) ||
		!TFPASS(doc->insertStrux(document_end(doc), PTX_Block)))
	{
		doc->unref();
		return;
	}

	const std::string path = UT_createTmpFile("fmtodtfull", ".odt");
	const UT_Error err = doc->AD_Document::saveAs(path.c_str(), IEFT_Unknown, false, nullptr);
	doc->unref();
	if (!TFPASS(err == UT_OK) || !is_office_odt(path))
	{
		std::remove(path.c_str());
		return;
	}

	GsfInput * input = nullptr;
	GsfInfile * zip = package_open(path, input, gsf_infile_zip_new);
	if (!TFPASS(zip != nullptr))
	{
		package_close(input, zip);
		std::remove(path.c_str());
		return;
	}

	const std::string content = zip_read(zip, "content.xml");
	const std::string meta = zip_read(zip, "meta.xml");

	TFPASS(meta.find("<meta:creation-date>2026-11-22T17:00:00</meta:creation-date>") != std::string::npos);
	TFPASS(meta.find("<dc:date>2026-11-23T08:15:30</dc:date>") != std::string::npos);

	TFPASS(content.find("<text:section") == std::string::npos);
	TFPASS(content.find("fo:background-color=\"#ffff00\"") != std::string::npos);
	TFPASS(content.find("style:text-underline-type=\"single\"") != std::string::npos);
	TFPASS(content.find("style:text-underline-style=\"solid\"") != std::string::npos);
	TFPASS(content.find("style:text-line-through-type=\"single\"") != std::string::npos);
	TFPASS(content.find("style:text-line-through-style=\"solid\"") != std::string::npos);

	// Positioned image must declare from-left/from-top and page-content coordinates.
	TFPASS(content.find("style:horizontal-pos=\"from-left\"") != std::string::npos);
	TFPASS(content.find("style:vertical-pos=\"from-top\"") != std::string::npos);
	TFPASS(content.find("style:horizontal-rel=\"page-content\"") != std::string::npos);
	TFPASS(content.find("style:vertical-rel=\"page-content\"") != std::string::npos);
	TFPASS(content.find("style:wrap=\"parallel\"") != std::string::npos);
	TFPASS(content.find("svg:x=\"1.25in\"") != std::string::npos);
	TFPASS(content.find("svg:y=\"0.75in\"") != std::string::npos);

	// Table column/row/cell style names must match their NCName definitions.
	TFPASS(content.find("table:border-model=\"collapsing\"") != std::string::npos);
	TFPASS(content.find("style:name=\"Table1-col1\"") != std::string::npos);
	TFPASS(content.find("table:style-name=\"Table1-col1\"") != std::string::npos);
	TFPASS(content.find("style:name=\"Table1-row1\"") != std::string::npos);
	TFPASS(content.find("table:style-name=\"Table1-row1\"") != std::string::npos);
	TFPASS(content.find("style:name=\"Table1-col1-row1\"") != std::string::npos);
	TFPASS(content.find("table:style-name=\"Table1-col1-row1\"") != std::string::npos);
	TFPASS(content.find("table:style-name=\"Table1.col1\"") == std::string::npos);
	TFPASS(content.find("table:style-name=\"Table1_col1_row1\"") == std::string::npos);

	package_close(input, zip);
	std::remove(path.c_str());
}

TFTEST_MAIN("PDF export embeds images, textbox frames, metadata and flushes cleanly")
{
	if (!exporter_known(".pdf"))
		return;

	PD_Document * doc = new PD_Document();
	if (!TFPASS(doc->newDocument() == UT_OK))
	{
		doc->unref();
		return;
	}

	doc->setMetaDataProp(PD_META_KEY_TITLE, "TitrePdf");
	doc->setMetaDataProp(PD_META_KEY_CREATOR, "AuteurPdf");
	doc->setMetaDataProp(PD_META_KEY_DATE, "Sun Nov 22 17:00:00 2026\n");

	if (!TFPASS(doc->createDataItem("pdfimg", false, tiny_png(), "image/png", nullptr)))
	{
		doc->unref();
		return;
	}

	const PT_DocPosition body = first_block_content(doc);
	const PP_PropertyVector imgAtts = {"dataid", "pdfimg"};
	const PP_PropertyVector imgProps = {"width", "1in", "height", "1in"};
	if (!TFPASS(doc->insertSpan(body, std::string("CorpsPdfImg"))) ||
		!TFPASS(doc->insertObject(body + 11, PTO_Image, imgAtts, imgProps)))
	{
		doc->unref();
		return;
	}

	pf_Frag_Strux * tb = nullptr;
	const PP_PropertyVector tbProps = {
		"frame-type", "textbox",
		"frame-width", "2in",
		"frame-height", "1in",
		"position-to", "column-above-text",
		"frame-col-xpos", "0.5in",
		"frame-col-ypos", "2in"
	};
	if (!TFPASS(doc->insertStrux(first_strux(doc, PTX_Block), PTX_SectionFrame, PP_NOPROPS, tbProps, &tb)) ||
		!TFPASS(tb != nullptr) ||
		!TFPASS(doc->insertStrux(tb->getPos() + tb->getLength(), PTX_Block)) ||
		!TFPASS(doc->insertSpan(tb->getPos() + tb->getLength() + 1, std::string("CadrePdf"))) ||
		!TFPASS(doc->insertStrux(tb->getPos() + tb->getLength() + 9, PTX_EndFrame)))
	{
		doc->unref();
		return;
	}

	const std::string path = UT_createTmpFile("fmtpdfimg", ".pdf");
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
	TFPASS(bytes.find("%%EOF") != std::string::npos);
	TFPASSEQ(declared, 1);
	TFPASS(bytes.find("/Subtype /Image") != std::string::npos || decoded.find("/Subtype /Image") != std::string::npos);
	TFPASS(pdf_has_text(bytes, "CorpsPdfImg"));
	TFPASS(pdf_has_text(bytes, "CadrePdf"));
}
