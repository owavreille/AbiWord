/* AbiWord
 * Copyright (C) 1998 AbiSource, Inc.
 *			 (c) 2002 Jordi Mas i Hern�ndez jmas@softcatala.org 
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
#include <stdio.h>
#include "ut_types.h"
#include "ut_assert.h"
#include "ut_debugmsg.h"
#include "ut_Language.h"
#include "ut_Win32OS.h"
#include "ut_unicode.h"
#include "ap_Win32StatusBar.h"
#include "xap_Win32App.h"
#include "ap_Win32Frame.h"
#include "ap_Win32App.h"
#include "ut_Win32LocaleString.h"

static int s_paneWidth(HWND bar, const char * text)
{
	HDC dc = bar ? GetDC(bar) : nullptr;
	int dpi = 96;
	if (dc)
	{
		dpi = GetDeviceCaps(dc, LOGPIXELSX);
		if (dpi < 96)
			dpi = 96;
	}
	int width = UT_status_fallback_width(text ? static_cast<int>(strlen(text)) : 0, dpi);
	if (dc && text && text[0])
	{
		HFONT font = reinterpret_cast<HFONT>(SendMessageW(bar, WM_GETFONT, 0, 0));
		HFONT previous = font ? static_cast<HFONT>(SelectObject(dc, font)) : nullptr;
		UT_Win32LocaleString str;
		str.fromUTF8(text);
		SIZE size;
		memset(&size, 0, sizeof(size));
		const wchar_t * wide = str.c_str();
		if (wide && GetTextExtentPoint32W(dc, wide, static_cast<int>(wcslen(wide)), &size))
			width = UT_status_pane_width(size.cx, dpi);
		if (previous)
			SelectObject(dc, previous);
	}
	if (dc)
		ReleaseDC(bar, dc);
	return width;
}

LRESULT APIENTRY StatusbarWndProc (HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) { 

	AP_Win32StatusBar *pBar = reinterpret_cast<AP_Win32StatusBar *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
	UT_return_val_if_fail (pBar, 0);

    if (uMsg == WM_ERASEBKGND)
	{
		const UT_Win32ThemeColors tc = UT_Win32GetThemeColors();
		if (!tc.highContrast)
			return 1;
	}

    if (uMsg == WM_PAINT)
	{
		UT_Win32Trace("status-paint");
		UT_Win32TraceHeap("status-paint");
		const UT_Win32ThemeColors tc = UT_Win32GetThemeColors();
		if (!tc.highContrast)
		{
			PAINTSTRUCT ps;
			HDC hdc = BeginPaint(hwnd, &ps);
			if (!hdc)
			{
				EndPaint(hwnd, &ps);
				return 0;
			}
			RECT rc = {};
			if (GetClientRect(hwnd, &rc) && rc.right > rc.left && rc.bottom > rc.top)
			{
				const int w = rc.right - rc.left;
				const int h = rc.bottom - rc.top;
				HDC paintDc = hdc;
				HDC memDc = CreateCompatibleDC(hdc);
				HBITMAP memBmp = memDc ? CreateCompatibleBitmap(hdc, w, h) : nullptr;
				HBITMAP oldBmp = (memDc && memBmp) ? static_cast<HBITMAP>(SelectObject(memDc, memBmp)) : nullptr;
				if (memDc && memBmp)
					paintDc = memDc;

				HBRUSH dcBrush = static_cast<HBRUSH>(GetStockObject(DC_BRUSH));
				COLORREF prevBrush = SetDCBrushColor(paintDc, tc.chromeBg);
				FillRect(paintDc, &rc, dcBrush);

				RECT topLine = { rc.left, rc.top, rc.right, rc.top + 1 };
				SetDCBrushColor(paintDc, tc.border);
				FillRect(paintDc, &topLine, dcBrush);

				HFONT font = reinterpret_cast<HFONT>(SendMessageW(hwnd, WM_GETFONT, 0, 0));
				HFONT oldFont = font ? static_cast<HFONT>(SelectObject(paintDc, font)) : nullptr;
				SetBkMode(paintDc, TRANSPARENT);
				SetTextColor(paintDc, tc.textMuted);

				const int dpi = UT_Win32WindowDpi(hwnd);
				const int padX = UT_ui_px_at_dpi(8, dpi);
				const int divInset = UT_ui_px_at_dpi(6, dpi);
				const int nParts = static_cast<int>(SendMessageW(hwnd, SB_GETPARTS, 0, 0));
				const UINT rtlFlag = (pBar->getDir() & SBT_RTLREADING) ? DT_RTLREADING : 0;
				bool prevNonEmpty = false;

				for (int i = 0; i < nParts; ++i)
				{
					RECT partRc = {};
					if (!SendMessageW(hwnd, SB_GETRECT, static_cast<WPARAM>(i), reinterpret_cast<LPARAM>(&partRc)))
						continue;
					const int len = static_cast<int>(LOWORD(SendMessageW(hwnd, SB_GETTEXTLENGTHW, static_cast<WPARAM>(i), 0)));
					if (len <= 0)
						continue;
					wchar_t buf[512];
					buf[0] = 0;
					if (len < 512)
						SendMessageW(hwnd, SB_GETTEXTW, static_cast<WPARAM>(i), reinterpret_cast<LPARAM>(buf));
					if (!buf[0])
						continue;

					if (prevNonEmpty && partRc.left > rc.left)
					{
						RECT divRc = { partRc.left, rc.top + divInset, partRc.left + 1, rc.bottom - divInset };
						if (divRc.bottom > divRc.top)
						{
							SetDCBrushColor(paintDc, tc.border);
							FillRect(paintDc, &divRc, dcBrush);
						}
					}

					RECT textRc = partRc;
					textRc.left += padX;
					textRc.right -= padX;
					textRc.top += 1;
					if (textRc.right > textRc.left)
					{
						DrawTextW(paintDc, buf, -1, &textRc,
							DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX | rtlFlag);
					}
					prevNonEmpty = true;
				}

				if (oldFont)
					SelectObject(paintDc, oldFont);
				SetDCBrushColor(paintDc, prevBrush);

				if (paintDc == memDc)
					BitBlt(hdc, 0, 0, w, h, memDc, 0, 0, SRCCOPY);
				if (oldBmp)
					SelectObject(memDc, oldBmp);
				if (memBmp)
					DeleteObject(memBmp);
				if (memDc)
					DeleteDC(memDc);
			}
			EndPaint(hwnd, &ps);
			return 0;
		}
	}

    if ((uMsg == WM_SIZE || uMsg == SB_SETPARTS) && hwnd) {
	
		// default processing
		LRESULT lresult = CallWindowProcW(pBar->getOrgWndProc(), hwnd, uMsg, wParam, lParam);
		
		// resize the 2nd pane so the panels fill-up the whole statusbar width
		// when the statusbar has been resized.
		
		// stopper when nothing has changed
		RECT rect;
		memset(&rect, 0, sizeof(rect));
		if (!GetWindowRect(hwnd, &rect))
			return lresult;
		
		if (pBar->getPrevWidth() != (rect.right - rect.left)) {
			
			pBar->setPrevWidth(rect.right - rect.left);
			
			// retrieve the number of parts
			int i, width, offset;
			int nParts = SendMessageW(hwnd, SB_GETPARTS, 0, 0);
			// The second pane is resized below by writing index 1.
			// A bar that still has its single default part must not do that.
			if (nParts < 2)
				return lresult;

			int *pArOffsets = new int[nParts];    
			int *pArWidths = new int[nParts];    

			int *o = pArOffsets;
			int *w = pArWidths;

			// retrieve the current offsets
			SendMessageW(hwnd, SB_GETPARTS, nParts, (LPARAM)pArOffsets);

			// determine the individual panel widths and how much space we 
			// have available for the 2nd panel.
			for (i = 0, width = 0; i < nParts; i++) {
				
				if (i == 0) {
					*w = *o;
					width += *w;
					}
				else if (i > 1 && i < nParts - 1) {
					*w = *o - *(o - 1);
					width += *w;
					}
					
				else if (i == nParts - 1) {
					*w = -1;
					*o = -1;
					}
				o++;
				w++;
				}
			
			// adjust and set width.
			width = (rect.right - rect.left - (rect.bottom - rect.top)) - width;
			
			if (width < 32) 
				width = 32;

			w = pArWidths; w++; *w = width;

			// compute new offsets
			o = pArOffsets;
			w = pArWidths;
			
			for (i = 0, offset = 0; i < nParts; i++) {
				offset += *w;
				*o = (i != nParts - 1) ? offset : -1;
				o++;
				w++;
				}
				
			// update
			SendMessageW(hwnd, SB_SETPARTS, nParts, (LPARAM)pArOffsets);
			// Part 0 is the page number. The progress bar belongs in the
			// message pane that follows it, so the page indicator stays visible.
			RECT client;
			memset(&client, 0, sizeof(client));
			if (!GetClientRect(hwnd, &client))
			{
				delete [] pArWidths;
				delete [] pArOffsets;
				return lresult;
			}
			int left = pArOffsets[0];
			int right = (nParts > 1) ? pArOffsets[1] : client.right;
			if (left < 0)
				left = 0;
			if (right < 0)
				right = client.right;
			int barH = client.bottom - client.top;
			if (barH > 6)
				barH -= 4;
			if (right > left + 8 && barH > 0)
				MoveWindow(pBar->getProgressBar(), left + 2, 2, right - left - 4, barH, TRUE);

			delete [] pArWidths;
			delete [] pArOffsets;
			}
			
		return lresult;
		}
 
    return CallWindowProcW(pBar->getOrgWndProc(), hwnd, uMsg, wParam, lParam); 
	} 

/*
	Callback object used to setup the status bar text
*/
class ABI_EXPORT ap_usb_TextListener : public AP_StatusBarFieldListener
{
public:
	ap_usb_TextListener(AP_StatusBarField *pStatusBarField, HWND hWnd,UINT nID,
						const AP_Win32StatusBar * pSB) :
		AP_StatusBarFieldListener(pStatusBarField),
		m_hWnd(hWnd),
		m_nID(nID),
		m_pSB(pSB)
	{UT_ASSERT_HARMLESS( m_pSB );}
	
	virtual void notify() override; 

private:
	HWND	m_hWnd;
	UINT	m_nID;
	const AP_Win32StatusBar * m_pSB;
};

void ap_usb_TextListener::notify()
{
	UT_return_if_fail (m_hWnd && m_pSB);	
	AP_StatusBarField_TextInfo * textInfo = ((AP_StatusBarField_TextInfo *)m_pStatusBarField);
	UT_Win32LocaleString str;	
	str.fromUTF8 (textInfo->getBuf().c_str());	

	SendMessageW(m_hWnd, SB_SETTEXTW, m_nID | SBT_NOBORDERS | m_pSB->getDir(), (LPARAM)  str.c_str());
	
}
/*****************************************************************/
class ap_usb_ProgressListener : public AP_StatusBarFieldListener
{
public:
	ap_usb_ProgressListener(AP_StatusBarField *pStatusBarField, HWND wProgress) : AP_StatusBarFieldListener(pStatusBarField) 
	{ 
		m_ProgressWND = wProgress; 
		m_isMARQUEEState=false;
	}
	virtual void notify() override; 

protected:
	HWND m_ProgressWND;
	bool m_isMARQUEEState;  //true isMARQUEE; false NoMARQUEE
};

void ap_usb_ProgressListener::notify()
{
	UT_return_if_fail (m_ProgressWND);

	AP_StatusBarField_ProgressBar * pProgress = ((AP_StatusBarField_ProgressBar *)m_pStatusBarField);
	const LONG style = GetWindowLong(m_ProgressWND, GWL_STYLE);
	const bool styledMarquee = (style & PBS_MARQUEE) != 0;
	if(pProgress->isDefinate())
	{
		// PBM_SETPOS is ignored while marquee mode is on. Clearing the
		// style is not enough: the timer stops only when PBM_SETMARQUEE
		// is sent with wParam FALSE.
		if(m_isMARQUEEState || styledMarquee)
		{
			if (styledMarquee)
			{
				SetWindowLong(m_ProgressWND, GWL_STYLE, style & ~PBS_MARQUEE);
				// SetWindowLong caches frame and control styles until SetWindowPos.
				SetWindowPos(m_ProgressWND, nullptr, 0, 0, 0, 0,
					SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
			}
			SendMessage(m_ProgressWND, PBM_SETMARQUEE, FALSE, 0);
			m_isMARQUEEState=false;
		}
		double fraction = pProgress->getFraction();
		SendMessage(m_ProgressWND,PBM_SETPOS,fraction*100,0);
	}
	else
	{   //here pulse process bar
		//gtk_progress_bar_pulse(GTK_PROGRESS_BAR(m_wProgress));
		if(!m_isMARQUEEState || !styledMarquee)
		{
			if (!styledMarquee)
			{
				SetWindowLong(m_ProgressWND, GWL_STYLE, style | PBS_MARQUEE);
				SetWindowPos(m_ProgressWND, nullptr, 0, 0, 0, 0,
					SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
			}
			SendMessage(m_ProgressWND,PBM_SETMARQUEE,TRUE,100);
			m_isMARQUEEState=true;
		}				
	}
}


/*****************************************************************/

AP_Win32StatusBar::AP_Win32StatusBar(XAP_Frame * pFrame)
	: AP_StatusBar(pFrame),
	  m_hwndStatusBar(nullptr),
	  m_pOrgStatusbarWndProc(nullptr),
	  m_iPrevWidth(-1),
	  m_iDIR(0)
{
	const XAP_StringSet * pSS = XAP_App::getApp()->getStringSet();
	UT_return_if_fail( pSS );
	
	UT_Language l;
	if(l.getDirFromCode(pSS->getLanguageName()) == UTLANG_RTL)
		m_iDIR = SBT_RTLREADING;

}

AP_Win32StatusBar::~AP_Win32StatusBar(void)
{
	if (m_hwndStatusBar && IsWindow(m_hwndStatusBar))
	{
		if (m_pOrgStatusbarWndProc)
		{
			SetWindowLongPtrW(m_hwndStatusBar, GWLP_WNDPROC,
							  reinterpret_cast<LONG_PTR>(m_pOrgStatusbarWndProc));
			m_pOrgStatusbarWndProc = nullptr;
		}
		SetWindowLongPtrW(m_hwndStatusBar, GWLP_USERDATA, 0);
		DestroyWindow(m_hwndStatusBar);
	}
	m_hwndStatusBar = nullptr;
}

void AP_Win32StatusBar::setView(AV_View * pView)
{	
	AP_StatusBar::setView(pView);
}

/*

*/
HWND AP_Win32StatusBar::createWindow(HWND hwndFrame,
									 UT_uint32 /*left*/, UT_uint32 /*top*/,
									 UT_uint32 /*width*/)
{
	XAP_Win32App * app = static_cast<XAP_Win32App *>(XAP_App::getApp());
	UINT nID = 0;
	
	/*
		The window procedure for the status bar automatically sets the 
		initial size and position of the window, ignoring the values 
		specified in the CreateWindowEx function.	
	
	*/
	m_hwndStatusBar = UT_CreateWindowEx(0, STATUSCLASSNAMEW, nullptr,
										WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
										0, 0, 0, 0,
										hwndFrame, nullptr, app->getInstance(),nullptr);
	if (!m_hwndStatusBar)
		UT_return_val_if_fail (m_hwndStatusBar, nullptr);
	UT_Win32ApplyWindowTheme(m_hwndStatusBar);
	UT_Win32RefreshStatusFont(m_hwndStatusBar);
	SendMessageW(m_hwndStatusBar, SB_SETMINHEIGHT,
		static_cast<WPARAM>(UT_ui_px_at_dpi(26, UT_Win32WindowDpi(m_hwndStatusBar))), 0);
	SendMessageW(m_hwndStatusBar, WM_SIZE, 0, 0);
	// add progress bar and hide it 
	//  set the color and range 
	m_hwndProgressBar= UT_CreateWindowEx(0, PROGRESS_CLASSW, nullptr,
			WS_CHILD,
			0, 0, 0, 0,
			m_hwndStatusBar, nullptr, app->getInstance(),nullptr);
	if (m_hwndProgressBar)
	{
		UT_Win32ApplyWindowTheme(m_hwndProgressBar);
		SendMessage(m_hwndProgressBar,PBM_SETRANGE,0,MAKELONG(0,100));
		ShowWindow(m_hwndProgressBar, SW_HIDE);
	}

	// route messages through our handler first (to size the status panels).
	m_pOrgStatusbarWndProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
		m_hwndStatusBar, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(StatusbarWndProc))
		);
	
	// attach a pointer to the statusbar window to <this> so we can get the 
	// original wndproc and previous window-width
	SetWindowLongPtrW(m_hwndStatusBar, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

	for (UT_sint32 k = 0; k < getFields()->getItemCount(); k++) 
	{
 		AP_StatusBarField * pf = (AP_StatusBarField *)m_vecFields.getNthItem(k);
		UT_ASSERT_HARMLESS(pf); // we should NOT have null elements
		if (pf->getFillMethod() == REPRESENTATIVE_STRING || (pf->getFillMethod() == MAX_POSSIBLE))
		{
		  AP_StatusBarField_TextInfo *pf_TextInfo = static_cast<AP_StatusBarField_TextInfo*>(pf);
		  // Create a Text element	
		  if (pf_TextInfo) 
		  {	
					
			pf->setListener((AP_StatusBarFieldListener *)(new ap_usb_TextListener(pf_TextInfo, m_hwndStatusBar,
																				  nID, this)));
		  }		  
		}
		else if(pf->getFillMethod() == PROGRESS_BAR)
		{
			pf->setListener((AP_StatusBarFieldListener *)(new ap_usb_ProgressListener(pf, m_hwndProgressBar)));
		}
		else 
		{
			UT_ASSERT_HARMLESS(UT_SHOULD_NOT_HAPPEN); // there are no other kinds of elements
		}
				
		nID++;		
	}

	layoutParts();

	return m_hwndStatusBar;
}

void AP_Win32StatusBar::layoutParts(void)
{
	if (!m_hwndStatusBar || !getFields())
		return;
	m_iPrevWidth = -1;
	const int count = getFields()->getItemCount();
	if (count < 0)
		return;
	int * edges = new int[count + 1];
	int edge = 0;
	for (int k = 0; k < count; ++k)
	{
		AP_StatusBarField * pf = static_cast<AP_StatusBarField *>(m_vecFields.getNthItem(k));
		if (pf && (pf->getFillMethod() == REPRESENTATIVE_STRING || pf->getFillMethod() == MAX_POSSIBLE))
		{
			AP_StatusBarField_TextInfo * info = static_cast<AP_StatusBarField_TextInfo *>(pf);
			edge += info ? s_paneWidth(m_hwndStatusBar, info->getRepresentativeString()) : 0;
		}
		edges[k] = edge;
	}
	edges[count] = -1;
	SendMessageW(m_hwndStatusBar, SB_SETPARTS, static_cast<WPARAM>(count + 1), (LPARAM)edges);
	delete [] edges;
}


void AP_Win32StatusBar::show()
{
	ShowWindow(m_hwndStatusBar, SW_SHOW);

	// Defer the screen repaint if we're leaving fullscreen mode
	if (!static_cast<AP_FrameData *>(m_pFrame->getFrameData())->m_bIsFullScreen)
		m_pFrame->queue_resize();
}

void AP_Win32StatusBar::hide()
{
	ShowWindow(m_hwndStatusBar, SW_HIDE);

	// Defer the screen repaint if we're entering fullscreen mode
	if (!static_cast<AP_FrameData *>(m_pFrame->getFrameData())->m_bIsFullScreen)
		m_pFrame->queue_resize();
}


void   AP_Win32StatusBar::showProgressBar(void)
{ 
   ShowWindow(m_hwndProgressBar,SW_SHOW);
}
void   AP_Win32StatusBar::hideProgressBar(void) 
{
   ShowWindow(m_hwndProgressBar,SW_HIDE);
   const LONG style = GetWindowLong(m_hwndProgressBar, GWL_STYLE);
   if (style & PBS_MARQUEE)
   {
	   SetWindowLong(m_hwndProgressBar, GWL_STYLE, style & ~PBS_MARQUEE);
	   SetWindowPos(m_hwndProgressBar, nullptr, 0, 0, 0, 0,
		   SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
   }
   SendMessage(m_hwndProgressBar,PBM_SETMARQUEE,FALSE,0);
   SendMessage(m_hwndProgressBar,PBM_SETPOS,0,0); //hide and set to zero
}
