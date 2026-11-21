/* AbiWord
 * Copyright (C) 2026
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "tf_test.h"
#include "ut_go_file.h"

#define TFSUITE "core.af.util.url"

TFTEST_MAIN("UT_go_url_may_open allows documents and refuses programs")
{
	TFPASS(!UT_go_url_may_open(nullptr));
	TFPASS(!UT_go_url_may_open(""));
	TFPASS(!UT_go_url_may_open("mailto:"));
	TFPASS(UT_go_url_may_open("mailto:user@example.com"));
	TFPASS(UT_go_url_may_open("https://example.com/a"));
	TFPASS(UT_go_url_may_open("http://example.com"));
	TFPASS(!UT_go_url_may_open("http://"));
	TFPASS(!UT_go_url_may_open("ms-msdt:id"));
	TFPASS(!UT_go_url_may_open("search-ms:query=x"));

	TFPASS(UT_go_url_may_open("file:///tmp/notes.txt"));
	TFPASS(UT_go_url_may_open("file:///tmp/picture.png"));
	TFPASS(!UT_go_url_may_open("file:///tmp/setup.exe"));
	TFPASS(!UT_go_url_may_open("file:///tmp/run.bat"));
	TFPASS(!UT_go_url_may_open("file:///tmp/script.ps1"));
	TFPASS(!UT_go_url_may_open("file:///tmp/link.lnk"));
	TFPASS(!UT_go_url_may_open("FILE:///TMP/SETUP.EXE"));
}
