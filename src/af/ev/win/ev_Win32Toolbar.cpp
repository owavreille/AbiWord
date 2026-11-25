/* -*- mode: C++; tab-width: 4; c-basic-offset: 4; -*- */

/* AbiSource Program Utilities
 * Copyright (C) 1998 AbiSource, Inc.
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

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <windows.h>
#include <commctrl.h>   // includes the common control header
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "ut_assert.h"
#include "ut_debugmsg.h"
#include "ut_vector.h"
#include "ut_Win32OS.h"
#include "ev_Win32Toolbar.h"
#include "xap_Win32App.h"
#include "xap_Frame.h"
#include "xap_Win32FrameImpl.h"
#include "ev_Toolbar_Actions.h"
#include "ev_Toolbar_Layouts.h"
#include "ev_Toolbar_Labels.h"
#include "ev_Toolbar_Control.h"
#include "ev_EditEventMapper.h"
#include "xap_Win32Toolbar_Icons.h"
#include "xap_EncodingManager.h"
#include "ev_Win32Toolbar_ViewListener.h"
#include "xav_View.h"
#include "ut_xml.h"
#include "xap_Prefs.h"
// The following are for the windows font colour dialog hack
#include "ap_Dialog_Id.h"
#include "ap_Dialog_Background.h"
#include "fv_View.h"
#include "pt_PieceTable.h"
#include "ap_Win32Toolbar_FontCombo.h"
#include "ap_FontMenu.h"
#include "ap_Win32Toolbar_StyleCombo.h"
#include "ap_Win32App.h"
#include "ap_Strings.h"
#include "ut_Win32LocaleString.h"
#include <stdio.h>
#include <string.h>


#ifndef TBSTYLE_EX_DRAWDDARROWS
#define TBSTYLE_EX_DRAWDDARROWS 0x00000001
#endif

#ifndef TB_SETEXTENDEDSTYLE
#define TB_SETEXTENDEDSTYLE     (WM_USER + 84)  // For TBSTYLE_EX_*
#endif

#ifndef TB_GETEXTENDEDSTYLE
#define TB_GETEXTENDEDSTYLE     (WM_USER + 85)  // For TBSTYLE_EX_*
#endif

#ifndef TBSTYLE_AUTOSIZE
#define TBSTYLE_AUTOSIZE  0x10
#endif

#ifndef CB_GETTOPINDEX
#define CB_GETTOPINDEX 0x015b
#endif
#ifndef CB_SETTOPINDEX
#define CB_SETTOPINDEX 0x015c
#endif

// Timer id. Windows scrolls the open list onto the current face after
// CBN_DROPDOWN returns, which hides the document faces above that row.
enum { ABI_FONT_SHOW_TOP = 0x51 };

static int s_fontPinTries = 0;

static void pinFontListToTop(HWND combo)
{
	if (!combo)
		return;
	SendMessageW(combo, CB_SETTOPINDEX, 0, 0);
	COMBOBOXINFO info;
	memset(&info, 0, sizeof(info));
	info.cbSize = sizeof(info);
	if (GetComboBoxInfo(combo, &info) && info.hwndList)
		SendMessageW(info.hwndList, LB_SETTOPINDEX, 0, 0);
}

/*****************************************************************/

EV_Win32Toolbar::EV_Win32Toolbar(XAP_Win32App * pWin32App, XAP_Frame * pFrame,
								 const char * szToolbarLayoutName,
								 const char * szToolbarLabelSetName)
:	EV_Toolbar(pWin32App->getEditMethodContainer(),
				szToolbarLayoutName,
				szToolbarLabelSetName),
	m_pWin32App(pWin32App),
	m_pWin32Frame(pFrame),
	m_pViewListener(nullptr),
	m_lid(0),				// view listener id
	m_hwnd(nullptr),
	m_imageList(nullptr),
	m_iconPx(0),
	m_pFontCtrl(nullptr),
	m_fontRefilling(false),
	m_fontDropdownReady(false),
	m_fontPinTop(false)
{
}

EV_Win32Toolbar::~EV_Win32Toolbar(void)
{
	_releaseListener();

	if (m_imageList)
		ImageList_Destroy(static_cast<HIMAGELIST>(m_imageList));
	for (UT_sint32 i = 0; i < m_iconNames.getItemCount(); ++i)
		free(m_iconNames.getNthItem(i));

	if (m_pFontCtrl)
		delete m_pFontCtrl;

	for (UT_sint32 c=0; c < m_vecOrgStylesNames.getItemCount(); c++)	
		delete m_vecOrgStylesNames.getNthItem(c);

}

bool EV_Win32Toolbar::toolbarEvent(XAP_Toolbar_Id id,
									  UT_UCS4Char * pData,
									  UT_uint32 dataLength)
{
	// user selected something from this toolbar.
	// invoke the appropriate function.
	// return true iff handled.

	// we use this for the color dialog hack	
	

	const EV_Toolbar_ActionSet * pToolbarActionSet = m_pWin32App->getToolbarActionSet();
	UT_return_val_if_fail(pToolbarActionSet,false);

	const EV_Toolbar_Action * pAction = pToolbarActionSet->getAction(id);
	if (!pAction)
		return false;

	AV_View * pView = m_pWin32Frame->getCurrentView();

#if 0
	// make sure we ignore presses on "down" group buttons
	if (pAction->getItemType() == EV_TBIT_GroupButton)
	{
		const char * szState = 0;
		EV_Toolbar_ItemState tis = pAction->getToolbarItemState(pView,&szState);

		if (EV_TIS_ShouldBeToggled(tis))
		{
			// if this assert fires, you got a click while the button is down
			// if your widget set won't let you prevent this, handle it here
			UT_ASSERT_HARMLESS(UT_TODO);
			
			// can safely ignore this event
			return true;
		}
	}
#endif 

	// TODO this windows font color dialog is a hack
	// TODO true implementation requires a custom widget on the toolbar
	if (pAction->getItemType() == EV_TBIT_ColorFore || pAction->getItemType() == EV_TBIT_ColorBack)
	{
		UT_UCS4String ucs4color;
		UT_uint32 colorLength = 0;
		XAP_Win32FrameImpl* fimpl = static_cast<XAP_Win32FrameImpl*>(m_pWin32Frame->getFrameImpl());
		//int id = ItemIdFromWmCommand(cmd);

		UT_ASSERT(id== AP_TOOLBAR_ID_COLOR_BACK || id== AP_TOOLBAR_ID_COLOR_FORE);

		pToolbarActionSet = m_pWin32App->getToolbarActionSet();
		UT_return_val_if_fail(pToolbarActionSet,false);

		pAction = pToolbarActionSet->getAction(id);

		if (!pAction)
            return true;

		pView = m_pWin32Frame->getCurrentView();
		
		const char * szMethodName = pAction->getMethodName();
		if (!szMethodName) 
            return true;
		
		const EV_EditMethodContainer * pEMC = m_pWin32App->getEditMethodContainer();
		EV_EditMethod * pEM = pEMC->findEditMethodByName(szMethodName);

		if (id==AP_TOOLBAR_ID_COLOR_BACK)
		{
			if (fimpl->m_sColorBack.empty())
				fimpl->m_sColorBack = "transparent";
			ucs4color = fimpl->m_sColorBack.ucs4_str();
			colorLength =  fimpl->m_sColorBack.size();
		}
		else
		{
			if (fimpl->m_sColorFore.empty())
				fimpl->m_sColorFore = "000000";
			ucs4color = fimpl->m_sColorFore.ucs4_str();
			colorLength =  fimpl->m_sColorFore.size();
		}	

		invokeToolbarMethod(pView,pEM, ucs4color.ucs4_str(),colorLength);
		return true;
	}

	const char * szMethodName = pAction->getMethodName();
	if (!szMethodName)
		return false;
	
	const EV_EditMethodContainer * pEMC = m_pWin32App->getEditMethodContainer();
	UT_return_val_if_fail(pEMC,false);

	EV_EditMethod * pEM = pEMC->findEditMethodByName(szMethodName);
	UT_ASSERT(pEM);						// make sure it's bound to something

	invokeToolbarMethod(pView,pEM,pData,dataLength);
	return true;
}

/*****************************************************************/

// HACK: forward declarations for subclassed controls
#ifdef STRICT   
#define WHICHPROC	WNDPROC
#else   
#define WHICHPROC	FARPROC
#endif

WHICHPROC s_lpfnDefCombo; 
WHICHPROC s_lpfnDefToolBar;


#define COMBO_BUF_LEN 256

// DEFAULT_GUI_FONT is the old bitmap MS Sans Serif. On Windows 11 it sits
// on the wrong baseline next to the font sample.
static int toolbarIconDpi(HWND hwnd)
{
	int iconDpi = 96;
	typedef UINT (WINAPI *GetDpiForWindowFn)(HWND);
	static bool s_resolved = false;
	static GetDpiForWindowFn getDpi = nullptr;
	if (!s_resolved)
	{
		s_resolved = true;
		HMODULE user32 = GetModuleHandleW(L"user32.dll");
		if (user32)
			getDpi = reinterpret_cast<GetDpiForWindowFn>(
				reinterpret_cast<void *>(GetProcAddress(user32, "GetDpiForWindow")));
	}
	if (getDpi && hwnd)
	{
		UINT got = getDpi(hwnd);
		if (got >= 96)
			iconDpi = (int)got;
	}
	if (iconDpi == 96)
	{
		HDC screen = GetDC(hwnd);
		if (screen)
		{
			int caps = GetDeviceCaps(screen, LOGPIXELSX);
			if (caps >= 96)
				iconDpi = caps;
			ReleaseDC(hwnd, screen);
		}
	}
	if (iconDpi < 96)
		iconDpi = 96;
	return iconDpi;
}

static int toolbarIconPx(HWND hwnd)
{
	int iconPx = (24 * toolbarIconDpi(hwnd) + 48) / 96;
	if (iconPx < 24)
		iconPx = 24;
	if (iconPx > 64)
		iconPx = 64;
	return iconPx;
}

static HFONT toolbarUIFont(int dpiOverride = 0)
{
	static HFONT font = nullptr;
	static int dpi = -1;
	int now = (dpiOverride >= 96) ? dpiOverride : toolbarIconDpi(nullptr);
	if (font && dpi == now)
		return font;

	NONCLIENTMETRICSW metrics;
	memset(&metrics, 0, sizeof(metrics));
	metrics.cbSize = sizeof(metrics);
	HFONT created = nullptr;
	if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0))
		created = CreateFontIndirectW(&metrics.lfMessageFont);
	if (!created)
		created = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
	if (created)
	{
		font = created;
		dpi = now;
	}
	return font ? font : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
}

static int centeredTextY(HDC hdc, const RECT & rc)
{
	TEXTMETRICW tm;
	if (!GetTextMetricsW(hdc, &tm))
		return rc.top;
	int y = rc.top + ((rc.bottom - rc.top) - tm.tmHeight) / 2;
	return (y < rc.top) ? rc.top : y;
}



LRESULT CALLBACK EV_Win32Toolbar::_ToolBarWndProc (HWND hWnd, UINT uMessage, WPARAM wParam, LPARAM lParam)
{
	// CBN_DROPDOWN is sent to the combo's parent. The combo procedure only
	// sees the list box, and LBN_SELCHANGE has the same code as CBN_SELCHANGE,
	// so the old handler never rebuilt the font list when it opened.
	if (uMessage == WM_COMMAND && lParam)
	{
		const UINT code = HIWORD(wParam);
		if (code == CBN_DROPDOWN || code == CBN_CLOSEUP)
		{
			EV_Win32Toolbar * bar = reinterpret_cast<EV_Win32Toolbar *>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
			if (bar && bar->ItemIdFromWmCommand(LOWORD(wParam)) == AP_TOOLBAR_ID_FMT_FONT)
			{
				if (code == CBN_DROPDOWN)
					bar->prepareFontDropdown(reinterpret_cast<HWND>(lParam));
				else
				{
					KillTimer(reinterpret_cast<HWND>(lParam), ABI_FONT_SHOW_TOP);
					bar->m_fontDropdownReady = false;
					bar->m_fontPinTop = false;
				}
			}
		}
	}

	// Windows delivers TTN_NEEDTEXT to the toolbar, the tooltip's parent.
	// Linux stores the tip on the widget. Leaving lpszText null crashes
	// comctl32 on Windows 11 a moment after the window appears.
	if (uMessage == WM_PAINT)
	{
		UT_Win32Trace("toolbar-paint");
		UT_Win32TraceHeap("toolbar-paint");
	}

	if (uMessage == WM_NOTIFY && lParam)
	{
		UINT code = reinterpret_cast<NMHDR *>(lParam)->code;
		if (code == TTN_NEEDTEXTW || code == TTN_NEEDTEXTA)
		{
			EV_Win32Toolbar * t = reinterpret_cast<EV_Win32Toolbar *>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
			if (t)
				t->getToolTip(lParam);
			return 0;
		}
	}

	// An owner-drawn combo asks its parent to paint each row. The
	// handler lives on the combo, so the font list stayed black.
	if (uMessage == WM_DRAWITEM && lParam)
	{
		const DRAWITEMSTRUCT * dis = reinterpret_cast<const DRAWITEMSTRUCT *>(lParam);
		if (dis->CtlType == ODT_COMBOBOX && dis->hwndItem)
			return _ComboWndProc(dis->hwndItem, uMessage, wParam, lParam);
	}

	if (uMessage == WM_CTLCOLORSTATIC) {
		if(SetBkColor((HDC) wParam, GetSysColor(COLOR_WINDOW)) == CLR_INVALID)
		{
			UT_ASSERT_HARMLESS(UT_SHOULD_NOT_HAPPEN);
		}
		if(SetTextColor((HDC) wParam, GetSysColor(COLOR_WINDOWTEXT)) == CLR_INVALID)
		{
			UT_ASSERT_HARMLESS(UT_SHOULD_NOT_HAPPEN);
		}
		return (LRESULT) GetSysColorBrush (COLOR_WINDOW);
	}

	if (uMessage == 0x031A) // WM_THEMECHANGED
	{
		LRESULT res = CallWindowProcW(s_lpfnDefToolBar, hWnd, uMessage, wParam, lParam);
		EV_Win32Toolbar * t = reinterpret_cast<EV_Win32Toolbar *>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
		if (t)
		{
			SendMessageW(hWnd, TB_AUTOSIZE, 0, 0);
			t->_placeControls();
		}
		return res;
	}
	
	return (CallWindowProcW(s_lpfnDefToolBar, hWnd, uMessage, wParam, lParam));
}

static HFONT s_fontSample(const wchar_t * face, int sampleDpi)
{
	// Bound the cache: opening the font list once can touch hundreds of
	// faces, and each HFONT lives until process exit without a cap.
	static const size_t kMaxFontSamples = 64;
	static std::map<std::wstring, HFONT> cache;
	if (!face || !face[0])
		return nullptr;

	if (sampleDpi < 96)
		sampleDpi = 96;

	wchar_t dpiSuffix[16];
	swprintf(dpiSuffix, 16, L"@%d", sampleDpi);
	const std::wstring key = std::wstring(face) + dpiSuffix;
	std::map<std::wstring, HFONT>::iterator found = cache.find(key);
	if (found != cache.end())
		return found->second;

	LOGFONTW lf;
	memset(&lf, 0, sizeof(lf));
	lf.lfHeight = -MulDiv(12, sampleDpi, 96);
	lf.lfWeight = FW_NORMAL;
	lf.lfCharSet = DEFAULT_CHARSET;
	lf.lfOutPrecision = OUT_DEFAULT_PRECIS;
	lf.lfClipPrecision = CLIP_DEFAULT_PRECIS;
	lf.lfQuality = DEFAULT_QUALITY;
	lf.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
	lstrcpynW(lf.lfFaceName, face, LF_FACESIZE);
	HFONT font = CreateFontIndirectW(&lf);
	if (!font)
		return nullptr;

	if (cache.size() >= kMaxFontSamples)
	{
		std::map<std::wstring, HFONT>::iterator drop = cache.begin();
		if (drop->second)
			DeleteObject(drop->second);
		cache.erase(drop);
	}
	cache.insert(std::make_pair(key, font));
	return font;
}

LRESULT CALLBACK EV_Win32Toolbar::_ComboWndProc( HWND hWnd, UINT uMessage, WPARAM wParam, LPARAM lParam)
{
	switch (uMessage)
	{
		/* Draws the font preview in the font selection combobox*/
		case WM_DRAWITEM:
		{
			UT_Win32Trace("combo-draw");
			UT_Win32TraceHeap("combo-draw");
			EV_Win32Toolbar * t = (EV_Win32Toolbar *) GetWindowLongPtrW(hWnd, GWLP_USERDATA);
			if (!t)
				return TRUE;
			UINT u = GetDlgCtrlID(hWnd);
			XAP_Toolbar_Id id = t->ItemIdFromWmCommand(u);
						
			if (id!=AP_TOOLBAR_ID_FMT_FONT)	/* Only owner draw the font selection*/
			{
				UT_ASSERT_HARMLESS(0);
				break;
			}
								
			DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lParam;
			if (!dis || dis->itemID == static_cast<UINT>(-1) ||
			    static_cast<UT_sint32>(dis->itemData) < 0)
			{
				return TRUE;
			}
			
			XAP_Toolbar_ControlFactory * pFactory = t->m_pWin32App->getControlFactory();			

			if (!t->m_pFontCtrl)
			{	
				EV_Toolbar_Control * pControl = pFactory->getControl(t, AP_TOOLBAR_ID_FMT_FONT);
				t->m_pFontCtrl = static_cast<AP_Win32Toolbar_FontCombo *>(pControl);
				if (!t->m_pFontCtrl)
					return TRUE;
				t->m_pFontCtrl->populate();
			}

			HFONT hFont, hUIFont;
			SIZE size;
			HFONT hfontSave;
            UT_Win32LocaleString str;
			const UT_GenericVector<const char*> * v = t->m_pFontCtrl->getContents();
			const char * fontName = (v != nullptr) ? v->getNthItem(static_cast<UT_sint32>(dis->itemData)) : nullptr;
			if (!fontName)
				return TRUE;
			if (AP_fontMenuIsSeparator(fontName))
			{
				FillRect(dis->hDC, &dis->rcItem, GetSysColorBrush(COLOR_WINDOW));
				if (!(dis->itemState & ODS_COMBOBOXEDIT))
				{
					HPEN pen = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_3DSHADOW));
					HPEN old = pen ? (HPEN)SelectObject(dis->hDC, pen) : nullptr;
					int y = (dis->rcItem.top + dis->rcItem.bottom) / 2;
					MoveToEx(dis->hDC, dis->rcItem.left + 4, y, nullptr);
					LineTo(dis->hDC, dis->rcItem.right - 4, y);
					if (old)
						SelectObject(dis->hDC, old);
					if (pen)
						DeleteObject(pen);
				}
				return TRUE;
			}
			str.fromUTF8 (fontName);

			int drawDpi = GetDeviceCaps(dis->hDC, LOGPIXELSY);
			if (drawDpi < 96)
				drawDpi = 96;
			hUIFont = toolbarUIFont(drawDpi);
			if(dis->itemState & ODS_COMBOBOXEDIT)
			{
				if (dis->itemState & ODS_SELECTED)
				{
					SetTextColor(dis->hDC, GetSysColor(COLOR_HIGHLIGHTTEXT));
					SetBkColor(dis->hDC, GetSysColor(COLOR_HIGHLIGHT));
					FillRect(dis->hDC, &dis->rcItem, GetSysColorBrush(COLOR_HIGHLIGHT));
				}
				else
				{
					SetTextColor(dis->hDC, GetSysColor(COLOR_WINDOWTEXT));
					SetBkColor(dis->hDC, GetSysColor(COLOR_WINDOW));
					FillRect(dis->hDC, &dis->rcItem, GetSysColorBrush(COLOR_WINDOW));
				}
				hfontSave = (HFONT) SelectObject (dis->hDC, hUIFont);
				ExtTextOutW(dis->hDC, dis->rcItem.left + 2, centeredTextY(dis->hDC, dis->rcItem),
							ETO_CLIPPED, &dis->rcItem, str.c_str(), (UINT)str.size(), nullptr);
				SelectObject (dis->hDC, hfontSave);
			}
			else
			{
				// One HFONT per face. Creating and deleting it on every
				// WM_DRAWITEM exhausts the GDI handle table while the
				// dropdown is open.
				hFont = s_fontSample(str.c_str(), drawDpi);
				const bool documentFace = t->m_pFontCtrl &&
					t->m_pFontCtrl->isDocumentFontRow(static_cast<UT_sint32>(dis->itemData));

				if(dis->itemState & ODS_SELECTED) /* HighLight the selected text*/
				{
					SetTextColor(dis->hDC, GetSysColor(COLOR_HIGHLIGHTTEXT));
					SetBkColor(dis->hDC, GetSysColor(COLOR_HIGHLIGHT));
					FillRect(dis->hDC, &dis->rcItem, GetSysColorBrush(COLOR_HIGHLIGHT));
				}
				else
				{
					SetTextColor(dis->hDC, GetSysColor(COLOR_WINDOWTEXT));
					SetBkColor(dis->hDC, GetSysColor(COLOR_WINDOW));
					FillRect(dis->hDC, &dis->rcItem, GetSysColorBrush(COLOR_WINDOW) );
				}
				if (documentFace && !(dis->itemState & ODS_SELECTED))
				{
					RECT bar = dis->rcItem;
					bar.right = bar.left + 3;
					FillRect(dis->hDC, &bar, GetSysColorBrush(COLOR_HIGHLIGHT));
				}

				/* The name stays in the interface font. The sample follows it
				   and must not repaint the whole row, or the name disappears. */
				hfontSave = (HFONT) SelectObject (dis->hDC, hUIFont);
				memset(&size, 0, sizeof(size));
				if (!GetTextExtentPoint32W(dis->hDC, str.c_str(), (int)str.size(), &size))
				{
					size.cx = 0;
					size.cy = 0;
				}

				RECT nameRect = dis->rcItem;
				nameRect.left += documentFace ? 8 : 2;
				nameRect.right = nameRect.left + size.cx + 8;
				if (nameRect.right > dis->rcItem.right)
					nameRect.right = dis->rcItem.right;
				ExtTextOutW(dis->hDC, nameRect.left, centeredTextY(dis->hDC, dis->rcItem),
							ETO_CLIPPED, &nameRect, str.c_str(), (UINT)str.size(), nullptr);

				RECT sampleRect = dis->rcItem;
				sampleRect.left = nameRect.right;
				if (hFont && sampleRect.left + 4 < sampleRect.right)
				{
					const wchar_t* szSample = L"AbCdEfGhIj";
					SelectObject (dis->hDC, hFont);
					ExtTextOutW(dis->hDC, sampleRect.left, centeredTextY(dis->hDC, dis->rcItem),
								ETO_CLIPPED, &sampleRect, szSample, (UINT)wcslen(szSample), nullptr);
				}
				SelectObject (dis->hDC, hfontSave);
			}

			return TRUE;
		}
		
		case WM_MOUSEMOVE:
		case WM_LBUTTONDOWN:
		case WM_LBUTTONUP:
		{
			// relay mouse messages from the combo box to get tool tips to work
			// TODO: similar relay needed from edit control, too??
			// ALT:  tell tooltip control to subclass for these events
			// TTM_RELAYEVENT reads hwnd, message, wParam, lParam, time and pt.
			// https://learn.microsoft.com/en-us/windows/win32/controls/ttm-relayevent
			MSG msg;
			memset(&msg, 0, sizeof(msg));
			msg.hwnd = hWnd;
			msg.message = uMessage;
			msg.wParam = wParam;
			msg.lParam = lParam;
			msg.time = GetMessageTime();
			const DWORD pos = GetMessagePos();
			msg.pt.x = static_cast<SHORT>(LOWORD(pos));
			msg.pt.y = static_cast<SHORT>(HIWORD(pos));

			HWND hwndToolbar = GetParent(hWnd);
			HWND hWndTT = hwndToolbar ? (HWND)SendMessageW(hwndToolbar, TB_GETTOOLTIPS, 0, 0) : nullptr;
			if (hWndTT)
				SendMessageW(hWndTT, TTM_RELAYEVENT, 0, (LPARAM)(LPMSG)&msg);
			break;
		}

		case WM_TIMER:
		{
			if (wParam != ABI_FONT_SHOW_TOP)
				break;
			EV_Win32Toolbar * t = (EV_Win32Toolbar *) GetWindowLongPtrW(hWnd, GWLP_USERDATA);
			if (!t || !t->m_fontPinTop)
			{
				KillTimer(hWnd, ABI_FONT_SHOW_TOP);
				return 0;
			}
			if (!SendMessageW(hWnd, CB_GETDROPPEDSTATE, 0, 0))
			{
				// Rebuilding the list during CBN_DROPDOWN can cancel the
				// popup. Open it once with the new rows, then give up.
				if (s_fontPinTries == 0)
				{
					s_fontPinTries = 1;
					SendMessageW(hWnd, CB_SHOWDROPDOWN, TRUE, 0);
				}
				else if (++s_fontPinTries > 10)
				{
					s_fontPinTries = 0;
					KillTimer(hWnd, ABI_FONT_SHOW_TOP);
					t->m_fontPinTop = false;
					t->m_fontDropdownReady = false;
				}
				return 0;
			}
			s_fontPinTries = 0;
			KillTimer(hWnd, ABI_FONT_SHOW_TOP);
			t->m_fontPinTop = false;
			pinFontListToTop(hWnd);
			return 0;
		}

		case WM_COMMAND:
		{
			switch (HIWORD(wParam))
			{								
				case CBN_DROPDOWN:
				{
					EV_Win32Toolbar * t = (EV_Win32Toolbar *) GetWindowLongPtrW(hWnd, GWLP_USERDATA);
					if (t)
					{
						UINT u = GetDlgCtrlID(hWnd);
						if (t->ItemIdFromWmCommand(u) == AP_TOOLBAR_ID_FMT_FONT)
							t->prepareFontDropdown(hWnd);
					}
					break;
				}

				case CBN_CLOSEUP:
				{
					KillTimer(hWnd, ABI_FONT_SHOW_TOP);
					EV_Win32Toolbar * t = (EV_Win32Toolbar *) GetWindowLongPtrW(hWnd, GWLP_USERDATA);
					if (t)
					{
						t->m_fontDropdownReady = false;
						t->m_fontPinTop = false;
					}
					break;
				}

				case CBN_SELCHANGE:
				{
					EV_Win32Toolbar * drop = (EV_Win32Toolbar *) GetWindowLongPtrW(hWnd, GWLP_USERDATA);
					if (drop && drop->m_fontRefilling)
						break;

					UT_sint32 iSelected = SendMessageW(hWnd, CB_GETCURSEL, 0, 0);
									
					if(iSelected != -1)
					{
						static UT_UCS4Char ucs_buf[COMBO_BUF_LEN];
						UT_Win32LocaleString str;
						int textChars = static_cast<int>(SendMessageW(hWnd, CB_GETLBTEXTLEN, iSelected, 0));
						wchar_t * buf = nullptr;
						if (textChars >= 0)
							buf = static_cast<wchar_t *>(g_try_malloc((static_cast<size_t>(textChars) + 1) * sizeof(wchar_t)));
						LRESULT got = CB_ERR;
						if (buf)
							got = SendMessageW(hWnd, CB_GETLBTEXT, iSelected, (LPARAM)buf);

						EV_Win32Toolbar * t = (EV_Win32Toolbar *) GetWindowLongPtrW(hWnd, GWLP_USERDATA);
						UT_ASSERT(t);

						if (buf && got != CB_ERR && got > 0 && t)
						{
							str.fromLocale(buf);
							if (str.size() < COMBO_BUF_LEN)
							{
								UT_UCS4_strcpy_utf8_char(ucs_buf, str.utf8_str().utf8_str());
								UT_UCS4Char * pData = ucs_buf;
								UT_uint32 dataLength = static_cast<UT_uint32>(got);

								UINT u = GetDlgCtrlID(hWnd);
								XAP_Toolbar_Id id = t->ItemIdFromWmCommand(u);

								if (id == AP_TOOLBAR_ID_FMT_FONT && t->m_pFontCtrl)
								{
									int nData = static_cast<int>(SendMessageW(hWnd, CB_GETITEMDATA, iSelected, 0));
									const char * chosen = (nData >= 0) ? t->m_pFontCtrl->getNthItem(static_cast<UT_uint32>(nData)) : nullptr;
									if (AP_fontMenuIsSeparator(chosen))
									{
										dataLength = 0;
										t->m_fontRefilling = true;
										t->_refreshID(id);
										t->m_fontRefilling = false;
									}
								}

								// If is a STYLE_NAME we should pass the internal one, not the localised
								if (id==AP_TOOLBAR_ID_FMT_STYLE)
								{
									int nData = static_cast<int>(SendMessageW(hWnd, CB_GETITEMDATA, iSelected, 0));

									XAP_Toolbar_ControlFactory * pFactory = t->m_pWin32App->getControlFactory();
									EV_Toolbar_Control * pControl = pFactory->getControl(t, AP_TOOLBAR_ID_FMT_STYLE);
									AP_Win32Toolbar_StyleCombo * pStyleC = static_cast<AP_Win32Toolbar_StyleCombo *>(pControl);
									pStyleC->repopulate();

									UT_UTF8String *utf = nullptr;
									if (nData >= 0 && nData < t->m_vecOrgStylesNames.getItemCount())
										utf = t->m_vecOrgStylesNames.getNthItem(nData);
									if (utf && utf->length() < (COMBO_BUF_LEN - 1))
									{
										UT_UCS4_strcpy_utf8_char(ucs_buf, utf->utf8_str());
										dataLength = UT_UCS4_strlen(ucs_buf);
									}
									else
										dataLength = 0;

									DELETEP(pControl);
								}

								if (dataLength)
									t->toolbarEvent(id, pData, dataLength);
							}
						}

						g_free(buf);
						if (t)
							SetFocus(static_cast<XAP_Win32FrameImpl*>(t->m_pWin32Frame->getFrameImpl())->getTopLevelWindow());
					}	
					else
					{
						PostMessageW(hWnd, WM_KEYDOWN, VK_ESCAPE, 0);
					}
						
					break;
				}					
				
							
				break;
				
			} // swich
		} // case command
		break;

		case WM_KEYDOWN:
		{
			EV_Win32Toolbar * t = (EV_Win32Toolbar *) GetWindowLongPtrW(hWnd, GWLP_USERDATA);
			UT_ASSERT(t);

			switch (wParam)
			{
				case VK_ESCAPE:
				{
					// restore combo state
					UINT u = GetDlgCtrlID(hWnd);
					XAP_Toolbar_Id id = t->ItemIdFromWmCommand(u);

					t->_refreshID(id);
				}
				// fall through

				case VK_TAB:
				case VK_RETURN:
				{
					SetFocus(static_cast<XAP_Win32FrameImpl*>(t->m_pWin32Frame->getFrameImpl())->getTopLevelWindow());
					return 0;
				}
			}
		}
	}
	return (CallWindowProcW(s_lpfnDefCombo, hWnd, uMessage, wParam, lParam));
}


/*****************************************************************/

bool EV_Win32Toolbar::synthesize(void)
{
	// create a toolbar from the info provided.

	////////////////////////////////////////////////////////////////
	// get toolbar button appearance from the preferences
	////////////////////////////////////////////////////////////////

	bool bIcons = true;
	bool bText = false;
	std::string value;
	m_pWin32App->getPrefsValue(XAP_PREF_KEY_ToolbarAppearance, value);
	UT_return_val_if_fail(!value.empty(), false);

	if (g_ascii_strcasecmp(value.c_str(), "icon") == 0)
	{
		bIcons = true;
		bText = false;
	}
	else if (g_ascii_strcasecmp(value.c_str(), "text") == 0)
	{
		bIcons = false;
		bText = true;
	}
	else if (g_ascii_strcasecmp(value.c_str(), "both") == 0)
	{
		bIcons = true;
		bText = true;
	}

	const EV_Toolbar_ActionSet * pToolbarActionSet = m_pWin32App->getToolbarActionSet();
	UT_ASSERT(pToolbarActionSet);
	
	UT_uint32 nrLabelItemsInLayout = m_pToolbarLayout->getLayoutItemCount();
	UT_ASSERT(nrLabelItemsInLayout > 0);

	XAP_Toolbar_ControlFactory * pFactory = m_pWin32App->getControlFactory();
	UT_ASSERT(pFactory);

	HWND hwndParent = static_cast<XAP_Win32FrameImpl*>(m_pWin32Frame->getFrameImpl())->getToolbarWindow();
	if(hwndParent == nullptr)
		return false;

	// NOTE: this toolbar will get placed later, by frame or rebar

	m_hwnd = UT_CreateWindowEx(0,                              
							   TOOLBARCLASSNAMEW, // window class name
							   nullptr,			// window caption
							   WS_CHILD | WS_VISIBLE 
							   | WS_CLIPCHILDREN | WS_CLIPSIBLINGS 
							   | TBSTYLE_TOOLTIPS | TBSTYLE_FLAT
							   | ( bText && !bIcons ? TBSTYLE_LIST : 0 )
							   | TBSTYLE_CUSTOMERASE
							   | CCS_NOPARENTALIGN | CCS_NODIVIDER
							   | CCS_NORESIZE
							   ,						// window style
							   0,						// initial x position
							   0,						// initial y position
							   0,						// initial x size
							   0,						// initial y size
							   hwndParent,				// parent window handle
							   nullptr,					// window menu handle
							   m_pWin32App->getInstance(),		// program instance handle
							   nullptr);  // creation parameters
							   					

	UT_ASSERT(m_hwnd);

	// override the window procedure 		
	s_lpfnDefToolBar = (WHICHPROC)GetWindowLongPtrW(m_hwnd, GWLP_WNDPROC);
	SetWindowLongPtrW(m_hwnd, GWLP_WNDPROC, (LONG_PTR)_ToolBarWndProc);
	SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, (LONG_PTR)this);


	SendMessageW(m_hwnd, TB_BUTTONSTRUCTSIZE, (WPARAM) sizeof(TBBUTTON), 0);  

	// the Windows Common Control Toolbar requires that we set
	// a bitmap size in the toolbar window **before** we actually
	// add any of them.  at this point in the code, we haven't
	// loaded any of the bitmaps yet and thus don't know the maximum
	// size.  we could go thru the layout twice and compute the
	// maxium before calling this, but this seems overkill since
	// we know at compile time what all of the bitmaps are....
	//
	// Now bitmaps are cut down to requested size if too large - HB
	
	const int iconPx = toolbarIconPx(hwndParent);
	m_iconPx = bIcons ? iconPx : 0;
	for (UT_sint32 i = m_iconNames.getItemCount() - 1; i >= 0; --i)
	{
		free(m_iconNames.getNthItem(i));
		m_iconNames.deleteNthItem(i);
	}
	if (m_imageList)
	{
		ImageList_Destroy(static_cast<HIMAGELIST>(m_imageList));
		m_imageList = nullptr;
	}

	if( bIcons )
	{
		SendMessageW(m_hwnd, TB_SETBITMAPSIZE, 0,
					(LPARAM) MAKELONG(iconPx, iconPx));
		HIMAGELIST images = ImageList_Create(iconPx, iconPx, ILC_COLOR32, 32, 8);
		if (images)
		{
			m_imageList = images;
			SendMessageW(m_hwnd, TB_SETIMAGELIST, 0, (LPARAM)images);
		}
	}
	else
		SendMessageW(m_hwnd, TB_SETBITMAPSIZE, 0, (LPARAM) MAKELONG(0,0));

	// Room around the 24px glyphs, closer to a Windows 11 command bar.
	SendMessageW(m_hwnd, TB_SETPADDING, 0, MAKELPARAM(6, 4));
	DWORD exStyle = (DWORD)SendMessageW(m_hwnd, TB_GETEXTENDEDSTYLE, 0, 0);
	exStyle |= TBSTYLE_EX_DOUBLEBUFFER | TBSTYLE_EX_HIDECLIPPEDBUTTONS;
	SendMessageW(m_hwnd, TB_SETEXTENDEDSTYLE, 0, exStyle);

	// TODO: is there any advantage to building up all the TBBUTTONs at once
	//		 and then adding them en masse, instead of one at a time? 
	UT_DebugOnly<UINT> last_id = 0;
	bool bControls = false;

	for (UT_uint32 k=0; (k < nrLabelItemsInLayout); k++)
	{
		EV_Toolbar_LayoutItem * pLayoutItem = m_pToolbarLayout->getLayoutItem(k);
		UT_continue_if_fail(pLayoutItem);

		XAP_Toolbar_Id id = pLayoutItem->getToolbarId();
		EV_Toolbar_Action * pAction = pToolbarActionSet->getAction(id);
		UT_ASSERT(pAction);
		EV_Toolbar_Label * pLabel = m_pToolbarLabelSet->getLabel(id);
		UT_ASSERT(pLabel);

		UINT u = WmCommandFromItemId(id);

		TBBUTTON tbb;
		memset(&tbb, 0, sizeof(tbb));
		
		switch (pLayoutItem->getToolbarLayoutFlags())
		{
		case EV_TLF_Normal:
			{
				tbb.idCommand = u;
				tbb.dwData = 0;
				
				last_id = u;

				bool bButton = false;

				switch (pAction->getItemType())
				{
					/* The button applies the current colour. The arrow
					   opens the swatch popup from TBN_DROPDOWN. */
				case EV_TBIT_ColorFore:
				case EV_TBIT_ColorBack:
					/* fall through */

				case EV_TBIT_PushButton:
					bButton = true;
					tbb.fsState = TBSTATE_ENABLED; 
					tbb.fsStyle = TBSTYLE_BUTTON;     
					break;

				case EV_TBIT_ToggleButton:
					bButton = true;
					tbb.fsState = TBSTATE_ENABLED; 
					tbb.fsStyle = TBSTYLE_CHECK;     
					break;

				case EV_TBIT_GroupButton:
					bButton = true;
					tbb.fsState = TBSTATE_ENABLED; 
					tbb.fsStyle = TBSTYLE_CHECKGROUP;     
					break;

				case EV_TBIT_ComboBox:
					{
						EV_Toolbar_Control * pControl = pFactory->getControl(this, id);
						UT_ASSERT_HARMLESS(pControl);

						int iWidth = 100;

						if (pControl)
						{
							iWidth = pControl->getPixelWidth();
						}
						
						bControls = true;
						tbb.fsStyle = TBSTYLE_SEP;   
						tbb.iBitmap = iWidth;

						// create a matching child control
						DWORD dwStyle = WS_CHILD | WS_BORDER | WS_VISIBLE | WS_VSCROLL |
								CBS_HASSTRINGS | CBS_DROPDOWN;

						if ((pControl) && (pControl->shouldSort()))
						{
							dwStyle |= CBS_SORT;
						}
						
						if (id==AP_TOOLBAR_ID_FMT_FONT)
							dwStyle |= CBS_OWNERDRAWFIXED;

						HWND hwndCombo = UT_CreateWindowEx ( 0L,   // No extended styles.
							L"COMBOBOX",						// Class name.
							L"",								// Default text.
							dwStyle,						// Styles and defaults.
							0, 2, iWidth, 250,				// Size and position.
							m_hwnd,							// Parent window.
							reinterpret_cast<HMENU>(static_cast<UINT_PTR>(u)),						// ID.
							m_pWin32App->getInstance(),		// Current instance.
							nullptr );							// No class data.

						UT_ASSERT(hwndCombo);
						
						/*
						 * Hack to create a combobox that is readonly, but which is capable of displaying text in 
						 * the edit control which differs from that in the drop-down list like GTK combos.
						 */
						HWND hwndEdit = FindWindowExW(hwndCombo, nullptr, nullptr, nullptr);
						if (!hwndEdit)
							UT_DEBUGMSG(("Toolbar: Failed to get handle of combos edit controls. Not setting read-only.\n"));
						else
							SendMessageW(hwndEdit, EM_SETREADONLY, (WPARAM)TRUE, (LPARAM)0);
						HFONT hComboFont = (id == AP_TOOLBAR_ID_FMT_FONT)
							? toolbarUIFont()
							: (HFONT)GetStockObject(DEFAULT_GUI_FONT);
						SendMessageW(hwndCombo, WM_SETFONT, (WPARAM)hComboFont, MAKELPARAM(TRUE, 0));
						if (id == AP_TOOLBAR_ID_FMT_FONT)
							SendMessageW(hwndCombo, CB_SETITEMHEIGHT, 0, 22);

						// populate it
						if (pControl)
						{
							SendMessageW(hwndCombo, CB_SETDROPPEDWIDTH,(WPARAM)pControl->getDroppedWidth(), 0);

							if (id == AP_TOOLBAR_ID_FMT_FONT)
							{
								delete m_pFontCtrl;
								m_pFontCtrl = static_cast<AP_Win32Toolbar_FontCombo *>(pControl);
								refillFontCombo(hwndCombo);
							}
							else
							{
								pControl->populate();

								const UT_GenericVector<const char*> * v = pControl->getContents();
								UT_ASSERT_HARMLESS(v);

								SendMessageW(hwndCombo, WM_SETREDRAW, FALSE,0);

								if (v)
								{
									UT_uint32 items = v->getItemCount();
									int nIndex;
									UT_Win32LocaleString localised;
									for (UT_uint32 item = 0; item < items; item++)
									{
										localised.fromUTF8(v->getNthItem(item));
										nIndex = SendMessageW(hwndCombo, CB_ADDSTRING,(WPARAM)0, (LPARAM)localised.c_str());
										SendMessageW(hwndCombo,CB_SETITEMDATA, nIndex, item);
									}
								}

								SendMessageW(hwndCombo, WM_SETREDRAW, TRUE,0);
							}
						}

						// override the window procedure for the combo box
						s_lpfnDefCombo = (WHICHPROC)GetWindowLongPtrW(hwndCombo, GWLP_WNDPROC);
						SetWindowLongPtrW(hwndCombo, GWLP_WNDPROC, (LONG_PTR)_ComboWndProc);
						SetWindowLongPtrW(hwndCombo, GWLP_USERDATA, (LONG_PTR)this);

						// Get the handle to the tooltip window.
						HWND hwndTT = (HWND)SendMessageW(m_hwnd, TB_GETTOOLTIPS, 0, 0);

						if (hwndTT)
						{
							// LPSTR_TEXTCALLBACK asks for TTN_NEEDTEXT when the
							// tip is shown. A pointer into the local string
							// would be freed before Windows 11 reads it.
							TOOLINFOW ti;
							memset(&ti, 0, sizeof(ti));
							ti.cbSize = sizeof(ti);
							ti.uFlags = TTF_IDISHWND | TTF_CENTERTIP;
							ti.lpszText = LPSTR_TEXTCALLBACKW;
							ti.hwnd = m_hwnd;
							ti.uId = (UINT_PTR)hwndCombo;
							SendMessageW(hwndTT, TTM_ADDTOOLW, 0, (LPARAM)(LPTOOLINFOW)&ti);
						}
						
						// bind this separator to its control
						tbb.dwData = (DWORD_PTR) hwndCombo;

						// The font list stays alive: owner-draw reads the names
						// back from it. Every other combo is finished here.
						if (id != AP_TOOLBAR_ID_FMT_FONT)
							DELETEP(pControl);
					}
					break;
					
				case EV_TBIT_EditText:
				case EV_TBIT_DropDown:
				case EV_TBIT_StaticLabel:
					// TODO do these...
					break;
					
				case EV_TBIT_Spacer:
				case EV_TBIT_BOGUS:
				default:
					UT_ASSERT_HARMLESS(0);
					break;
				}

				if (bButton)
				{
					if (bIcons)
					{
						const char * iconName = pLabel->getIconName();
						const char * storedName = iconName ? iconName : "";
						char * copy = static_cast<char *>(malloc(strlen(storedName) + 1));
						if (copy)
						{
							memcpy(copy, storedName, strlen(storedName) + 1);
							m_iconNames.addItem(copy);
						}
						HBITMAP hBitmap = nullptr;
						bool foundIcon = iconName && m_imageList &&
							XAP_Win32Toolbar_Icons::getAlphaBitmapForIcon(m_hwnd,
								static_cast<UT_uint32>(iconPx),
								static_cast<UT_uint32>(iconPx),
								iconName,
								&hBitmap);
						int index = -1;
						if (foundIcon && hBitmap)
							index = ImageList_Add(static_cast<HIMAGELIST>(m_imageList), hBitmap, nullptr);
						if (hBitmap)
							DeleteObject(hBitmap);
						if (index < 0 && m_imageList)
						{
							BITMAPINFO blankInfo;
							memset(&blankInfo, 0, sizeof(blankInfo));
							blankInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
							blankInfo.bmiHeader.biWidth = iconPx;
							blankInfo.bmiHeader.biHeight = iconPx;
							blankInfo.bmiHeader.biPlanes = 1;
							blankInfo.bmiHeader.biBitCount = 32;
							void * bits = nullptr;
							HBITMAP blank = CreateDIBSection(nullptr, &blankInfo, DIB_RGB_COLORS, &bits, nullptr, 0);
							if (blank && bits)
								memset(bits, 0, static_cast<size_t>(iconPx) * static_cast<size_t>(iconPx) * 4);
							if (blank)
							{
								index = ImageList_Add(static_cast<HIMAGELIST>(m_imageList), blank, nullptr);
								DeleteObject(blank);
							}
						}
						tbb.iBitmap = (index >= 0) ? index : 0;
					}
					
					if (bText)
					{
						const char * szLabel = pLabel->getToolbarLabel();
						// As long as translators don't cut the text short,
						// we need to autosize just to squize anything insize 1280 :)
						tbb.fsStyle |= TBSTYLE_AUTOSIZE;
						UT_Win32LocaleString label;
						label.fromUTF8(szLabel ? szLabel : "");
						const wchar_t * wide = label.c_str();
						size_t n = wide ? wcslen(wide) : 0;
						wchar_t * pool = new wchar_t[n + 2];
						if (n && wide)
							memcpy(pool, wide, n * sizeof(wchar_t));
						pool[n] = L'\0';
						pool[n + 1] = L'\0';
						tbb.iString = (INT_PTR)SendMessageW(m_hwnd, TB_ADDSTRINGW, (WPARAM) 0, (LPARAM) pool);
						delete [] pool;
					}
					
					if (pAction->getItemType() == EV_TBIT_ColorFore || pAction->getItemType() == EV_TBIT_ColorBack)
					{
						tbb.fsStyle |=TBSTYLE_DROPDOWN;

						//tbb.fsStyle |=BTNS_WHOLEDROPDOWN;

						DWORD dwStyle = SendMessageW(m_hwnd, TB_GETEXTENDEDSTYLE, (WPARAM) 0, (LPARAM) 0);						
						dwStyle |= TBSTYLE_EX_DRAWDDARROWS;																		
						SendMessageW(m_hwnd, TB_SETEXTENDEDSTYLE, (WPARAM) 0, (LPARAM) dwStyle);						
					}
					
				}
			}
			break;
		
		case EV_TLF_Spacer:
			tbb.fsState = TBSTATE_ENABLED; 
			tbb.fsStyle = TBSTYLE_SEP;     
			break;
			
		default:
			UT_ASSERT_HARMLESS(0);
		}

		// add this button to the bar
		SendMessageW(m_hwnd, TB_ADDBUTTONS, (WPARAM) 1, (LPARAM) (LPTBBUTTON) &tbb);
	}

	// Apply the window theme before measuring button rectangles and rebar
	// dimensions; SetWindowTheme changes ToolbarWindow32 button metrics,
	// which would otherwise leave child combo boxes shifted right of their
	// TBSTYLE_SEP placeholders.
	UT_Win32ApplyWindowTheme(m_hwnd);

	// figure out bar dimensions now that buttons are all there
	SendMessageW(m_hwnd, TB_AUTOSIZE, 0, 0); 
	
	if (bControls)
		_placeControls();

	UT_ASSERT(last_id > 0);

	_addToRebar();	// add this bar to the rebar

	// Theme, TB_AUTOSIZE, and _placeControls() already ran above; only apply
	// toolbar and rebar band colors now instead of repeating the full pass.
	{
		const bool dark = UT_Win32AppsUseDarkTheme();
		COLORSCHEME scheme;
		memset(&scheme, 0, sizeof(scheme));
		scheme.dwSize = sizeof(scheme);
		scheme.clrBtnHighlight = GetSysColor(COLOR_HIGHLIGHT);
		scheme.clrBtnShadow = dark ? RGB(45, 45, 45) : GetSysColor(COLOR_BTNFACE);
		SendMessageW(m_hwnd, TB_SETCOLORSCHEME, 0, (LPARAM)&scheme);

		const int band = _getBandForHwnd(m_hwnd);
		HWND rebar = static_cast<XAP_Win32FrameImpl *>(m_pWin32Frame->getFrameImpl())->getToolbarWindow();
		if (rebar && band >= 0)
		{
			REBARBANDINFOW rbbi;
			memset(&rbbi, 0, sizeof(rbbi));
			rbbi.cbSize = sizeof(rbbi);
			rbbi.fMask = RBBIM_COLORS;
			rbbi.clrFore = dark ? RGB(255, 255, 255) : GetSysColor(COLOR_BTNTEXT);
			rbbi.clrBack = dark ? RGB(32, 32, 32) : GetSysColor(COLOR_BTNFACE);
			SendMessageW(rebar, RB_SETBANDINFO, (WPARAM)band, (LPARAM)&rbbi);
		}
	}

	// I think that this is true due to the WS_VISIBLE flag
	// sent to CreateWindowEx
	m_bVisible = true;

	return true;
}

HWND EV_Win32Toolbar::getWindow(void) const
{
	return m_hwnd;
}

void EV_Win32Toolbar::_placeControls(void)
{
	if (!m_hwnd || !m_pToolbarLayout || !m_pWin32App)
		return;
	const EV_Toolbar_ActionSet * pToolbarActionSet = m_pWin32App->getToolbarActionSet();
	if (!pToolbarActionSet)
		return;
	const UT_uint32 count = m_pToolbarLayout->getLayoutItemCount();
	for (UT_uint32 k = 0; k < count; ++k)
	{
		EV_Toolbar_LayoutItem * pLayoutItem = m_pToolbarLayout->getLayoutItem(k);
		if (!pLayoutItem)
			continue;
		XAP_Toolbar_Id id = pLayoutItem->getToolbarId();
		EV_Toolbar_Action * pAction = pToolbarActionSet->getAction(id);
		if (!pAction || pAction->getItemType() != EV_TBIT_ComboBox)
			continue;

		HWND hwndCtrl = _getControlWindow(id);
		if (!hwndCtrl)
			continue;
		SendMessageW(hwndCtrl, WM_SETFONT, (WPARAM)toolbarUIFont(), TRUE);
		if (id == AP_TOOLBAR_ID_FMT_FONT)
		{
			int item = (22 * toolbarIconDpi(m_hwnd) + 48) / 96;
			if (item < 22)
				item = 22;
			SendMessageW(hwndCtrl, CB_SETITEMHEIGHT, 0, item);
		}
		RECT r;
		memset(&r, 0, sizeof(r));
		if (!GetWindowRect(hwndCtrl, &r))
			continue;
		int nHeight = r.bottom - r.top;
		LRESULT btnIndex = SendMessageW(m_hwnd, TB_COMMANDTOINDEX, (WPARAM)WmCommandFromItemId(id), 0);
		if (btnIndex < 0)
			btnIndex = static_cast<LRESULT>(k);
		if (!SendMessageW(m_hwnd, TB_GETITEMRECT, (WPARAM)btnIndex, (LPARAM)&r))
			continue;
		int nSep = (r.bottom - r.top - nHeight) / 2;
		if (nSep < 0)
			nSep = 0;
		MoveWindow(hwndCtrl, r.left, r.top + nSep, r.right - r.left, nHeight, TRUE);
	}
}

void EV_Win32Toolbar::applyChrome(void)
{
	if (!m_hwnd)
		return;
	const bool dark = UT_Win32AppsUseDarkTheme();
	COLORSCHEME scheme;
	memset(&scheme, 0, sizeof(scheme));
	scheme.dwSize = sizeof(scheme);
	scheme.clrBtnHighlight = GetSysColor(COLOR_HIGHLIGHT);
	scheme.clrBtnShadow = dark ? RGB(45, 45, 45) : GetSysColor(COLOR_BTNFACE);
	SendMessageW(m_hwnd, TB_SETCOLORSCHEME, 0, (LPARAM)&scheme);
	UT_Win32ApplyWindowTheme(m_hwnd);
	SendMessageW(m_hwnd, TB_AUTOSIZE, 0, 0);
	_placeControls();

	const int band = _getBandForHwnd(m_hwnd);
	HWND rebar = static_cast<XAP_Win32FrameImpl *>(m_pWin32Frame->getFrameImpl())->getToolbarWindow();
	if (!rebar || band < 0)
		return;
	REBARBANDINFOW rbbi;
	memset(&rbbi, 0, sizeof(rbbi));
	rbbi.cbSize = sizeof(rbbi);
	rbbi.fMask = RBBIM_COLORS;
	rbbi.clrFore = dark ? RGB(255, 255, 255) : GetSysColor(COLOR_BTNTEXT);
	rbbi.clrBack = dark ? RGB(32, 32, 32) : GetSysColor(COLOR_BTNFACE);
	SendMessageW(rebar, RB_SETBANDINFO, (WPARAM)band, (LPARAM)&rbbi);
}

void EV_Win32Toolbar::rebuildIcons(void)
{
	if (!m_hwnd || m_iconNames.getItemCount() <= 0)
		return;
	HWND parent = GetParent(m_hwnd);
	const int iconPx = toolbarIconPx(parent ? parent : m_hwnd);
	if (iconPx == m_iconPx && m_imageList)
		return;

	HIMAGELIST neu = ImageList_Create(iconPx, iconPx, ILC_COLOR32,
		m_iconNames.getItemCount(), 4);
	if (!neu)
		return;
	for (UT_sint32 n = 0; n < m_iconNames.getItemCount(); ++n)
	{
		const char * name = m_iconNames.getNthItem(n);
		HBITMAP bmp = nullptr;
		bool ok = name && name[0] &&
			XAP_Win32Toolbar_Icons::getAlphaBitmapForIcon(m_hwnd,
				static_cast<UT_uint32>(iconPx),
				static_cast<UT_uint32>(iconPx),
				name,
				&bmp);
		int index = -1;
		if (ok && bmp)
			index = ImageList_Add(neu, bmp, nullptr);
		if (bmp)
			DeleteObject(bmp);
		if (index < 0)
		{
			BITMAPINFO blankInfo;
			memset(&blankInfo, 0, sizeof(blankInfo));
			blankInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
			blankInfo.bmiHeader.biWidth = iconPx;
			blankInfo.bmiHeader.biHeight = iconPx;
			blankInfo.bmiHeader.biPlanes = 1;
			blankInfo.bmiHeader.biBitCount = 32;
			void * bits = nullptr;
			HBITMAP blank = CreateDIBSection(nullptr, &blankInfo, DIB_RGB_COLORS, &bits, nullptr, 0);
			if (blank && bits)
				memset(bits, 0, static_cast<size_t>(iconPx) * static_cast<size_t>(iconPx) * 4);
			if (blank)
			{
				ImageList_Add(neu, blank, nullptr);
				DeleteObject(blank);
			}
		}
	}

	const int count = static_cast<int>(SendMessageW(m_hwnd, TB_BUTTONCOUNT, 0, 0));
	std::vector<TBBUTTON> saved;
	saved.resize(static_cast<size_t>(count > 0 ? count : 0));
	for (int i = 0; i < count; ++i)
	{
		memset(&saved[static_cast<size_t>(i)], 0, sizeof(TBBUTTON));
		SendMessageW(m_hwnd, TB_GETBUTTON, (WPARAM)i, (LPARAM)&saved[static_cast<size_t>(i)]);
	}
	for (int i = count - 1; i >= 0; --i)
		SendMessageW(m_hwnd, TB_DELETEBUTTON, (WPARAM)i, 0);

	SendMessageW(m_hwnd, TB_SETBITMAPSIZE, 0, (LPARAM)MAKELONG(iconPx, iconPx));
	SendMessageW(m_hwnd, TB_SETIMAGELIST, 0, (LPARAM)neu);
	if (m_imageList)
		ImageList_Destroy(static_cast<HIMAGELIST>(m_imageList));
	m_imageList = neu;
	m_iconPx = iconPx;

	if (count > 0)
		SendMessageW(m_hwnd, TB_ADDBUTTONS, (WPARAM)count, (LPARAM)saved.data());
	SendMessageW(m_hwnd, TB_AUTOSIZE, 0, 0);
	_placeControls();

	SIZE size;
	memset(&size, 0, sizeof(size));
	SendMessageW(m_hwnd, TB_GETMAXSIZE, 0, (LPARAM)&size);
	const int band = _getBandForHwnd(m_hwnd);
	HWND rebar = static_cast<XAP_Win32FrameImpl *>(m_pWin32Frame->getFrameImpl())->getToolbarWindow();
	if (rebar && band >= 0 && size.cy > 0)
	{
		REBARBANDINFOW info;
		memset(&info, 0, sizeof(info));
		info.cbSize = sizeof(info);
		info.fMask = RBBIM_CHILDSIZE;
		info.cyMinChild = static_cast<UINT>(size.cy);
		info.cyChild = info.cyMinChild;
		info.cxMinChild = static_cast<UINT>(size.cx > 0 ? size.cx : iconPx);
		SendMessageW(rebar, RB_SETBANDINFO, (WPARAM)band, (LPARAM)&info);
	}
}

HWND EV_Win32Toolbar::_getControlWindow(XAP_Toolbar_Id id)
{
	TBBUTTON tbb;
	memset(&tbb, 0, sizeof(tbb));
	UINT u = WmCommandFromItemId(id);

	// TB_COMMANDTOINDEX returns -1 when the command is not on the bar.
	// A failed TB_GETBUTTON leaves TBBUTTON untouched.
	// https://learn.microsoft.com/en-us/windows/win32/controls/tb-commandtoindex
	// https://learn.microsoft.com/en-us/windows/win32/controls/tb-getbutton
	const LRESULT index = SendMessageW(m_hwnd, TB_COMMANDTOINDEX, (WPARAM) u, 0);
	if (index < 0)
		return nullptr;
	if (!SendMessageW(m_hwnd, TB_GETBUTTON, (WPARAM) index, (LPARAM)&tbb))
		return nullptr;
	if (tbb.idCommand != static_cast<int>(u) || !(tbb.fsStyle & TBSTYLE_SEP))
		return nullptr;

	return (HWND) tbb.dwData;
}

void EV_Win32Toolbar::_releaseListener(void)
{
	if (!m_pViewListener)
		return;
	DELETEP(m_pViewListener);
	m_pViewListener = nullptr;
	m_lid = 0;
}
	
bool EV_Win32Toolbar::bindListenerToView(AV_View * pView)
{
	if(m_hwnd == nullptr)
		return false;

	_releaseListener();
	
	m_pViewListener = new EV_Win32Toolbar_ViewListener(this,pView);
	UT_ASSERT(m_pViewListener);

	UT_DebugOnly<bool> bResult = pView->addListener(static_cast<AV_Listener *>(m_pViewListener),&m_lid);
	UT_ASSERT(bResult);

	if(pView->isDocumentPresent())
	{
		refreshToolbar(pView, AV_CHG_ALL);
	}
	
	return true;
}

bool EV_Win32Toolbar::refreshToolbar(AV_View * pView, AV_ChangeMask mask)
{
	if(m_hwnd == nullptr)
		return false;

	// make the toolbar reflect the current state of the document
	// at the current insertion point or selection.
	
	const EV_Toolbar_ActionSet * pToolbarActionSet = m_pWin32App->getToolbarActionSet();
	UT_ASSERT(pToolbarActionSet);
	
	UT_uint32 nrLabelItemsInLayout = m_pToolbarLayout->getLayoutItemCount();
	for (UT_uint32 k=0; (k < nrLabelItemsInLayout); k++)
	{
		EV_Toolbar_LayoutItem * pLayoutItem = m_pToolbarLayout->getLayoutItem(k);
		UT_continue_if_fail(pLayoutItem);

		XAP_Toolbar_Id id = pLayoutItem->getToolbarId();
		EV_Toolbar_Action * pAction = pToolbarActionSet->getAction(id);
		UT_continue_if_fail(pAction);

		AV_ChangeMask maskOfInterest = pAction->getChangeMaskOfInterest();
		if ((maskOfInterest & mask) == 0)					// if this item doesn't care about
			continue;										// changes of this type, skip it...

		switch (pLayoutItem->getToolbarLayoutFlags())
		{
		case EV_TLF_Normal:
			_refreshItem(pView, pAction, id);
			break;
			
		case EV_TLF_Spacer:
			break;
			
		default:
			UT_ASSERT_HARMLESS(0);
			break;
		}
	}

	return true;
}

bool EV_Win32Toolbar::_refreshID(XAP_Toolbar_Id id)
{
	const EV_Toolbar_ActionSet * pToolbarActionSet = m_pWin32App->getToolbarActionSet();
	UT_return_val_if_fail(pToolbarActionSet,false);

	EV_Toolbar_Action * pAction = pToolbarActionSet->getAction(id);
	UT_ASSERT(pAction);

	AV_View * pView = m_pWin32Frame->getCurrentView();
	UT_ASSERT(pView);

	return _refreshItem(pView, pAction, id);
}

bool EV_Win32Toolbar::_refreshItem(AV_View * pView, const EV_Toolbar_Action * pAction, XAP_Toolbar_Id id)
{
	if (!pView || !pAction)
		return false;

	const char * szState = nullptr;
	EV_Toolbar_ItemState tis = pAction->getToolbarItemState(pView,&szState);
		
	UINT u = WmCommandFromItemId(id);

	switch (pAction->getItemType())
	{
		// These two are unhandled and for the moment drop down to PushButton
		case EV_TBIT_ColorFore:
		case EV_TBIT_ColorBack:

		case EV_TBIT_PushButton:
			{
				bool bGrayed = EV_TIS_ShouldBeGray(tis);

				SendMessageW(m_hwnd, TB_ENABLEBUTTON, u, (!bGrayed ? 1 : 0)) ;

				//UT_DEBUGMSG(("refreshToolbar: PushButton [%s] is %s\n",
				//			m_pToolbarLabelSet->getLabel(id)->getToolbarLabel(),
				//			((bGrayed) ? "disabled" : "enabled")));
			}
			break;
	
		case EV_TBIT_ToggleButton:
		case EV_TBIT_GroupButton:
			{
				bool bGrayed = EV_TIS_ShouldBeGray(tis);
				bool bToggled = EV_TIS_ShouldBeToggled(tis);
				
				SendMessageW(m_hwnd, TB_ENABLEBUTTON, u, (!bGrayed ? 1 : 0));
				SendMessageW(m_hwnd, TB_CHECKBUTTON, u, (bToggled ? 1 : 0));

				//UT_DEBUGMSG(("refreshToolbar: ToggleButton [%s] is %s and %s\n",
				//			 m_pToolbarLabelSet->getLabel(id)->getToolbarLabel(),
				//			 ((bGrayed) ? "disabled" : "enabled"),
				//			 ((bToggled) ? "pressed" : "not pressed")));
			}
			break;

		case EV_TBIT_ComboBox:
			{
				//bool bGrayed = EV_TIS_ShouldBeGray(tis);
				//bool bString = EV_TIS_ShouldUseString(tis);
				HWND hwndCombo = _getControlWindow(id);
				UT_return_val_if_fail(hwndCombo, true);
				std::string utf8;
				UT_String str;

				// A new paragraph stores no style attribute. getStyle reports
				// that as "None", which is not in the list, so the field stayed
				// empty. The paragraph still uses the default style, Normal.
				if (id == AP_TOOLBAR_ID_FMT_STYLE &&
					(!szState || !szState[0] || strcmp(szState, "None") == 0))
					szState = "Normal";

				// NOTE: no string means a mixed or empty selection. Clear the
				// control; do not pass a null pointer to CB_SELECTSTRING.
				if (!szState)
				{
					SendMessageW(hwndCombo, CB_SETCURSEL, static_cast<WPARAM>(-1), 0);
					SetWindowTextW(hwndCombo, L"");
					break;
				}					
				
				// Find the proper non-localised text
                UT_Win32LocaleString localised;
				if (id==AP_TOOLBAR_ID_FMT_STYLE)
				{
                        pt_PieceTable::s_getLocalisedStyleName(szState, utf8);
						localised.fromUTF8 (utf8.c_str());
                }       
                 else
    					localised.fromUTF8 (szState);
					
                int idx = SendMessageW(hwndCombo, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)localised.c_str());
				if (idx!=CB_ERR)
					SendMessageW(hwndCombo, CB_SETCURSEL, idx, 0);
				/*
				 * If the string didn't exist within the combos list, we handle things differently for
				 * different combos.
				 */
				if (idx==CB_ERR)
				{
					const XAP_StringSet * pSS;
					
					switch (id)
					{
					/*
					 * In the case of the font and font size combo, this means that the document contains a font not
					 * installed on the system. Add it to the edit control, as is the case with the unix version.
					 */
					case AP_TOOLBAR_ID_FMT_SIZE:
					case AP_TOOLBAR_ID_FMT_FONT:
						idx = SendMessageW(hwndCombo, WM_SETTEXT, (WPARAM)-1, (LPARAM)localised.c_str());
						if (idx == CB_ERR)
						{
							UT_DEBUGMSG(("refreshToolbar: Failed to set text for font combo.\n"));
						}
						break;
					/* 
					 * If this is the zoom combo, select the "Other..." string.
					 */
					case AP_TOOLBAR_ID_ZOOM:
						pSS = XAP_App::getApp()->getStringSet();
                        localised.fromUTF8 (pSS->getValue(XAP_STRING_ID_TB_Zoom_Percent));
						idx = SendMessageW(hwndCombo, CB_SELECTSTRING, (WPARAM)-1, (LPARAM)localised.c_str());
						break;

					case AP_TOOLBAR_ID_FMT_STYLE:
						UT_DEBUGMSG(("refreshToolbar: Unknown string selected in style combo.\n"));
						break;
					default:
						UT_DEBUGMSG(("refreshToolbar: Unknown string selected in unknown combo.\n"));
						break;
					}
				}
	
				//UT_DEBUGMSG(("refreshToolbar: ComboBox [%s] is %s and %s\n",
				//			 m_pToolbarLabelSet->getLabel(id)->getToolbarLabel(),
				//			 ((bGrayed) ? "disabled" : "enabled"),
				//			 ((bString) ? szState : "no state")));
			}
			break;

		case EV_TBIT_EditText:
		case EV_TBIT_DropDown:
		case EV_TBIT_StaticLabel:
		case EV_TBIT_Spacer:
			// TODO do these later...
			break;
			

		case EV_TBIT_BOGUS:
		default:
			UT_ASSERT_HARMLESS(UT_SHOULD_NOT_HAPPEN);
			break;
	}

	return true;
}

bool EV_Win32Toolbar::getToolTip(LPARAM lParam)
{
	if (!lParam || !m_pToolbarLabelSet)
		return false;

	// Windows 11 leaves lpszText null. Point it at the struct's own
	// buffer before any lookup, for both the Unicode and ANSI notify.
	NMHDR * hdr = reinterpret_cast<NMHDR *>(lParam);
	const bool ansi = (hdr->code == TTN_NEEDTEXTA);
	UINT flags = 0;
	UINT_PTR idFrom = hdr->idFrom;

	if (ansi)
	{
		NMTTDISPINFOA * info = reinterpret_cast<NMTTDISPINFOA *>(lParam);
		flags = info->uFlags;
		info->hinst = nullptr;
		info->szText[0] = '\0';
		info->lpszText = info->szText;
	}
	else
	{
		NMTTDISPINFOW * info = reinterpret_cast<NMTTDISPINFOW *>(lParam);
		flags = info->uFlags;
		info->hinst = nullptr;
		info->szText[0] = L'\0';
		info->lpszText = info->szText;
	}

	UINT idButton;
	if (flags & TTF_IDISHWND)
		idButton = static_cast<UINT>(GetDlgCtrlID(reinterpret_cast<HWND>(idFrom)));
	else
		idButton = static_cast<UINT>(idFrom);

	XAP_Toolbar_Id id = ItemIdFromWmCommand(idButton);
	EV_Toolbar_Label * pLabel = m_pToolbarLabelSet->getLabel(id);
	if (!pLabel)
		return false;

	const char * szToolTip = pLabel->getToolTip();
	if (!szToolTip || !*szToolTip)
		szToolTip = pLabel->getStatusMsg();
	if (!szToolTip || !*szToolTip)
		szToolTip = pLabel->getToolbarLabel();
	if (!szToolTip || !*szToolTip)
		return true;

	UT_Win32LocaleString str;
	str.fromUTF8(szToolTip);
	if (str.c_str() == nullptr)
		return true;

	if (ansi)
	{
		NMTTDISPINFOA * info = reinterpret_cast<NMTTDISPINFOA *>(lParam);
		info->szText[0] = '\0';
		if (WideCharToMultiByte(CP_ACP, 0, str.c_str(), -1, info->szText, 80, nullptr, nullptr) <= 0)
			info->szText[0] = '\0';
		info->szText[79] = '\0';
		info->lpszText = info->szText;
	}
	else
	{
		NMTTDISPINFOW * info = reinterpret_cast<NMTTDISPINFOW *>(lParam);
		wcsncpy(info->szText, str.c_str(), 79);
		info->szText[79] = L'\0';
		info->lpszText = info->szText;
	}

	return true;
}


void EV_Win32Toolbar::show(void)
{
	UT_return_if_fail(m_pWin32Frame);
	UT_DebugOnly<HWND> hRebar = static_cast<XAP_Win32FrameImpl*>(m_pWin32Frame->getFrameImpl())->getToolbarWindow();
	UT_ASSERT(hRebar);
	UT_DebugOnly<int> iBand = _getBandForHwnd(m_hwnd);
	UT_ASSERT(iBand < 0);	// It can't already be displayed!
	_addToRebar();

	m_bVisible = true;
}

void EV_Win32Toolbar::hide(void)
{
	UT_return_if_fail(m_pWin32Frame);
	HWND hRebar = static_cast<XAP_Win32FrameImpl*>(m_pWin32Frame->getFrameImpl())->getToolbarWindow();
	UT_ASSERT(hRebar);
	const int iBand = _getBandForHwnd(m_hwnd);
	if (iBand >= 0)
	{
		SendMessageW(hRebar, RB_DELETEBAND, (WPARAM)iBand, 0);
		m_bVisible = false;
	}
}

//
// Returns the index of hToolbar in the rebar hRebar.
// Returns -1 on error or if the toolbar wasn't recognized as a
// child of the specified rebar control.
//
int EV_Win32Toolbar::_getBandForHwnd(HWND hToolbar) const
{
	HWND hRebar = static_cast<XAP_Win32FrameImpl*>(m_pWin32Frame->getFrameImpl())->getToolbarWindow();

	// If we ever get more than 20 toolbars I don't wanna be around.
	REBARBANDINFOW rbi;
	memset(&rbi, 0, sizeof(rbi));
	// Windows 11 writes the Vista chevron fields. The V6 size stops
	// before them, so the control overflows the heap on the first paint.
	rbi.cbSize = sizeof(rbi);
	rbi.fMask  = RBBIM_CHILD;
	for (int i = 0; i < 20; ++i)
	{
		if (!SendMessageW(hRebar, RB_GETBANDINFO, (WPARAM)i, (LPARAM)&rbi))
		{
			continue;
		}
		if (hToolbar == rbi.hwndChild)
		{
			return i;
		}
	}
	return -1;
}

void EV_Win32Toolbar::_addToRebar()
{
	// Get the height of the toolbar.
	DWORD dwBtnSize = SendMessageW(m_hwnd, TB_GETBUTTONSIZE, 0,0);

	RECT rc = {};
	// GetWindowRect leaves the rectangle untouched on failure.
	// The button size is the width Windows already reported.
	UINT iWidth = LOWORD(dwBtnSize);
	if (GetWindowRect(m_hwnd, &rc) && rc.right > rc.left)
		iWidth = static_cast<UINT>(rc.right - rc.left);
	iWidth += 2;

	// add this bar to the rebar
	REBARBANDINFOW rbbi;
	memset(&rbbi, 0, sizeof(rbbi));
	// Initialize REBARBANDINFO
	rbbi.cbSize = sizeof(rbbi);
	rbbi.fMask =	RBBIM_COLORS	|	// clrFore and clrBack are valid
					RBBIM_CHILD		|	// hwndChild is valid
					RBBIM_CHILDSIZE	|	// cxMinChild and cyMinChild are valid
					RBBIM_SIZE		|	// cx is valid
					RBBIM_IDEALSIZE	|	// cxIdeal is valid
					RBBIM_STYLE;		// fStyle is valid
	const bool dark = UT_Win32AppsUseDarkTheme();
	rbbi.clrFore = dark ? RGB(255, 255, 255) : GetSysColor(COLOR_BTNTEXT);
	rbbi.clrBack = dark ? RGB(32, 32, 32) : GetSysColor(COLOR_BTNFACE);
	// One row per toolbar. No gripper and no child edge: those were the
	// empty strip between the bar and the rulers.
#ifndef RBBS_NOGRIPPER
#define RBBS_NOGRIPPER 0x00000100
#endif
#ifndef RBBS_HIDETITLE
#define RBBS_HIDETITLE 0x00000400
#endif
	rbbi.fStyle =	RBBS_NOVERT			|
					RBBS_BREAK			|
					RBBS_USECHEVRON		|
					RBBS_NOGRIPPER		|
					RBBS_HIDETITLE;
	rbbi.hwndChild = m_hwnd;
	rbbi.cxMinChild = LOWORD(dwBtnSize);
	rbbi.cyMinChild = HIWORD(dwBtnSize);
	rbbi.cx = iWidth;
	rbbi.cxIdeal = iWidth;

	HWND hRebar = static_cast<XAP_Win32FrameImpl*>(m_pWin32Frame->getFrameImpl())->getToolbarWindow();
	// Add it at the the end
	SendMessageW(hRebar, RB_INSERTBAND, (WPARAM)-1, (LPARAM)&rbbi);
}


XAP_Frame * EV_Win32Toolbar::getFrame(void)
{
	return m_pWin32Frame;
}

void EV_Win32Toolbar::prepareFontDropdown(HWND hwndCombo)
{
	if (!hwndCombo || m_fontRefilling || m_fontDropdownReady)
		return;
	m_fontDropdownReady = true;
	m_fontPinTop = true;
	s_fontPinTries = 0;
	refillFontCombo(hwndCombo);
	SetTimer(hwndCombo, ABI_FONT_SHOW_TOP, 30, nullptr);
}

void EV_Win32Toolbar::refillFontCombo(HWND hwndCombo)
{
	if (m_fontRefilling || !m_pFontCtrl || !hwndCombo)
		return;

	m_fontRefilling = true;

	wchar_t kept[512];
	kept[0] = 0;
	GetWindowTextW(hwndCombo, kept, 512);

	SendMessageW(hwndCombo, WM_SETREDRAW, FALSE, 0);
	SendMessageW(hwndCombo, CB_RESETCONTENT, 0, 0);
	m_pFontCtrl->populate();

	const UT_GenericVector<const char*> * v = m_pFontCtrl->getContents();
	if (v)
	{
		UT_uint32 items = v->getItemCount();
		UT_Win32LocaleString localised;
		for (UT_uint32 item = 0; item < items; ++item)
		{
			const char * name = v->getNthItem(item);
			if (!name)
				continue;
			localised.fromUTF8(name);
			int nIndex = static_cast<int>(SendMessageW(hwndCombo, CB_ADDSTRING, 0, (LPARAM)localised.c_str()));
			if (nIndex != CB_ERR)
				SendMessageW(hwndCombo, CB_SETITEMDATA, nIndex, item);
		}
	}

	if (kept[0])
	{
		// The first exact match is the copy in the document section.
		const LRESULT idx = SendMessageW(hwndCombo, CB_FINDSTRINGEXACT, static_cast<WPARAM>(-1), (LPARAM)kept);
		if (idx != CB_ERR)
			SendMessageW(hwndCombo, CB_SETCURSEL, static_cast<WPARAM>(idx), 0);
		else
			SetWindowTextW(hwndCombo, kept);
	}

	SendMessageW(hwndCombo, WM_SETREDRAW, TRUE, 0);
	m_fontRefilling = false;
}


/*!
 * This method examines the current document and repopulates the Styles
 * Combo box with what is in the document. It returns false if no styles 
 * combo box was found. True if it all worked.
 */
bool EV_Win32Toolbar::repopulateStyles(void)
{
//
// First off find the Styles combobox in a toolbar somewhere
//
	UT_uint32 count = m_pToolbarLayout->getLayoutItemCount();
	UT_uint32 i =0;
	EV_Toolbar_LayoutItem * pLayoutItem = nullptr;
	XAP_Toolbar_Id id;
	for(i=0; i < count; i++)
	{
		pLayoutItem = m_pToolbarLayout->getLayoutItem(i);
		id = pLayoutItem->getToolbarId();
	//	wd = (_wd *) getNthItem(i);
		if(id == AP_TOOLBAR_ID_FMT_STYLE)
			break;
	}
	if(i>=count)
		return false;
	


//
// GOT IT!
//
  //	UT_ASSERT(wd->m_id == AP_TOOLBAR_ID_FMT_STYLE);
	XAP_Toolbar_ControlFactory * pFactory = m_pWin32App->getControlFactory();
	UT_return_val_if_fail(pFactory,false);
	EV_Toolbar_Control * pControl = pFactory->getControl(this, id);
	UT_return_val_if_fail(pControl,false);
	AP_Win32Toolbar_StyleCombo * pStyleC = static_cast<AP_Win32Toolbar_StyleCombo *>(pControl);
	pStyleC->repopulate();
	
	
	HWND hwndCombo = _getControlWindow(id);
	if (!hwndCombo)
	{
		delete pStyleC;
		return false;
	}
	// GtkCombo * item = GTK_COMBO(wd->m_widget);
//
// Now the combo box has to be refilled from this
//						
	const UT_GenericVector<const char*> * v = pControl->getContents();
	if (!v)
	{
		delete pStyleC;
		UT_return_val_if_fail(v,false);
	}
	
//
// Now  we must remove and delete the old data so we add the new
// list of styles to the combo box.
//
// Try this....
	SendMessageW(hwndCombo, WM_SETREDRAW, FALSE, 0);
	SendMessageW(hwndCombo, CB_RESETCONTENT, 0 , 0);

	SendMessageW(hwndCombo, CB_SETDROPPEDWIDTH,
				(WPARAM)pStyleC->getDroppedWidth(), 0);

//
//	GtkList * oldlist = GTK_LIST(item->list);
//	gtk_list_clear_items(oldlist,0,-1);
//
// Now make a new one.
//
	int	nItem;													    
	std::string utf8;
	UT_String str;

	for (UT_sint32 c=0; c < m_vecOrgStylesNames.getItemCount(); c++)	
		delete m_vecOrgStylesNames.getNthItem(c);

	m_vecOrgStylesNames.clear();
	
	
	for (UT_sint32 k=0; k < v->getItemCount(); k++)
	{
		const char*	sz = (char *)v->getNthItem(k);
		UT_Win32LocaleString localised;
		
		pt_PieceTable::s_getLocalisedStyleName(sz, utf8);
		localised.fromUTF8 (utf8.c_str());
		
		nItem = SendMessageW(hwndCombo, CB_ADDSTRING,(WPARAM)0, (LPARAM)localised.c_str());
		m_vecOrgStylesNames.addItem (new UT_UTF8String ((char *)v->getNthItem(k)));
		SendMessageW(hwndCombo, CB_SETITEMDATA,(WPARAM)nItem, (LPARAM)m_vecOrgStylesNames.getItemCount()-1);
	}
//
// Don't need this anymore and we don't like memory leaks in abi
//
	SendMessageW(hwndCombo, WM_SETREDRAW, TRUE, 0);
	InvalidateRect (hwndCombo, nullptr, true);
	delete pStyleC;

	// CB_RESETCONTENT clears the edit. Put the current style back; a new
	// document then shows Normal instead of an empty field.
	if (m_pWin32Frame && m_pWin32Frame->getCurrentView())
		_refreshID(id);

	return true;
}

/* Eight columns, five rows. Each column is one hue, dark at the top. */
static const COLORREF s_swatches[40] =
{
	RGB(0x00,0x00,0x00), RGB(0x1F,0x4E,0x79), RGB(0x83,0x3C,0x0C), RGB(0x37,0x56,0x23),
	RGB(0x1D,0x4E,0x4E), RGB(0x20,0x38,0x64), RGB(0x5B,0x2C,0x6F), RGB(0x7B,0x24,0x1C),
	RGB(0x59,0x59,0x59), RGB(0x2E,0x75,0xB6), RGB(0xC6,0x59,0x11), RGB(0x54,0x82,0x35),
	RGB(0x2E,0x8A,0x8A), RGB(0x2F,0x54,0x96), RGB(0x70,0x30,0xA0), RGB(0xC0,0x00,0x00),
	RGB(0x80,0x80,0x80), RGB(0x5B,0x9B,0xD5), RGB(0xED,0x7D,0x31), RGB(0x70,0xAD,0x47),
	RGB(0x4E,0xCD,0xC4), RGB(0x5B,0x7C,0x99), RGB(0x9B,0x59,0xB6), RGB(0xFF,0x50,0x50),
	RGB(0xBF,0xBF,0xBF), RGB(0x9D,0xC3,0xE6), RGB(0xF4,0xB1,0x83), RGB(0xA9,0xD0,0x8E),
	RGB(0x9F,0xE2,0xE0), RGB(0x8F,0xAA,0xDC), RGB(0xC3,0x9B,0xD3), RGB(0xFF,0x99,0x99),
	RGB(0xFF,0xFF,0xFF), RGB(0xDE,0xEB,0xF7), RGB(0xFC,0xE4,0xD6), RGB(0xE2,0xEF,0xDA),
	RGB(0xE8,0xF6,0xF5), RGB(0xD6,0xDC,0xE4), RGB(0xE8,0xDA,0xEF), RGB(0xF8,0xCB,0xAD)
};

enum
{
	COLOR_COLS = 8,
	COLOR_ROWS = 5,
	COLOR_COUNT = 40,
	HOT_CLEAR = 40,
	HOT_MORE = 41
};

static int s_colorScale(int px96, int minimum, int maximum, HWND hwnd = nullptr)
{
	int dpi = toolbarIconDpi(hwnd);
	int value = (px96 * dpi + 48) / 96;
	if (value < minimum)
		value = minimum;
	if (value > maximum)
		value = maximum;
	return value;
}

static int s_colorCell(HWND hwnd = nullptr)
{
	return s_colorScale(22, 18, 36, hwnd);
}

static int s_colorPad(HWND hwnd = nullptr)
{
	return s_colorScale(8, 6, 14, hwnd);
}

static int s_colorFooter(HWND hwnd = nullptr)
{
	return s_colorScale(28, 24, 44, hwnd);
}

struct AbiColorPopup
{
	EV_Win32Toolbar * toolbar;
	XAP_Toolbar_Id id;
	bool background;
	bool ready;
	int hot;
	wchar_t clearText[160];
	wchar_t moreText[160];
};

static int s_colorClientWidth(HWND hwnd = nullptr)
{
	int w = s_colorPad(hwnd) * 2 + COLOR_COLS * s_colorCell(hwnd);
	return (w < 220) ? 220 : w;
}

static int s_colorClientHeight(HWND hwnd = nullptr)
{
	return s_colorPad(hwnd) + COLOR_ROWS * s_colorCell(hwnd) + s_colorPad(hwnd) / 2 + s_colorFooter(hwnd) * 2 + s_colorPad(hwnd);
}

static int s_colorGridX(int width, HWND hwnd = nullptr)
{
	return (width - COLOR_COLS * s_colorCell(hwnd)) / 2;
}

static void s_copyWide(wchar_t * dest, size_t destChars, const wchar_t * src)
{
	size_t i = 0;
	if (!src)
		src = L"";
	for (; i + 1 < destChars && src[i]; ++i)
		dest[i] = src[i];
	dest[i] = 0;
}

static void s_loadLabel(wchar_t * dest, size_t destChars, XAP_String_Id id, const wchar_t * fallback)
{
	const XAP_StringSet * pSS = XAP_App::getApp()->getStringSet();
	const char * utf8 = pSS ? pSS->getValue(id) : nullptr;
	if (utf8 && utf8[0])
	{
		UT_Win32LocaleString localised;
		localised.fromUTF8(utf8);
		s_copyWide(dest, destChars, localised.c_str());
	}
	else
		s_copyWide(dest, destChars, fallback);
}

static void s_applyToolbarColor(EV_Win32Toolbar * toolbar, XAP_Toolbar_Id id, const char * hex)
{
	if (!toolbar || !hex || !hex[0])
		return;

	XAP_Frame * frame = toolbar->getFrame();
	if (!frame)
		return;

	XAP_Win32FrameImpl * fimpl = static_cast<XAP_Win32FrameImpl *>(frame->getFrameImpl());
	if (!fimpl)
		return;

	if (id == AP_TOOLBAR_ID_COLOR_BACK)
		fimpl->m_sColorBack = hex;
	else
		fimpl->m_sColorFore = hex;

	toolbar->toolbarEvent(id, nullptr, 0);
}

static void s_chooseCustomColor(EV_Win32Toolbar * toolbar, XAP_Toolbar_Id id)
{
	if (!toolbar)
		return;

	XAP_Frame * pFrame = toolbar->getFrame();
	if (!pFrame)
		return;

	pFrame->raise();

	XAP_DialogFactory * pDialogFactory = static_cast<XAP_DialogFactory *>(pFrame->getDialogFactory());
	AP_Dialog_Background * pDialog = static_cast<AP_Dialog_Background *>(
		pDialogFactory->requestDialog(static_cast<XAP_Dialog_Id>(AP_DIALOG_ID_BACKGROUND)));
	if (!pDialog)
		return;

	XAP_Win32FrameImpl * fimpl = static_cast<XAP_Win32FrameImpl *>(pFrame->getFrameImpl());
	if (id == AP_TOOLBAR_ID_COLOR_BACK && !fimpl->m_sColorBack.empty())
		pDialog->setColor(fimpl->m_sColorBack.utf8_str());
	if (id == AP_TOOLBAR_ID_COLOR_FORE && !fimpl->m_sColorFore.empty())
		pDialog->setColor(fimpl->m_sColorFore.utf8_str());

	pDialog->runModal(pFrame);
	if (pDialog->getAnswer() == AP_Dialog_Background::a_OK)
		s_applyToolbarColor(toolbar, id, reinterpret_cast<const char *>(pDialog->getColor()));

	pDialogFactory->releaseDialog(pDialog);
}

static int s_hitColor(HWND hwnd, int x, int y)
{
	RECT rc;
	memset(&rc, 0, sizeof(rc));
	if (!hwnd || !GetClientRect(hwnd, &rc))
		return -1;
	int gx = s_colorGridX(rc.right - rc.left, hwnd);
	int gy = s_colorPad(hwnd);
	const int cell = s_colorCell(hwnd);
	const int footer = s_colorFooter(hwnd);

	if (x >= gx && y >= gy)
	{
		int col = (x - gx) / cell;
		int row = (y - gy) / cell;
		if (col >= 0 && col < COLOR_COLS && row >= 0 && row < COLOR_ROWS)
			return row * COLOR_COLS + col;
	}

	int footerTop = gy + COLOR_ROWS * cell + gy / 2;
	if (y >= footerTop && y < footerTop + footer)
		return HOT_CLEAR;
	if (y >= footerTop + footer && y < footerTop + footer * 2)
		return HOT_MORE;
	return -1;
}

static void s_fillSwatch(HDC hdc, const RECT & cell, COLORREF color, bool hot)
{
	HBRUSH dcBrush = static_cast<HBRUSH>(GetStockObject(DC_BRUSH));
	COLORREF prevColor = SetDCBrushColor(hdc, color);
	FillRect(hdc, &cell, dcBrush);

	RECT edge = cell;
	if (hot)
		InflateRect(&edge, 1, 1);
	SetDCBrushColor(hdc, hot ? RGB(0, 0, 0) : RGB(90, 90, 90));
	FrameRect(hdc, &edge, dcBrush);
	SetDCBrushColor(hdc, prevColor);
}

static void s_paintFooter(HDC hdc, const RECT & cell, const wchar_t * text, bool hot)
{
	FillRect(hdc, &cell, GetSysColorBrush(hot ? COLOR_HIGHLIGHT : COLOR_BTNFACE));

	SetBkMode(hdc, TRANSPARENT);
	SetTextColor(hdc, hot ? GetSysColor(COLOR_HIGHLIGHTTEXT) : GetSysColor(COLOR_BTNTEXT));
	RECT textRc = cell;
	DrawTextW(hdc, text, -1, &textRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

static LRESULT CALLBACK s_colorPopupProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	AbiColorPopup * popup = reinterpret_cast<AbiColorPopup *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

	switch (msg)
	{
	case WM_CREATE:
		{
			CREATESTRUCTW * cs = reinterpret_cast<CREATESTRUCTW *>(lParam);
			SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
			return 0;
		}

	case WM_MOUSEMOVE:
		if (popup)
		{
			int hit = s_hitColor(hwnd, static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam)));
			if (hit != popup->hot)
			{
				popup->hot = hit;
				InvalidateRect(hwnd, nullptr, FALSE);
			}
			TRACKMOUSEEVENT track;
			track.cbSize = sizeof(track);
			track.dwFlags = TME_LEAVE;
			track.hwndTrack = hwnd;
			track.dwHoverTime = 0;
			TrackMouseEvent(&track);
		}
		return 0;

	case WM_MOUSELEAVE:
		if (popup && popup->hot != -1)
		{
			popup->hot = -1;
			InvalidateRect(hwnd, nullptr, FALSE);
		}
		return 0;

	case WM_LBUTTONDOWN:
	case WM_LBUTTONUP:
		if (popup)
		{
			/* The click that opened the arrow is released on the toolbar,
			   above this window. Ignore that button-up. */
			if (msg == WM_LBUTTONUP && !popup->ready)
			{
				popup->ready = true;
				return 0;
			}
			popup->ready = true;

			int x = static_cast<short>(LOWORD(lParam));
			int y = static_cast<short>(HIWORD(lParam));
			RECT rc;
			memset(&rc, 0, sizeof(rc));
			if (!GetClientRect(hwnd, &rc))
			{
				DestroyWindow(hwnd);
				return 0;
			}
			bool inside = (x >= 0 && y >= 0 && x < rc.right && y < rc.bottom);
			if (!inside)
			{
				DestroyWindow(hwnd);
				return 0;
			}
			if (msg == WM_LBUTTONDOWN)
				return 0;

			int hit = s_hitColor(hwnd, x, y);
			EV_Win32Toolbar * toolbar = popup->toolbar;
			XAP_Toolbar_Id id = popup->id;
			bool background = popup->background;
			char hex[16];
			hex[0] = 0;

			if (hit >= 0 && hit < COLOR_COUNT)
			{
				COLORREF color = s_swatches[hit];
				snprintf(hex, sizeof(hex), "%02x%02x%02x",
					GetRValue(color), GetGValue(color), GetBValue(color));
			}
			else if (hit == HOT_CLEAR)
				snprintf(hex, sizeof(hex), "%s", background ? "transparent" : "000000");

			bool more = (hit == HOT_MORE);
			DestroyWindow(hwnd);
			if (more)
				s_chooseCustomColor(toolbar, id);
			else if (hex[0])
				s_applyToolbarColor(toolbar, id, hex);
		}
		return 0;

	case WM_KEYDOWN:
		if (wParam == VK_ESCAPE)
			DestroyWindow(hwnd);
		return 0;

	case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC hdc = BeginPaint(hwnd, &ps);
			if (!hdc)
			{
				EndPaint(hwnd, &ps);
				return 0;
			}
			RECT rc;
			memset(&rc, 0, sizeof(rc));
			if (!GetClientRect(hwnd, &rc))
			{
				EndPaint(hwnd, &ps);
				return 0;
			}
			FillRect(hdc, &rc, reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1));

			int gx = s_colorGridX(rc.right - rc.left, hwnd);
			int gy = s_colorPad(hwnd);
			const int cellPx = s_colorCell(hwnd);
			const int footer = s_colorFooter(hwnd);
			for (int i = 0; i < COLOR_COUNT; ++i)
			{
				RECT cell;
				cell.left = gx + (i % COLOR_COLS) * cellPx + 2;
				cell.top = gy + (i / COLOR_COLS) * cellPx + 2;
				cell.right = cell.left + cellPx - 4;
				cell.bottom = cell.top + cellPx - 4;
				s_fillSwatch(hdc, cell, s_swatches[i], popup && popup->hot == i);
			}

			int footerTop = gy + COLOR_ROWS * cellPx + gy / 2;
			RECT clearRc;
			clearRc.left = 4;
			clearRc.right = rc.right - 4;
			clearRc.top = footerTop;
			clearRc.bottom = footerTop + footer - 2;
			RECT moreRc = clearRc;
			moreRc.top = footerTop + footer;
			moreRc.bottom = footerTop + footer * 2 - 2;

			if (popup)
			{
				HFONT font = toolbarUIFont();
				HFONT old = static_cast<HFONT>(SelectObject(hdc, font));
				s_paintFooter(hdc, clearRc, popup->clearText, popup->hot == HOT_CLEAR);
				s_paintFooter(hdc, moreRc, popup->moreText, popup->hot == HOT_MORE);
				SelectObject(hdc, old);
			}

			EndPaint(hwnd, &ps);
			return 0;
		}

	case WM_CAPTURECHANGED:
		if (popup && reinterpret_cast<HWND>(lParam) != hwnd)
			DestroyWindow(hwnd);
		return 0;

	case WM_DESTROY:
		if (!popup)
			return 0;
		SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
		if (GetCapture() == hwnd)
			ReleaseCapture();
		delete popup;
		return 0;
	}

	return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void s_showColorPopup(EV_Win32Toolbar * toolbar, UINT cmd, XAP_Toolbar_Id id)
{
	HWND existing = FindWindowW(L"AbiWordColorPopup", nullptr);
	if (existing)
		DestroyWindow(existing);

	static bool registered = false;
	if (!registered)
	{
		WNDCLASSW wc;
		memset(&wc, 0, sizeof(wc));
		wc.lpfnWndProc = s_colorPopupProc;
		wc.hInstance = GetModuleHandleW(nullptr);
		wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
		wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
		wc.lpszClassName = L"AbiWordColorPopup";
		registered = (RegisterClassW(&wc) != 0) || (GetLastError() == ERROR_CLASS_ALREADY_EXISTS);
	}
	if (!registered)
		return;

	AbiColorPopup * popup = new AbiColorPopup;
	popup->toolbar = toolbar;
	popup->id = id;
	popup->background = (id == AP_TOOLBAR_ID_COLOR_BACK);
	popup->ready = false;
	popup->hot = -1;
	s_loadLabel(popup->clearText, sizeof(popup->clearText) / sizeof(popup->clearText[0]),
		popup->background ? XAP_STRING_ID_TB_ClearBackground : XAP_STRING_ID_TB_ClearForeground,
		popup->background ? L"Clear Background" : L"Clear Foreground");
	s_loadLabel(popup->moreText, sizeof(popup->moreText) / sizeof(popup->moreText[0]),
		popup->background ? AP_STRING_ID_DLG_Background_TitleHighlight : AP_STRING_ID_DLG_Background_TitleFore,
		popup->background ? L"Change Highlight Color" : L"Change Text Color");

	RECT button;
	POINT origin;
	origin.x = 0;
	origin.y = 0;
	HWND toolbarHwnd = toolbar->getWindow();
	if (toolbarHwnd && SendMessageW(toolbarHwnd, TB_GETRECT, cmd, reinterpret_cast<LPARAM>(&button)))
	{
		origin.x = button.left;
		origin.y = button.bottom;
		ClientToScreen(toolbarHwnd, &origin);
	}
	else
		GetCursorPos(&origin);

	RECT windowRc = { 0, 0, s_colorClientWidth(toolbarHwnd), s_colorClientHeight(toolbarHwnd) };
	AdjustWindowRectEx(&windowRc, WS_POPUP | WS_BORDER, FALSE, WS_EX_TOOLWINDOW | WS_EX_TOPMOST);
	int width = windowRc.right - windowRc.left;
	int height = windowRc.bottom - windowRc.top;

	RECT work;
	memset(&work, 0, sizeof(work));
	bool haveWork = false;
	HMONITOR mon = MonitorFromPoint(origin, MONITOR_DEFAULTTONEAREST);
	if (mon)
	{
		MONITORINFO mi;
		memset(&mi, 0, sizeof(mi));
		mi.cbSize = sizeof(mi);
		if (GetMonitorInfoW(mon, &mi))
		{
			work = mi.rcWork;
			haveWork = true;
		}
	}
	if (!haveWork && !SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0))
		SetRect(&work, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
	int x = origin.x;
	int y = origin.y;
	if (x + width > work.right)
		x = work.right - width;
	if (y + height > work.bottom)
		y = origin.y - height;
	if (x < work.left)
		x = work.left;
	if (y < work.top)
		y = work.top;

	HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
		L"AbiWordColorPopup", L"",
		WS_POPUP | WS_BORDER,
		x, y, width, height,
		toolbarHwnd, nullptr, GetModuleHandleW(nullptr), popup);
	if (!hwnd)
	{
		delete popup;
		s_chooseCustomColor(toolbar, id);
		return;
	}

	ShowWindow(hwnd, SW_SHOWNA);
	SetCapture(hwnd);
}

/*
	The arrow next to the text and highlight colour buttons opens this
	swatch menu. The button itself still applies the current colour.
*/
void EV_Win32Toolbar::showChevron(POINT screen)
{
	if (!m_hwnd || !m_pWin32Frame || !m_pToolbarLabelSet)
		return;

	RECT bar;
	memset(&bar, 0, sizeof(bar));
	if (!GetClientRect(m_hwnd, &bar))
		return;
	const int count = (int)SendMessageW(m_hwnd, TB_BUTTONCOUNT, 0, 0);
	HMENU menu = CreatePopupMenu();
	if (!menu)
		return;

	int added = 0;
	for (int i = 0; i < count; ++i)
	{
		TBBUTTON btn;
		memset(&btn, 0, sizeof(btn));
		if (!SendMessageW(m_hwnd, TB_GETBUTTON, i, (LPARAM)&btn))
			continue;
		if (btn.fsStyle & TBSTYLE_SEP)
			continue;

		// TB_GETITEMRECT refuses a button in the TBSTATE_HIDDEN state.
		// HIDECLIPPEDBUTTONS sets that state on every button that does
		// not fit, so those commands have to be taken from TB_GETBUTTON.
		const bool hidden = (btn.fsState & TBSTATE_HIDDEN) != 0;
		RECT item;
		const BOOL haveRect = (BOOL)SendMessageW(m_hwnd, TB_GETITEMRECT, i, (LPARAM)&item);
		if (!hidden)
		{
			if (!haveRect)
				continue;
			if (item.left >= bar.left && item.right <= bar.right
			    && item.top >= bar.top && item.bottom <= bar.bottom)
				continue;
		}

		XAP_Toolbar_Id id = ItemIdFromWmCommand(btn.idCommand);
		EV_Toolbar_Label * pLabel = m_pToolbarLabelSet->getLabel(id);
		if (!pLabel)
			continue;
		const char * utf8 = pLabel->getToolbarLabel();
		if (!utf8 || !*utf8)
			utf8 = pLabel->getToolTip();
		if (!utf8 || !*utf8)
			continue;

		UT_Win32LocaleString text;
		text.fromUTF8(utf8);
		UINT flags = MF_STRING;
		if (!(btn.fsState & TBSTATE_ENABLED))
			flags |= MF_GRAYED;
		if (AppendMenuW(menu, flags, btn.idCommand, text.c_str()))
			++added;
	}

	if (!added)
	{
		DestroyMenu(menu);
		return;
	}

	HWND frame = static_cast<XAP_Win32FrameImpl *>(m_pWin32Frame->getFrameImpl())->getTopLevelWindow();
	SetForegroundWindow(frame);
	const UINT cmd = (UINT)TrackPopupMenu(menu,
		TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
		screen.x, screen.y, 0, frame, nullptr);
	PostMessageW(frame, WM_NULL, 0, 0);
	DestroyMenu(menu);
	if (cmd)
		SendMessageW(frame, WM_COMMAND, MAKEWPARAM(cmd, 0), (LPARAM)m_hwnd);
}

void	EV_Win32Toolbar::onDropArrow(UINT cmd)
{
	if (!m_pWin32Frame)
		return;

	XAP_Toolbar_Id id = ItemIdFromWmCommand(cmd);
	if (id != AP_TOOLBAR_ID_COLOR_FORE && id != AP_TOOLBAR_ID_COLOR_BACK)
		return;

	s_showColorPopup(this, cmd, id);
}

