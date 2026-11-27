/* -*- mode: C++; tab-width: 4; c-basic-offset: 4; -*- */

/* AbiSource Application Framework
 * Copyright (C) 1998-2000 AbiSource, Inc.
 * Copyright (C) 2002
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "xap_Win32FrameImpl.h"
#include "commctrl.h"
#include <stdio.h>
#include <limits.h>					/* for INT_MAX */

#include "zmouse.h"
#ifdef __MINGW32__
#include "winezmouse.h"
#endif

#ifndef WM_MOUSEHWHEEL
#define WM_MOUSEHWHEEL 0x020E
#endif

#include "ut_debugmsg.h"
#include "ut_path.h"
#include "ut_Win32OS.h"
#include "ut_unicode.h"
#include "ut_Win32LocaleString.h"
#include "ap_Win32StatusBar.h"
#include "xap_Win32DialogBase.h"
#include "xap_ViewListener.h"
#include "ev_EditMethod.h"
#include "xav_View.h"
#include "xap_Prefs.h"
#include "ie_impGraphic.h"
#include "ie_exp.h"
#include "fv_View.h"
#include "ap_Win32App.h"
#include "fg_Graphic.h"
#include "gr_Win32Graphics.h"


#ifdef _MSC_VER
#pragma warning(disable: 4355)	// 'this' used in base member initializer list
#endif

#ifdef STRICT   
#define WHICHPROC	WNDPROC
#else   
#define WHICHPROC	FARPROC
#endif

/* Defines not present in old header files*/

#ifndef TBN_FIRST
#define TBN_FIRST               (0U-700U)       // toolbar
#endif

#ifndef TBN_DROPDOWN
#define TBN_DROPDOWN            (TBN_FIRST - 10)
#endif

#ifndef TBDDRET_DEFAULT
#define TBDDRET_DEFAULT 0 
#endif

#ifndef WM_UAHDRAWMENU
#define WM_UAHDRAWMENU 0x0091
#endif

#ifndef WM_UAHDRAWMENUITEM
#define WM_UAHDRAWMENUITEM 0x0092
#endif

#ifndef ODS_HOTLIGHT
#define ODS_HOTLIGHT 0x0040
#endif

#ifndef ODS_INACTIVE
#define ODS_INACTIVE 0x0080
#endif

#ifndef ODS_NOACCEL
#define ODS_NOACCEL 0x0100
#endif

#ifndef DT_HIDEPREFIX
#define DT_HIDEPREFIX 0x00100000
#endif

#ifndef OBJID_MENU
#define OBJID_MENU ((LONG)0xFFFFFFFD)
#endif

struct AbiUahMenu
{
	HMENU hmenu;
	HDC hdc;
	DWORD dwFlags;
};

union AbiUahMenuItemMetrics
{
	struct { DWORD cx; DWORD cy; } rgsizeBar[2];
	struct { DWORD cx; DWORD cy; } rgsizePopup[4];
};

struct AbiUahMenuPopupMetrics
{
	DWORD rgcx[4];
	DWORD fUpdateMaxWidths : 2;
};

struct AbiUahMenuItem
{
	int iPosition;
	AbiUahMenuItemMetrics umim;
	AbiUahMenuPopupMetrics umpm;
};

struct AbiUahDrawMenuItem
{
	DRAWITEMSTRUCT dis;
	AbiUahMenu um;
	AbiUahMenuItem umi;
};

static HFONT s_menuBarFont(int dpi)
{
	if (dpi < 96)
		dpi = 96;
	struct Entry { int dpi; HFONT font; };
	static Entry s_cache[8] = {};
	for (int i = 0; i < 8; ++i)
	{
		if (s_cache[i].dpi == dpi && s_cache[i].font)
			return s_cache[i].font;
	}
	NONCLIENTMETRICSW ncm;
	HFONT created = nullptr;
	if (UT_Win32GetNonClientMetrics(&ncm, dpi))
		created = CreateFontIndirectW(&ncm.lfMenuFont);
	if (!created)
		created = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
	for (int i = 0; i < 8; ++i)
	{
		if (!s_cache[i].font)
		{
			s_cache[i].dpi = dpi;
			s_cache[i].font = created;
			return created;
		}
	}
	if (created != static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)))
		DeleteObject(created);
	return s_cache[0].font ? s_cache[0].font : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
}

static void s_paintDarkMenuBottomLine(HWND hwnd, COLORREF color)
{
	if (!hwnd || !GetMenu(hwnd))
		return;
	RECT rcClient = {};
	RECT rcWindow = {};
	if (!GetClientRect(hwnd, &rcClient) || !GetWindowRect(hwnd, &rcWindow))
		return;
	MapWindowPoints(hwnd, nullptr, reinterpret_cast<LPPOINT>(&rcClient), 2);
	OffsetRect(&rcClient, -rcWindow.left, -rcWindow.top);
	RECT rcLine = rcClient;
	rcLine.bottom = rcLine.top;
	rcLine.top = rcLine.bottom - 1;
	if (rcLine.right <= rcLine.left || rcLine.top < 0)
		return;
	HDC hdc = GetWindowDC(hwnd);
	if (!hdc)
		return;
	HBRUSH dcBrush = static_cast<HBRUSH>(GetStockObject(DC_BRUSH));
	COLORREF prevBrush = SetDCBrushColor(hdc, color);
	FillRect(hdc, &rcLine, dcBrush);
	SetDCBrushColor(hdc, prevBrush);
	ReleaseDC(hwnd, hdc);
}

WHICHPROC s_oldRedBar; 

// Where the heck is this function????
// TODO Fix the following header file. It seems to be incomplete
// TODO #include <ap_EditMethods.h>
// TODO In the mean time, define the needed function by hand
extern XAP_Dialog_MessageBox::tAnswer s_CouldNotLoadFileMessage(XAP_Frame * pFrame, const char * pNewFile, UT_Error errorCode);

static bool s_FrameSizing = false;
static bool s_LayingOutBars = false;

/*
	Owner drawn for child font combo box
*/
LRESULT CALLBACK s_rebarWndProc( HWND hWnd, UINT uMessage, WPARAM wParam, LPARAM lParam)
{
	switch (uMessage)
	{		
		case WM_DRAWITEM:
		{
			 DRAWITEMSTRUCT* pDrawItem = (DRAWITEMSTRUCT*)lParam;
			 SendMessageW(pDrawItem->hwndItem, WM_DRAWITEM, wParam, lParam);
			 return TRUE;
		}

		case WM_MEASUREITEM:
		{							   			
			MEASUREITEMSTRUCT*	mesure = (MEASUREITEMSTRUCT*) lParam;

			if (mesure->CtlType==ODT_COMBOBOX)
			{
				mesure->itemHeight = static_cast<UINT>(UT_ui_px_at_dpi(22, UT_Win32WindowDpi(hWnd)));
				return TRUE;
			}
			break;
		}
		
		default:
			break;		
	}

	return CallWindowProcW(s_oldRedBar, hWnd, uMessage, wParam, lParam);
}



XAP_Win32FrameImpl::XAP_Win32FrameImpl(XAP_Frame *pFrame) :
	XAP_FrameImpl(pFrame),
	m_hwndFrame(nullptr),
	m_hwndRebar(nullptr),
	m_hwndContainer(nullptr),
	m_hwndStatusBar(nullptr),
	m_dialogFactory(XAP_App::getApp(), pFrame), /* note: pFrame->getApp() not initialized properly yet! */		
	m_pWin32Menu(nullptr),
	m_pWin32Popup(nullptr),
	m_iBarHeight(0),
	m_iStatusBarHeight(0),
	m_iRealSizeHeight(0),
	m_iRealSizeWidth(0),
	m_bHasWpBeforeFS(false),
	m_mouseWheelMessage(0),
	m_iSizeWidth(0),
	m_iSizeHeight(0)
{
	memset(&m_wpBeforeFS, 0, sizeof(m_wpBeforeFS));
	m_wpBeforeFS.length = sizeof(WINDOWPLACEMENT);
}

XAP_Win32FrameImpl::~XAP_Win32FrameImpl(void)
{
	// only delete the things we created...
	
	DELETEP(m_pWin32Menu);
	DELETEP(m_pWin32Popup);

	// have to reset the window long, so our message functions do not
	// try to derefernce it
	if (m_hwndFrame && IsWindow(m_hwndFrame))
	{
		RevokeDragDrop(m_hwndFrame);
		SetWindowLongPtrW(m_hwndFrame, GWLP_USERDATA, 0);
		DestroyWindow(m_hwndFrame);
	}
	m_hwndFrame = nullptr;
}


#if 0
void XAP_Win32FrameImpl::_startViewAutoUpdater(void) {}
static void XAP_Win32FrameImpl::viewAutoUpdater(UT_Worker *wkr) {}
#endif


bool XAP_Win32FrameImpl::_updateTitle(void) 
{
	UT_return_val_if_fail(m_hwndFrame, false);
	XAP_Frame *pFrame = getFrame();
	UT_return_val_if_fail(pFrame, false);


	if (!XAP_FrameImpl::_updateTitle())
	{
		// no relevant change, so skip it
		return false;
	}

	std::string sTmp = pFrame->getTitle();
	sTmp += " - ";
	sTmp += XAP_App::getApp()->getApplicationTitleForTitleBar();
	XAP_Win32DialogBase::setWindowText (m_hwndFrame, sTmp.c_str());

	return true;
}

void XAP_Win32FrameImpl::_initialize(void)
{
	// we assume AP_{FE}Frame has already called XAP_Frame::initialize(...);

	// get a handle to our keyboard binding mechanism
	// and to our mouse binding mechanism.
	
	EV_EditEventMapper * pEEM = XAP_App::getApp()->getEditEventMapper();
	UT_return_if_fail(pEEM);

	m_pKeyboard = new ev_Win32Keyboard(pEEM);
	UT_return_if_fail(m_pKeyboard);
	
	m_pMouse = new EV_Win32Mouse(pEEM);
	UT_return_if_fail(m_pMouse);
}

void XAP_Win32FrameImpl::_createTopLevelWindow(void)
{
	RECT r = {};
	UT_uint32 iHeight, iWidth;
	UT_sint32 iPosX, iPosY;
	static bool firstWindow = true;	/* position only 1st window! */

	// create a top-level window for us.
	// get the default window size from preferences or something.
	// should set size for all, but position only on 1st created
	// TODO determine where to save & restore from Window flag (since
	//      we can't use the geometry flag (its some other junk about validity of pos & size)
	//      so we can properly restore Maximized/Minimized/Normal mode windows

	// get window width & height from preferences
	UT_uint32 t_flag;		// dummy variable
	const bool savedGeometry =
		XAP_App::getApp()->getGeometry(&iPosX,&iPosY,&iWidth,&iHeight,&t_flag)
		&& (iWidth > 0) && (iHeight > 0);
	if (!savedGeometry)
	{
		UT_DEBUGMSG(("Unable to obtain saved geometry, using window defaults!\n"));
		iWidth = CW_USEDEFAULT;
		iHeight = CW_USEDEFAULT;
		iPosX = CW_USEDEFAULT;
		iPosY = CW_USEDEFAULT;
	} else {
		// Ensure the window fits current desktop area.
		// GetWindowPlacement stores workspace coordinates (offset from the
		// primary monitor's work-area origin), whereas MonitorFromRect,
		// MONITORINFO::rcWork, and SPI_GETWORKAREA use screen coordinates.
		RECT rcDesktop = {}, rcPrimaryWork = {}, rcWindow = {};
		const bool havePrimaryWork =
			(SystemParametersInfoW(SPI_GETWORKAREA, 0, &rcPrimaryWork, 0) != 0);
		const LONG workOffsetX = havePrimaryWork ? rcPrimaryWork.left : 0;
		const LONG workOffsetY = havePrimaryWork ? rcPrimaryWork.top : 0;

		LONG screenX = static_cast<LONG>(iPosX) + workOffsetX;
		LONG screenY = static_cast<LONG>(iPosY) + workOffsetY;

#if (_WIN32_WINNT >= 0x0500)
		if (GetSystemMetrics(SM_CMONITORS)>1) {
			HMONITOR m;
			MONITORINFO mif;
			SetRect(&rcWindow, screenX, screenY,
					screenX + static_cast<LONG>(iWidth),
					screenY + static_cast<LONG>(iHeight));
			if ((m = MonitorFromRect(&rcWindow, MONITOR_DEFAULTTONEAREST))) {
				mif.cbSize=sizeof(MONITORINFO);
				if (GetMonitorInfoW(m,&mif)) {
					rcDesktop=mif.rcWork;
				}
			}
		}

		if (rcDesktop.left==rcDesktop.right)
#endif
		{
			if (havePrimaryWork)
				rcDesktop = rcPrimaryWork;
			else
				SetRect(&rcDesktop,0,0,GetSystemMetrics(SM_CXSCREEN),GetSystemMetrics(SM_CYSCREEN));
		}

		if (static_cast<LONG>(iWidth) > (rcDesktop.right-rcDesktop.left)) iWidth=rcDesktop.right-rcDesktop.left;
		if (static_cast<LONG>(iHeight) > (rcDesktop.bottom-rcDesktop.top)) iHeight=rcDesktop.bottom-rcDesktop.top;

		if (screenX + static_cast<LONG>(iWidth) > rcDesktop.right) screenX = rcDesktop.right - static_cast<LONG>(iWidth);
		if (screenY + static_cast<LONG>(iHeight) > rcDesktop.bottom) screenY = rcDesktop.bottom - static_cast<LONG>(iHeight);

		if (screenX < rcDesktop.left) screenX = rcDesktop.left;
		if (screenY < rcDesktop.top)  screenY = rcDesktop.top;

		// Convert clamped screen coordinates back to workspace coordinates for SetWindowPlacement.
		iPosX = static_cast<UT_sint32>(screenX - workOffsetX);
		iPosY = static_cast<UT_sint32>(screenY - workOffsetY);
	}
	/* let Windows(R) place the Window for all but 1st one, for stairstep effect */
	// GetWindowPlacement stores workspace coordinates. CreateWindowEx
	// expects screen coordinates, so a top or left taskbar would shift
	// the restored window. The saved rectangle is applied afterwards
	// with SetWindowPlacement, which is the call that understands them.
	const bool restorePlacement = firstWindow && savedGeometry;
	const UT_sint32 placeX = iPosX;
	const UT_sint32 placeY = iPosY;
	const UT_uint32 placeW = iWidth;
	const UT_uint32 placeH = iHeight;
	if (!firstWindow || restorePlacement)
	{
		iPosX = CW_USEDEFAULT;
		iPosY = CW_USEDEFAULT;
	}
	if (firstWindow)
		firstWindow = false;

	UT_DEBUGMSG(("KJD: Window Frame should be %d x %d [width x height]\n", iWidth, iHeight));


	XAP_Win32App *pWin32App = static_cast<XAP_Win32App *>(XAP_App::getApp());

	UT_Win32LocaleString str, title;
	str.fromASCII (pWin32App->getApplicationName());	
	title.fromASCII (pWin32App->getApplicationTitleForTitleBar());
		
	m_hwndFrame = UT_CreateWindowEx(0L, str.c_str(), title.c_str(),
									WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
									iPosX, iPosY, iWidth, iHeight,
									nullptr, nullptr, pWin32App->getInstance(), nullptr);
	UT_ASSERT(m_hwndFrame);


	// bind this frame to its window
	// WARNING: We assume in many places this refers to a XAP_Frame or descendant!!!
	//SetWindowLongPtrW(m_hwndFrame, GWLP_USERDATA,(LONG_PTR)this);
	SetWindowLongPtrW(m_hwndFrame, GWLP_USERDATA,(LONG_PTR)getFrame());

	if (restorePlacement)
	{
		WINDOWPLACEMENT wp;
		wp.length = sizeof(wp);
		if (!GetWindowPlacement(m_hwndFrame, &wp))
		{
			memset(&wp, 0, sizeof(wp));
			wp.length = sizeof(wp);
		}
		wp.showCmd = SW_HIDE;
		wp.rcNormalPosition.left = placeX;
		wp.rcNormalPosition.top = placeY;
		wp.rcNormalPosition.right = placeX + static_cast<UT_sint32>(placeW);
		wp.rcNormalPosition.bottom = placeY + static_cast<UT_sint32>(placeH);
		SetWindowPlacement(m_hwndFrame, &wp);
	}

#ifndef UNICODE
	// remove this when we are a true unicode app
	m_mouseWheelMessage = RegisterWindowMessageW(L"MSWHEEL_ROLLMSG");
#else
	m_mouseWheelMessage = RegisterWindowMessageW(MSH_MOUSEWHEEL);
#endif

	// synthesize a menu from the info in our
	// base class and install it into the window.
	m_pWin32Menu = new EV_Win32MenuBar(pWin32App,
							 XAP_App::getApp()->getEditEventMapper(),
							m_szMenuLayoutName,
							m_szMenuLabelSetName);
	UT_return_if_fail(m_pWin32Menu);
	UT_DebugOnly<bool> bResult = m_pWin32Menu->synthesizeMenuBar(getFrame());
	UT_ASSERT(bResult);

	HMENU oldMenu = GetMenu(m_hwndFrame);
	if (SetMenu(m_hwndFrame, m_pWin32Menu->getMenuHandle()))
	{
		DrawMenuBar(m_hwndFrame);
		if (oldMenu)
			DestroyMenu(oldMenu);
	}

	// Flat command bar. Band borders are the old raised separators;
	// Windows 11 keeps the bar on the window color without a frame.
	m_hwndRebar = UT_CreateWindowEx(0L, REBARCLASSNAMEW, nullptr,
									WS_VISIBLE | WS_CHILD | WS_CLIPCHILDREN |
									WS_CLIPSIBLINGS | CCS_NODIVIDER | CCS_NOPARENTALIGN |
									RBS_VARHEIGHT,
									0, 0, 0, 0,
									m_hwndFrame, nullptr, pWin32App->getInstance(), nullptr);
	UT_ASSERT(m_hwndRebar);
	UT_Win32ApplyWindowTheme(m_hwndRebar);
	
	/* override the window procedure*/
	s_oldRedBar = (WHICHPROC)GetWindowLongPtrW(m_hwndRebar, GWLP_WNDPROC);
	SetWindowLongPtrW(m_hwndRebar, GWLP_WNDPROC, (LONG_PTR)s_rebarWndProc);

	// create a toolbar instance for each toolbar listed in our base class.

	_createToolbars();

	// figure out how much room is left for the child.
	// GetClientRect leaves the rectangle untouched on failure.
	if (!m_hwndFrame || !GetClientRect(m_hwndFrame, &r))
	{
		iHeight = 0;
		iWidth = 0;
	}
	else
	{
		iHeight = r.bottom - r.top;
		iWidth = r.right - r.left;
	}

	m_iSizeWidth = iWidth;
	m_iSizeHeight = iHeight;
	
	// Give the rebar a width, then shrink it to the height of its bands.
	// RB_GETBARHEIGHT is that height. The window client rectangle stays
	// at whatever size we last set, so it cannot be used here.
	if( m_hwndRebar != nullptr )
	{
		MoveWindow(m_hwndRebar, 0, 0, iWidth, iHeight, FALSE);
		int barHeight = (int)SendMessageW(m_hwndRebar, RB_GETBARHEIGHT, 0, 0);
		if (barHeight <= 0)
		{
			RECT barRect = {};
			if (GetClientRect(m_hwndRebar, &barRect) && barRect.bottom > barRect.top)
				barHeight = barRect.bottom - barRect.top;
			else
				barHeight = 0;
		}
		if (barHeight > static_cast<int>(iHeight))
			barHeight = static_cast<int>(iHeight);
		MoveWindow(m_hwndRebar, 0, 0, iWidth, barHeight, TRUE);
		m_iBarHeight = (UT_uint32)barHeight;

		UT_ASSERT(iHeight > m_iBarHeight);
		iHeight -= m_iBarHeight;
	}
	else
		m_iBarHeight = 0;

	m_hwndContainer = _createDocumentWindow(getFrame(), m_hwndFrame, 0, m_iBarHeight, iWidth, iHeight);

	// Let the app-specific frame code create the status bar
	// if it wants to.  we will put it below the document
	// window (a peer with toolbars and the overall sunkenbox)
	// so that it will appear outside of the scrollbars.

	m_hwndStatusBar = _createStatusBarWindow(getFrame(), m_hwndFrame,0,m_iBarHeight+iHeight,iWidth);
	m_iStatusBarHeight = 0;
	if (m_hwndStatusBar)
	{
		RECT statusRect = {};
		if (GetClientRect(m_hwndStatusBar, &statusRect) && statusRect.bottom > 0)
			m_iStatusBarHeight = statusRect.bottom;
	}

	
	// Register drag and drop data and files
	m_dropTarget.setFrame(getFrame());
	RegisterDragDrop(m_hwndFrame, &m_dropTarget);
	UT_Win32ApplyCaptionTheme(m_hwndFrame);
	if (m_hwndFrame && GetClientRect(m_hwndFrame, &r) && r.right > 0 && r.bottom > 0)
		_layoutRebar(r.right - r.left, r.bottom - r.top);

	return;
}

bool XAP_Win32FrameImpl::_close(void)
{
	UT_return_val_if_fail(m_hwndFrame, false);

	// NOTE: This may not be the proper place, but it does mean that the
	// last window closed is the one that the window state is stored from.
	WINDOWPLACEMENT wndPlacement;
	wndPlacement.length = sizeof(WINDOWPLACEMENT); // must do
	if (GetWindowPlacement(m_hwndFrame, &wndPlacement))
	{
		UT_uint32 nFlags = PREF_FLAG_GEOMETRY_POS | PREF_FLAG_GEOMETRY_SIZE;

		if (wndPlacement.showCmd == SW_SHOWMAXIMIZED)
			nFlags |= PREF_FLAG_GEOMETRY_MAXIMIZED;

		XAP_App::getApp()->setGeometry(wndPlacement.rcNormalPosition.left, 
				wndPlacement.rcNormalPosition.top, 
				wndPlacement.rcNormalPosition.right - wndPlacement.rcNormalPosition.left,
				wndPlacement.rcNormalPosition.bottom - wndPlacement.rcNormalPosition.top,	
				nFlags);
	}	
	else
	{
		// if failed to get placement then invalidate stored settings
		XAP_App::getApp()->setGeometry(0,0,0,0,0);
	}
	
	RevokeDragDrop(m_hwndFrame);
	

	// NOTE: this should only be called from the closeWindow edit method
	DestroyWindow(m_hwndFrame);
	m_hwndFrame = nullptr;

	return true;
}

bool XAP_Win32FrameImpl::_raise(void)
{
	UT_return_val_if_fail(m_hwndFrame, false);
	BringWindowToTop(m_hwndFrame);
	return true;
}

bool XAP_Win32FrameImpl::_show(void)
{
	UT_return_val_if_fail(m_hwndFrame, false);

	ShowWindow(m_hwndFrame, SW_SHOW);
	UpdateWindow(m_hwndFrame);

	return true;
}


XAP_DialogFactory * XAP_Win32FrameImpl::_getDialogFactory(void)
{
	return &m_dialogFactory;
}

EV_Toolbar * XAP_Win32FrameImpl::_newToolbar(XAP_Frame *frame, const char *szLayout, const char *szLanguage)
{
	EV_Win32Toolbar *result = new EV_Win32Toolbar(static_cast<XAP_Win32App *>(XAP_App::getApp()), 
												  frame, 
												  szLayout, szLanguage);
	return result;
}

EV_Menu* XAP_Win32FrameImpl::_getMainMenu(void)
{
	return m_pWin32Menu;
}

// Useful to refresh the size of the Frame.  For instance,
// when the user selects hide statusbar, the Frame has to be
// resized in order to fill the gap leaved by the statusbar
void XAP_Win32FrameImpl::_queue_resize(void)
{
	if (s_FrameSizing || m_iRealSizeWidth <= 0 || m_iRealSizeHeight <= 0)
		return;
	::SendMessageW(m_hwndFrame, WM_SIZE, 0, MAKELONG(m_iRealSizeWidth, m_iRealSizeHeight));
}

void XAP_Win32FrameImpl::_layoutRebar(int frameWidth, int frameHeight)
{
	if (s_LayingOutBars || frameWidth <= 0 || frameHeight <= 0)
		return;
	s_LayingOutBars = true;

	UINT bandCount = 0;
	if (m_hwndRebar)
		bandCount = (UINT)SendMessageW(m_hwndRebar, RB_GETBANDCOUNT, 0, 0);

	int barHeight = 0;
	if (m_hwndRebar && bandCount > 0)
	{
		ShowWindow(m_hwndRebar, SW_SHOW);
		barHeight = (int)SendMessageW(m_hwndRebar, RB_GETBARHEIGHT, 0, 0);
		if (barHeight < 0)
			barHeight = 0;
		if (barHeight > frameHeight)
			barHeight = frameHeight;
		MoveWindow(m_hwndRebar, 0, 0, frameWidth, barHeight, TRUE);
	}
	else if (m_hwndRebar)
	{
		ShowWindow(m_hwndRebar, SW_HIDE);
		barHeight = 0;
	}

	m_iBarHeight = (UT_uint32)barHeight;

	int docHeight = frameHeight - barHeight;
	if (m_hwndStatusBar && ::IsWindowVisible(m_hwndStatusBar))
		docHeight -= (int)m_iStatusBarHeight;
	if (docHeight < 0)
		docHeight = 0;

	if (m_hwndStatusBar)
		MoveWindow(m_hwndStatusBar, 0, barHeight + docHeight, frameWidth, (int)m_iStatusBarHeight, TRUE);
	if (m_hwndContainer)
		MoveWindow(m_hwndContainer, 0, barHeight, frameWidth, docHeight, TRUE);

	m_iSizeWidth = (UT_uint32)frameWidth;
	m_iSizeHeight = (UT_uint32)docHeight;

	s_LayingOutBars = false;
}

void XAP_Win32FrameImpl::_refreshChrome(void)
{
	UT_Win32ApplyCaptionTheme(m_hwndFrame);
	if (m_hwndRebar)
		UT_Win32ApplyWindowTheme(m_hwndRebar);
	if (m_hwndStatusBar)
	{
		UT_Win32ApplyWindowTheme(m_hwndStatusBar);
		UT_Win32RefreshStatusFont(m_hwndStatusBar);
		SendMessageW(m_hwndStatusBar, SB_SETMINHEIGHT,
			static_cast<WPARAM>(UT_ui_px_at_dpi(26, UT_Win32WindowDpi(m_hwndStatusBar))), 0);
		SendMessageW(m_hwndStatusBar, WM_SIZE, 0, 0);
		RECT statusRect = {};
		if (GetClientRect(m_hwndStatusBar, &statusRect) && statusRect.bottom > statusRect.top)
			m_iStatusBarHeight = statusRect.bottom - statusRect.top;
		AP_Win32StatusBar * bar = reinterpret_cast<AP_Win32StatusBar *>(
			GetWindowLongPtrW(m_hwndStatusBar, GWLP_USERDATA));
		if (bar)
			bar->layoutParts();
	}
	if (m_pWin32Menu && m_hwndFrame)
		m_pWin32Menu->refreshForDpi(UT_Win32WindowDpi(m_hwndFrame));

	UT_uint32 bars = m_vecToolbars.getItemCount();
	for (UT_uint32 i = 0; i < bars; ++i)
	{
		EV_Win32Toolbar * bar = static_cast<EV_Win32Toolbar *>(m_vecToolbars.getNthItem(i));
		if (!bar)
			continue;
		bar->rebuildIcons();
		bar->applyChrome();
	}
	if (m_hwndContainer && IsWindow(m_hwndContainer))
		SendMessageW(m_hwndContainer, WM_SYSCOLORCHANGE, 0, 0);
	RECT rc;
	memset(&rc, 0, sizeof(rc));
	if (m_hwndFrame && GetClientRect(m_hwndFrame, &rc) && rc.right > 0 && rc.bottom > 0)
		_layoutRebar(rc.right - rc.left, rc.bottom - rc.top);
	if (m_hwndFrame)
		RedrawWindow(m_hwndFrame, nullptr, nullptr,
			RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_ERASE);
}


bool XAP_Win32FrameImpl::_runModalContextMenu(AV_View * pView, const char * szMenuName,
									  UT_sint32 x, UT_sint32 y)
{
	bool bResult = false;

	UT_return_val_if_fail((m_pWin32Popup==nullptr), false);

	x = pView->getGraphics()->tdu(x);
	y = pView->getGraphics()->tdu(y);

	m_pWin32Popup = new EV_Win32MenuPopup(static_cast<XAP_Win32App*>(XAP_App::getApp()),szMenuName,m_szMenuLabelSetName);
	m_pWin32Popup->setTrackMenu(true);
	if (m_hwndFrame)
		m_pWin32Popup->refreshForDpi(UT_Win32WindowDpi(m_hwndFrame));

	if (m_pWin32Popup && m_pWin32Popup->synthesizeMenuPopup(getFrame()))
	{
		
		UT_DEBUGMSG(("ContextMenu: %s at [%d,%d]\n",szMenuName,x,y));
		
		_translateDocumentToScreen(x,y);

		// The owner must be the foreground window, or a click outside leaves
		// the menu open. WM_NULL lets the menu finish dismissing.
		SetForegroundWindow(m_hwndFrame);
		TrackPopupMenu(m_pWin32Popup->getMenuHandle(),
					    TPM_TOPALIGN | TPM_RIGHTBUTTON,
					   x,y,0,m_hwndFrame,nullptr);
		PostMessageW(m_hwndFrame, WM_NULL, 0, 0);

		// the popup steals our capture, so we need to reset our counter.
		EV_Win32Mouse *pWin32Mouse = static_cast<EV_Win32Mouse *>(m_pMouse);
		pWin32Mouse->reset();
	}

	DELETEP(m_pWin32Popup);
	return bResult;
}

void XAP_Win32FrameImpl::_setFullScreen(bool isFullScreen)
{
	UT_return_if_fail(m_hwndFrame);
	HWND hwndFrame = m_hwndFrame;

	// Get the window's style so we can add or remove the titlebar later
	LONG_PTR hStyle = GetWindowLongPtrW(hwndFrame, GWL_STYLE);

	if (isFullScreen)
	{
		// Save full window placement (normal rect + maximized state) prior to fullscreen
		m_wpBeforeFS.length = sizeof(WINDOWPLACEMENT);
		m_bHasWpBeforeFS = (GetWindowPlacement(hwndFrame, &m_wpBeforeFS) != 0);

		// Remove title-bar. SWP_FRAMECHANGED applies the style.
		SetWindowLongPtrW(hwndFrame, GWL_STYLE, hStyle & ~WS_CAPTION);

		// Hide before maximizing to avoid resizing animation
		ShowWindow(hwndFrame, SW_HIDE);
		ShowWindow(hwndFrame, SW_SHOWMAXIMIZED);

		HMONITOR monitor = MonitorFromWindow(hwndFrame, MONITOR_DEFAULTTONEAREST);
		MONITORINFO info;
		info.cbSize = sizeof(info);
		if (GetMonitorInfoW(monitor, &info))
		{
			const RECT & rc = info.rcMonitor;
			SetWindowPos(hwndFrame, nullptr, rc.left, rc.top,
						 rc.right - rc.left, rc.bottom - rc.top,
						 SWP_NOZORDER | SWP_FRAMECHANGED);
		}
		else
		{
			SetWindowPos(hwndFrame, nullptr, 0, 0, 0, 0,
						 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
		}
	}
	else
	{
		// Restore title-bar and previous window placement (including maximized state)
		SetWindowLongPtrW(hwndFrame, GWL_STYLE, hStyle | WS_CAPTION);

		if (m_bHasWpBeforeFS)
		{
			SetWindowPlacement(hwndFrame, &m_wpBeforeFS);
		}
		else
		{
			ShowWindow(hwndFrame, SW_SHOWNORMAL);
		}

		SetWindowPos(hwndFrame, nullptr, 0, 0, 0, 0,
					 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
	}

	UpdateWindow(hwndFrame);
}




void XAP_Win32FrameImpl::_nullUpdate (void) const
{
	MSG msg;
	for( int i = 0 ; i < 10 ; i++ )
	{
		if( PeekMessageW( &msg, (HWND) nullptr, 0, 0, PM_REMOVE) )
		{
			if (msg.message == WM_QUIT)
			{
				PostQuitMessage(static_cast<int>(msg.wParam));
				break;
			}
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}
	}
}

void XAP_Win32FrameImpl::_setCursor(GR_Graphics::Cursor /*cursor*/)
{
	FV_View* pView = (FV_View *) getFrame()->getCurrentView();

	// this is legitimate, when we first load, the frame has no view
	if(!pView)
		return;
	
	GR_Win32Graphics * pG = (GR_Win32Graphics*)pView->getGraphics();
	UT_return_if_fail( pG );

	pG->handleSetCursorMessage();
}

UT_RGBColor XAP_Win32FrameImpl::getColorSelBackground () const
{
	DWORD dwResult = GetSysColor(COLOR_HIGHLIGHT);
	return UT_RGBColor((unsigned char)GetRValue(dwResult),
					   (unsigned char)GetGValue(dwResult),
					   (unsigned char)GetBValue(dwResult));
}

UT_RGBColor XAP_Win32FrameImpl::getColorSelForeground () const
{
	DWORD dwResult = GetSysColor(COLOR_HIGHLIGHTTEXT);
	return UT_RGBColor((unsigned char)GetRValue(dwResult),
					   (unsigned char)GetGValue(dwResult),
					   (unsigned char)GetBValue(dwResult));
}

#define MAXAPPNAME 256
bool XAP_Win32FrameImpl::_RegisterClass(XAP_Win32App * app)
{
	ATOM a = UT_RegisterClassEx(CS_DBLCLKS, XAP_Win32FrameImpl::_FrameWndProc, app->getInstance(),
								app->getIcon(), LoadCursor(nullptr,IDC_ARROW), (HBRUSH)(COLOR_BTNFACE+1), app->getSmallIcon(),
								nullptr, /*app->getApplicationName()*/ L"Abiword");
	
	UT_return_val_if_fail(a, false);

	return true;
}

/*!
 * This code is used by the dynamic menu API to rebuild the menus after a
 * a change in the menu structure.
 */
void XAP_Win32FrameImpl::_rebuildMenus(void)
{
	if (!m_pWin32Menu || !m_hwndFrame)
		return;

	SetMenu(m_hwndFrame, nullptr);
	m_pWin32Menu->destroy();
	UT_DebugOnly<bool> bResult = m_pWin32Menu->synthesizeMenuBar(getFrame());
	UT_ASSERT(bResult);
	SetMenu(m_hwndFrame, m_pWin32Menu->getMenuHandle());
	DrawMenuBar(m_hwndFrame);
}

/*****************************************************************/


LRESULT CALLBACK XAP_Win32FrameImpl::_FrameWndProc(HWND hwnd, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
	XAP_Frame * f = (XAP_Frame*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
	if (!f)
	{
		return UT_DefWindowProc(hwnd,iMsg,wParam,lParam);
	}
	
	XAP_Win32FrameImpl * fimpl = static_cast<XAP_Win32FrameImpl *>(f->getFrameImpl());
	UT_return_val_if_fail(fimpl, UT_DefWindowProc(hwnd,iMsg,wParam,lParam));

	AV_View * pView = nullptr;

	pView = f->getCurrentView();

	if(iMsg == fimpl->m_mouseWheelMessage)
	{
		wParam = MAKEWPARAM(0, (short)(int)wParam);
		return SendMessageW(hwnd, WM_MOUSEWHEEL, wParam, lParam);
	}

	switch (iMsg)
	{
	
	case WM_SETFOCUS:
		{
			// on set focus we want to make sure that the kbd layout
			// is the same as it was before we lost focus. If the user
			// was in some other application and changed the layout in
			// there to something else and now returns to AW then we
			// should not be in that different layout (this should not
			// happen as the layouts are supposed to be thread
			// specific, but I noted that sometimes even thought we
			// have the correct layout, the icon on the task bar is
			// out of sync with our layout; this should fix that).
			XAP_Win32App *pWin32App = static_cast<XAP_Win32App *>(XAP_App::getApp());
			UT_ASSERT( pWin32App );

			if (pWin32App && pWin32App->getHKL())
			{
				ActivateKeyboardLayout(pWin32App->getHKL(),0);
			}
			
			
			// fall through ...
		}
		[[fallthrough]];

	case WM_EXITMENULOOP:
		if (pView)
		{
			pView->focusChange(AV_FOCUS_HERE);
			SetFocus(fimpl->m_hwndContainer);
		}
		return 0;

	case WM_ENTERMENULOOP:
	case WM_KILLFOCUS:
		if (pView)
		{
			pView->focusChange(AV_FOCUS_NONE);
		}
		return 0;

	case WM_CREATE:
		return 0;

	case WM_COMMAND:
		if (fimpl->m_pWin32Popup)
		{
			if (fimpl->m_pWin32Popup->onCommand(pView,hwnd,wParam))
				return 0;
		}
		else if (fimpl->m_pWin32Menu && fimpl->m_pWin32Menu->onCommand(pView,hwnd,wParam))
		{
			return 0;
		}
		else if (HIWORD(wParam) == 0)
		{
			// after menu passes on it, give each of the toolbars a chance
			UT_uint32 nrToolbars, k;
			nrToolbars = fimpl->m_vecToolbars.getItemCount();
			for (k=0; k < nrToolbars; k++)
			{
				EV_Win32Toolbar * t = (EV_Win32Toolbar *)fimpl->m_vecToolbars.getNthItem(k);
				XAP_Toolbar_Id id = t->ItemIdFromWmCommand(LOWORD(wParam));
				if (t->toolbarEvent(id))
					return 0;
			}
		}
		return UT_DefWindowProc(hwnd,iMsg,wParam,lParam);

	case WM_INITMENU:
		if (fimpl->m_pWin32Popup)
		{
			if (fimpl->m_pWin32Popup->onInitMenu(f,pView,hwnd,(HMENU)wParam))
				return 0;
		}
		else if (fimpl->m_pWin32Menu && fimpl->m_pWin32Menu->onInitMenu(f,pView,hwnd,(HMENU)wParam))
			return 0;
		return UT_DefWindowProc(hwnd,iMsg,wParam,lParam);
		
	case WM_MEASUREITEM: 
	{	

		if (fimpl->m_pWin32Popup)
			(fimpl->m_pWin32Popup->onMeasureItem(hwnd, wParam,lParam));
		else if (fimpl->m_pWin32Menu)
			fimpl->m_pWin32Menu->onMeasureItem(hwnd, wParam,lParam);					

		return 0;		
	} 

	case WM_MENUCHAR: 
	{	
		if (fimpl->m_pWin32Popup)
			return (fimpl->m_pWin32Popup->onMenuChar(hwnd, wParam,lParam));
		else if (fimpl->m_pWin32Menu)
			return fimpl->m_pWin32Menu->onMenuChar(hwnd, wParam,lParam);					
		return 0;
	} 

 	case WM_DRAWITEM: 
 	{

		if (fimpl->m_pWin32Popup)
		{
			fimpl->m_pWin32Popup->onDrawItem(hwnd,wParam,lParam);
		}
		else if (fimpl->m_pWin32Menu)
			fimpl->m_pWin32Menu->onDrawItem(hwnd,wParam,lParam);

		return 0;					
	}

	case WM_MENUSELECT:
	{
		if (fimpl->m_pWin32Popup)
		{
			fimpl->m_pWin32Popup->onMenuSelect(f,pView,hwnd,(HMENU)lParam,wParam);
		}
		else if (fimpl->m_pWin32Menu)
			fimpl->m_pWin32Menu->onMenuSelect(f,pView,hwnd,(HMENU)lParam,wParam);

		return 0;					

	}

	case WM_NOTIFY:
		switch (((LPNMHDR) lParam)->code) 
		{
		
		case TBN_DROPDOWN:
		{
			HWND hWnd = ((LPNMHDR) lParam)->hwndFrom;
			EV_Win32Toolbar * t = (EV_Win32Toolbar *)GetWindowLongPtrW(hWnd, GWLP_USERDATA);						
			if (t)
				t->onDropArrow(((LPNMTOOLBARW) lParam)->iItem);
			return TBDDRET_DEFAULT;			/* Windows restores the pushed button*/
		}

		case TTN_NEEDTEXTA:
		case TTN_NEEDTEXTW:
			{
				UT_uint32 nrToolbars, k;
				nrToolbars = fimpl->m_vecToolbars.getItemCount();
				for (k=0; k < nrToolbars; k++)
				{
					EV_Win32Toolbar * t = (EV_Win32Toolbar *)fimpl->m_vecToolbars.getNthItem(k);
					if (t->getToolTip(lParam))
						break;
				}
			}
			break;

		case RBN_CHEVRONPUSHED:
			{
				NMREBARCHEVRON * chev = (NMREBARCHEVRON *) lParam;
				REBARBANDINFOW info;
				memset(&info, 0, sizeof(info));
				info.cbSize = sizeof(info);
				info.fMask = RBBIM_CHILD;
				if (SendMessageW(fimpl->m_hwndRebar, RB_GETBANDINFO, chev->uBand, (LPARAM)&info)
				    && info.hwndChild)
				{
					EV_Win32Toolbar * t = (EV_Win32Toolbar *)GetWindowLongPtrW(info.hwndChild, GWLP_USERDATA);
					POINT pt = { chev->rc.left, chev->rc.bottom };
					ClientToScreen(fimpl->m_hwndRebar, &pt);
					if (t)
						t->showChevron(pt);
				}
			}
			break;

		case RBN_HEIGHTCHANGE:
			{
				// The rebar asks its parent to adopt the band height.
				// Resizing it from here with WM_SIZE re-enters the control.
				// _layoutRebar moves the windows directly; the queued resize
				// then redraws the rulers at the new document size.
				if (s_FrameSizing || s_LayingOutBars)
					break;

				RECT r = {};
				if (!GetClientRect(fimpl->m_hwndFrame, &r))
					break;
				fimpl->_layoutRebar(r.right - r.left, r.bottom - r.top);
				f->queue_resize();
			}
			break;

		case NM_CUSTOMDRAW:
			{
				LPNMCUSTOMDRAW pNMcd = (LPNMCUSTOMDRAW)lParam;
				UT_uint32 nrToolbars, k;
				nrToolbars = fimpl->m_vecToolbars.getItemCount();
				for (k=0; k < nrToolbars; k++)
				{
					EV_Win32Toolbar * t = (EV_Win32Toolbar *)fimpl->m_vecToolbars.getNthItem(k);
					if( t && (pNMcd->hdr.hwndFrom == t->getWindow()) )
					{
						return t->onCustomDraw(reinterpret_cast<LPNMTBCUSTOMDRAW>(lParam));
					}
				}
			}
			break;

		// Process other notifications here
		default:
			break;
		} /* switch (((LPNMHDR) lParam)->code) */
		break;

	case 0x02E0: // WM_DPICHANGED
	{
		if (pView)
		{
			GR_Win32Graphics * pG = static_cast<GR_Win32Graphics *>(pView->getGraphics());
			if (pG)
				pG->refreshDeviceScale();
		}
		fimpl->_refreshRulersForDpi();
		const RECT * suggested = reinterpret_cast<const RECT *>(lParam);
		if (suggested)
		{
			SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
				suggested->right - suggested->left,
				suggested->bottom - suggested->top,
				SWP_NOZORDER | SWP_NOACTIVATE);
		}
		fimpl->_refreshChrome();
		return 0;
	}

	case WM_SETTINGCHANGE:
		if (lParam == 0 || _wcsicmp(reinterpret_cast<LPCWSTR>(lParam), L"ImmersiveColorSet") == 0)
		{
			UT_Win32NoteThemeChanged();
			fimpl->_refreshChrome();
		}
		break;

	case WM_SIZE:
	{
		if (s_FrameSizing)
			return 0;
		s_FrameSizing = true;

		int nWidth = LOWORD(lParam);
		int nHeight = HIWORD(lParam);

		char sizeNote[64];
		snprintf(sizeNote, sizeof(sizeNote), "size %d %d", nWidth, nHeight);
		UT_Win32Trace(sizeNote);
		UT_DEBUGMSG ((("xap_Win32Frame::WM_SIZE %u - %u\n"), nWidth, nHeight));
		
		if( pView && !pView->isLayoutFilling() )
		{
			fimpl->m_iRealSizeHeight = nHeight;
			fimpl->m_iRealSizeWidth = nWidth;
			
			fimpl->_startViewAutoUpdater();

			UT_Win32Trace("size-place");
			// Always fit the rebar to its bands. A width-only check left the
			// old toolbar height in place, so the rulers stayed under it.
			fimpl->_layoutRebar(nWidth, nHeight);

			UT_Win32Trace("size-zoom");
			f->updateZoom();
			UT_Win32Trace("size-done");
		}

		s_FrameSizing = false;
		UT_Win32Trace("size-return");
		return 0;
	}

	case WM_ENDSESSION:
		if (wParam && pView)
		{
			FV_View * pFV = static_cast<FV_View *>(pView);
			PD_Document * pDoc = pFV->getDocument();
			if (pDoc && pDoc->isDirty())
			{
				if (f->getFilename())
					pFV->cmdSave();
				else
				{
					const char * dir = XAP_App::getApp()->getUserPrivateDirectory();
					if (dir && *dir)
					{
						static int s_recovery = 0;
						char path[4096];
						snprintf(path, sizeof(path), "%s\\abiword-recovery-%d.abw", dir, ++s_recovery);
						pFV->cmdSaveAs(path, static_cast<int>(IE_Exp::fileTypeForSuffix(".abw")));
					}
				}
			}
		}
		return 0;

	case WM_CLOSE:
	{
		XAP_App * pApp = XAP_App::getApp();
		UT_return_val_if_fail(pApp,0);

		const EV_EditMethodContainer * pEMC = pApp->getEditMethodContainer();
		UT_return_val_if_fail(pEMC,0);

		EV_EditMethod * pEM = pEMC->findEditMethodByName("closeWindowX");
		UT_ASSERT_HARMLESS(pEM);						// make sure it's bound to something

		if (pEM)
		{
			pEM->Fn(pView,nullptr);
			return 0;
		}

		// let the window be destroyed
		break;
	}

	case WM_INPUTLANGCHANGE:
	{
		UT_DEBUGMSG(("XAP_Win32FrameImpl::_FrameWndProc: received input language change\n"));

		// This will remap the static tables used by all frames.
		// (see the comment in ev_Win32Keyboard.cpp.)
		ev_Win32Keyboard *pWin32Keyboard = static_cast<ev_Win32Keyboard *>(fimpl->m_pKeyboard);
		pWin32Keyboard->remapKeyboard((HKL)lParam);

		// set the language
		XAP_Win32App *pWin32App = static_cast<XAP_Win32App *>(XAP_App::getApp());
		UT_ASSERT_HARMLESS( pWin32App );

		if(pWin32App)
			pWin32App->setKbdLanguage((HKL)lParam);

		// Do not propagate this message.
		
		return 1; //DefWindowProc(hwnd, iMsg, wParam, lParam);
	}

	case WM_MOUSEWHEEL:
	case WM_MOUSEHWHEEL:
	{
		if (fimpl->m_hwndContainer)
			return SendMessageW(fimpl->m_hwndContainer, iMsg, wParam, lParam);
		break;
	}

	case WM_VSCROLL:
	case WM_HSCROLL:
		if (fimpl->m_hwndContainer)
			return SendMessageW(fimpl->m_hwndContainer, iMsg, wParam, lParam);
		break;

	case WM_GETMINMAXINFO:
	{
		MINMAXINFO * info = reinterpret_cast<MINMAXINFO *>(lParam);
		if (!info)
			break;
		const int dpi = UT_Win32WindowDpi(hwnd);
		const int minW = UT_ui_px_at_dpi(480, dpi);
		const int minH = UT_ui_px_at_dpi(320, dpi);
		if (info->ptMinTrackSize.x < minW)
			info->ptMinTrackSize.x = minW;
		if (info->ptMinTrackSize.y < minH)
			info->ptMinTrackSize.y = minH;
		return 0;
	}

	case WM_SYSCOLORCHANGE:
	{
		if (fimpl->m_hwndRebar)
			SendMessageW(fimpl->m_hwndRebar,WM_SYSCOLORCHANGE,0,0);
		fimpl->_refreshChrome();
		if (fimpl->m_hwndStatusBar)
			SendMessageW(fimpl->m_hwndStatusBar,WM_SYSCOLORCHANGE,0,0);
		return 0;
	}

	case WM_DROPFILES:
		{
			HDROP hDrop = (HDROP) wParam;
			const bool oleOwned = (lParam != 0);
			// How many files were dropped?
			int count = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
			UT_Win32LocaleString str;
			for (int i = 0; i < count; i++)
			{
				const UINT pathlength = DragQueryFileW(hDrop, i, nullptr, 0);
				if (pathlength == 0)
					continue;

				WCHAR * szFileName = static_cast<WCHAR *>(UT_calloc(pathlength + 1, sizeof(WCHAR)));
				if (!szFileName)
					continue;
				if (DragQueryFileW(hDrop, i, szFileName, pathlength + 1) == 0)
				{
					FREEP(szFileName);
					continue;
				}

				XAP_App * pApp = XAP_App::getApp();
				if (!pApp)
				{
					FREEP(szFileName);
					if (!oleOwned)
						DragFinish(hDrop);
					return 0;
				}
				FV_View* pCurrentView = (FV_View *) f->getCurrentView();
				XAP_Frame * pNewFrame = nullptr;
				IEGraphicFileType iegft = IEGFT_Unknown;
				IE_ImpGraphic *pIEG = nullptr;
				FG_ConstGraphicPtr pFG;
				UT_Error errorCode = UT_ERROR;

				/*
					The user may be dropping any kind of file
					Check first if the file is a graphic. If it's a graphic we insert it
					in the document, if not we assume that it's a document
				*/
				str.fromLocale(szFileName);
				FREEP(szFileName);
				char * uri = UT_go_filename_to_uri(str.utf8_str().utf8_str());
				if(uri)
					errorCode = IE_ImpGraphic::constructImporter(uri, iegft, &pIEG);

				if(errorCode == UT_OK && pIEG)
				{
					errorCode = pIEG->importGraphic(uri, pFG);

					DELETEP(pIEG);
					if(errorCode == UT_OK && pFG && pCurrentView)
					{
					  errorCode = pCurrentView->cmdInsertGraphic(pFG);
					}
				}

				if (errorCode != UT_OK)
				{
					// Check if the current document is empty.
					if (f->isDirty() || f->getFilename() ||
						(f->getViewNumber() > 0))
					{
						pNewFrame = pApp->newFrame();
						if (pNewFrame == nullptr)
						{
						  f->setStatusMessage("Could not open another window");
						  g_free(uri);
						  if (!oleOwned)
							  DragFinish(hDrop);
						  return 0;
						}
					}
					else
					{
						pNewFrame = f;
					}

					if(uri)
						errorCode = pNewFrame->loadDocument(uri, IEFT_Unknown);

					if (!UT_IS_IE_SUCCESS(errorCode))
					{
						if (f != pNewFrame)
						{
							pApp->forgetFrame(pNewFrame);
							pNewFrame->close();
							delete pNewFrame;
						}
					}
					else
					{
						pNewFrame->show();
					}
				}

				if (errorCode != UT_OK)
				{
					XAP_Frame * pMsgFrame = (UT_IS_IE_SUCCESS(errorCode) && pNewFrame) ? pNewFrame : f;
					s_CouldNotLoadFileMessage(pMsgFrame, uri ? uri : str.utf8_str().utf8_str(), errorCode);
				}
				g_free(uri);
			}
			if (!oleOwned)
				DragFinish(hDrop);
		}
		return 0;

	case WM_UAHDRAWMENU:
	{
		const UT_Win32ThemeColors tc = UT_Win32GetThemeColors();
		if (!tc.dark || tc.highContrast)
			break;
		AbiUahMenu * pUDM = reinterpret_cast<AbiUahMenu *>(lParam);
		if (!pUDM || !pUDM->hdc)
			break;
		MENUBARINFO mbi;
		memset(&mbi, 0, sizeof(mbi));
		mbi.cbSize = sizeof(mbi);
		RECT rcWindow = {};
		if (GetMenuBarInfo(hwnd, OBJID_MENU, 0, &mbi) && GetWindowRect(hwnd, &rcWindow))
		{
			OffsetRect(&mbi.rcBar, -rcWindow.left, -rcWindow.top);
			HBRUSH dcBrush = static_cast<HBRUSH>(GetStockObject(DC_BRUSH));
			COLORREF prevBrush = SetDCBrushColor(pUDM->hdc, tc.chromeBg);
			FillRect(pUDM->hdc, &mbi.rcBar, dcBrush);
			SetDCBrushColor(pUDM->hdc, prevBrush);
		}
		return 0;
	}

	case WM_UAHDRAWMENUITEM:
	{
		const UT_Win32ThemeColors tc = UT_Win32GetThemeColors();
		if (!tc.dark || tc.highContrast)
			break;
		AbiUahDrawMenuItem * pUDMI = reinterpret_cast<AbiUahDrawMenuItem *>(lParam);
		if (!pUDMI || !pUDMI->um.hdc)
			break;

		HDC hdc = pUDMI->um.hdc;
		RECT rc = pUDMI->dis.rcItem;
		const UINT state = pUDMI->dis.itemState;
		const bool disabled = (state & (ODS_DISABLED | ODS_INACTIVE)) != 0;
		const bool selected = (state & ODS_SELECTED) != 0;
		const bool hot = (state & ODS_HOTLIGHT) != 0;
		const int dpi = UT_Win32WindowDpi(hwnd);

		HBRUSH dcBrush = static_cast<HBRUSH>(GetStockObject(DC_BRUSH));
		COLORREF prevBrush = SetDCBrushColor(hdc, tc.chromeBg);
		FillRect(hdc, &rc, dcBrush);

		if (!disabled && (hot || selected))
		{
			RECT pill = rc;
			InflateRect(&pill, -UT_ui_px_at_dpi(1, dpi), -UT_ui_px_at_dpi(2, dpi));
			const int rad = UT_ui_px_at_dpi(4, dpi) * 2;
			HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(hdc, dcBrush));
			HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, GetStockObject(NULL_PEN)));
			SetDCBrushColor(hdc, selected ? tc.pressedFill : tc.hoverFill);
			RoundRect(hdc, pill.left, pill.top, pill.right, pill.bottom, rad, rad);
			if (oldPen)
				SelectObject(hdc, oldPen);
			if (oldBrush)
				SelectObject(hdc, oldBrush);
		}
		SetDCBrushColor(hdc, prevBrush);

		wchar_t buf[256];
		buf[0] = 0;
		MENUITEMINFOW mii;
		memset(&mii, 0, sizeof(mii));
		mii.cbSize = sizeof(mii);
		mii.fMask = MIIM_STRING;
		mii.dwTypeData = buf;
		mii.cch = 256;
		GetMenuItemInfoW(pUDMI->um.hmenu, static_cast<UINT>(pUDMI->umi.iPosition), TRUE, &mii);

		HFONT oldFont = static_cast<HFONT>(SelectObject(hdc, s_menuBarFont(dpi)));
		SetBkMode(hdc, TRANSPARENT);
		SetTextColor(hdc, disabled ? tc.textDisabled : tc.text);
		DWORD dtFlags = DT_CENTER | DT_SINGLELINE | DT_VCENTER;
		if (state & ODS_NOACCEL)
			dtFlags |= DT_HIDEPREFIX;
		DrawTextW(hdc, buf, -1, &rc, dtFlags);
		if (oldFont)
			SelectObject(hdc, oldFont);
		return 0;
	}

	case WM_NCPAINT:
	case WM_NCACTIVATE:
	{
		LRESULT res = UT_DefWindowProc(hwnd, iMsg, wParam, lParam);
		const UT_Win32ThemeColors tc = UT_Win32GetThemeColors();
		if (tc.dark && !tc.highContrast)
			s_paintDarkMenuBottomLine(hwnd, tc.chromeBg);
		return res;
	}

	case WM_ERASEBKGND:
	{
		UT_Win32Trace("frame-erase");
		HDC hdc = reinterpret_cast<HDC>(wParam);
		RECT rc = {};
		if (hdc && GetClientRect(hwnd, &rc))
		{
			const UT_Win32ThemeColors tc = UT_Win32GetThemeColors();
			HBRUSH dcBrush = static_cast<HBRUSH>(GetStockObject(DC_BRUSH));
			COLORREF prevBrush = SetDCBrushColor(hdc, tc.chromeBg);
			FillRect(hdc, &rc, dcBrush);
			SetDCBrushColor(hdc, prevBrush);
			return 1;
		}
		break;
	}

	case WM_PAINT:
	{
		UT_Win32Trace("frame-paint");
		UT_Win32TraceHeap("frame-paint");
		break;
	}

	case WM_DESTROY:
		UT_Win32Trace("destroy");
		return 0;

	} /* switch (iMsg) */

	return UT_DefWindowProc(hwnd, iMsg, wParam, lParam);
}
