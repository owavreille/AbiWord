/* AbiSource Program Utilities
 * Copyright (C) 1998-2000 AbiSource, Inc.
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


#include <stdio.h>
#ifdef _WIN32
#include <windows.h>
#endif


#include "ut_path.h"


UT_UTF8String UT_go_basename(const char* uri)
{
	UT_UTF8String _base_name;
	char *base_name = UT_go_basename_from_uri(uri);
	if(base_name) {
		_base_name = base_name;
		g_free(base_name);
	}
	return _base_name;
}

std::string UT_createTmpFile(const std::string& prefix, const std::string& extension)
{
	const gchar *filename = g_build_filename (g_get_tmp_dir (), prefix.c_str(), nullptr);
	UT_return_val_if_fail(filename, "");

	std::string sName = filename;
	FREEP(filename);

	UT_UTF8String rand = UT_UTF8String_sprintf("%X", UT_rand() * 0xFFFFFF);
	sName += rand.utf8_str();
	sName += extension;

#ifdef _WIN32
	const int needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, sName.c_str(), -1, nullptr, 0);
	if (needed <= 0)
		return "";
	WCHAR * wide = new WCHAR[needed];
	FILE * f = nullptr;
	if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, sName.c_str(), -1, wide, needed) > 0)
		f = _wfopen(wide, L"w+b");
	delete [] wide;
#else
	FILE* f = fopen (sName.c_str(), "w+b");
#endif
	if (!f)
		return "";

	fclose(f);
	return sName;
}
