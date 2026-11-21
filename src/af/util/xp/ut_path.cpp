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
#include <fcntl.h>
#ifndef O_BINARY
#define O_BINARY 0
#endif

#include <glib/gstdio.h>

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
	// g_file_open_tmp uses mkstemp (O_EXCL). The old name was prefix plus a
	// value seeded from the clock, then created with fopen, so a local user
	// could win the race.
	std::string tmpl;
	tmpl.reserve(prefix.size() + 6);
	for (char c : prefix)
	{
		if (c == '/' || c == '\\' || c == ':' || c == '.')
			tmpl.push_back('_');
		else
			tmpl.push_back(c);
	}
	if (tmpl.empty())
		tmpl = "abi";
	tmpl += "XXXXXX";

	gchar * name = nullptr;
	GError * err = nullptr;
	const gint fd = g_file_open_tmp(tmpl.c_str(), &name, &err);
	if (fd < 0)
	{
		if (err)
			g_error_free(err);
		return "";
	}
	g_close(fd);

	std::string result = name ? name : "";
	g_free(name);
	if (result.empty())
		return "";
	if (extension.find_first_of("/\\:") != std::string::npos)
	{
		g_unlink(result.c_str());
		return "";
	}
	if (extension.empty())
		return result;

	const std::string withExt = result + extension;
	const int outfd = g_open(withExt.c_str(), O_CREAT | O_EXCL | O_RDWR | O_BINARY, 0600);
	if (outfd < 0)
	{
		g_unlink(result.c_str());
		return "";
	}
	g_close(outfd);
	g_unlink(result.c_str());
	return withExt;
}
