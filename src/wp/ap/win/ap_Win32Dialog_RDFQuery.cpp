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
#include <commctrl.h>

#include "ut_assert.h"
#include "ut_Win32LocaleString.h"
#include "xap_App.h"

#include "ap_Strings.h"
#include "ap_Dialog_Id.h"
#include "ap_Win32Dialog_RDFQuery.h"
#include "ap_Win32Resources.rc2"
#include "xap_Win32FrameImpl.h"

namespace {

std::string windowText(HWND hWnd)
{
	const int n = GetWindowTextLengthW(hWnd);
	std::wstring buf(n + 1, L'\0');
	GetWindowTextW(hWnd, &buf[0], n + 1);
	buf.resize(n < 0 ? 0 : n);
	UT_Win32LocaleString str;
	str.fromLocale(buf.c_str());
	return str.utf8_str().utf8_str();
}

void setItemText(HWND hList, int row, int column, const std::string& text)
{
	UT_Win32LocaleString str;
	str.fromUTF8(text.c_str());
	LVITEMW item;
	memset(&item, 0, sizeof(item));
	item.mask = LVIF_TEXT;
	item.iItem = row;
	item.iSubItem = column;
	item.pszText = const_cast<LPWSTR>(str.c_str());
	SendMessageW(hList, LVM_SETITEMW, 0, (LPARAM)&item);
}

}

XAP_Dialog * AP_Win32Dialog_RDFQuery::static_constructor(XAP_DialogFactory * pFactory,
														 XAP_Dialog_Id id)
{
	return new AP_Win32Dialog_RDFQuery(pFactory, id);
}

AP_Win32Dialog_RDFQuery::AP_Win32Dialog_RDFQuery(XAP_DialogFactory * pDlgFactory,
												 XAP_Dialog_Id id)
	: AP_Dialog_RDFQuery(pDlgFactory, id)
{
}

AP_Win32Dialog_RDFQuery::~AP_Win32Dialog_RDFQuery(void)
{
}

void AP_Win32Dialog_RDFQuery::runModeless(XAP_Frame * pFrame)
{
	UT_return_if_fail(pFrame);
	UT_return_if_fail(m_id == static_cast<XAP_Dialog_Id>(AP_DIALOG_ID_RDF_QUERY));
	createModeless(pFrame, MAKEINTRESOURCEW(AP_RID_DIALOG_RDFQUERY));
	const UT_sint32 sid = (UT_sint32)getDialogId();
	m_pApp->rememberModelessId(sid, (XAP_Dialog_Modeless *)m_pDialog);
	showWindow(SW_SHOW);
	bringWindowToTop();
	showAllRDF();
}

void AP_Win32Dialog_RDFQuery::activate(void)
{
	ConstructWindowName();
	setDialogTitle(m_WindowName.c_str());
	showWindow(SW_SHOW);
	bringWindowToTop();
}

void AP_Win32Dialog_RDFQuery::destroy(void)
{
	if (m_hDlg && IsWindow(m_hDlg))
		destroyWindow();
	modeless_cleanup();
}

void AP_Win32Dialog_RDFQuery::notifyActiveFrame(XAP_Frame * /*pFrame*/)
{
	ConstructWindowName();
	setDialogTitle(m_WindowName.c_str());
}

BOOL AP_Win32Dialog_RDFQuery::_onInitDialog(HWND hWnd, WPARAM /*wParam*/, LPARAM /*lParam*/)
{
	const XAP_StringSet * pSS = m_pApp->getStringSet();
	setDialogTitle(pSS->getValue(AP_STRING_ID_DLG_RDF_Query_Title));
	setDlgItemText(AP_RID_DIALOG_RDFQUERY_BTN_SHOWALL, pSS->getValue(AP_STRING_ID_DLG_RDF_Query_ShowAll));
	setDlgItemText(AP_RID_DIALOG_RDFQUERY_BTN_EXECUTE, pSS->getValue(AP_STRING_ID_DLG_RDF_Query_Execute));
	setDlgItemText(AP_RID_DIALOG_RDFQUERY_BTN_CLOSE, pSS->getValue(AP_STRING_ID_DLG_CloseButton));

	HWND hList = GetDlgItem(hWnd, AP_RID_DIALOG_RDFQUERY_LIST);
	ListView_SetExtendedListViewStyle(hList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
	centerDialog();
	return 1;
}

std::string AP_Win32Dialog_RDFQuery::_queryText(void) const
{
	return windowText(GetDlgItem(m_hDlg, AP_RID_DIALOG_RDFQUERY_EDIT));
}

void AP_Win32Dialog_RDFQuery::_setListColumns(const std::vector< std::string >& columns)
{
	HWND hList = GetDlgItem(m_hDlg, AP_RID_DIALOG_RDFQUERY_LIST);
	if (!hList)
		return;
	while (ListView_DeleteColumn(hList, 0))
		;
	const XAP_StringSet * pSS = m_pApp->getStringSet();
	LVCOLUMNW col;
	memset(&col, 0, sizeof(col));
	col.mask = LVCF_TEXT | LVCF_SUBITEM | LVCF_WIDTH;
	for (size_t i = 0; i < columns.size(); ++i)
	{
		const char * label = columns[i].c_str();
		if (columns[i] == "s")
			label = pSS->getValue(AP_STRING_ID_DLG_RDF_Query_Column_Subject);
		else if (columns[i] == "p")
			label = pSS->getValue(AP_STRING_ID_DLG_RDF_Query_Column_Predicate);
		else if (columns[i] == "o")
			label = pSS->getValue(AP_STRING_ID_DLG_RDF_Query_Column_Object);
		UT_Win32LocaleString str;
		str.fromUTF8(label);
		col.iSubItem = static_cast<int>(i);
		col.cx = 160;
		col.pszText = const_cast<LPWSTR>(str.c_str());
		SendMessageW(hList, LVM_INSERTCOLUMNW, i, (LPARAM)&col);
	}
}

void AP_Win32Dialog_RDFQuery::clear()
{
	AP_Dialog_RDFQuery::clear();
	HWND hList = GetDlgItem(m_hDlg, AP_RID_DIALOG_RDFQUERY_LIST);
	if (hList)
		ListView_DeleteAllItems(hList);
}

void AP_Win32Dialog_RDFQuery::setupBindingsView(std::map< std::string, std::string >& b)
{
	m_columns.clear();
	const char * preferred[] = { "s", "p", "o", "rdflink" };
	for (size_t i = 0; i < sizeof(preferred) / sizeof(preferred[0]); ++i)
	{
		if (b.find(preferred[i]) != b.end())
			m_columns.push_back(preferred[i]);
	}
	for (std::map< std::string, std::string >::const_iterator it = b.begin(); it != b.end(); ++it)
	{
		bool known = false;
		for (size_t i = 0; i < m_columns.size(); ++i)
		{
			if (m_columns[i] == it->first)
				known = true;
		}
		if (!known)
			m_columns.push_back(it->first);
	}
	_setListColumns(m_columns);
}

void AP_Win32Dialog_RDFQuery::addBinding(std::map< std::string, std::string >& b)
{
	AP_Dialog_RDFQuery::addBinding(b);
	HWND hList = GetDlgItem(m_hDlg, AP_RID_DIALOG_RDFQUERY_LIST);
	if (!hList || m_columns.empty())
		return;

	LVITEMW item;
	memset(&item, 0, sizeof(item));
	item.mask = LVIF_TEXT;
	item.iItem = ListView_GetItemCount(hList);
	item.pszText = const_cast<LPWSTR>(L"");
	const int row = static_cast<int>(SendMessageW(hList, LVM_INSERTITEMW, 0, (LPARAM)&item));
	for (size_t i = 0; i < m_columns.size(); ++i)
	{
		std::map< std::string, std::string >::const_iterator found = b.find(m_columns[i]);
		const std::string text = (found == b.end()) ? std::string() : found->second;
		setItemText(hList, row, static_cast<int>(i), text);
	}
}

void AP_Win32Dialog_RDFQuery::setStatus(const std::string& msg)
{
	setDlgItemText(AP_RID_DIALOG_RDFQUERY_STATUS, msg.c_str());
}

void AP_Win32Dialog_RDFQuery::setQueryString(const std::string& sparql)
{
	setDlgItemText(AP_RID_DIALOG_RDFQUERY_EDIT, sparql.c_str());
}

BOOL AP_Win32Dialog_RDFQuery::_onCommand(HWND /*hWnd*/, WPARAM wParam, LPARAM /*lParam*/)
{
	switch (LOWORD(wParam))
	{
	case IDCANCEL:
		destroy();
		return 1;
	case AP_RID_DIALOG_RDFQUERY_BTN_SHOWALL:
		showAllRDF();
		return 1;
	case AP_RID_DIALOG_RDFQUERY_BTN_EXECUTE:
		executeQuery(_queryText());
		return 1;
	default:
		return 0;
	}
}

BOOL AP_Win32Dialog_RDFQuery::_onDlgMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (msg == WM_CLOSE)
	{
		destroy();
		return 1;
	}
	return XAP_Win32DialogBase::_onDlgMessage(hWnd, msg, wParam, lParam);
}
