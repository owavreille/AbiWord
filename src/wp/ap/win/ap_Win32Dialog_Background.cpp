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
#include <string.h>

#include "ut_string.h"
#include "ut_assert.h"
#include "ut_debugmsg.h"

#include "xap_App.h"
#include "xap_Win32App.h"
#include "xap_Win32FrameImpl.h"

#include "ap_Strings.h"
#include "ap_Dialog_Id.h"
#include "ap_Dialog_Background.h"
#include "ap_Win32Dialog_Background.h"
#include "xap_Win32DialogHelper.h"
#include "ap_Win32Resources.rc2"

/*****************************************************************/

XAP_Dialog * AP_Win32Dialog_Background::static_constructor(XAP_DialogFactory * pFactory,
													 XAP_Dialog_Id id)
{
	AP_Win32Dialog_Background * p = new AP_Win32Dialog_Background(pFactory,id);
	return p;
}

AP_Win32Dialog_Background::AP_Win32Dialog_Background(XAP_DialogFactory * pDlgFactory,
										 XAP_Dialog_Id id)
	: AP_Dialog_Background(pDlgFactory,id)
{
}

AP_Win32Dialog_Background::~AP_Win32Dialog_Background(void)
{
}

UINT_PTR CALLBACK AP_Win32Dialog_Background::s_hookProc(HWND hdlg,UINT uiMsg,WPARAM /*wParam*/,LPARAM lParam)
{
	AP_Win32Dialog_Background * pThis = nullptr;
	if (uiMsg==WM_INITDIALOG)
	{
		CHOOSECOLORW * pCC = nullptr;
		pCC = (CHOOSECOLORW *) lParam;
		pThis = (AP_Win32Dialog_Background *)pCC->lCustData;
		SetWindowLongPtrW(hdlg, DWLP_USER, (LONG_PTR) pThis);
		pThis->m_hDlg = hdlg;
		pThis->_centerDialog();
		return 1;
	}
	else
	{
		pThis = (AP_Win32Dialog_Background *)GetWindowLongPtrW(hdlg, DWLP_USER);
	}

	if (uiMsg == WM_HELP && pThis)
	{
		pThis->_callHelp();
		return 1;
	}
	
	return 0;
}

void AP_Win32Dialog_Background::runModal(XAP_Frame * pFrame)
{
	UT_return_if_fail (pFrame);

	const gchar *  pszC = getColor();
	UT_RGBColor rgbColor(255,255,255);
	if(strcmp(pszC,"transparent") != 0)
	{
		UT_parseColor(pszC,rgbColor);
	}


	CHOOSECOLORW cc;                 // common dialog box structure 
	static COLORREF acrCustClr[16]; // array of custom colors 
	DWORD rgbCurrent;				// initial color selection

	rgbCurrent = RGB( rgbColor.m_red, rgbColor.m_grn, rgbColor.m_blu );

	// Initialize CHOOSECOLOR 
	ZeroMemory(&cc, sizeof(CHOOSECOLORW));
	cc.lStructSize = sizeof(CHOOSECOLORW);
	cc.hwndOwner = static_cast<XAP_Win32FrameImpl*>(pFrame->getFrameImpl())->getTopLevelWindow();
	cc.lpCustColors = (LPDWORD) acrCustClr;
	cc.rgbResult = rgbCurrent;
	cc.Flags = CC_RGBINIT |CC_ENABLEHOOK;
	cc.lpfnHook  = AP_Win32Dialog_Background::s_hookProc;
	cc.lCustData = (LPARAM)this;
 
	if( ChooseColorW(&cc) )
	{
		rgbCurrent = cc.rgbResult;

		UT_setColor( rgbColor, GetRValue(rgbCurrent), GetGValue(rgbCurrent), GetBValue(rgbCurrent) );
		setColor( rgbColor );

		setAnswer( a_OK );
	}
	else
		setAnswer( a_CANCEL );
}

void AP_Win32Dialog_Background::_centerDialog()
{
	UT_return_if_fail (IsWindow(m_hDlg));
	
	RECT rc, rcParent;
	memset(&rc, 0, sizeof(rc));
	memset(&rcParent, 0, sizeof(rcParent));
	if (!GetWindowRect(m_hDlg, &rc))
		return;

	HWND parent = GetParent(m_hDlg);
	if (!parent)
	{
		if (!GetWindowRect(GetDesktopWindow(), &rcParent))
			return;
	}
	else if (!GetClientRect(parent, &rcParent))
		return;

	const int nWidth = rc.right - rc.left;
	const int nHeight = rc.bottom - rc.top;
	if (nWidth <= 0 || nHeight <= 0)
		return;

	POINT pt;
	pt.x = (rcParent.right - rcParent.left) / 2;
	pt.y = (rcParent.bottom - rcParent.top) / 2;

	if (!parent)
	{
		if (!ClientToScreen(GetDesktopWindow(), &pt))
			return;
	}
	else if (!ClientToScreen(parent, &pt))
		return;

	pt.x -= nWidth / 2;
	pt.y -= nHeight / 2;

	MoveWindow(m_hDlg, pt.x, pt.y, nWidth, nHeight, TRUE);		
}

extern bool helpLocalizeAndOpenURL(const char* pathBeforeLang, const char* pathAfterLang, const char *remoteURLbase);

void AP_Win32Dialog_Background::_callHelp()
{
	if ( getHelpUrl().size () > 0 )
    {
		helpLocalizeAndOpenURL ("help", getHelpUrl().c_str(), "http://www.abisource.com/help/");
    }
	else
    {
		// TODO: warn no help on this topic
		// UT_DEBUGMSG(("NO HELP FOR THIS TOPIC!!\n"));
    }
	return;
}

