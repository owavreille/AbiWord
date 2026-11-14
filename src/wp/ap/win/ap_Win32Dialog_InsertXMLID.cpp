/* AbiWord
 * Copyright (C) 2011 Ben Martin
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

#include "ut_assert.h"
#include "ut_Win32LocaleString.h"
#include "xap_App.h"

#include "ap_Strings.h"
#include "ap_Dialog_Id.h"
#include "ap_Win32Dialog_InsertXMLID.h"
#include "ap_Win32Resources.rc2"

static const wchar_t * s_oldProcProp = L"abi-xmlid-proc";

XAP_Dialog * AP_Win32Dialog_InsertXMLID::static_constructor(XAP_DialogFactory * pFactory,
															XAP_Dialog_Id id)
{
	return new AP_Win32Dialog_InsertXMLID(pFactory, id);
}

AP_Win32Dialog_InsertXMLID::AP_Win32Dialog_InsertXMLID(XAP_DialogFactory * pDlgFactory,
													   XAP_Dialog_Id id)
	: AP_Dialog_InsertXMLID(pDlgFactory, id)
{
}

AP_Win32Dialog_InsertXMLID::~AP_Win32Dialog_InsertXMLID(void)
{
}

void AP_Win32Dialog_InsertXMLID::runModal(XAP_Frame * pFrame)
{
	UT_return_if_fail(pFrame);
	UT_return_if_fail(m_id == static_cast<XAP_Dialog_Id>(AP_DIALOG_ID_INSERTXMLID));
	setDialog(this);
	createModal(pFrame, MAKEINTRESOURCEW(AP_RID_DIALOG_INSERTXMLID));
}

BOOL AP_Win32Dialog_InsertXMLID::_onInitDialog(HWND hWnd, WPARAM /*wParam*/, LPARAM /*lParam*/)
{
	const XAP_StringSet * pSS = m_pApp->getStringSet();
	setDialogTitle(pSS->getValue(AP_STRING_ID_DLG_InsertXMLID_Title));
	setDlgItemText(AP_RID_DIALOG_INSERTXMLID_LBL_MESSAGE, pSS->getValue(AP_STRING_ID_DLG_InsertXMLID_Msg));
	setDlgItemText(AP_RID_DIALOG_INSERTXMLID_BTN_OK, pSS->getValue(AP_STRING_ID_DLG_InsertButton));
	setDlgItemText(AP_RID_DIALOG_INSERTXMLID_BTN_DELETE, pSS->getValue(XAP_STRING_ID_DLG_Delete));
	setDlgItemText(AP_RID_DIALOG_INSERTXMLID_BTN_CANCEL, pSS->getValue(XAP_STRING_ID_DLG_Cancel));
	enableControl(AP_RID_DIALOG_INSERTXMLID_BTN_OK, false);

	HWND hEdit = GetDlgItem(hWnd, AP_RID_DIALOG_INSERTXMLID_EBX_NAME);
	WNDPROC old = (WNDPROC)SetWindowLongPtrW(hEdit, GWLP_WNDPROC, (LONG_PTR)s_nameProc);
	SetPropW(hEdit, s_oldProcProp, (HANDLE)old);

	centerDialog();
	return 1;
}

std::string AP_Win32Dialog_InsertXMLID::_nameText(void) const
{
	HWND hEdit = GetDlgItem(m_hDlg, AP_RID_DIALOG_INSERTXMLID_EBX_NAME);
	const int n = GetWindowTextLengthW(hEdit);
	std::wstring buf(n + 1, L'\0');
	GetWindowTextW(hEdit, &buf[0], n + 1);
	buf.resize(n);
	UT_Win32LocaleString str;
	str.fromLocale(buf.c_str());
	return str.utf8_str().utf8_str();
}

BOOL AP_Win32Dialog_InsertXMLID::_onCommand(HWND hWnd, WPARAM wParam, LPARAM /*lParam*/)
{
	const WORD wId = LOWORD(wParam);
	switch (wId)
	{
	case IDCANCEL:
		setAnswer(a_CANCEL);
		EndDialog(hWnd, 0);
		return 1;

	case IDOK:
		{
			const std::string name = _nameText();
			if (name.empty())
				setAnswer(a_CANCEL);
			else
			{
				setString(name);
				setAnswer(a_OK);
			}
		}
		EndDialog(hWnd, 0);
		return 1;

	case AP_RID_DIALOG_INSERTXMLID_BTN_DELETE:
		setString(_nameText());
		setAnswer(a_DELETE);
		EndDialog(hWnd, 0);
		return 1;

	default:
		return 0;
	}
}

BOOL AP_Win32Dialog_InsertXMLID::_onDlgMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (msg == WM_COMMAND
		&& HIWORD(wParam) == EN_CHANGE
		&& LOWORD(wParam) == AP_RID_DIALOG_INSERTXMLID_EBX_NAME)
	{
		enableControl(AP_RID_DIALOG_INSERTXMLID_BTN_OK, !_nameText().empty());
		return 1;
	}
	return XAP_Win32DialogBase::_onDlgMessage(hWnd, msg, wParam, lParam);
}

LRESULT CALLBACK AP_Win32Dialog_InsertXMLID::s_nameProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	WNDPROC old = (WNDPROC)GetPropW(hWnd, s_oldProcProp);
	if (msg == WM_CHAR)
	{
		const wchar_t c = static_cast<wchar_t>(wParam);
		const bool ok = (c >= L'0' && c <= L'9')
			|| (c >= L'A' && c <= L'Z')
			|| (c >= L'a' && c <= L'z')
			|| c == VK_BACK;
		if (!ok)
			return 0;
	}
	if (msg == WM_NCDESTROY)
		RemovePropW(hWnd, s_oldProcProp);
	return CallWindowProcW(old, hWnd, msg, wParam, lParam);
}
