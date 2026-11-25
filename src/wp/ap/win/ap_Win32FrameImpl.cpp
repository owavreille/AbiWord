/* AbiWord
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

#include "ap_Win32FrameImpl.h"
#include "fv_View.h"

#include "ap_Win32Frame.h"
#include "ut_Win32Timer.h"
#include "ut_Win32OS.h"
#include "gr_Win32Graphics.h"
#include "gr_Painter.h"
#include "ap_Win32TopRuler.h"
#include "ap_Win32LeftRuler.h"
#include "ap_Win32StatusBar.h"
#include "ut_Win32LocaleString.h"
#include "ut_misc.h"

#include <winuser.h>
#include <imm.h>

#include <zmouse.h>
#ifdef __MINGW32__
#include "winezmouse.h"
#include <imm.h>
#endif

#ifndef SPI_GETWHEELSCROLLLINES
#define SPI_GETWHEELSCROLLLINES   104
#endif

// I really hate doing this, but this constant is only defined with _WIN32_WINNT >=
// 0x0501, i.e., winXP, and I do not want to enable the lot
#ifndef WM_UNICHAR
#define WM_UNICHAR 0x109
#endif

// Vista. The project still targets _WIN32_WINNT 0x0500, so the tilt
// message is not declared by the headers we compile against.
#ifndef WM_MOUSEHWHEEL
#define WM_MOUSEHWHEEL 0x020E
#endif

// GetClientRect leaves the RECT untouched when it fails.
// https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getclientrect
static bool s_clientRect(HWND hwnd, RECT &r)
{
	memset(&r, 0, sizeof(r));
	return hwnd && GetClientRect(hwnd, &r);
}

#ifndef UNICODE_NOCHAR
#define UNICODE_NOCHAR 0xFFFF
#endif

#define GWL(hwnd)		reinterpret_cast<AP_Win32Frame *>(GetWindowLongPtrW((hwnd), GWLP_USERDATA))
#define SWL(hwnd, f)	SetWindowLongPtrW((hwnd), GWLP_USERDATA,(LONG_PTR)(f))

// reserve space for static variables
wchar_t AP_Win32FrameImpl::s_ContainerWndClassName[MAXCNTWNDCLSNMSIZE];
wchar_t AP_Win32FrameImpl::s_DocumentWndClassName[MAXDOCWNDCLSNMSIZE];
//static char s_LeftRulerWndClassName[256];

AP_Win32FrameImpl::AP_Win32FrameImpl(AP_Frame *pFrame) :
	XAP_Win32FrameImpl(static_cast<XAP_Frame *>(pFrame)),
	m_hwndContainer(nullptr),
	m_hwndTopRuler(nullptr),
	m_hwndLeftRuler(nullptr),
	m_hwndDocument(nullptr),
	m_hWndHScroll(nullptr),
	m_hWndVScroll(nullptr),
	m_hWndGripperHack(nullptr),
	m_wheelRemainderV(0),
	m_wheelRemainderH(0),
	m_wheelRemainderZoom(0),
	m_bMouseWheelTrack(false),
	m_startMouseWheelY(0),
	m_startScrollPosition(0),
	m_bMouseActivateReceived(false)
{
}

AP_Win32FrameImpl::~AP_Win32FrameImpl(void)
{
}


XAP_FrameImpl * AP_Win32FrameImpl::createInstance(XAP_Frame *pFrame)
{
	XAP_FrameImpl *pFrameImpl = new AP_Win32FrameImpl(static_cast<AP_Frame *>(pFrame));

	UT_ASSERT_HARMLESS(pFrameImpl);

	return pFrameImpl;
}

void AP_Win32FrameImpl::_initialize(void)
{
	// FrameData initialized by AP_Win32Frame

	XAP_Win32FrameImpl::_initialize();

	_createTopLevelWindow();
}

HWND AP_Win32FrameImpl::_createDocumentWindow(XAP_Frame *pFrame, HWND hwndParent,
							UT_uint32 iLeft, UT_uint32 iTop,
							UT_uint32 iWidth, UT_uint32 iHeight)
{
	UT_return_val_if_fail(pFrame, nullptr);

	// create the window(s) that the user will consider to be the
	// document window -- the thing between the bottom of the toolbars
	// and the top of the status bar.  return the handle to it.

	// create a child window for us -- this will be the 'container'.
	// the 'container' will in turn contain the document window, the
	// rulers, and the variousscroll bars and other dead space.

	m_hwndContainer = UT_CreateWindowEx(WS_EX_CLIENTEDGE, s_ContainerWndClassName, nullptr,
									 WS_CHILD | WS_VISIBLE,
									 iLeft, iTop, iWidth, iHeight,
									 hwndParent, nullptr, static_cast<XAP_Win32App *>(XAP_App::getApp())->getInstance(), nullptr);
	UT_return_val_if_fail (m_hwndContainer, nullptr);
	// WARNING!!! many places expect an XAP_Frame or descendant!!!
	//SWL(m_hwndContainer, this);
	SWL(m_hwndContainer, pFrame);

	// now create all the various windows inside the container window.

	RECT r;
	if (!s_clientRect(m_hwndContainer, r))
		SetRect(&r, 0, 0, static_cast<int>(iWidth), static_cast<int>(iHeight));
	const int cyHScroll = GetSystemMetrics(SM_CYHSCROLL);
	const int cxVScroll = GetSystemMetrics(SM_CXVSCROLL);

	m_hWndHScroll = UT_CreateWindowEx(0, L"ScrollBar", nullptr, WS_CHILD | WS_VISIBLE | SBS_HORZ,
									0, r.bottom - cyHScroll,
									r.right - cxVScroll, cyHScroll,
									m_hwndContainer,
									nullptr, static_cast<XAP_Win32App *>(XAP_App::getApp())->getInstance(), nullptr);
	UT_return_val_if_fail (m_hWndHScroll, nullptr);
	// WARNING!!! many places expact an XAP_Frame or descendant!!!
	//SWL(m_hWndHScroll, this);
	SWL(m_hWndHScroll, pFrame);

	m_hWndVScroll = UT_CreateWindowEx(0, L"ScrollBar", nullptr, WS_CHILD | WS_VISIBLE | SBS_VERT,
									r.right - cxVScroll, 0,
									cxVScroll, r.bottom - cyHScroll,
									m_hwndContainer,
									nullptr, static_cast<XAP_Win32App *>(XAP_App::getApp())->getInstance(), nullptr);
	UT_return_val_if_fail (m_hWndVScroll, nullptr);
	// WARNING!!! many places expact an XAP_Frame or descendant!!!
	//SWL(m_hWndVScroll, this);
	SWL(m_hWndVScroll, pFrame);

#if 1 // if the StatusBar is enabled, our lower-right corner is a dead spot
#  define XX_StyleBits          (WS_DISABLED | SBS_SIZEBOX)
#else
#  define XX_StyleBits          (SBS_SIZEGRIP)
#endif

	m_hWndGripperHack = UT_CreateWindowEx(0, L"ScrollBar", nullptr,
										WS_CHILD | WS_VISIBLE | XX_StyleBits,
										r.right-cxVScroll, r.bottom-cyHScroll, cxVScroll, cyHScroll,
										m_hwndContainer, nullptr, static_cast<XAP_Win32App *>(XAP_App::getApp())->getInstance(), nullptr);

	UT_return_val_if_fail (m_hWndGripperHack, nullptr);
	// WARNING!!! many places expact an XAP_Frame or descendant!!!
	//SWL(m_hWndGripperHack, this);
	SWL(m_hWndGripperHack, pFrame);

	// create the rulers, if needed

	int yTopRulerHeight = 0;
	int xLeftRulerWidth = 0;

	/* Create Graphics. GetDC(nullptr) would be the screen DC. */
	HDC containerDC = GetDC(m_hwndContainer);
	UT_return_val_if_fail(containerDC, nullptr);
	GR_Win32AllocInfo ai(containerDC, m_hwndContainer);
	GR_Win32Graphics * pG = (GR_Win32Graphics *)XAP_App::getApp()->newGraphics(ai);

	UT_return_val_if_fail (pG, nullptr);

	static_cast<AP_FrameData *>(pFrame->getFrameData())->m_pG = pG;

	if (static_cast<AP_FrameData *>(pFrame->getFrameData())->m_bShowRuler)
	{
		_createTopRuler(pFrame);
		if (static_cast<AP_FrameData*>(pFrame->getFrameData())->m_pViewMode == VIEW_PRINT)
	       _createLeftRuler(pFrame);

		_getRulerSizes(static_cast<AP_FrameData *>(pFrame->getFrameData()), yTopRulerHeight, xLeftRulerWidth);
	}

	// create a child window for us.
	m_hwndDocument = UT_CreateWindowEx(0, s_DocumentWndClassName, nullptr,
									   WS_CHILD | WS_VISIBLE,
									   xLeftRulerWidth, yTopRulerHeight,
									   r.right - xLeftRulerWidth - cxVScroll,
									   r.bottom - yTopRulerHeight - cyHScroll,
									   m_hwndContainer, nullptr,
									   static_cast<XAP_Win32App *>(XAP_App::getApp())->getInstance(), nullptr);

	UT_return_val_if_fail (m_hwndDocument, nullptr);
	// WARNING!!! many places expact an XAP_Frame or descendant!!!
	//SWL(m_hwndDocument, this);
	SWL(m_hwndDocument, pFrame);

	// QueueDraw and updateScreen must hit the page, not the container
	// sitting behind this child.
	pG->setWindow(m_hwndDocument);

	return m_hwndContainer;
}

HWND AP_Win32FrameImpl::_createStatusBarWindow(XAP_Frame *pFrame, HWND hwndParent,
										   UT_uint32 iLeft, UT_uint32 iTop,
										   UT_uint32 iWidth)
{
	UT_return_val_if_fail(pFrame, nullptr);
	AP_Win32StatusBar * pStatusBar = new AP_Win32StatusBar(pFrame);
	UT_return_val_if_fail (pStatusBar, nullptr);
	_setHwndStatusBar(pStatusBar->createWindow(hwndParent,iLeft,iTop,iWidth));
	static_cast<AP_FrameData *>(pFrame->getFrameData())->m_pStatusBar = pStatusBar;

	return _getHwndStatusBar();
}

void AP_Win32FrameImpl::_refillToolbarsInFrameData()
{
	UT_ASSERT_HARMLESS(UT_NOT_IMPLEMENTED);
}

void AP_Win32FrameImpl::_rebuildToolbar(UT_uint32 /*ibar*/)
{
	UT_ASSERT_HARMLESS(UT_NOT_IMPLEMENTED);
}

void AP_Win32FrameImpl::_toggleBar(UT_uint32 iBarNb, bool bBarOn)
{
	UT_DEBUGMSG(("AP_Win32FrameImpl::toggleBar %d, %d\n", iBarNb, bBarOn));

	// TMN: Yes, this cast is correct. We store EV_Win32Toolbar in
	// m_vecToolbars, but we only need the EV_Toolbar part of it.
	EV_Toolbar* pToolbar = (EV_Toolbar*)m_vecToolbars.getNthItem(iBarNb);
	UT_return_if_fail(pToolbar);

	if (bBarOn)
	{
		pToolbar->show();
	}
	else
	{
		pToolbar->hide();
	}

	// Showing or hiding Tableau / Extra changes the band height. Fit the
	// rebar to the bands that remain, then let the frame redraw the rulers
	// against the new document rectangle.
	RECT frameRect;
	if (s_clientRect(_getTopLevelWindow(), frameRect))
		_layoutRebar(frameRect.right - frameRect.left, frameRect.bottom - frameRect.top);

	if (!static_cast<AP_FrameData *>(getFrame()->getFrameData())->m_bIsFullScreen)
		getFrame()->queue_resize();
}

void AP_Win32FrameImpl::_bindToolbars(AV_View *pView)
{
	const UT_uint32 nrToolbars = m_vecToolbars.getItemCount();
	for (UT_uint32 k = 0; k < nrToolbars; ++k)
	{
		// TODO Toolbars are a frame-level item, but a view-listener is
		// TODO a view-level item.  I've bound the toolbar-view-listeners
		// TODO to the current view within this frame and have code in the
		// TODO toolbar to allow the view-listener to be rebound to a different
		// TODO view.  in the future, when we have support for multiple views
		// TODO in the frame (think splitter windows), we will need to have
		// TODO a loop like this to help change the focus when the current
		// TODO view changes.

		EV_Win32Toolbar* pWin32Toolbar = (EV_Win32Toolbar *)m_vecToolbars.getNthItem(k);
		pWin32Toolbar->bindListenerToView(pView);
	}
}

void AP_Win32FrameImpl::_showOrHideToolbars(void)
{
	XAP_Frame* pFrame = getFrame();
	UT_return_if_fail ( pFrame );
	bool *bShowBar = static_cast<AP_FrameData *>(pFrame->getFrameData())->m_bShowBar;

	for (UT_sint32 i = 0; i < m_vecToolbarLayoutNames.getItemCount(); i++)
	{
		if (!bShowBar[i])
			pFrame->toggleBar( i, false );
	}
}

void AP_Win32FrameImpl::_showOrHideStatusbar(void)
{
	XAP_Frame* pFrame = getFrame();
	UT_return_if_fail ( pFrame );
	bool bShowStatusBar = static_cast<AP_FrameData *>(pFrame->getFrameData())->m_bShowStatusBar;
	pFrame->toggleStatusBar(bShowStatusBar);
}

void AP_Win32FrameImpl::_hideMenuScroll(bool /*bHideMenuScroll*/)
{
	UT_ASSERT_HARMLESS(UT_NOT_IMPLEMENTED);
}

/* helper methods for helper methods for _showDocument (meta-helper-methods?) :-)
 * called by AP_Frame::_replaceView which is called by AP_Frame::_showDocument 
 */
void AP_Win32FrameImpl::_getDocumentArea(RECT &r)
{
	HWND hwnd = getHwndDocument();
	s_clientRect(hwnd, r);
}

UT_sint32 AP_Win32FrameImpl::_getDocumentAreaWidth(void)
{
	RECT r;
	_getDocumentArea(r);
	return r.right - r.left;
}

UT_sint32 AP_Win32FrameImpl::_getDocumentAreaHeight(void)
{
	RECT r;
	_getDocumentArea(r);
	return r.bottom - r.top;
}


void AP_Win32FrameImpl::_createTopRuler(XAP_Frame *pFrame)
{
	RECT r;
	int cxVScroll;

	if (!s_clientRect(m_hwndContainer, r))
		return;
	cxVScroll = GetSystemMetrics(SM_CXVSCROLL);

	// create the top ruler
	AP_Win32TopRuler * pWin32TopRuler = new AP_Win32TopRuler(pFrame);
	UT_return_if_fail (pWin32TopRuler);
	m_hwndTopRuler = pWin32TopRuler->createWindow(m_hwndContainer,
												  0,0, (r.right - cxVScroll));
	static_cast<AP_FrameData *>(pFrame->getFrameData())->m_pTopRuler = pWin32TopRuler;


	// get the width from the left ruler and stuff it into the top ruler.
	UT_uint32 xLeftRulerWidth = 0;
#if 0   // Why? We still want to draw the corner, don't we? Also, the width value is messed when leaving normal mode.
	if( m_hwndLeftRuler )
	{
		AP_Win32LeftRuler * pWin32LeftRuler = nullptr;
		pWin32LeftRuler =  static_cast<AP_Win32LeftRuler *>(static_cast<AP_FrameData *>(pFrame->getFrameData())->m_pLeftRuler);
		xLeftRulerWidth = pWin32LeftRuler->getWidth();
	}
#endif
	pWin32TopRuler->setOffsetLeftRuler(xLeftRulerWidth);
}
 

void AP_Win32FrameImpl::_createLeftRuler(XAP_Frame *pFrame)
{
	RECT r;
	int cyHScroll;

	if (!s_clientRect(m_hwndContainer, r))
		return;
	cyHScroll = GetSystemMetrics(SM_CYHSCROLL);

	UT_uint32 yTopRulerHeight = 0;

	if( m_hwndTopRuler )
	{
		AP_FrameData * pFrameData = nullptr;
		pFrameData = static_cast<AP_FrameData *>( pFrame->getFrameData() );
		AP_Win32TopRuler * pWin32TopRuler = nullptr;
		pWin32TopRuler =  static_cast<AP_Win32TopRuler *>( pFrameData->m_pTopRuler );
		yTopRulerHeight =  pWin32TopRuler->getGraphics()->tdu(pWin32TopRuler->getHeight());
	}

	// create the left ruler
	AP_Win32LeftRuler * pWin32LeftRuler = new AP_Win32LeftRuler(pFrame);
	UT_return_if_fail (pWin32LeftRuler);
	m_hwndLeftRuler = pWin32LeftRuler->createWindow(m_hwndContainer,0,yTopRulerHeight,
													r.bottom - yTopRulerHeight - cyHScroll);
	static_cast<AP_FrameData *>(pFrame->getFrameData())->m_pLeftRuler = pWin32LeftRuler;

	// get the width from the left ruler and stuff it into the top ruler.
    if( m_hwndTopRuler )
	{
		AP_FrameData * pFrameData = nullptr;
		pFrameData = static_cast<AP_FrameData *>( pFrame->getFrameData() );
		UT_uint32 xLeftRulerWidth = pWin32LeftRuler->getGraphics()->tdu(pWin32LeftRuler->getWidth());
		AP_Win32TopRuler * pWin32TopRuler = nullptr;
		pWin32TopRuler =  static_cast<AP_Win32TopRuler *>( pFrameData->m_pTopRuler );
		pWin32TopRuler->setOffsetLeftRuler( xLeftRulerWidth );
	}
}

void AP_Win32FrameImpl::_toggleTopRuler(AP_Win32Frame *pFrame, bool bRulerOn)
{
	UT_return_if_fail(pFrame);
	AP_FrameData *pFrameData = pFrame->getAPFrameData();
	UT_return_if_fail(pFrameData);

	if (bRulerOn)
	{
		if (m_hwndTopRuler)
		{
			DestroyWindow(m_hwndTopRuler);
			m_hwndTopRuler = nullptr;
		}
		DELETEP(pFrameData->m_pTopRuler);

		_createTopRuler(pFrame);

		if (pFrameData->m_pTopRuler)
			pFrameData->m_pTopRuler->setView(pFrame->getCurrentView(), pFrame->getZoomPercentage());
	}
	else
	{
		// delete the actual widgets
		if (m_hwndTopRuler)
		{
			DestroyWindow(m_hwndTopRuler);
			m_hwndTopRuler = nullptr;
		}

		DELETEP(pFrameData->m_pTopRuler);
		FV_View * pFV = static_cast<FV_View *>(pFrame->getCurrentView());
		if (pFV)
			pFV->setTopRuler(nullptr);
	}

	// repack the child windows
	RECT r;
	if (s_clientRect(m_hwndContainer, r))
		_onSize(pFrameData, r.right - r.left, r.bottom - r.top);
}

void AP_Win32FrameImpl::_toggleLeftRuler(AP_Win32Frame *pFrame, bool bRulerOn)
{
	UT_return_if_fail(pFrame);
	AP_FrameData *pFrameData = pFrame->getAPFrameData();
	UT_return_if_fail(pFrameData);
		

	if (bRulerOn && pFrameData->m_pViewMode == VIEW_PRINT)
	{
		if (m_hwndLeftRuler)
		{
			DestroyWindow(m_hwndLeftRuler);
			m_hwndLeftRuler = nullptr;
		}
		DELETEP(pFrameData->m_pLeftRuler);

		_createLeftRuler(pFrame);

		if (pFrameData->m_pLeftRuler)
			pFrameData->m_pLeftRuler->setView(pFrame->getCurrentView(), pFrame->getZoomPercentage());
	}
	else
	{
		// delete the actual widgets
		if (m_hwndLeftRuler)
		{
			DestroyWindow(m_hwndLeftRuler);
			m_hwndLeftRuler = nullptr;
		}

		DELETEP(pFrameData->m_pLeftRuler);
		FV_View * pFV = static_cast<FV_View *>(pFrame->getCurrentView());
		if (pFV)
			pFV->setLeftRuler(nullptr);
	}

	// repack the child windows
	RECT r;
	if (s_clientRect(m_hwndContainer, r))
		_onSize(pFrameData, r.right - r.left, r.bottom - r.top);
}

void AP_Win32FrameImpl::_translateDocumentToScreen(UT_sint32 &x, UT_sint32 &y)
{
	UT_return_if_fail(m_hwndDocument);

	// translate the given document mouse coordinates into absolute screen coordinates.

	POINT pt = { x, y };
	ClientToScreen(m_hwndDocument,&pt);
	x = pt.x;
	y = pt.y;
}

void AP_Win32FrameImpl::_setXScrollRange(AP_FrameData * pData, AV_View *pView)
{
	UT_return_if_fail(m_hwndDocument);

	RECT r;
	if (!s_clientRect(m_hwndDocument, r))
		return;
	const UT_uint32 iWindowWidth = r.right - r.left;
	const UT_uint32 iWidth = pView->getGraphics()->tdu(pData->m_pDocLayout->getWidth());
	XAP_Frame::tZoomType tZoom = getFrame()->getZoomType();	
	SCROLLINFO si;
	memset(&si, 0, sizeof(si));

	si.cbSize = sizeof(si);
	si.fMask = SIF_ALL | SIF_DISABLENOSCROLL;
	si.nMin = 0;
	si.nMax = iWidth;
	si.nPos = ((pView) ? pView->getGraphics()->tdu(pView->getXScrollOffset()) : 0);
	si.nPage = iWindowWidth;
	SetScrollInfo(m_hWndHScroll, SB_CTL, &si, TRUE);
	// SetScrollInfo clamps a position left over from another layout.
	// The view has to follow that clamp, or the page sits above the window.
	// A failed read leaves si as requested.
	GetScrollInfo(m_hWndHScroll, SB_CTL, &si);
	pView->sendHorizontalScrollEvent(pView->getGraphics()->tlu(si.nPos),pView->getGraphics()->tlu(si.nMax-si.nPage));

	// hide the horizontal scrollbar if the scroll range is such that the window can contain it all
	// show it otherwise
	// Hide the horizontal scrollbar if we've set to page width or fit to page.
	// This stops a resizing race condition.
 	if (iWidth <= iWindowWidth || tZoom == XAP_Frame::z_PAGEWIDTH || tZoom == XAP_Frame::z_WHOLEPAGE)
	{	 	
		if (IsWindowVisible (m_hWndHScroll)) {
			RECT clientRect;

			ShowWindow (m_hWndHScroll, SW_HIDE);
			ShowWindow (m_hWndGripperHack, SW_HIDE);
			if (s_clientRect(m_hwndContainer, clientRect))
				_onSize(pData, clientRect.right - clientRect.left, clientRect.bottom - clientRect.top);
		}
	}
 	else
	{	
		if (!IsWindowVisible (m_hWndHScroll)) {
			RECT clientRect;

			ShowWindow(m_hWndHScroll, SW_NORMAL);
			ShowWindow (m_hWndGripperHack, SW_NORMAL);
			if (s_clientRect(m_hwndContainer, clientRect))
				_onSize(pData, clientRect.right - clientRect.left, clientRect.bottom - clientRect.top);
		}		
	}

}

void AP_Win32FrameImpl::_setYScrollRange(AP_FrameData * pData, AV_View *pView)
{
	UT_return_if_fail(m_hwndDocument);

	RECT r;
	if (!s_clientRect(m_hwndDocument, r))
		return;
	const UT_uint32 iWindowHeight = r.bottom - r.top;
	const UT_uint32 iHeight = pView->getGraphics()->tdu(pData->m_pDocLayout->getHeight());

	SCROLLINFO si;
	memset(&si, 0, sizeof(si));

	si.cbSize = sizeof(si);
	si.fMask = SIF_ALL | SIF_DISABLENOSCROLL;
	si.nMin = 0;
	si.nMax = iHeight;
	si.nPos = ((pView) ? pView->getGraphics()->tdu(pView->getYScrollOffset()) : 0);
	si.nPage = iWindowHeight;
	_setVerticalScrollInfo(&si);
	// Same clamp as the horizontal bar: a print-layout offset can fall
	// past the end of the shorter normal or web layout.
	// A failed read leaves si as requested.
	_getVerticalScrollInfo(&si);
	pView->sendVerticalScrollEvent(pView->getGraphics()->tlu(si.nPos),pView->getGraphics()->tlu(si.nMax-si.nPage));
}

// scroll event came in (probably from an EditMethod (like a PageDown
// or insertData or something).  update the on-screen scrollbar and
// then warp the document window contents.
void AP_Win32FrameImpl::_scrollFuncY(UT_sint32 yoff, UT_sint32 /*ylimit*/)
{
	AV_View * view = getFrame()->getCurrentView();
	GR_Graphics * pGr = view ? view->getGraphics() : nullptr;
	if (!pGr)
		return;

	SCROLLINFO si;
	memset(&si, 0, sizeof(si));
	si.cbSize = sizeof(si);
	si.fMask = SIF_ALL;
	// A failed read must not be written back: SIF_ALL would clear the range.
	if (!_getVerticalScrollInfo(&si))
		return;

	si.nPos = pGr->tdu (yoff);
	_setVerticalScrollInfo(&si);
	if (!_getVerticalScrollInfo(&si))
		return;
	view->setYScrollOffset(pGr->tlu(si.nPos));
}

void AP_Win32FrameImpl::_scrollFuncX(UT_sint32 xoff, UT_sint32 /*xlimit*/)
{
	SCROLLINFO si;
	memset(&si, 0, sizeof(si));

	si.cbSize = sizeof(si);
	si.fMask = SIF_ALL;

	AV_View * view = getFrame()->getCurrentView();
	GR_Graphics * pGr = view ? view->getGraphics() : nullptr;
	if (!pGr)
		return;

	HWND hwndH = _getHwndHScroll();
	if (!GetScrollInfo(hwndH, SB_CTL, &si))
		return;

	si.nPos = pGr->tdu(xoff);
	SetScrollInfo(hwndH, SB_CTL, &si, TRUE);

	if (!GetScrollInfo(hwndH, SB_CTL, &si))
		return;
	view->setXScrollOffset(pGr->tlu(si.nPos));
}

bool AP_Win32FrameImpl::_RegisterClass(XAP_Win32App * app)
{
	// This is a static method, so can't access 'this' or other non-static items directly

	if (!XAP_Win32FrameImpl::_RegisterClass(app))
		return false;

	ATOM a;
    UT_Win32LocaleString str;	

	// register class for the container window (this will contain the document
	// and the rulers and the scroll bars)

    str.fromASCII (app->getApplicationName());	
	_snwprintf(s_ContainerWndClassName, MAXCNTWNDCLSNMSIZE, L"%sContainer", str.c_str());
 	

	a = UT_RegisterClassEx(CS_DBLCLKS | CS_OWNDC, AP_Win32FrameImpl::_ContainerWndProc, app->getInstance(),
						   nullptr, LoadCursor(nullptr, IDC_ARROW), nullptr, nullptr,
						   nullptr, s_ContainerWndClassName);
	
	UT_return_val_if_fail(a, false);

	// register class for the actual document window
	//_snprintf(s_DocumentWndClassName, MAXDOCWNDCLSNMSIZE, "%sDocument", app->getApplicationName());
  	wcscpy (s_DocumentWndClassName, L"Document");
	a = UT_RegisterClassEx(CS_DBLCLKS | CS_OWNDC, AP_Win32FrameImpl::_DocumentWndProc, app->getInstance(),
						   nullptr, nullptr, nullptr, nullptr,
						   nullptr, s_DocumentWndClassName);

	UT_return_val_if_fail(a, false);

	if (!AP_Win32TopRuler::registerClass(app) ||
		!AP_Win32LeftRuler::registerClass(app))
	{
		return false;
	}

	return true;
}

/*****************************************************************/

#define SCROLL_LINE_SIZE 20

void AP_Win32FrameImpl::_getRulerSizes(AP_FrameData * pData, int &yTopRulerHeight, int &xLeftRulerWidth)
{
	UT_return_if_fail(pData);

	if (pData->m_pTopRuler)
		yTopRulerHeight = pData->m_pG->tdu(pData->m_pTopRuler->getHeight());
	else
		yTopRulerHeight = 0;

	if (pData->m_pLeftRuler)
		xLeftRulerWidth = pData->m_pG->tdu(pData->m_pLeftRuler->getWidth());
	else
		xLeftRulerWidth = 0;
}

void AP_Win32FrameImpl::_onSize(AP_FrameData* pData, int nWidth, int nHeight)
{
	UT_return_if_fail(m_hwndDocument);
	UT_return_if_fail(m_hWndVScroll);
	UT_return_if_fail(m_hWndHScroll);
	UT_return_if_fail(m_hWndGripperHack);

	int cyHScroll = GetSystemMetrics(SM_CYHSCROLL);
	const int cxVScroll = GetSystemMetrics(SM_CXVSCROLL);
	int yTopRulerHeight = 0;
	int xLeftRulerWidth = 0;	

	_getRulerSizes(pData, yTopRulerHeight, xLeftRulerWidth);

	MoveWindow(m_hWndHScroll, 0, nHeight-cyHScroll, nWidth - cxVScroll, cyHScroll, FALSE);
	MoveWindow(m_hWndGripperHack, nWidth-cxVScroll, nHeight-cyHScroll, cxVScroll, cyHScroll, FALSE);

	if (IsWindowVisible (m_hWndHScroll) == FALSE)
		cyHScroll = 0;	
	
	MoveWindow(m_hWndVScroll, nWidth-cxVScroll, 0, cxVScroll, nHeight-cyHScroll, FALSE);

	if (m_hwndTopRuler)
	{
		MoveWindow(m_hwndTopRuler, 0, 0, nWidth - cxVScroll, yTopRulerHeight, FALSE);
		InvalidateRect(m_hwndTopRuler, nullptr, TRUE);
	}

	if (m_hwndLeftRuler)
	{
		MoveWindow(m_hwndLeftRuler, 0, yTopRulerHeight,
				   xLeftRulerWidth, nHeight - yTopRulerHeight - cyHScroll, FALSE);
		InvalidateRect(m_hwndLeftRuler, nullptr, TRUE);
	}

	MoveWindow(m_hwndDocument, xLeftRulerWidth, yTopRulerHeight,
			   nWidth - xLeftRulerWidth - cxVScroll,
			   nHeight - yTopRulerHeight - cyHScroll, FALSE);
	InvalidateRect(m_hwndDocument, nullptr, TRUE);
}


void AP_Win32FrameImpl::_setVerticalScrollInfo(const SCROLLINFO * psi)
{
	// SCROLLINFO stores a 32-bit range. Shrinking it to fit HIWORD(wParam)
	// dropped the low bits, so a long document jumped while dragging.
	// https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-scrollinfo
	SetScrollInfo(m_hWndVScroll, SB_CTL, psi, TRUE);
}

bool AP_Win32FrameImpl::_getVerticalScrollInfo(SCROLLINFO * psi)
{
	// GetScrollInfo leaves the structure untouched when it fails.
	return psi && GetScrollInfo(m_hWndVScroll, SB_CTL, psi) != FALSE;
}

int AP_Win32FrameImpl::_getMouseWheelLines()
{
	UINT nScrollLines;
	if (SystemParametersInfoW(	SPI_GETWHEELSCROLLLINES,
								0,
								(PVOID) &nScrollLines,
								0))
	{
		return nScrollLines;
	}

 	return 3;
}

void AP_Win32FrameImpl::_scrollWheel(AV_View * pView, HWND hwnd, bool horizontal, int delta)
{
	if (!pView || !pView->getGraphics() || delta == 0)
		return;

	// A precision touchpad reports fractions of WHEEL_DELTA. Rounding a
	// zero quotient up to one line turns a small downward delta into an
	// upward jump. The unused part stays for the next message.
	int & remainder = horizontal ? m_wheelRemainderH : m_wheelRemainderV;
	remainder += delta;
	const int notches = remainder / WHEEL_DELTA;
	remainder -= notches * WHEEL_DELTA;
	if (notches == 0)
		return;

	const int lines = _getMouseWheelLines();
	if (lines == static_cast<int>(WHEEL_PAGESCROLL))
	{
		const UINT message = horizontal ? WM_HSCROLL : WM_VSCROLL;
		const WORD code = horizontal
			? (notches > 0 ? SB_PAGEDOWN : SB_PAGEUP)
			: (notches > 0 ? SB_PAGEUP : SB_PAGEDOWN);
		int count = notches > 0 ? notches : -notches;
		while (count-- > 0)
			SendMessageW(hwnd, message, MAKELONG(code, 0), 0);
		return;
	}

	const int pixels = notches * lines * SCROLL_LINE_SIZE;
	SCROLLINFO si;
	memset(&si, 0, sizeof(si));
	si.cbSize = sizeof(si);
	si.fMask = SIF_ALL;

	if (horizontal)
	{
		if (!GetScrollInfo(m_hWndHScroll, SB_CTL, &si))
			return;
		int iNewPos = si.nPos + pixels;
		// The thumb stops at nMax - (nPage - 1). A larger nPos is clamped
		// by SetScrollInfo, so the view would otherwise keep the request.
		int limit = si.nMax;
		if (si.nPage > 1 && si.nMax - static_cast<int>(si.nPage) + 1 > si.nMin)
			limit = si.nMax - static_cast<int>(si.nPage) + 1;
		if (iNewPos > limit)
			iNewPos = limit;
		if (iNewPos < si.nMin)
			iNewPos = si.nMin;
		if (iNewPos == si.nPos)
			return;
		si.nPos = iNewPos;
		SetScrollInfo(m_hWndHScroll, SB_CTL, &si, TRUE);
		GetScrollInfo(m_hWndHScroll, SB_CTL, &si);
		pView->sendHorizontalScrollEvent(pView->getGraphics()->tlu(si.nPos));
		return;
	}

	if (!_getVerticalScrollInfo(&si))
		return;
	int iNewPos = si.nPos - pixels;
	int limit = si.nMax;
	if (si.nPage > 1 && si.nMax - static_cast<int>(si.nPage) + 1 > si.nMin)
		limit = si.nMax - static_cast<int>(si.nPage) + 1;
	if (iNewPos > limit)
		iNewPos = limit;
	if (iNewPos < si.nMin)
		iNewPos = si.nMin;
	if (iNewPos == si.nPos)
		return;
	si.nPos = iNewPos;
	_setVerticalScrollInfo(&si);
	_getVerticalScrollInfo(&si);
	pView->sendVerticalScrollEvent(pView->getGraphics()->tlu(si.nPos));
}

/////////////////////////////////////////////////////////////////////////

void AP_Win32FrameImpl::_startTracking(UT_sint32 /*x*/, UT_sint32 y)
{
	m_startMouseWheelY = y;
	m_bMouseWheelTrack = true;

	// GetScrollPos is the compatibility call and returns 0 both at the
	// top of the document and when the bar cannot be read.
	// https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getscrollinfo
	SCROLLINFO si;
	memset(&si, 0, sizeof(si));
	si.cbSize = sizeof(si);
	si.fMask = SIF_POS;
	m_startScrollPosition = 0;
	if (GetScrollInfo(m_hWndVScroll, SB_CTL, &si))
		m_startScrollPosition = si.nPos;

	SetCapture(m_hwndDocument);
}

void AP_Win32FrameImpl::_endTracking(UT_sint32 /*x*/, UT_sint32 /*y*/)
{
	m_bMouseWheelTrack = false;
	ReleaseCapture();
}

void AP_Win32FrameImpl::_track(UT_sint32 /*x*/, UT_sint32 y)
{
	UT_sint32 Delta = y - m_startMouseWheelY;

	// map delta to scroll bar range.

	RECT rect;
	if (!s_clientRect(m_hwndDocument, rect))
		return;

	if(y < rect.top || y > rect.bottom)
		return;

	// A synthetic WM_VSCROLL can only carry 16 bits in HIWORD, and the
	// handler reads nTrackPos, which that message does not update.
	// https://learn.microsoft.com/en-us/windows/win32/controls/wm-vscroll
	SCROLLINFO si;
	memset(&si, 0, sizeof(si));
	si.cbSize = sizeof(si);
	si.fMask = SIF_RANGE | SIF_POS | SIF_PAGE;
	if (!GetScrollInfo(m_hWndVScroll, SB_CTL, &si))
		return;

	// The thumb cannot pass nMax - (nPage - 1). SetScrollInfo clamps a
	// larger value, so the view must read the position Windows kept.
	int limit = si.nMax;
	if (si.nPage > 1 && si.nMax - static_cast<int>(si.nPage) + 1 > si.nMin)
		limit = si.nMax - static_cast<int>(si.nPage) + 1;

	int iNewPosition;
	if(Delta < 0)
	{
		const int span = m_startMouseWheelY - rect.top;
		if (span <= 0)
			return;
		iNewPosition = static_cast<int>(si.nMin
			+ (static_cast<long long>(m_startScrollPosition) - si.nMin)
			* (y - rect.top) / span);
	}
	else
	{
		const int span = rect.bottom - m_startMouseWheelY;
		if (span <= 0)
			return;
		iNewPosition = static_cast<int>(m_startScrollPosition
			+ (static_cast<long long>(limit) - m_startScrollPosition)
			* (y - m_startMouseWheelY) / span);
	}
	if (iNewPosition < si.nMin)
		iNewPosition = si.nMin;
	if (iNewPosition > limit)
		iNewPosition = limit;

	si.fMask = SIF_POS;
	si.nPos = iNewPosition;
	SetScrollInfo(m_hWndVScroll, SB_CTL, &si, TRUE);
	si.fMask = SIF_ALL;
	if (!GetScrollInfo(m_hWndVScroll, SB_CTL, &si))
		si.nPos = iNewPosition;
	AV_View * pView = getFrame()->getCurrentView();
	if (pView && pView->getGraphics())
		pView->sendVerticalScrollEvent(pView->getGraphics()->tlu(si.nPos));

}

/*****************************************************************/

LRESULT CALLBACK AP_Win32FrameImpl::_ContainerWndProc(HWND hwnd, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
	AP_Win32Frame * f = reinterpret_cast<AP_Win32Frame *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
	if (!f)
	{
		return UT_DefWindowProc(hwnd, iMsg, wParam, lParam);
	}

	AP_Win32FrameImpl *fImpl = f->getAPWin32FrameImpl();
	UT_return_val_if_fail(fImpl, UT_DefWindowProc(hwnd, iMsg, wParam, lParam));

	AV_View* pView = f->getCurrentView();
	//UT_ASSERT(pView);		/* only fatal for some messages */
	UT_return_val_if_fail((pView && ((iMsg == WM_VSCROLL) || (iMsg == WM_HSCROLL))) || ((iMsg != WM_VSCROLL) && (iMsg != WM_HSCROLL)), 0);

	switch (iMsg)
	{
		case WM_SETFOCUS:
		{
			SetFocus(fImpl->getHwndDocument());
			return 0;
		}

		case WM_SIZE:
		{
			const int nWidth = LOWORD(lParam);
			const int nHeight = HIWORD(lParam);

			fImpl->_onSize(static_cast<AP_FrameData *>(f->getFrameData()), nWidth, nHeight);
			return 0;
		}

		case WM_VSCROLL:
		{
			int nScrollCode = (int) LOWORD(wParam); // scroll bar value

			SCROLLINFO si;
			memset(&si, 0, sizeof(si));

			si.cbSize = sizeof(si);
			si.fMask = SIF_ALL;
			if (!fImpl->_getVerticalScrollInfo(&si))
				return 0;

			switch (nScrollCode)
			{
				default:
					return 0;

				case SB_PAGEUP:
					si.nPos -= si.nPage;
					if (si.nPos < 0)
					si.nPos = 0;
					break;
				case SB_PAGEDOWN:
					si.nPos += si.nPage;
					break;
				case SB_LINEDOWN:
					si.nPos += SCROLL_LINE_SIZE;
					break;
				case SB_LINEUP:
					si.nPos -= SCROLL_LINE_SIZE;
					if (si.nPos < 0)
						si.nPos = 0;
					break;
				case SB_THUMBPOSITION:
				case SB_THUMBTRACK:
					// HIWORD(wParam) is only 16 bits. nTrackPos is the full position.
					// https://learn.microsoft.com/en-us/windows/win32/controls/wm-vscroll
					si.nPos = si.nTrackPos;
					break;
			}

			fImpl->_setVerticalScrollInfo(&si);				// notify window of new value.
			if (!fImpl->_getVerticalScrollInfo(&si))
				return 0;
			pView->sendVerticalScrollEvent(pView->getGraphics()->tlu(si.nPos));	// now tell the view

			return 0;
		}

		case WM_HSCROLL:
		{
			int nScrollCode = (int) LOWORD(wParam); // scroll bar value

			SCROLLINFO si;
			memset(&si, 0, sizeof(si));

			si.cbSize = sizeof(si);
			si.fMask = SIF_ALL;
			if (!GetScrollInfo(fImpl->m_hWndHScroll, SB_CTL, &si))
				return 0;

			switch (nScrollCode)
			{
				case SB_PAGEUP:
					si.nPos -= si.nPage;
					if (si.nPos < 0)
					{
						si.nPos = 0;
					}
					SetScrollInfo(fImpl->m_hWndHScroll, SB_CTL, &si, TRUE);
					break;
				case SB_PAGEDOWN:
					si.nPos += si.nPage;
					SetScrollInfo(fImpl->m_hWndHScroll, SB_CTL, &si, TRUE);
					break;
				case SB_LINEDOWN:
					si.nPos += SCROLL_LINE_SIZE;
					SetScrollInfo(fImpl->m_hWndHScroll, SB_CTL, &si, TRUE);
					break;
				case SB_LINEUP:
					si.nPos -= SCROLL_LINE_SIZE;
					if (si.nPos < 0)
					{
						si.nPos = 0;
					}
					SetScrollInfo(fImpl->m_hWndHScroll, SB_CTL, &si, TRUE);
					break;
				case SB_THUMBPOSITION:
				case SB_THUMBTRACK:
					// HIWORD(wParam) is only 16 bits. nTrackPos is the full position.
					si.nPos = si.nTrackPos;
					SetScrollInfo(fImpl->m_hWndHScroll, SB_CTL, &si, TRUE);
					break;
			}

			if (nScrollCode != SB_ENDSCROLL)
			{
				// SetScrollInfo clamps nPos. Read back the position it kept.
				if (!GetScrollInfo(fImpl->m_hWndHScroll, SB_CTL, &si))
					return 0;

				pView->sendHorizontalScrollEvent(pView->getGraphics()->tlu(si.nPos));
			}

			return 0;
		}

		case WM_SYSCOLORCHANGE:
		{
			SendMessageW(fImpl->m_hwndTopRuler,WM_SYSCOLORCHANGE,0,0);
			SendMessageW(fImpl->m_hwndLeftRuler,WM_SYSCOLORCHANGE,0,0);
			SendMessageW(fImpl->m_hwndDocument,WM_SYSCOLORCHANGE,0,0);
			return UT_DefWindowProc(hwnd, iMsg, wParam, lParam);
		}

	 	case WM_MOUSEWHEEL:
		case WM_MOUSEHWHEEL:
 		{
			if (!pView)
				return 0;

			const int iDelta = static_cast<short>(HIWORD(wParam));
			const bool horizontalTilt = (iMsg == WM_MOUSEHWHEEL);

			if (!horizontalTilt && (wParam & MK_CONTROL))
			{
				EV_Win32Mouse*	pMouse = (EV_Win32Mouse *) fImpl->m_pMouse;
				if (!pMouse || iDelta == 0)
					return 0;

				fImpl->m_wheelRemainderZoom += iDelta;
				const int notches = fImpl->m_wheelRemainderZoom / WHEEL_DELTA;
				fImpl->m_wheelRemainderZoom -= notches * WHEEL_DELTA;
				if (notches == 0)
					return 0;

				const EV_EditMouseButton button = notches > 0 ? EV_EMB_BUTTON4 : EV_EMB_BUTTON5;
				int count = notches > 0 ? notches : -notches;
				while (count-- > 0)
					pMouse->onButtonWheel(pView,hwnd,button,wParam,signedLoWord(static_cast<UT_uint32>(lParam)),signedHiWord(static_cast<UT_uint32>(lParam)));
				return 0;
			}

			// Tilt right scrolls right. Shift plus a vertical wheel scrolls
			// sideways, with the wheel-up direction moving toward the left.
			const bool horizontal = horizontalTilt || ((wParam & MK_SHIFT) != 0);
			const int signedDelta = (horizontal && !horizontalTilt) ? -iDelta : iDelta;
			fImpl->_scrollWheel(pView, hwnd, horizontal, signedDelta);
			return 0;
		}

		default:
			return UT_DefWindowProc(hwnd, iMsg, wParam, lParam);
	}

	return 0; // to silence the compiler
}

static void s_placeImeWindow(HWND hwnd, AV_View * pView)
{
	if (!hwnd || !pView)
		return;
	FV_View * pFV = static_cast<FV_View *>(pView);
	GR_Graphics * pG = pFV->getGraphics();
	if (!pG)
		return;

	typedef HIMC (WINAPI *GetCtx)(HWND);
	typedef BOOL (WINAPI *SetComp)(HIMC, LPCOMPOSITIONFORM);
	typedef BOOL (WINAPI *RelCtx)(HWND, HIMC);
	static bool s_resolved = false;
	static GetCtx getCtx = nullptr;
	static SetComp setComp = nullptr;
	static RelCtx relCtx = nullptr;
	if (!s_resolved)
	{
		s_resolved = true;
		HMODULE imm = GetModuleHandleW(L"imm32.dll");
		if (!imm)
			imm = LoadLibraryW(L"imm32.dll");
		if (imm)
		{
			getCtx = reinterpret_cast<GetCtx>(reinterpret_cast<void *>(GetProcAddress(imm, "ImmGetContext")));
			setComp = reinterpret_cast<SetComp>(reinterpret_cast<void *>(GetProcAddress(imm, "ImmSetCompositionWindow")));
			relCtx = reinterpret_cast<RelCtx>(reinterpret_cast<void *>(GetProcAddress(imm, "ImmReleaseContext")));
		}
	}
	if (!getCtx || !setComp || !relCtx)
		return;

	HIMC himc = getCtx(hwnd);
	if (!himc)
		return;

	COMPOSITIONFORM cf;
	memset(&cf, 0, sizeof(cf));
	cf.dwStyle = CFS_POINT;
	GR_Caret * caret = pG->allCarets()->getBaseCaret();
	UT_sint32 x = 0;
	UT_sint32 y = 0;
	if (caret && caret->getCoords(x, y))
	{
		int cx = 0;
		int cy = 0;
		static_cast<GR_Win32Graphics *>(pG)->layoutToClient(x, y, cx, cy);
		cf.ptCurrentPos.x = cx;
		cf.ptCurrentPos.y = cy;
	}
	setComp(himc, &cf);
	relCtx(hwnd, himc);
}

LRESULT CALLBACK AP_Win32FrameImpl::_DocumentWndProc(HWND hwnd, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
	AP_Win32Frame * f = reinterpret_cast<AP_Win32Frame *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
	if (!f || !IsWindow(hwnd))
	{
		return UT_DefWindowProc(hwnd, iMsg, wParam, lParam);
	}

	AP_Win32FrameImpl *fImpl = static_cast<AP_Win32FrameImpl *>(f->getFrameImpl());
	UT_return_val_if_fail(fImpl, UT_DefWindowProc(hwnd, iMsg, wParam, lParam));

	AV_View* 		pView = f->getCurrentView();
	EV_Win32Mouse*	pMouse = (EV_Win32Mouse *) fImpl->m_pMouse;

	switch (iMsg)
	{
		case WM_MOUSEACTIVATE:
			fImpl->m_bMouseActivateReceived = true;
			return MA_ACTIVATE;

		case WM_MOUSEWHEEL:
		case WM_MOUSEHWHEEL:
		{
			HWND parent = GetParent(hwnd);
			if (parent)
				return SendMessageW(parent, iMsg, wParam, lParam);
			break;
		}

		case WM_SETFOCUS:			
			if (pView)
			{					
				pView->focusChange(AV_FOCUS_HERE);

				if(!fImpl->m_bMouseActivateReceived)
				{
					// HACK:	Capture leaving a tool bar combo.
					// We need to get a mouse down signal.
					// windows is not activated so send a mouse down if the mouse is pressed.

					UT_DEBUGMSG(("%s(%d): Need to set mouse down\n", __FILE__, __LINE__));

					// GetKeyState
				}
			}
			return 0;

		case WM_KILLFOCUS:			
			if (pView)
			{
				pView->focusChange(AV_FOCUS_NONE);			
			}
			return 0;

		case WM_CREATE:
			return 0;

		case WM_SETCURSOR:
		{
			if (!pView)
				break;
			FV_View * pFV = static_cast<FV_View *>(pView);
			GR_Win32Graphics * pGWin32 = static_cast<GR_Win32Graphics *>(pFV->getGraphics());
			if (!pGWin32)
				break;
			pGWin32->handleSetCursorMessage();
			return 1;
		}

		case WM_CAPTURECHANGED:
			if (pMouse && reinterpret_cast<HWND>(lParam) != hwnd)
				pMouse->onCaptureLost(hwnd);
			return 0;

		case WM_LBUTTONDOWN:
			if(GetFocus() != hwnd)
			{
				SetFocus(hwnd);
				pView = f->getCurrentView();
				pMouse = (EV_Win32Mouse *) fImpl->m_pMouse;
			}
			pMouse->onButtonDown(pView,hwnd,EV_EMB_BUTTON1,wParam,signedLoWord(static_cast<UT_uint32>(lParam)),signedHiWord(static_cast<UT_uint32>(lParam)));
			fImpl->m_bMouseActivateReceived = false;
			return 0;

		case WM_MBUTTONDOWN:
			fImpl->_startTracking(signedLoWord(static_cast<UT_uint32>(lParam)), signedHiWord(static_cast<UT_uint32>(lParam)));
			pMouse->onButtonDown(pView,hwnd,EV_EMB_BUTTON2,wParam,signedLoWord(static_cast<UT_uint32>(lParam)),signedHiWord(static_cast<UT_uint32>(lParam)));
			return 0;

		case WM_RBUTTONDOWN:
			pMouse->onButtonDown(pView,hwnd,EV_EMB_BUTTON3,wParam,signedLoWord(static_cast<UT_uint32>(lParam)),signedHiWord(static_cast<UT_uint32>(lParam)));
			return 0;

		case WM_LBUTTONDBLCLK:
			pMouse->onDoubleClick(pView,hwnd,EV_EMB_BUTTON1,wParam,signedLoWord(static_cast<UT_uint32>(lParam)),signedHiWord(static_cast<UT_uint32>(lParam)));
			return 0;

		case WM_MBUTTONDBLCLK:
			pMouse->onDoubleClick(pView,hwnd,EV_EMB_BUTTON2,wParam,signedLoWord(static_cast<UT_uint32>(lParam)),signedHiWord(static_cast<UT_uint32>(lParam)));
			return 0;

		case WM_RBUTTONDBLCLK:
			pMouse->onDoubleClick(pView,hwnd,EV_EMB_BUTTON3,wParam,signedLoWord(static_cast<UT_uint32>(lParam)),signedHiWord(static_cast<UT_uint32>(lParam)));
			return 0;

		case WM_SYSKEYDOWN:
		{
			UT_DEBUGMSG(("WM_SYSKEYDOWN %d  - %d\n",wParam, lParam));
			ev_Win32Keyboard *pWin32Keyboard = static_cast<ev_Win32Keyboard *>(fImpl->m_pKeyboard);

			BYTE	keyboardState[256];

			::GetKeyboardState(keyboardState);

			UT_DEBUGMSG(("ev_Win32Keyboard::_getModifierState VK_LCONTROL: %u, VK_RCONTROL %u, VK_LMENU %u, VK_LMENU %u, VK_CONTROL %u, VK_MENU %u\n",
				(GetKeyState(VK_LCONTROL) & 0x8000), (GetKeyState(VK_RCONTROL) & 0x8000),
				(GetKeyState(VK_LMENU) & 0x8000), (GetKeyState(VK_LMENU) & 0x8000),
				(GetKeyState(VK_CONTROL) & 0x8000), (GetKeyState(VK_MENU) & 0x8000)	));

			UT_DEBUGMSG(("ev_Win32Keyboard::GetKeyboardState VK_LCONTROL: %u, VK_RCONTROL %u, VK_LMENU %u, VK_LMENU %u, VK_CONTROL %u, VK_MENU %u\n",
				(keyboardState[VK_LCONTROL]), (keyboardState[VK_RCONTROL]),
				(keyboardState[VK_LMENU] ), (keyboardState[VK_LMENU]),
				(keyboardState[VK_CONTROL]), (keyboardState[VK_MENU])	));

	    
	     	if (pWin32Keyboard->onKeyDown(pView,hwnd,iMsg,wParam,lParam))
    			return UT_DefWindowProc(hwnd,iMsg,wParam,lParam);
	 		else
 				return false;	    				 	     			
		
		}

		case WM_KEYUP:
		{
			BYTE	keyboardState[256];

			::GetKeyboardState(keyboardState);

			/*
				Win 9.x does not diferenciate between left and right keys, we have to
				do it ourselfs. Win NT based system do.
			*/
			if (wParam == VK_CONTROL)
			{
				keyboardState[(lParam & 0x1000000) ? VK_RCONTROL : VK_LCONTROL] &= ~0x80;
				SetKeyboardState (keyboardState);
			}
			else if (wParam == VK_MENU)
			{						
				keyboardState[(lParam & 0x1000000) ? VK_RMENU : VK_LMENU]  &= ~0x80;
				SetKeyboardState (keyboardState);
			}

			break;
		}
		case WM_KEYDOWN:
		{
			UT_DEBUGMSG(("WM_KEYDOWN %d  - %d\n",wParam, lParam));
			ev_Win32Keyboard *pWin32Keyboard = static_cast<ev_Win32Keyboard *>(fImpl->m_pKeyboard);

				BYTE	keyboardState[256];

				::GetKeyboardState(keyboardState);

				/*
					Win 9.x does not diferenciate between left and right keys, we have to
					do it ourselfs. Win NT based system do.
				*/
				if (wParam == VK_CONTROL)
				{
					keyboardState[(lParam & 0x1000000) ? VK_RCONTROL : VK_LCONTROL] |= 0x80;
					SetKeyboardState (keyboardState);
				}
				else if (wParam == VK_MENU)
				{						
					keyboardState[(lParam & 0x1000000) ? VK_RMENU : VK_LMENU] |= 0x80;
					SetKeyboardState (keyboardState);
				}				

				/*
				UT_DEBUGMSG(("ev_Win32Keyboard::_getModifierState VK_LCONTROL: %u, VK_RCONTROL %u, VK_RMENU %u, VK_LMENU %u, VK_CONTROL %u, VK_MENU %u\n",
					(GetKeyState(VK_LCONTROL) & 0x8000), (GetKeyState(VK_RCONTROL) & 0x8000),
					(GetKeyState(VK_RMENU) & 0x8000), (GetKeyState(VK_LMENU) & 0x8000),
					(GetKeyState(VK_CONTROL) & 0x8000), (GetKeyState(VK_MENU) & 0x8000)	));

				UT_DEBUGMSG(("ev_Win32Keyboard::GetKeyboardState VK_LCONTROL: %u, VK_RCONTROL %u, VK_RMENU %u, VK_LMENU %u, VK_CONTROL %u, VK_MENU %u, sys %u\n",
					(keyboardState[VK_LCONTROL]), (keyboardState[VK_RCONTROL]),
					(keyboardState[VK_RMENU] ), (keyboardState[VK_LMENU]),
					(keyboardState[VK_CONTROL]), (keyboardState[VK_MENU]),
					(lParam & 0x1000000) ));
				*/

	    
		     	if (pWin32Keyboard->onKeyDown(pView,hwnd,iMsg,wParam,lParam))
     				return UT_DefWindowProc(hwnd,iMsg,wParam,lParam);
	 		else
 				return false;	    				 	     			
    		
		}

		case WM_SYSCHAR:
		{
			UT_DEBUGMSG(("WM_SYSCHAR %d  - %d\n",wParam, lParam));
			return UT_DefWindowProc(hwnd,iMsg,wParam,lParam);
		}

		case WM_CHAR:
		{
//            bool is_uni = IsWindowUnicode (hwnd);
			UT_DEBUGMSG(("WM_CHAR %d  - %d\n",wParam, lParam));
			ev_Win32Keyboard *pWin32Keyboard = static_cast<ev_Win32Keyboard *>(fImpl->m_pKeyboard);
	    
			pWin32Keyboard->onChar(pView,hwnd,iMsg,wParam,lParam);		
			return UT_DefWindowProc(hwnd,iMsg,wParam,lParam);
		}

		case WM_UNICHAR:
		{
			UT_DEBUGMSG(("WM_CHAR %d  - %d\n",wParam, lParam));

			// UNICODE_NOCHAR is sent to test if we can handle this message
			if(wParam == UNICODE_NOCHAR)
				return 1;
			
			ev_Win32Keyboard *pWin32Keyboard = static_cast<ev_Win32Keyboard *>(fImpl->m_pKeyboard);
	    
			pWin32Keyboard->onUniChar(pView,hwnd,iMsg,wParam,lParam);		
			return UT_DefWindowProc(hwnd,iMsg,wParam,lParam);
		}

		case WM_IME_STARTCOMPOSITION:
		case WM_IME_COMPOSITION:
			s_placeImeWindow(hwnd, pView);
			break;

		case WM_IME_CHAR:
		{
			ev_Win32Keyboard *pWin32Keyboard = static_cast<ev_Win32Keyboard *>(fImpl->m_pKeyboard);
			if (pWin32Keyboard->onIMEChar(pView,hwnd,iMsg,wParam,lParam))
				return 0;
			return UT_DefWindowProc(hwnd,iMsg,wParam,lParam);
		}

		case WM_MOUSEMOVE:
			if (fImpl->_isTracking())
			{
				fImpl->_track(signedLoWord(static_cast<UT_uint32>(lParam)),signedHiWord(static_cast<UT_uint32>(lParam)));
			}
			else
			{
				pMouse->onButtonMove(pView,hwnd,wParam,signedLoWord(static_cast<UT_uint32>(lParam)),signedHiWord(static_cast<UT_uint32>(lParam)));
			}
			return 0;

		case WM_LBUTTONUP:
			if(GetFocus() != hwnd)	// HACK: get focus back from tool bar combos.
			{
				SetFocus(hwnd);
				pView = f->getCurrentView();
				pMouse = (EV_Win32Mouse *) fImpl->m_pMouse;
				pMouse->onButtonDown(pView,hwnd,EV_EMB_BUTTON1,wParam,signedLoWord(static_cast<UT_uint32>(lParam)),signedHiWord(static_cast<UT_uint32>(lParam)));
			}
			pMouse->onButtonUp(pView,hwnd,EV_EMB_BUTTON1,wParam,signedLoWord(static_cast<UT_uint32>(lParam)),signedHiWord(static_cast<UT_uint32>(lParam)));
			return 0;

		case WM_MBUTTONUP:
			fImpl->_endTracking(signedLoWord(static_cast<UT_uint32>(lParam)), signedHiWord(static_cast<UT_uint32>(lParam)));
			pMouse->onButtonUp(pView,hwnd,EV_EMB_BUTTON2,wParam,signedLoWord(static_cast<UT_uint32>(lParam)),signedHiWord(static_cast<UT_uint32>(lParam)));
			return 0;

		case WM_RBUTTONUP:
			pMouse->onButtonUp(pView,hwnd,EV_EMB_BUTTON3,wParam,signedLoWord(static_cast<UT_uint32>(lParam)),signedHiWord(static_cast<UT_uint32>(lParam)));
			return 0;

		case WM_SIZE:
		{
			if (pView)
			{
				int nWidth = LOWORD(lParam);
				int nHeight = HIWORD(lParam);

				pView->setWindowSize(nWidth, nHeight);
				FV_View * pFVSize = static_cast<FV_View *>(pView);
				if (pFVSize->getViewMode() == VIEW_WEB && pFVSize->getLayout() &&
				    !pFVSize->getLayout()->isLayoutFilling())
					pFVSize->reflowWebPageToWindow();

				// when resizing, set the rulers to their new height/width
				if ((f->getAPFrameData()->m_pTopRuler) && (f->getAPFrameData()->m_pLeftRuler))
				{
					f->getAPFrameData()->m_pTopRuler->setWidth(nWidth + pView->getGraphics()->tdu(f->getAPFrameData()->m_pLeftRuler->getWidth()));
					f->getAPFrameData()->m_pLeftRuler->setHeight(nHeight + pView->getGraphics()->tdu(f->getAPFrameData()->m_pTopRuler->getHeight()));
				} else {
					if (f->getAPFrameData()->m_pTopRuler)
						f->getAPFrameData()->m_pTopRuler->setWidth(nWidth);
					if (f->getAPFrameData()->m_pLeftRuler)
						f->getAPFrameData()->m_pLeftRuler->setHeight(nHeight);
				}

				// may need to scroll to keep everything in sync.
				// the following is necessary to make sure that the
				// window de-scrolls as it gets larger.

				SCROLLINFO si;
				memset(&si, 0, sizeof(si));

				si.cbSize = sizeof(si);
				si.fMask = SIF_ALL;

				if (fImpl->_getVerticalScrollInfo(&si))
					pView->sendVerticalScrollEvent(pView->getGraphics()->tlu(si.nPos),pView->getGraphics()->tlu(si.nMax-si.nPage));

				if (GetScrollInfo(fImpl->m_hWndHScroll, SB_CTL, &si))
					pView->sendHorizontalScrollEvent(pView->getGraphics()->tlu(si.nPos),pView->getGraphics()->tlu(si.nMax-si.nPage));
			}
			return 0;
		}

		case WM_PAINT:
		{
			UT_Win32Trace("paint");
			UT_Win32TraceHeap("paint");
			PAINTSTRUCT ps;
			HDC hdc = BeginPaint(hwnd, &ps);
			UT_Win32TraceF("paint-begin hdc=%p view=%p l=%ld t=%ld r=%ld b=%ld",
						   (void*)hdc, (void*)pView,
						   ps.rcPaint.left, ps.rcPaint.top,
						   ps.rcPaint.right, ps.rcPaint.bottom);
			if (!pView || !hdc)
			{
				UT_Win32Trace("paint-skip");
				EndPaint(hwnd, &ps);
				return 0;
			}

			FV_View * pFV = static_cast<FV_View *>(pView);
			GR_Graphics * pG = pFV->getGraphics();
			if (!pG)
			{
				EndPaint(hwnd, &ps);
				return 0;
			}

			GR_Win32Graphics * pWinG = static_cast<GR_Win32Graphics *>(pG);
			HDC previous = pWinG->swapDC(hdc);
			GR_Painter caretDisablerPainter(pG); // not an elegant way to disable all carets, but it works beautifully - MARCM
			
			UT_Rect r(pG->tlu(ps.rcPaint.left), pG->tlu(ps.rcPaint.top),
					pG->tlu(ps.rcPaint.right-ps.rcPaint.left+1),
					pG->tlu(ps.rcPaint.bottom-ps.rcPaint.top+1));
			UT_Win32Trace("paint-draw");
			pView->drawImmediate(&r);
			UT_Win32Trace("paint-draw-end");
			UT_Win32TraceHeap("paint-draw");
			pWinG->swapDC(previous);

			EndPaint(hwnd, &ps);
			// BeginPaint's clip survives EndPaint on a CS_OWNDC window.
			pWinG->clearScreenClip();
			UT_Win32Trace("paint-done");
			UT_Win32TraceHeap("paint-done");

			return 0;
		}

		case WM_TIMER:
		{
			UT_DebugOnly<TIMERPROC *> pfn = (TIMERPROC *)lParam;
			UT_ASSERT_HARMLESS( (void *)(pfn) == (void *)(Global_Win32TimerProc) );
			Global_Win32TimerProc(hwnd,WM_TIMER,(UINT_PTR)wParam, 0);
			return 0;
		}

		case WM_DESTROY:
			return 0;

		case WM_INPUTLANGCHANGE:	// let the XAP_Win32Frame handle this
			return ::SendMessageW(fImpl->_getTopLevelWindow(), WM_INPUTLANGCHANGE, wParam, lParam);
#ifdef COPY_ON_DEMAND
		case WM_RENDERFORMAT:
			if(static_cast<AP_Win32App *>(XAP_App::getApp())->copyFmtToClipboardOnDemand((UINT)wParam))
				return 0;
			else
				break;
			
		case WM_RENDERALLFORMATS:
			if(static_cast<AP_Win32App *>(XAP_App::getApp())->copyAllFmtsToClipboardOnDemand())
				return 0;
			else
				break;
#endif
	} /* switch (iMsg) */

	return UT_DefWindowProc(hwnd, iMsg, wParam, lParam);
}

UT_RGBColor AP_Win32FrameImpl::getColorSelBackground () const
{
	DWORD bgcolor = GetSysColor(COLOR_HIGHLIGHT);

	unsigned char red   = static_cast<unsigned char>(((bgcolor)      ) & 0xff);
	unsigned char green = static_cast<unsigned char>(((bgcolor) >> 8 ) & 0xff); 
 	unsigned char blue  = static_cast<unsigned char>(((bgcolor) >> 16) & 0xff);
	
	if( !bgcolor )
	{
		red = green = blue = 192;
	}

	return UT_RGBColor( red, green, blue );
}

GR_Win32Graphics *AP_Win32FrameImpl::createDocWndGraphics(void)
{
	HWND doc = getHwndDocument();
	if (!doc)
		return nullptr;
	// CS_OWNDC: this is the class DC. GetDC(nullptr) would be the screen.
	HDC hdc = GetDC(doc);
	if (!hdc)
		return nullptr;
	GR_Win32AllocInfo ai(hdc, doc);
	return (GR_Win32Graphics *)XAP_App::getApp()->newGraphics(ai);
}
