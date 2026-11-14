/* AbiWord
 * Copyright (C) 2000 AbiSource, Inc.
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

#include "ut_string.h"
#include "ut_types.h"
#include "ut_assert.h"
#include "ut_debugmsg.h"

#include "xap_App.h"
#include "xap_Win32App.h"
#include "xap_Win32FrameImpl.h"

#include "ap_Strings.h"
#include "ap_Dialog_Id.h"
#include "ap_Dialog_Goto.h"
#include "ap_Win32Dialog_Goto.h"
#include "ap_Win32Resources.rc2"
#include "fv_View.h"
#include "fl_DocLayout.h"
#include "fl_FootnoteLayout.h"
#include "ap_Win32App.h"

#include <set>

/*****************************************************************/

XAP_Dialog * AP_Win32Dialog_Goto::static_constructor(XAP_DialogFactory * pFactory, XAP_Dialog_Id id)
{
	AP_Win32Dialog_Goto * p = new AP_Win32Dialog_Goto(pFactory,id);
	return p;
}

AP_Win32Dialog_Goto::AP_Win32Dialog_Goto(XAP_DialogFactory * pDlgFactory, XAP_Dialog_Id id) : AP_Dialog_Goto(pDlgFactory,id)
{
}

AP_Win32Dialog_Goto::~AP_Win32Dialog_Goto(void)
{
}


static void s_takeBookmark(HWND list, int index, UT_Win32LocaleString & dest)
{
	dest.clear();
	if (!list || index < 0)
		return;

	const LRESULT len = SendMessageW(list, LB_GETTEXTLEN, (WPARAM)index, 0);
	if (len < 0 || len > 8 * 1024 * 1024)
		return;

	wchar_t * value = static_cast<wchar_t *>(UT_calloc(static_cast<UT_uint32>(len) + 1, sizeof(wchar_t)));
	if (!value)
		return;
	if (SendMessageW(list, LB_GETTEXT, (WPARAM)index, (LPARAM)value) != LB_ERR)
		dest.fromLocale(value);
	FREEP(value);
}

static void s_showBookmark(HWND dialog, const UT_Win32LocaleString & dest)
{
	if (dest.empty())
		return;
	SetWindowTextW(GetDlgItem(dialog, AP_RID_DIALOG_GOTO_EDIT_NUMBER), dest.c_str());
	EnableWindow(GetDlgItem(dialog, AP_RID_DIALOG_GOTO_BTN_GOTO), TRUE);
}

static bool s_targetUsesList(int row)
{
	return row == static_cast<int>(AP_JUMPTARGET_BOOKMARK)
		|| row == static_cast<int>(AP_JUMPTARGET_XMLID)
		|| row == static_cast<int>(AP_JUMPTARGET_ANNOTATION);
}

static void s_fillJumpList(AP_Win32Dialog_Goto * dlg, HWND list, int row)
{
	if (!list)
		return;
	SendMessageW(list, LB_RESETCONTENT, 0, 0);

	UT_Win32LocaleString str;
	if (row == static_cast<int>(AP_JUMPTARGET_BOOKMARK))
	{
		const UT_sint32 count = dlg->getExistingBookmarksCount();
		for (UT_sint32 i = 0; i < count; ++i)
		{
			str.fromUTF8(dlg->getNthExistingBookmark(i).c_str());
			SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)str.c_str());
		}
	}
	else if (row == static_cast<int>(AP_JUMPTARGET_XMLID))
	{
		if (PD_DocumentRDFHandle rdf = dlg->getRDF())
		{
			std::set<std::string> xmlids;
			rdf->getAllIDs(xmlids);
			for (const std::string & id : xmlids)
			{
				str.fromUTF8(id.c_str());
				SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)str.c_str());
			}
		}
	}
	else if (row == static_cast<int>(AP_JUMPTARGET_ANNOTATION))
	{
		FV_View * view = dlg->getView();
		FL_DocLayout * layout = view ? view->getLayout() : nullptr;
		if (!view || !layout)
			return;
		const UT_uint32 count = view->countAnnotations();
		for (UT_uint32 i = 0; i < count; ++i)
		{
			fl_AnnotationLayout * ann = layout->getNthAnnotation(static_cast<UT_sint32>(i));
			if (!ann)
				continue;
			const UT_uint32 pid = ann->getAnnotationPID();
			std::string label = std::to_string(pid);
			const char * title = ann->getTitle();
			const char * author = ann->getAuthor();
			if (title && title[0])
			{
				label += " - ";
				label += title;
			}
			if (author && author[0])
			{
				label += (title && title[0]) ? ", " : " - ";
				label += author;
			}
			str.fromUTF8(label.c_str());
			const LRESULT index = SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)str.c_str());
			if (index != LB_ERR)
				SendMessageW(list, LB_SETITEMDATA, (WPARAM)index, (LPARAM)pid);
		}
	}
}

static void s_takeJumpValue(HWND list, int index, int row, UT_Win32LocaleString & dest)
{
	dest.clear();
	if (!list || index < 0)
		return;
	if (row == static_cast<int>(AP_JUMPTARGET_ANNOTATION))
	{
		const LRESULT pid = SendMessageW(list, LB_GETITEMDATA, (WPARAM)index, 0);
		if (pid == LB_ERR || pid < 0)
			return;
		wchar_t buf[16];
		swprintf(buf, L"%u", static_cast<unsigned>(pid));
		dest.fromLocale(buf);
		return;
	}
	s_takeBookmark(list, index, dest);
}

static bool s_stepList(HWND list, bool forward, int & index)
{
	const int count = static_cast<int>(SendMessageW(list, LB_GETCOUNT, 0, 0));
	if (count <= 0)
		return false;
	index = static_cast<int>(SendMessageW(list, LB_GETCURSEL, 0, 0));
	if (forward)
	{
		if (index < 0)
			index = 0;
		else if (++index >= count)
			index = 0;
	}
	else
	{
		index--;
		if (index < 0)
			index = count - 1;
	}
	SendMessageW(list, LB_SETCURSEL, (WPARAM)index, 0);
	return true;
}

void AP_Win32Dialog_Goto::activate(void)
{
	ConstructWindowName();
	setDialogTitle (m_WindowName);

	SetFocus( GetDlgItem( m_hWnd,AP_RID_DIALOG_GOTO_EDIT_NUMBER ) );

	showWindow(SW_SHOW);
	bringWindowToTop();
}


void AP_Win32Dialog_Goto::destroy(void)
{
    destroyWindow();
	modeless_cleanup();
}

void AP_Win32Dialog_Goto::notifyActiveFrame(XAP_Frame *pFrame)
{
	if((HWND)GetWindowLongPtrW(m_hWnd, GWLP_HWNDPARENT) != static_cast<XAP_Win32FrameImpl*>(pFrame->getFrameImpl())->getTopLevelWindow())
	{
		// Update the caption
		ConstructWindowName();
        setDialogTitle(m_WindowName);
		SetWindowLongPtrW(m_hWnd, GWLP_HWNDPARENT, (LONG_PTR)static_cast<XAP_Win32FrameImpl*>(pFrame->getFrameImpl())->getTopLevelWindow());
		SetWindowPos(m_hWnd, nullptr, 0, 0, 0, 0,
						SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
	}
}

void AP_Win32Dialog_Goto::notifyCloseFrame(XAP_Frame *pFrame)
{
	if((HWND)GetWindowLongPtrW(m_hWnd, GWLP_HWNDPARENT) == static_cast<XAP_Win32FrameImpl*>(pFrame->getFrameImpl())->getTopLevelWindow())
	{
		SetWindowLongPtrW(m_hWnd, GWLP_HWNDPARENT, 0);
		SetWindowPos(m_hWnd, nullptr, 0, 0, 0, 0,
						SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
	}
}

void AP_Win32Dialog_Goto::runModeless(XAP_Frame * pFrame)
{
	UT_return_if_fail (pFrame && m_id == static_cast<XAP_Dialog_Id>(AP_DIALOG_ID_GOTO));
    createModeless (pFrame, MAKEINTRESOURCEW(AP_RID_DIALOG_GOTO));

	// Save dialog the ID number and pointer to the widget
	UT_sint32 sid =(UT_sint32)	getDialogId();
	m_pApp->rememberModelessId( sid, (XAP_Dialog_Modeless *) m_pDialog);
 	m_pView->focusChange(AV_FOCUS_MODELESS);
}

void AP_Win32Dialog_Goto::GoTo (const char *number)
{
	UT_UCS4Char *ucsnumber = (UT_UCS4Char *) g_try_malloc (sizeof (UT_UCS4Char) * (strlen(number) + 1));
	UT_return_if_fail(ucsnumber);
	UT_UCS4_strcpy_utf8_char(ucsnumber, number);
	int target = this->getSelectedRow ();
	this->getView()->gotoTarget ((AP_JumpTarget) target, ucsnumber);
	g_free (ucsnumber);
}

void AP_Win32Dialog_Goto::setSelectedRow (int row)
{
	m_iRow = row;
}

int AP_Win32Dialog_Goto::getSelectedRow (void)
{
	return (m_iRow);
}

#define _DSI(c,i)	SetDlgItemInt(m_hDlg,AP_RID_DIALOG_##c,m_count.##i,FALSE)
#define _DS(c,s)	setDlgItemText(AP_RID_DIALOG_##c,pSS->getValue(AP_STRING_ID_##s))
#define _DSX(c,s)	setDlgItemText(AP_RID_DIALOG_##c,pSS->getValue(XAP_STRING_ID_##s))

BOOL AP_Win32Dialog_Goto::_onInitDialog(HWND hWnd, WPARAM /*wParam*/, LPARAM /*lParam*/)
{
	int iTarget;
	const char **ppszTargets;
	const XAP_StringSet * pSS = m_pApp->getStringSet();

	UT_Win32LocaleString str;

	m_oldvalue.clear();

	// Update the caption
	ConstructWindowName();
	setDialogTitle(m_WindowName); 

	// Disable the Go To button until something has been entered into the Number box
	EnableWindow( GetDlgItem(hWnd,AP_RID_DIALOG_GOTO_BTN_GOTO), FALSE );

	m_iRow = 0;
	ppszTargets = getJumpTargets();
	for ( iTarget = 0; ppszTargets[ iTarget ] != nullptr; iTarget++ ) {
		str.fromUTF8(ppszTargets[ iTarget ] );
		SendMessageW( GetDlgItem(hWnd,AP_RID_DIALOG_GOTO_LIST_WHAT), LB_ADDSTRING, 0, (LPARAM)str.c_str());
	}

	SendMessageW(GetDlgItem(hWnd,AP_RID_DIALOG_GOTO_LIST_WHAT), LB_SETCURSEL, m_iRow, 0);

	// localize controls
	_DSX(GOTO_BTN_CLOSE,		DLG_Close);
	_DS(GOTO_BTN_GOTO,			DLG_Goto_Btn_Goto);
	_DS(GOTO_BTN_PREV,			DLG_Goto_Btn_Prev);
	_DS(GOTO_BTN_NEXT,			DLG_Goto_Btn_Next);
	_DS(GOTO_TEXT_WHAT, 		DLG_Goto_Label_What);
	_DS(GOTO_TEXT_NUMBER,		DLG_Goto_Label_Number);
	_DS(GOTO_TEXT_INFO, 		DLG_Goto_Label_Help);

	ShowWindow(GetDlgItem(hWnd,AP_RID_DIALOG_GOTO_LIST_BOOKMARKS), SW_HIDE);


	SetFocus( GetDlgItem(hWnd,AP_RID_DIALOG_GOTO_EDIT_NUMBER) );
	
	centerDialog();

	return 0;							// 0 == we called SetFocus()
}

BOOL AP_Win32Dialog_Goto::_onCommand(HWND hWnd, WPARAM wParam, LPARAM lParam)
{
	wchar_t * pBuf = nullptr;
	bool bValueOK = TRUE;
	WORD wNotifyCode = HIWORD(wParam);
	WORD wId = LOWORD(wParam);
	HWND hWndCtrl = (HWND)lParam;
	DWORD dwTextLength, dwCounter, dwStart;

	// before doing anything else, make sure that the main window is not entirely
	// without focus
	m_pView->focusChange(AV_FOCUS_MODELESS);

	switch (wId)
	{
	case IDCANCEL:						// also AP_RID_DIALOG_GOTO_BTN_CLOSE
		destroy();
		return 1;

	case AP_RID_DIALOG_GOTO_LIST_WHAT:
		switch( wNotifyCode )
		{
		case LBN_SELCHANGE:
			m_iRow = (short) SendMessageW(GetDlgItem(hWnd,AP_RID_DIALOG_GOTO_LIST_WHAT), LB_GETCURSEL, 0, 0);
			m_oldvalue.clear();
			SetWindowTextW(GetDlgItem(hWnd, AP_RID_DIALOG_GOTO_EDIT_NUMBER), L"");
			EnableWindow(GetDlgItem(hWnd, AP_RID_DIALOG_GOTO_BTN_GOTO), FALSE);
			{
				HWND list = GetDlgItem(hWnd, AP_RID_DIALOG_GOTO_LIST_BOOKMARKS);
				const bool useList = s_targetUsesList(m_iRow);
				ShowWindow(GetDlgItem(hWnd, AP_RID_DIALOG_GOTO_TEXT_INFO), useList ? SW_HIDE : SW_SHOW);
				ShowWindow(list, useList ? SW_SHOW : SW_HIDE);
				const XAP_StringSet * pSS = m_pApp->getStringSet();
				const XAP_String_Id label = (m_iRow == (short) AP_JUMPTARGET_BOOKMARK
					|| m_iRow == (short) AP_JUMPTARGET_XMLID)
					? AP_STRING_ID_DLG_Goto_Label_Name
					: AP_STRING_ID_DLG_Goto_Label_Number;
				setDlgItemText(AP_RID_DIALOG_GOTO_TEXT_NUMBER, pSS->getValue(label));
				if (useList)
					s_fillJumpList(this, list, m_iRow);
			}
			return 1;

		default:
			return 0;
		}

	case AP_RID_DIALOG_GOTO_LIST_BOOKMARKS:
		switch( wNotifyCode )
		{
		case LBN_DBLCLK:
		case LBN_SELCHANGE:
			{
				int nIndex = SendMessageW( hWndCtrl, LB_GETCURSEL, 0, 0);
				if( nIndex != LB_ERR )
				{
					s_takeJumpValue(hWndCtrl, nIndex, m_iRow, m_oldvalue);
					s_showBookmark(hWnd, m_oldvalue);
					if( wNotifyCode == LBN_DBLCLK && !m_oldvalue.empty() )
						GoTo( m_oldvalue.utf8_str().utf8_str());
				}
				return 1;
			}

		default:
			return 0;
		}

	case AP_RID_DIALOG_GOTO_BTN_PREV:
		if( s_targetUsesList(m_iRow) )
		{
			HWND list = GetDlgItem(hWnd, AP_RID_DIALOG_GOTO_LIST_BOOKMARKS);
			int nIndex = 0;
			if (!s_stepList(list, false, nIndex))
				return 1;
			s_takeJumpValue(list, nIndex, m_iRow, m_oldvalue);
			s_showBookmark(hWnd, m_oldvalue);

			if (!m_oldvalue.empty ())
				GoTo(m_oldvalue.utf8_str().utf8_str());
		}
		else
		{
			GoTo("-1");
		}
		return 1;

	case AP_RID_DIALOG_GOTO_BTN_NEXT:
		if( s_targetUsesList(m_iRow) )
		{
			HWND list = GetDlgItem(hWnd, AP_RID_DIALOG_GOTO_LIST_BOOKMARKS);
			int nIndex = 0;
			if (!s_stepList(list, true, nIndex))
				return 1;
			s_takeJumpValue(list, nIndex, m_iRow, m_oldvalue);
			s_showBookmark(hWnd, m_oldvalue);

			if (!m_oldvalue.empty ())
				GoTo( m_oldvalue.utf8_str().utf8_str());	
		}
		else
		{		
			GoTo("+1");
		}
		return 1;

	case AP_RID_DIALOG_GOTO_BTN_GOTO:
		if (!m_oldvalue.empty())		
			GoTo(m_oldvalue.utf8_str().utf8_str());
		return 1;

	case AP_RID_DIALOG_GOTO_EDIT_NUMBER:
		switch (wNotifyCode)
		{
		case EN_UPDATE:
			dwTextLength = GetWindowTextLengthW( GetDlgItem(hWnd,AP_RID_DIALOG_GOTO_EDIT_NUMBER) );
			if( m_iRow != (short) AP_JUMPTARGET_BOOKMARK
				&& m_iRow != (short) AP_JUMPTARGET_XMLID )
			{
				if( dwTextLength )
				{
					pBuf = new wchar_t [ dwTextLength + 1 ];
					if( !pBuf )
						return 0;
				
					GetWindowTextW( hWndCtrl, pBuf, dwTextLength + 1 );

					// If the first character is + or -, skip over it in the
					// check loop below
					if( *pBuf == '-' || *pBuf == '+' )
						dwStart = 1;
					else
						dwStart = 0;

					// Make sure everything we have is numeric
					for( dwCounter = dwStart; dwCounter < dwTextLength; dwCounter++ )
					{
						if( !UT_UCS4_isdigit( pBuf[ dwCounter ] ) )
						{
							if (!m_oldvalue.empty ())
								m_oldvalue.fromLocale(L"0");
 							
							SetWindowTextW( hWndCtrl, m_oldvalue.c_str());
							
							bValueOK = FALSE;

							break;
						}
					}
	
					if( bValueOK )
					{
						m_oldvalue.fromLocale (pBuf);
	
						// Only enable the goto button if what we have actually contains a number
						EnableWindow( GetDlgItem(hWnd,AP_RID_DIALOG_GOTO_BTN_GOTO), !(((pBuf[ 0 ] == '-') || (pBuf[ 0 ] == '+')) && (pBuf[ 1 ] == '\0')) );
					}
					else
					{
						FREEP( pBuf );
		
						EnableWindow( GetDlgItem(hWnd,AP_RID_DIALOG_GOTO_BTN_GOTO), FALSE );
					}
				}
				else
				{
					m_oldvalue.clear();
					EnableWindow( GetDlgItem(hWnd,AP_RID_DIALOG_GOTO_BTN_GOTO), FALSE );
				}
			}
			else
			{

				if( dwTextLength )
				{
					EnableWindow( GetDlgItem(hWnd,AP_RID_DIALOG_GOTO_BTN_GOTO), TRUE ); 						
					wchar_t* value = new wchar_t [dwTextLength+1];
					GetWindowTextW( hWndCtrl, value, dwTextLength + 1 );
					m_oldvalue.fromLocale (value);
					delete value;
				}
				else
				{
					EnableWindow( GetDlgItem(hWnd,AP_RID_DIALOG_GOTO_BTN_GOTO), FALSE );
				}
			}
			return 1;

		case EN_SETFOCUS:
			SendMessageW( GetDlgItem(hWnd, AP_RID_DIALOG_GOTO_LIST_BOOKMARKS), LB_SETCURSEL, (WPARAM) -1, 0);
			return 1;

		default:
			return 0;
		}

	default:							// we did not handle this notification
		UT_DEBUGMSG(("WM_Command for id %ld\n",wId));
		return 0;						// return zero to let windows take care of it.
	}
}

