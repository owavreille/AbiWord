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
#include "ap_Win32Dialog_RDFEditor.h"
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

XAP_Dialog * AP_Win32Dialog_RDFEditor::static_constructor(XAP_DialogFactory * pFactory,
														  XAP_Dialog_Id id)
{
	return new AP_Win32Dialog_RDFEditor(pFactory, id);
}

AP_Win32Dialog_RDFEditor::AP_Win32Dialog_RDFEditor(XAP_DialogFactory * pDlgFactory,
												   XAP_Dialog_Id id)
	: AP_Dialog_RDFEditor(pDlgFactory, id)
{
}

AP_Win32Dialog_RDFEditor::~AP_Win32Dialog_RDFEditor(void)
{
}

void AP_Win32Dialog_RDFEditor::runModeless(XAP_Frame * pFrame)
{
	UT_return_if_fail(pFrame);
	UT_return_if_fail(m_id == static_cast<XAP_Dialog_Id>(AP_DIALOG_ID_RDF_EDITOR));
	createModeless(pFrame, MAKEINTRESOURCEW(AP_RID_DIALOG_RDFEDITOR));
	const UT_sint32 sid = (UT_sint32)getDialogId();
	m_pApp->rememberModelessId(sid, (XAP_Dialog_Modeless *)m_pDialog);
	showWindow(SW_SHOW);
	bringWindowToTop();
	showAllRDF();
}

void AP_Win32Dialog_RDFEditor::activate(void)
{
	ConstructWindowName();
	setDialogTitle(m_WindowName.c_str());
	showWindow(SW_SHOW);
	bringWindowToTop();
}

void AP_Win32Dialog_RDFEditor::destroy(void)
{
	if (m_hDlg && IsWindow(m_hDlg))
		destroyWindow();
	modeless_cleanup();
}

void AP_Win32Dialog_RDFEditor::notifyActiveFrame(XAP_Frame * /*pFrame*/)
{
	ConstructWindowName();
	setDialogTitle(m_WindowName.c_str());
}

BOOL AP_Win32Dialog_RDFEditor::_onInitDialog(HWND hWnd, WPARAM /*wParam*/, LPARAM /*lParam*/)
{
	const XAP_StringSet * pSS = m_pApp->getStringSet();
	setDialogTitle(pSS->getValue(AP_STRING_ID_DLG_RDF_Editor_Title));
	setDlgItemText(AP_RID_DIALOG_RDFEDITOR_LBL_LINK, pSS->getValue(AP_STRING_ID_DLG_RDF_Editor_Restrict));
	setDlgItemText(AP_RID_DIALOG_RDFEDITOR_LBL_SUBJ, pSS->getValue(AP_STRING_ID_DLG_RDF_Query_Column_Subject));
	setDlgItemText(AP_RID_DIALOG_RDFEDITOR_LBL_PRED, pSS->getValue(AP_STRING_ID_DLG_RDF_Query_Column_Predicate));
	setDlgItemText(AP_RID_DIALOG_RDFEDITOR_LBL_OBJ, pSS->getValue(AP_STRING_ID_DLG_RDF_Query_Column_Object));
	setDlgItemText(AP_RID_DIALOG_RDFEDITOR_BTN_NEW, pSS->getValue(AP_STRING_ID_TOOLBAR_LABEL_FILE_NEW));
	setDlgItemText(AP_RID_DIALOG_RDFEDITOR_BTN_COPY, pSS->getValue(AP_STRING_ID_MENU_LABEL_EDIT_COPY));
	setDlgItemText(AP_RID_DIALOG_RDFEDITOR_BTN_DELETE, pSS->getValue(XAP_STRING_ID_DLG_Delete));
	setDlgItemText(AP_RID_DIALOG_RDFEDITOR_BTN_APPLY, pSS->getValue(XAP_STRING_ID_DLG_Apply));
	setDlgItemText(AP_RID_DIALOG_RDFEDITOR_BTN_UPDATE, pSS->getValue(AP_STRING_ID_DLG_RDF_Editor_ShowAll));
	setDlgItemText(AP_RID_DIALOG_RDFEDITOR_BTN_CLOSE, pSS->getValue(AP_STRING_ID_DLG_CloseButton));

	HWND hList = GetDlgItem(hWnd, AP_RID_DIALOG_RDFEDITOR_LIST);
	ListView_SetExtendedListViewStyle(hList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
	const char * labels[3] = {
		pSS->getValue(AP_STRING_ID_DLG_RDF_Query_Column_Subject),
		pSS->getValue(AP_STRING_ID_DLG_RDF_Query_Column_Predicate),
		pSS->getValue(AP_STRING_ID_DLG_RDF_Query_Column_Object)
	};
	LVCOLUMNW col;
	memset(&col, 0, sizeof(col));
	col.mask = LVCF_TEXT | LVCF_SUBITEM | LVCF_WIDTH;
	for (int i = 0; i < 3; ++i)
	{
		UT_Win32LocaleString str;
		str.fromUTF8(labels[i]);
		col.iSubItem = i;
		col.cx = 180;
		col.pszText = const_cast<LPWSTR>(str.c_str());
		SendMessageW(hList, LVM_INSERTCOLUMNW, i, (LPARAM)&col);
	}

	if (m_hideRestrictionXMLID)
	{
		showControl(AP_RID_DIALOG_RDFEDITOR_LBL_LINK, SW_HIDE);
		showControl(AP_RID_DIALOG_RDFEDITOR_CBX_LINK, SW_HIDE);
	}
	else
		_fillLinkCombo();

	centerDialog();
	return 1;
}

void AP_Win32Dialog_RDFEditor::_fillLinkCombo(void)
{
	resetComboContent(AP_RID_DIALOG_RDFEDITOR_CBX_LINK);
	addItemToCombo(AP_RID_DIALOG_RDFEDITOR_CBX_LINK, "");
	PD_DocumentRDFHandle rdf = getRDF();
	if (!rdf)
		return;
	std::set< std::string > ids;
	rdf->getAllIDs(ids);
	for (std::set< std::string >::const_iterator it = ids.begin(); it != ids.end(); ++it)
		addItemToCombo(AP_RID_DIALOG_RDFEDITOR_CBX_LINK, it->c_str());
}

void AP_Win32Dialog_RDFEditor::clear()
{
	AP_Dialog_RDFEditor::clear();
	m_rows.clear();
	HWND hList = GetDlgItem(m_hDlg, AP_RID_DIALOG_RDFEDITOR_LIST);
	if (hList)
		ListView_DeleteAllItems(hList);
}

void AP_Win32Dialog_RDFEditor::addStatement(const PD_RDFStatement& st)
{
	AP_Dialog_RDFEditor::addStatement(st);
	m_rows.push_back(st);
	HWND hList = GetDlgItem(m_hDlg, AP_RID_DIALOG_RDFEDITOR_LIST);
	if (!hList)
		return;
	const PD_RDFStatement shown = st.uriToPrefixed(getModel());
	LVITEMW item;
	memset(&item, 0, sizeof(item));
	item.mask = LVIF_TEXT;
	item.iItem = ListView_GetItemCount(hList);
	item.pszText = const_cast<LPWSTR>(L"");
	const int row = static_cast<int>(SendMessageW(hList, LVM_INSERTITEMW, 0, (LPARAM)&item));
	setItemText(hList, row, 0, shown.getSubject().toString());
	setItemText(hList, row, 1, shown.getPredicate().toString());
	setItemText(hList, row, 2, shown.getObject().toString());
}

void AP_Win32Dialog_RDFEditor::setStatus(const std::string& msg)
{
	if (m_hDlg)
		setDlgItemText(AP_RID_DIALOG_RDFEDITOR_STATUS, msg.c_str());
}

void AP_Win32Dialog_RDFEditor::removeStatement(const PD_RDFStatement& st)
{
	HWND hList = GetDlgItem(m_hDlg, AP_RID_DIALOG_RDFEDITOR_LIST);
	for (size_t i = 0; i < m_rows.size(); ++i)
	{
		if (m_rows[i] == st)
		{
			m_rows.erase(m_rows.begin() + static_cast<std::vector< PD_RDFStatement >::difference_type>(i));
			if (hList)
				ListView_DeleteItem(hList, static_cast<int>(i));
			return;
		}
	}
}

int AP_Win32Dialog_RDFEditor::_selectedIndex(void) const
{
	HWND hList = GetDlgItem(m_hDlg, AP_RID_DIALOG_RDFEDITOR_LIST);
	if (!hList)
		return -1;
	return ListView_GetNextItem(hList, -1, LVNI_SELECTED);
}

std::list< PD_RDFStatement > AP_Win32Dialog_RDFEditor::getSelection()
{
	std::list< PD_RDFStatement > ret;
	const int index = _selectedIndex();
	if (index >= 0 && static_cast<size_t>(index) < m_rows.size())
		ret.push_back(m_rows[static_cast<size_t>(index)]);
	return ret;
}

void AP_Win32Dialog_RDFEditor::setSelection(const std::list< PD_RDFStatement >& l)
{
	if (l.empty())
		return;
	HWND hList = GetDlgItem(m_hDlg, AP_RID_DIALOG_RDFEDITOR_LIST);
	if (!hList)
		return;
	for (size_t i = 0; i < m_rows.size(); ++i)
	{
		if (m_rows[i] == l.front())
		{
			ListView_SetItemState(hList, static_cast<int>(i), LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
			_fillEdits(m_rows[i]);
			return;
		}
	}
}

void AP_Win32Dialog_RDFEditor::hideRestrictionXMLID(bool v)
{
	AP_Dialog_RDFEditor::hideRestrictionXMLID(v);
	if (!m_hDlg)
		return;
	showControl(AP_RID_DIALOG_RDFEDITOR_LBL_LINK, v ? SW_HIDE : SW_SHOW);
	showControl(AP_RID_DIALOG_RDFEDITOR_CBX_LINK, v ? SW_HIDE : SW_SHOW);
	if (!v)
		_fillLinkCombo();
}

std::string AP_Win32Dialog_RDFEditor::_editText(int controlId) const
{
	return windowText(GetDlgItem(m_hDlg, controlId));
}

void AP_Win32Dialog_RDFEditor::_fillEdits(const PD_RDFStatement& st)
{
	const PD_RDFStatement shown = st.uriToPrefixed(getModel());
	setDlgItemText(AP_RID_DIALOG_RDFEDITOR_EDIT_SUBJ, shown.getSubject().toString().c_str());
	setDlgItemText(AP_RID_DIALOG_RDFEDITOR_EDIT_PRED, shown.getPredicate().toString().c_str());
	setDlgItemText(AP_RID_DIALOG_RDFEDITOR_EDIT_OBJ, shown.getObject().toString().c_str());
}

void AP_Win32Dialog_RDFEditor::_applyEdits(void)
{
	const std::list< PD_RDFStatement > sel = getSelection();
	if (sel.empty())
		return;
	const PD_RDFStatement oldst = sel.front();
	const PD_RDFStatement typed(_editText(AP_RID_DIALOG_RDFEDITOR_EDIT_SUBJ),
								_editText(AP_RID_DIALOG_RDFEDITOR_EDIT_PRED),
								PD_Literal(_editText(AP_RID_DIALOG_RDFEDITOR_EDIT_OBJ)));
	const PD_RDFStatement newst = typed.prefixedToURI(getModel());
	PD_DocumentRDFMutationHandle mutation = getModel()->createMutation();
	if (mutation->add(newst))
	{
		mutation->remove(oldst);
		mutation->commit();
		showAllRDF();
		setSelection(std::list< PD_RDFStatement >(1, newst));
	}
}

void AP_Win32Dialog_RDFEditor::_deleteSelection(void)
{
	const std::list< PD_RDFStatement > sel = getSelection();
	if (sel.empty())
		return;
	PD_DocumentRDFMutationHandle mutation = getModel()->createMutation();
	for (std::list< PD_RDFStatement >::const_iterator it = sel.begin(); it != sel.end(); ++it)
	{
		mutation->remove(*it);
		removeStatement(*it);
		if (m_count > 0)
			--m_count;
	}
	mutation->commit();
	statusIsTripleCount();
}

BOOL AP_Win32Dialog_RDFEditor::_onCommand(HWND /*hWnd*/, WPARAM wParam, LPARAM /*lParam*/)
{
	const WORD wNotify = HIWORD(wParam);
	const WORD wId = LOWORD(wParam);
	switch (wId)
	{
	case IDCANCEL:
		destroy();
		return 1;
	case AP_RID_DIALOG_RDFEDITOR_BTN_NEW:
		createStatement();
		return 1;
	case AP_RID_DIALOG_RDFEDITOR_BTN_COPY:
		copyStatement();
		return 1;
	case AP_RID_DIALOG_RDFEDITOR_BTN_DELETE:
		_deleteSelection();
		return 1;
	case AP_RID_DIALOG_RDFEDITOR_BTN_APPLY:
		_applyEdits();
		return 1;
	case AP_RID_DIALOG_RDFEDITOR_BTN_UPDATE:
		showAllRDF();
		return 1;
	case AP_RID_DIALOG_RDFEDITOR_CBX_LINK:
		if (wNotify == CBN_SELCHANGE)
		{
			UT_Win32LocaleString str;
			getDlgItemText(AP_RID_DIALOG_RDFEDITOR_CBX_LINK, str);
			setRestrictedXMLID(str.utf8_str().utf8_str());
		}
		return 1;
	default:
		return 0;
	}
}

BOOL AP_Win32Dialog_RDFEditor::_onDlgMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (msg == WM_NOTIFY)
	{
		const NMHDR * hdr = reinterpret_cast<const NMHDR *>(lParam);
		if (hdr->idFrom == AP_RID_DIALOG_RDFEDITOR_LIST && hdr->code == LVN_ITEMCHANGED)
		{
			const NMLISTVIEW * view = reinterpret_cast<const NMLISTVIEW *>(lParam);
			if ((view->uNewState & LVIS_SELECTED) && !(view->uOldState & LVIS_SELECTED))
			{
				if (view->iItem >= 0 && static_cast<size_t>(view->iItem) < m_rows.size())
					_fillEdits(m_rows[static_cast<size_t>(view->iItem)]);
			}
			return 1;
		}
	}
	if (msg == WM_CLOSE)
	{
		destroy();
		return 1;
	}
	return XAP_Win32DialogBase::_onDlgMessage(hWnd, msg, wParam, lParam);
}
