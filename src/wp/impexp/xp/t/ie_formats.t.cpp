/* AbiWord
 * Copyright (C) 2026
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include <cstdlib>
#include <cstring>
#include <string>

#include <glib.h>

#include "tf_test.h"
#include "pd_Document.h"
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

static bool text_has_markers(const std::string & text)
{
	return text.find(MARKER) != std::string::npos &&
		text.find(MARKER_NEXT) != std::string::npos;
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

	UT_Error err = doc->saveAs(path.c_str(), IEFT_Unknown, false, nullptr);
	doc->unref();
	if (!TFPASS(err == UT_OK))
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
	err = again->saveAs(second_path.c_str(), IEFT_Unknown, false, nullptr);
	again->unref();
	if (!TFPASS(err == UT_OK))
	{
		std::remove(second_path.c_str());
		return false;
	}

	text.clear();
	ok = load_text(second_path, text) && TFPASS(text_has_markers(text));
	std::remove(second_path.c_str());
	return ok;
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
