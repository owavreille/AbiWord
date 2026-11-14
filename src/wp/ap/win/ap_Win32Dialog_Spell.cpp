/* AbiWord
 * Copyright (C) 1998,1999 AbiSource, Inc.
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


#include <windows.h>
#include <richedit.h>

#include "ut_string.h"
#include "ut_assert.h"
#include "ut_debugmsg.h"
#include "ut_Win32LocaleString.h"

#include "xap_App.h"
#include "xap_Win32App.h"
#include "xap_Win32FrameImpl.h"

#include "ap_Strings.h"
#include "ap_Dialog_Id.h"
#include "ap_Dialog_Spell.h"
#include "ap_Win32Dialog_Spell.h"
#include "xap_Win32DialogHelper.h"
#include "ap_Win32Resources.rc2"

/*****************************************************************/

XAP_Dialog * AP_Win32Dialog_Spell::static_constructor(XAP_DialogFactory * pFactory, XAP_Dialog_Id id)
{
   AP_Win32Dialog_Spell * p = new AP_Win32Dialog_Spell(pFactory,id);
   return p;
}

AP_Win32Dialog_Spell::AP_Win32Dialog_Spell(XAP_DialogFactory * pDlgFactory, XAP_Dialog_Id id)
  : AP_Dialog_Spell(pDlgFactory,id)
{
	m_bChangingSelection = 0;
}

AP_Win32Dialog_Spell::~AP_Win32Dialog_Spell(void)
{
}

/*****************************************************************/

void AP_Win32Dialog_Spell::runModal(XAP_Frame * pFrame)
{
	// call the base class method to initialize some basic xp stuff
	AP_Dialog_Spell::runModal(pFrame);

	m_bCancelled = false;
	bool bRes = nextMisspelledWord();

	// if nothing misspelled, then nothing to do
	if (!bRes)
		return;

	// raise the dialog
	XAP_Win32App * pWin32App = static_cast<XAP_Win32App *>(m_pApp);

	LPCWSTR lpTemplate = nullptr;

	UT_return_if_fail (m_id == static_cast<XAP_Dialog_Id>(AP_DIALOG_ID_SPELL));

	lpTemplate = MAKEINTRESOURCEW(AP_RID_DIALOG_SPELL);

	UT_DebugOnly<int> result = DialogBoxParamW(pWin32App->getInstance(),lpTemplate,
						static_cast<XAP_Win32FrameImpl*>(pFrame->getFrameImpl())->getTopLevelWindow(),
						s_dlgProc,(LPARAM)this);
	UT_ASSERT_HARMLESS((result != -1));
}

INT_PTR CALLBACK AP_Win32Dialog_Spell::s_dlgProc(HWND hWnd,UINT msg,WPARAM wParam,LPARAM lParam)
{
	// This is a static function.

	AP_Win32Dialog_Spell * pThis;
	
	switch (msg)
	{
	case WM_INITDIALOG:
		pThis = (AP_Win32Dialog_Spell *)lParam;
		SetWindowLongPtrW(hWnd,DWLP_USER,lParam);
		return pThis->_onInitDialog(hWnd,wParam,lParam);
		
	case WM_COMMAND:
		pThis = (AP_Win32Dialog_Spell *)GetWindowLongPtrW(hWnd,DWLP_USER);
		return pThis->_onCommand(hWnd,wParam,lParam);
		
	default:
		return 0;
	}
}

#define _DS(c,s)	str.fromUTF8(pSS->getValue(AP_STRING_ID_##s)); \
					SetDlgItemTextW(hWnd,AP_RID_DIALOG_##c,str.c_str())
#define _DSX(c,s)	str.fromUTF8(pSS->getValue(XAP_STRING_ID_##s)); \
					SetDlgItemTextW(hWnd,AP_RID_DIALOG_##c,str.c_str())

BOOL AP_Win32Dialog_Spell::_onInitDialog(HWND hWnd, WPARAM /*wParam*/, LPARAM /*lParam*/)
{
	const XAP_StringSet * pSS = m_pApp->getStringSet();
	
	UT_Win32LocaleString str;

	str.fromUTF8(pSS->getValue(AP_STRING_ID_DLG_Spell_SpellTitle));
	SetWindowTextW(hWnd, str.c_str());

	// localize controls
	_DSX(SPELL_BTN_CANCEL,		DLG_Cancel);

	_DS(SPELL_TEXT_NOT,			DLG_Spell_UnknownWord);
	_DS(SPELL_TEXT_CHANGE,		DLG_Spell_ChangeTo);
	_DS(SPELL_TEXT_SUGGEST,		DLG_Spell_Suggestions);
	_DS(SPELL_BTN_IGNORE,		DLG_Spell_Ignore);
	_DS(SPELL_BTN_IGNOREALL,	DLG_Spell_IgnoreAll);
	_DS(SPELL_BTN_ADD,			DLG_Spell_AddToDict);
	_DS(SPELL_BTN_CHANGE,		DLG_Spell_Change);
	_DS(SPELL_BTN_CHANGEALL,	DLG_Spell_ChangeAll);

	// remember the windows we're using 
	m_hwndDlg = hWnd;
	m_hwndSentence = GetDlgItem(hWnd, AP_RID_DIALOG_SPELL_RICH_SENTENCE);
	m_hwndChangeTo = GetDlgItem(hWnd, AP_RID_DIALOG_SPELL_EDIT_CHANGE);
	m_hwndSuggest  = GetDlgItem(hWnd, AP_RID_DIALOG_SPELL_LIST_SUGGEST);

	// set initial state
	makeWordVisible();
	_showMisspelledWord();
	XAP_Win32DialogHelper::s_centerDialog(hWnd);			

	return 1;							// 1 == we did not call SetFocus()
}

void AP_Win32Dialog_Spell::_toggleChangeButtons(bool bEnable) const
{
	EnableWindow(GetDlgItem(m_hwndDlg,AP_RID_DIALOG_SPELL_BTN_CHANGE),bEnable);
	EnableWindow(GetDlgItem(m_hwndDlg,AP_RID_DIALOG_SPELL_BTN_CHANGEALL),bEnable);
}

// The iterator hands back a span of iLength characters that is not
// terminated there. fromUCS4() would read until a later zero.
static void spellSpanToLocale(UT_Win32LocaleString & str, const UT_UCS4Char * text, UT_sint32 length)
{
	if (!text || length <= 0)
	{
		str.fromUTF8("");
		return;
	}
	UT_UCS4Char * copy = new UT_UCS4Char[static_cast<size_t>(length) + 1];
	memcpy(copy, text, static_cast<size_t>(length) * sizeof(UT_UCS4Char));
	copy[length] = 0;
	str.fromUCS4(copy);
	delete [] copy;
}

void AP_Win32Dialog_Spell::_showMisspelledWord(void)
{
	// clear existing values
	SendMessageW(m_hwndSentence, WM_SETTEXT, 0, 0);
	SendMessageW(m_hwndChangeTo, WM_SETTEXT, 0, 0);
	SendMessageW(m_hwndSuggest, LB_RESETCONTENT, 0, 0);

	CHARFORMATW cf;
	const UT_UCS4Char *p;
	UT_uint32 len;
	UT_uint32 sum = 0;

	// insert start of sentence
	SendMessageW(m_hwndSentence, EM_SETSEL, (WPARAM) sum, (LPARAM) sum);

	cf.cbSize = sizeof(cf);
	cf.dwMask = CFM_COLOR | CFM_PROTECTED;
	cf.dwEffects = CFE_AUTOCOLOR | CFE_PROTECTED;
	SendMessageW(m_hwndSentence, EM_SETCHARFORMAT, (WPARAM) SCF_ALL, (LPARAM) &cf);

	UT_sint32 iLength;

	UT_Win32LocaleString str;

    p = m_pWordIterator->getPreWord(iLength);
	if (0 < iLength)
	{
		spellSpanToLocale(str, p, iLength);
		SendMessageW(m_hwndSentence, WM_SETTEXT, 0, (LPARAM)str.c_str());
		sum += static_cast<UT_uint32>(str.size());
	}

	// insert misspelled word (in highlight color)
	SendMessageW(m_hwndSentence, EM_SETSEL, (WPARAM) sum, (LPARAM) sum);

	cf.dwMask = CFM_COLOR | CFM_PROTECTED;
	cf.dwEffects = CFE_PROTECTED;
	cf.crTextColor = RGB(255,0,0);
	SendMessageW(m_hwndSentence, EM_SETCHARFORMAT, (WPARAM) SCF_SELECTION, (LPARAM) &cf);

    p = m_pWordIterator->getCurrentWord(iLength);
	if (0 < iLength)
	{
		spellSpanToLocale(str, p, iLength);
		SendMessageW(m_hwndSentence, EM_REPLACESEL, FALSE, (LPARAM)str.c_str());
		sum += static_cast<UT_uint32>(str.size());
	}

    // save this offset for centering of selected word later on
    m_iWordOffsetInSentence = sum;

	// insert end of sentence
	SendMessageW(m_hwndSentence, EM_SETSEL, (WPARAM) sum, (LPARAM) sum);
	cf.dwMask = CFM_COLOR | CFM_PROTECTED;
	cf.dwEffects = CFE_AUTOCOLOR | CFE_PROTECTED;
	SendMessageW(m_hwndSentence, EM_SETCHARFORMAT, (WPARAM) SCF_SELECTION, (LPARAM) &cf);

    p = m_pWordIterator->getPostWord(iLength);
	if (0 < iLength)
	{
		spellSpanToLocale(str, p, iLength);
		SendMessageW(m_hwndSentence, EM_REPLACESEL, FALSE, (LPARAM)str.c_str());
	}

	// insert suggestions
	if (!m_Suggestions->getItemCount())
	{
		const XAP_StringSet * pSS = m_pApp->getStringSet();
		str.fromUTF8(pSS->getValue(AP_STRING_ID_DLG_Spell_NoSuggestions));
		SendMessageW(m_hwndSuggest, LB_ADDSTRING, 0, (LPARAM)str.c_str());

		m_iSelectedRow = -1;
		_toggleChangeButtons(false);
	} 
	else 
	{
		for (UT_sint32 i = 0; i < m_Suggestions->getItemCount(); i++)
		{
			p = (UT_UCS4Char *) m_Suggestions->getNthItem(i);
			len = UT_UCS4_strlen(p);
			if (len)
			{
				//buf = new char [len + 1];
				//UT_UCS4_strcpy_to_char(buf, p);
				str.fromUCS4(p);
				SendMessageW(m_hwndSuggest, LB_ADDSTRING, 0, (LPARAM)str.c_str());
				//DELETEP(buf);
			}
		}

		m_iSelectedRow = 0;
		_toggleChangeButtons(true);
	}

	SendMessageW(m_hwndSuggest, LB_SETCURSEL, m_iSelectedRow, 0);

	// populate the change field, if appropriate
	m_bChangingSelection = 1;
	_suggestChange();
	m_bChangingSelection = 0;
}

void AP_Win32Dialog_Spell::_suggestChange(void)
{
	// cast is to match Unix usage
	// should be safe here, because there just aren't that many suggestions 
	m_iSelectedRow = (short) SendMessageW(m_hwndSuggest, LB_GETCURSEL, 0, 0);

	if (!m_Suggestions->getItemCount()) 
	{
		// FIXME: this should insert the original word, surely!

		// no change to suggest, ignore it
		if (m_iSelectedRow != -1)
			SendMessageW(m_hwndSuggest, LB_SETCURSEL, -1, 0);
	}
	else
	{
		// copy suggestion to edit field
		UT_return_if_fail ((m_iSelectedRow > -1));

		const LRESULT textLen = SendMessageW(m_hwndSuggest, LB_GETTEXTLEN, m_iSelectedRow, 0);
		if (textLen < 0)
			return;
		wchar_t * buf = new wchar_t[static_cast<size_t>(textLen) + 1];
		buf[0] = 0;
		if (SendMessageW(m_hwndSuggest, LB_GETTEXT, m_iSelectedRow, (LPARAM)buf) != LB_ERR)
			SendMessageW(m_hwndChangeTo, WM_SETTEXT, 0, (LPARAM)buf);
		delete [] buf;

		// you'd think this'd be overkill...
		SendMessageW(m_hwndSuggest, LB_SETCURSEL, m_iSelectedRow, 0);
	}
}

// returns a pointer which needs to be FREEP'd by the caller
static UT_UCS4Char * s_getUCSText(HWND hwnd)
{
	const int len = GetWindowTextLengthW(hwnd);
	if (len <= 0)
		return nullptr;

	wchar_t * wide = new wchar_t[static_cast<size_t>(len) + 1];
	if (!wide)
		return nullptr;
	if (GetWindowTextW(hwnd, wide, len + 1) <= 0)
	{
		delete [] wide;
		return nullptr;
	}

	UT_UCS4Char * text = static_cast<UT_UCS4Char *>(UT_calloc(static_cast<UT_uint32>(len) + 1, sizeof(UT_UCS4Char)));
	if (!text)
	{
		delete [] wide;
		return nullptr;
	}

	int out = 0;
	for (int i = 0; wide[i] != 0 && out < len; )
	{
		UT_uint32 unit = static_cast<UT_uint32>(wide[i++]);
		if (unit >= 0xD800 && unit <= 0xDBFF && wide[i] >= 0xDC00 && wide[i] <= 0xDFFF)
		{
			const UT_uint32 low = static_cast<UT_uint32>(wide[i++]);
			unit = 0x10000 + (((unit - 0xD800) << 10) | (low - 0xDC00));
		}
		text[out++] = static_cast<UT_UCS4Char>(unit);
	}
	text[out] = 0;
	delete [] wide;
	return text;
}

void AP_Win32Dialog_Spell::_change(void)
{
	UT_UCS4Char * replace = nullptr;

	if (m_iSelectedRow != -1)
	{
		replace = (UT_UCS4Char*) m_Suggestions->getNthItem(m_iSelectedRow);
		changeWordWith(replace);
	}
	else
	{
		replace = s_getUCSText(m_hwndChangeTo);
		if (!replace || !UT_UCS4_strlen(replace))
		{
			FREEP(replace);
			return;
		}
		changeWordWith(replace);
		FREEP(replace);
	}

	_tryAgain();
}

void AP_Win32Dialog_Spell::_changeAll(void)
{
	UT_UCS4Char * replace = nullptr;
	if (m_iSelectedRow != -1)
	{
		replace = (UT_UCS4Char*) m_Suggestions->getNthItem(m_iSelectedRow);
		addChangeAll(replace);
		changeWordWith(replace);
	}
	else
	{
		replace = s_getUCSText(m_hwndChangeTo);
		if (!replace || !UT_UCS4_strlen(replace))
		{
			FREEP(replace);
			return;
		}
		addChangeAll(replace);
		changeWordWith(replace);
		FREEP(replace);
	}

	_tryAgain();
}

void AP_Win32Dialog_Spell::_tryAgain(void)
{
	// clear prior suggestions
	_purgeSuggestions();

	// what's next
	bool bRes = nextMisspelledWord();

	if (bRes)
	{
		// show word in main window
		makeWordVisible();

		// update dialog with new misspelled word info/suggestions
		_showMisspelledWord();
	}
	else
	{
		// all done
		UT_ASSERT_HARMLESS((m_bCancelled == false));
		EndDialog(m_hwndDlg,0);
	}
}

BOOL AP_Win32Dialog_Spell::_onCommand(HWND hWnd, WPARAM wParam, LPARAM lParam)
{
	WORD wNotifyCode = HIWORD(wParam);
	WORD wId = LOWORD(wParam);
	HWND hWndCtrl = (HWND)lParam;

	switch (wId)
	{
	case AP_RID_DIALOG_SPELL_RICH_SENTENCE:
		switch (wNotifyCode)
		{
		case EN_SETFOCUS:
			{
				// set scroll position so misspelled word is centered
				SendMessageW(m_hwndSentence, EM_SETSEL, (WPARAM) m_iWordOffsetInSentence, 
                            (LPARAM) m_iWordOffsetInSentence);
				SendMessageW(m_hwndSentence, EM_SCROLLCARET, 0, 0);
			}
			return 1;

		default:
			return 0;
		}
		
	case AP_RID_DIALOG_SPELL_EDIT_CHANGE:
		switch (wNotifyCode)
		{
		case EN_CHANGE:
			if (GetWindowTextLength(hWndCtrl))
			{
				if( !m_bChangingSelection )
				{
					m_iSelectedRow = -1;
					SendMessageW(m_hwndSuggest, LB_SETCURSEL, m_iSelectedRow, 0);
					_toggleChangeButtons(true);
				}
			}
			else
			{
				_toggleChangeButtons(false);
			}
			return 1;

		default:
			return 0;
		}
		
	case AP_RID_DIALOG_SPELL_LIST_SUGGEST:
		switch (HIWORD(wParam))
		{
			case LBN_SELCHANGE:
				m_bChangingSelection = 1;
				_suggestChange();
				m_bChangingSelection = 0;
				return 1;

			case LBN_DBLCLK:
				if (!m_Suggestions->getItemCount()) return 1;
				m_bChangingSelection = 1;
				m_iSelectedRow = SendMessage(hWndCtrl, LB_GETCURSEL, 0, 0);
				if (m_iSelectedRow!=CB_ERR)	_change();
				m_bChangingSelection = 0;
				return 1;

			default:
				return 0;
		}
		break;

	case AP_RID_DIALOG_SPELL_BTN_IGNORE:
		ignoreWord();
		_tryAgain();
		return 1;

	case AP_RID_DIALOG_SPELL_BTN_IGNOREALL:
		addIgnoreAll();
		ignoreWord();
		_tryAgain();
		return 1;

	case AP_RID_DIALOG_SPELL_BTN_ADD:
		addToDict();
		ignoreWord();
		_tryAgain();
		return 1;

	case AP_RID_DIALOG_SPELL_BTN_CHANGE:
		_change();
		return 1;

	case AP_RID_DIALOG_SPELL_BTN_CHANGEALL:
		_changeAll();
		return 1;

	case IDCANCEL:						// also AP_RID_DIALOG_SPELL_BTN_CANCEL
		m_bCancelled = true;
		EndDialog(hWnd,0);
		return 1;

	default:							// we did not handle this notification
		UT_DEBUGMSG(("WM_Command for id %ld\n",wId));
		return 0;						// return zero to let windows take care of it.
	}
}

