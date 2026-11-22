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

#ifndef IE_EXP_MSWORD_97_H
#define IE_EXP_MSWORD_97_H

#include "ie_exp.h"

class ABI_EXPORT IE_Exp_MsWord_97 : public IE_Exp
{
public:
	IE_Exp_MsWord_97(PD_Document * pDocument);
	virtual ~IE_Exp_MsWord_97();

protected:
	virtual GsfOutput * _openFile(const char * szFilename) override;
	virtual UT_Error _writeDocument(void) override;
};

#endif
