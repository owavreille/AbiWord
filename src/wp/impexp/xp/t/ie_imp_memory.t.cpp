/* AbiWord
 * Copyright (C) 2026
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include <cstdio>
#include <string>

#include "tf_test.h"
#include "pd_Document.h"
#include "ut_path.h"

#define TFSUITE "core.wp.impexp.import"

namespace {

static UT_Error load_bytes(const std::string & bytes, const char * extension)
{
	const std::string path = UT_createTmpFile("imp", extension);
	TFPASS(!path.empty());
	if (path.empty())
		return UT_ERROR;

	FILE * out = std::fopen(path.c_str(), "wb");
	TFPASS(out != nullptr);
	if (!out)
	{
		std::remove(path.c_str());
		return UT_ERROR;
	}
	const size_t wrote = std::fwrite(bytes.data(), 1, bytes.size(), out);
	std::fclose(out);
	TFPASS(wrote == bytes.size());

	PD_Document * doc = new PD_Document();
	const UT_Error err = doc->readFromFile(path.c_str(), IEFT_Unknown, nullptr);
	doc->unref();
	std::remove(path.c_str());
	return err;
}

static UT_Error load_fixture(const char * relative)
{
	std::string data_file;
	TFPASS(TF_Test::ensure_test_data(relative, data_file));
	if (data_file.empty())
		return UT_ERROR;

	PD_Document * doc = new PD_Document();
	const UT_Error err = doc->readFromFile(data_file.c_str(), IEFT_Unknown, nullptr);
	doc->unref();
	return err;
}

}

TFTEST_MAIN("Import the shipped ABW, text and RTF fixtures")
{
	TFPASS(UT_IS_IE_SUCCESS(load_fixture("/test/auto/hello.dat.1.abw")));
	TFPASS(UT_IS_IE_SUCCESS(load_fixture("/test/auto/hello.dat.2.txt")));
	TFPASS(UT_IS_IE_SUCCESS(load_fixture("/test/wp/rtftest.rtf")));
	TFPASS(UT_IS_IE_SUCCESS(load_fixture("/test/wp/hyphen.rtf")));
}

TFTEST_MAIN("Import a tiny RTF and one with an overlong parameter")
{
	TFPASS(UT_IS_IE_SUCCESS(load_bytes("{\\rtf1\\ansi Hello\\par}", ".rtf")));

	std::string huge = "{\\rtf1\\ansi\\b";
	huge.append(300, '9');
	huge += " Hello\\par}";
	TFPASS(UT_IS_IE_SUCCESS(load_bytes(huge, ".rtf")));
}

TFTEST_MAIN("An RTF import field cannot name an absolute path")
{
	TFPASS(UT_IS_IE_SUCCESS(load_bytes(
		"{\\rtf1\\ansi{\\field{\\*\\fldinst IMPORT /etc/passwd}}\\par}",
		".rtf")));
}
