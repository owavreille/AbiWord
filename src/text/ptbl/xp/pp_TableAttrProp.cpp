/* -*- mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: t -*- */
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

#include <memory>

#include "ut_types.h"
#include "ut_assert.h"
#include "ut_vector.h"
#include "ut_string.h"
#include "pp_AttrProp.h"
#include "pp_TableAttrProp.h"


pp_TableAttrProp::pp_TableAttrProp():
	m_vecTable(54,4,true) // there seems to be 50+ of these at the moment
{
	m_mapByCheckSum.reserve(64);
}

pp_TableAttrProp::~pp_TableAttrProp()
{
	UT_VECTOR_PURGEALL(PP_AttrProp *, m_vecTable);
}

bool pp_TableAttrProp::addAP(PP_AttrProp * pAP,
								UT_sint32 * pSubscript)
{
	UT_sint32 u;
	bool result = (m_vecTable.addItem(pAP,&u) == 0);

	if (result)
	{
		if (pSubscript)
		{
			*pSubscript = u;
		}
		pAP->setIndex(u);	//$HACK
		m_mapByCheckSum.emplace(pAP->getCheckSum(), u);
	}

	return result;
}

bool pp_TableAttrProp::createAP(UT_sint32 * pSubscript)
{
	PP_AttrProp * pNew = new PP_AttrProp();
	if (!pNew)
		return false;
	UT_sint32 u;
	if (m_vecTable.addItem(pNew,&u) != 0)
	{
		delete pNew;
		return false;
	}

	pNew->setIndex(u);	//$HACK

	if (pSubscript)
	{
		*pSubscript = u;
	}
	else
	{
		// create default empty AP
		pNew->markReadOnly();
		m_mapByCheckSum.emplace(pNew->getCheckSum(), u);
	}

	return true;
}

bool pp_TableAttrProp::createAP(const PP_PropertyVector & attributes,
								   const PP_PropertyVector & properties,
								   UT_sint32 * pSubscript)
{
	std::unique_ptr<PP_AttrProp> pAP(new PP_AttrProp());
	if (!pAP->setAttributes(attributes) || !pAP->setProperties(properties))
		return false;

	pAP->markReadOnly();

	UT_sint32 subscript = 0;
	if (!addAP(pAP.get(), &subscript))
		return false;
	pAP.release();

	if (pSubscript)
		*pSubscript = subscript;
	return true;
}

bool pp_TableAttrProp::createAP(const PP_PropertyVector & pVector,
								   UT_sint32 * pSubscript)
{
	std::unique_ptr<PP_AttrProp> pAP(new PP_AttrProp());
	if (!pAP->setAttributes(pVector))
		return false;

	pAP->markReadOnly();

	UT_sint32 subscript = 0;
	if (!addAP(pAP.get(), &subscript))
		return false;
	pAP.release();

	if (pSubscript)
		*pSubscript = subscript;
	return true;
}

bool pp_TableAttrProp::findMatch(const PP_AttrProp * pMatch,
									UT_sint32 * pSubscript) const
{
	// return true if we find an AP in our table which is
	// an exact match for the attributes/properties in pMatch.
	// set *pSubscript to the subscript of the matching item.

	UT_uint32 checksum = pMatch->getCheckSum();
	auto range = m_mapByCheckSum.equal_range(checksum);
	for (auto it = range.first; it != range.second; ++it)
	{
		const PP_AttrProp * pK = m_vecTable.getNthItem(it->second);
		if (pK && pMatch->isExactMatch(*pK))
		{
			// Need to return an index of the element in the MAIN
			// vector table
			*pSubscript = it->second;
			return true;
		}
	}
	return false;
}

	
const PP_AttrProp * pp_TableAttrProp::getAP(UT_sint32 subscript) const
{
	UT_sint32 count = m_vecTable.getItemCount();
	if (subscript >= 0 && subscript < count)
		return (const PP_AttrProp *)m_vecTable.getNthItem(subscript);
	else
		return nullptr;
}
