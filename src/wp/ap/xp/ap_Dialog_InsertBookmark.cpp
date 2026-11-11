/* AbiWord
 * Copyright (C) 2001 Tomas Frydrych
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

#include <string.h>

#include "xap_Dialog_Id.h"
#include "xap_DialogFactory.h"
#include "ap_Dialog_InsertBookmark.h"

AP_Dialog_InsertBookmark::AP_Dialog_InsertBookmark( XAP_DialogFactory * pDlgFactory,
                                                    XAP_Dialog_Id id )
    : AP_Dialog_Modal(pDlgFactory,id, "interface/dialogbookmark")
    , m_answer(a_CANCEL)
{
	m_pBookmark[0] = 0;
}

AP_Dialog_InsertBookmark::~AP_Dialog_InsertBookmark(void)
{
}

void AP_Dialog_InsertBookmark::setAnswer(AP_Dialog_InsertBookmark::tAnswer a)
{
  m_answer = a;
}

AP_Dialog_InsertBookmark::tAnswer AP_Dialog_InsertBookmark::getAnswer(void) const
{
  return m_answer;
}

UT_sint32 AP_Dialog_InsertBookmark::getExistingBookmarksCount() const
{
	UT_return_val_if_fail (m_pDoc, 0);
	return m_pDoc->getBookmarkCount();
}

const std::string & AP_Dialog_InsertBookmark::getNthExistingBookmark(UT_uint32 n) const
{
	UT_ASSERT(m_pDoc);
	return m_pDoc->getNthBookmark(n);
}

const gchar * AP_Dialog_InsertBookmark::getBookmark() const
{
  return m_pBookmark;
}

void AP_Dialog_InsertBookmark::setBookmark(const gchar * mark)
{
	if (!mark)
	{
		m_pBookmark[0] = 0;
		return;
	}

	UT_uint32 n = 0;
	UT_uint32 chars = 0;
	while (mark[n] && chars < BOOKMARK_CHAR_LIMIT && n < BOOKMARK_SIZE_LIMIT)
	{
		unsigned char c = static_cast<unsigned char>(mark[n]);
		UT_uint32 clen = 1;
		if ((c & 0x80) == 0)
			clen = 1;
		else if ((c & 0xe0) == 0xc0)
			clen = 2;
		else if ((c & 0xf0) == 0xe0)
			clen = 3;
		else if ((c & 0xf8) == 0xf0)
			clen = 4;
		else
			break;

		if (n + clen > BOOKMARK_SIZE_LIMIT)
			break;

		bool complete = true;
		for (UT_uint32 i = 1; i < clen; ++i)
		{
			if (mark[n + i] == 0)
			{
				complete = false;
				break;
			}
		}
		if (!complete)
			break;

		n += clen;
		++chars;
	}

	if (n > 0)
		memcpy(m_pBookmark, mark, n);
	m_pBookmark[n] = 0;
}

void AP_Dialog_InsertBookmark::setDoc(FV_View * pView)
{
	m_pDoc = pView->getDocument();
}
