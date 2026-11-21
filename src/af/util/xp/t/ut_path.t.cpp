/* AbiWord
 * Copyright (C) 2026
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include <cstdio>

#include "tf_test.h"
#include "ut_path.h"

#define TFSUITE "core.af.util.path"

TFTEST_MAIN("UT_createTmpFile is exclusive and rejects a path extension")
{
	const std::string first = UT_createTmpFile("abi", ".txt");
	const std::string second = UT_createTmpFile("abi", ".txt");

	TFPASS(!first.empty());
	TFPASS(!second.empty());
	TFPASS(first != second);
	TFPASS(first.size() >= 4);
	TFPASS(first.compare(first.size() - 4, 4, ".txt") == 0);

	FILE * created = std::fopen(first.c_str(), "rb");
	TFPASS(created != nullptr);
	if (created)
		std::fclose(created);

	TFPASS(UT_createTmpFile("abi", "/tmp/escaped").empty());
	TFPASS(UT_createTmpFile("abi", "..\\secret").empty());

	const std::string slashed = UT_createTmpFile("a/b", ".txt");
	TFPASS(!slashed.empty());
	const std::string::size_type slash = slashed.find_last_of("/\\");
	const std::string base = slash == std::string::npos ? slashed : slashed.substr(slash + 1);
	TFPASS(base.find("a_b") != std::string::npos);
	TFPASS(base.find("a/b") == std::string::npos);

	std::remove(first.c_str());
	std::remove(second.c_str());
	std::remove(slashed.c_str());
}
