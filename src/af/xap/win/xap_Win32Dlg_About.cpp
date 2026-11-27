/* AbiSource Application Framework
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

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "ut_string.h"
#include "ut_assert.h"
#include "ut_debugmsg.h"
#include "ut_Win32OS.h"
#include "ut_Win32LocaleString.h"

#include "xap_App.h"
#include "xap_Dialog_Id.h"
#include "xap_Win32App.h"
#include "xap_Frame.h"
#include "xap_Win32FrameImpl.h"

#include "xap_Strings.h"
#include "xap_Dialog_Id.h"
#include "xap_Dlg_About.h"
#include "xap_Win32Dlg_About.h"

#include "gr_Win32Graphics.h"
#include "gr_Win32Image.h"
#include "gr_Win32USPGraphics.h"
#include "ut_bytebuf.h"
#include "ut_png.h"

/*****************************************************************/

extern unsigned char g_pngSidebar[];		// see ap_wp_sidebar.cpp
extern unsigned long g_pngSidebar_sizeof;	// see ap_wp_sidebar.cpp

bool XAP_Win32Dialog_About::s_bEventLoopDone;

/*****************************************************************/
XAP_Dialog * XAP_Win32Dialog_About::static_constructor(XAP_DialogFactory * pFactory,
															 XAP_Dialog_Id id)
{
	XAP_Win32Dialog_About * p = new XAP_Win32Dialog_About(pFactory,id);
	return p;
}

XAP_Win32Dialog_About::XAP_Win32Dialog_About(XAP_DialogFactory * pDlgFactory,
											 XAP_Dialog_Id id)
	: XAP_Dialog_About(pDlgFactory,id)
{
	m_pGrImageSidebar = nullptr;
}

XAP_Win32Dialog_About::~XAP_Win32Dialog_About(void)
{
	DELETEP(m_pGrImageSidebar);
}

const int ABOUT_MIN_TEXT_W	= 340;
const int BUTTON_WIDTH		= 84;
const int BUTTON_HEIGHT		= 28;
const int BUTTON_GAP		= 12;

const int ID_BUTTON_URL		= 3000;

static HBRUSH s_aboutBgBrush(COLORREF color)
{
	static HBRUSH brush = nullptr;
	static COLORREF cached = CLR_INVALID;
	if (!brush || cached != color)
	{
		if (brush)
			DeleteObject(brush);
		brush = CreateSolidBrush(color);
		cached = color;
	}
	return brush;
}

static int s_measureWrappedTextHeight(HDC hdc, HFONT hfont, const wchar_t * text, int maxWidth)
{
	if (!hdc || !text || !*text || maxWidth <= 0)
		return 0;
	HGDIOBJ prev = hfont ? SelectObject(hdc, hfont) : nullptr;
	RECT rc = { 0, 0, maxWidth, 0 };
	DrawTextW(hdc, text, -1, &rc, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
	if (prev)
		SelectObject(hdc, prev);
	return rc.bottom - rc.top;
}

static int s_measureSingleLineWidth(HDC hdc, HFONT hfont, const wchar_t * text)
{
	if (!hdc || !text || !*text)
		return 0;
	HGDIOBJ prev = hfont ? SelectObject(hdc, hfont) : nullptr;
	RECT rc = { 0, 0, 0, 0 };
	DrawTextW(hdc, text, -1, &rc, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
	if (prev)
		SelectObject(hdc, prev);
	return rc.right - rc.left;
}

// Windows 11 draws themed static controls with their own fill. A blank
// theme makes them use the brush we return from WM_CTLCOLORSTATIC.
static void turnOffVisualTheme(HWND hwnd)
{
	using SetWindowThemeFn = HRESULT (WINAPI *)(HWND, LPCWSTR, LPCWSTR);
	static SetWindowThemeFn setTheme = nullptr;
	static bool lookedUp = false;
	if (!lookedUp)
	{
		HMODULE themeDll = GetModuleHandleW(L"uxtheme.dll");
		if (!themeDll)
			themeDll = LoadLibraryW(L"uxtheme.dll");
		if (themeDll)
			setTheme = reinterpret_cast<SetWindowThemeFn>(reinterpret_cast<void *>(GetProcAddress(themeDll, "SetWindowTheme")));
		lookedUp = true;
	}
	if (setTheme && hwnd)
		setTheme(hwnd, L"", L"");
}

void XAP_Win32Dialog_About::runModal(XAP_Frame * pFrame)
{
	// raise the dialog
	XAP_Win32App * pWin32App = static_cast<XAP_Win32App *>(XAP_App::getApp());
	UT_Win32LocaleString strbis;
	m_pFrame = pFrame;

	UT_ByteBufPtr pBB(new UT_ByteBuf(g_pngSidebar_sizeof));
	pBB->ins(0,g_pngSidebar,g_pngSidebar_sizeof);

	UT_sint32 iImageWidth = 0;
	UT_sint32 iImageHeight = 0;
		
	UT_PNG_getDimensions(pBB, iImageWidth, iImageHeight);

	HWND hWndFrame = static_cast<XAP_Win32FrameImpl*>(m_pFrame->getFrameImpl())->getTopLevelWindow();
	int scale = 96;
	typedef UINT (WINAPI *GetDpiForWindowFn)(HWND);
	static GetDpiForWindowFn getDpi = nullptr;
	static bool dpiResolved = false;
	if (!dpiResolved)
	{
		dpiResolved = true;
		HMODULE user32 = GetModuleHandleW(L"user32.dll");
		if (user32)
			getDpi = reinterpret_cast<GetDpiForWindowFn>(
				reinterpret_cast<void *>(GetProcAddress(user32, "GetDpiForWindow")));
	}
	if (getDpi && hWndFrame)
	{
		const UINT got = getDpi(hWndFrame);
		if (got >= 96)
			scale = static_cast<int>(got);
	}
	if (scale == 96 && hWndFrame)
	{
		HDC windowDc = GetDC(hWndFrame);
		if (windowDc)
		{
			const int caps = GetDeviceCaps(windowDc, LOGPIXELSY);
			if (caps >= 96)
				scale = caps;
			ReleaseDC(hWndFrame, windowDc);
		}
	}

	const int btnMinW    = MulDiv(BUTTON_WIDTH, scale, 96);
	const int btnH       = MulDiv(BUTTON_HEIGHT, scale, 96);
	const int btnGap     = MulDiv(BUTTON_GAP, scale, 96);
	const int sidebarW   = MulDiv(iImageWidth, scale, 96);
	const int sidebarH   = MulDiv(iImageHeight, scale, 96);
	const int padSmall   = MulDiv(6, scale, 96);
	const int textColW   = MulDiv(ABOUT_MIN_TEXT_W, scale, 96);

	LOGFONTW lf;
	memset(&lf, 0, sizeof(lf));
	wcscpy(lf.lfFaceName, L"Segoe UI");
	lf.lfQuality = CLEARTYPE_QUALITY;

	// A point size is a negative character height. 9 pt is the Windows UI size.
	lf.lfHeight = -MulDiv(9, scale, 72);
	lf.lfWeight = FW_NORMAL;
	HFONT hfontPrimary = CreateFontIndirectW(&lf);

	lf.lfHeight = -MulDiv(9, scale, 72);
	lf.lfWeight = FW_NORMAL;
	HFONT hfontSmall = CreateFontIndirectW(&lf);

	lf.lfHeight = -MulDiv(20, scale, 72);
	lf.lfWeight = FW_SEMIBOLD;
	HFONT hfontHeading = CreateFontIndirectW(&lf);

	const XAP_StringSet* pSS = XAP_App::getApp()->getStringSet();

	UT_Win32LocaleString strTitleFmt, strAppName, strOK, strHeading, strVersion, strCopyright, strUSP;
	strTitleFmt.fromUTF8(pSS->getValue(XAP_STRING_ID_DLG_ABOUT_Title));
	strAppName.fromASCII(XAP_App::getApp()->getApplicationName());
	wchar_t titleBuf[1024];
	// The Microsoft swprintf has no size argument. The format comes from
	// the translation catalog, so keep the write inside titleBuf.
	_snwprintf(titleBuf, G_N_ELEMENTS(titleBuf), strTitleFmt.c_str(), strAppName.c_str());
	titleBuf[G_N_ELEMENTS(titleBuf) - 1] = 0;

	strOK.fromUTF8(pSS->getValue(XAP_STRING_ID_DLG_OK));
	strHeading.fromUTF8(XAP_App::getApp()->getApplicationName());

	const char * versiontext = pSS->getValue(XAP_STRING_ID_DLG_ABOUT_Version);
	UT_UTF8String version = UT_UTF8String_sprintf(versiontext, XAP_App::s_szBuild_Version);
	strVersion.fromUTF8(version.utf8_str());

	strCopyright.fromASCII(XAP_ABOUT_COPYRIGHT);

	wchar_t gplBuf[1024];
	strbis.fromASCII(XAP_ABOUT_GPL_LONG);
	_snwprintf(gplBuf, G_N_ELEMENTS(gplBuf), strbis.c_str(), strAppName.c_str());
	gplBuf[G_N_ELEMENTS(gplBuf) - 1] = 0;

	XAP_Frame * pLastFrame = pWin32App->getLastFocussedFrame();
	AV_View * pLastView = pLastFrame ? pLastFrame->getCurrentView() : nullptr;
	GR_Graphics * pLastG = pLastView ? pLastView->getGraphics() : nullptr;
	const bool haveUSP = (pLastG && pLastG->getClassId() == GRID_WIN32_UNISCRIBE);
	if (haveUSP)
	{
		GR_Win32USPGraphics * pUSP = static_cast<GR_Win32USPGraphics*>(pLastG);
		strUSP.fromASCII(pUSP->getUSPVersion());
	}

	const wchar_t * const kUrlLabel = L"www.abisource.com";
	int headingH   = MulDiv(36, scale, 96);
	int versionH   = MulDiv(24, scale, 96);
	int copyrightH = MulDiv(36, scale, 96);
	int gplH       = MulDiv(220, scale, 96);
	int uspH       = MulDiv(18, scale, 96);
	int okBtnW     = btnMinW;
	int urlBtnW    = btnMinW * 2;

	HDC measureDc = GetDC(nullptr);
	if (measureDc)
	{
		const int mHeading   = s_measureWrappedTextHeight(measureDc, hfontHeading, strHeading.c_str(), textColW);
		const int mVersion   = s_measureWrappedTextHeight(measureDc, hfontPrimary, strVersion.c_str(), textColW);
		const int mCopyright = s_measureWrappedTextHeight(measureDc, hfontPrimary, strCopyright.c_str(), textColW);
		const int mGpl       = s_measureWrappedTextHeight(measureDc, hfontSmall, gplBuf, textColW);
		if (mHeading > 0)   headingH   = mHeading + padSmall;
		if (mVersion > 0)   versionH   = mVersion + padSmall;
		if (mCopyright > 0) copyrightH = mCopyright + padSmall;
		if (mGpl > 0)       gplH       = mGpl + padSmall;
		if (haveUSP)
		{
			const int mUsp = s_measureWrappedTextHeight(measureDc, hfontSmall, strUSP.c_str(), sidebarW > btnGap ? (sidebarW - btnGap) : textColW);
			if (mUsp > 0) uspH = mUsp + padSmall / 2;
		}
		const int mOkW  = s_measureSingleLineWidth(measureDc, hfontPrimary, strOK.c_str()) + 2 * btnGap;
		const int mUrlW = s_measureSingleLineWidth(measureDc, hfontPrimary, kUrlLabel) + 2 * btnGap;
		if (mOkW > okBtnW)   okBtnW  = mOkW;
		if (mUrlW > urlBtnW) urlBtnW = mUrlW;
		ReleaseDC(nullptr, measureDc);
	}

	const int minBtnRowW = okBtnW + urlBtnW + btnGap;
	const int effectiveTextW = (textColW > minBtnRowW) ? textColW : minBtnRowW;
	const int clientW = sidebarW + effectiveTextW + 2 * btnGap;
	const int yHeading   = btnGap;
	const int yVersion   = yHeading + headingH + padSmall;
	const int yCopyright = yVersion + versionH + padSmall;
	const int yGpl       = yCopyright + copyrightH + padSmall;
	const int contentBottom = yGpl + gplH + btnGap + btnH + btnGap;
	const int sidebarMinH   = sidebarH + 2 * btnGap;
	const int clientH = (contentBottom > sidebarMinH) ? contentBottom : sidebarMinH;

	const DWORD dwStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
	RECT rcAdjusted = { 0, 0, clientW, clientH };
	AdjustWindowRectEx(&rcAdjusted, dwStyle, FALSE, 0);
	const int aboutW = rcAdjusted.right - rcAdjusted.left;
	const int aboutH = rcAdjusted.bottom - rcAdjusted.top;

	m_pGrImageSidebar = new GR_Win32Image(nullptr);
	m_pGrImageSidebar->convertFromBuffer(pBB, "image/png", sidebarW, sidebarH);

	const wchar_t * pClassName = L"AbiSource_About";
	
	ATOM a = UT_RegisterClassEx(CS_HREDRAW | CS_VREDRAW, s_dlgProc, pWin32App->getInstance(),
								nullptr, LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW), nullptr, nullptr,
								nullptr, pClassName);
	if (!a)
	{
		DeleteObject(hfontHeading);
		DeleteObject(hfontPrimary);
		DeleteObject(hfontSmall);
		::MessageBeep(MB_ICONEXCLAMATION);
		return;
	}

	RECT rcScreen;
	bool haveWorkArea = false;
	rcScreen.left = rcScreen.top = rcScreen.right = rcScreen.bottom = 0;

#if (_WIN32_WINNT >= 0x0500 )
	if (GetSystemMetrics(SM_CMONITORS) > 1) {
		HMONITOR m = MonitorFromWindow(hWndFrame,MONITOR_DEFAULTTONEAREST);
		if (m) {
			MONITORINFO mi;
			mi.cbSize=sizeof(MONITORINFO);
			if (GetMonitorInfoW(m,&mi)) {
				rcScreen = mi.rcWork;
				haveWorkArea = true;
			}
		}
	}
#endif
	// A primary work area of (0, 0) is valid. Comparing left with top
	// discarded it and replaced it with SM_CXFULLSCREEN, which is the
	// client of a maximized window and still overlaps the taskbar.
	if (!haveWorkArea)
	{
		if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &rcScreen, 0))
			SetRect(&rcScreen, 0, 0, GetSystemMetrics(SM_CXSCREEN),
			        GetSystemMetrics(SM_CYSCREEN));
	}

	const int iScreenWidth  = rcScreen.right - rcScreen.left;
	const int iScreenHeight = rcScreen.bottom - rcScreen.top;

	BringWindowToTop(hWndFrame);
	pWin32App->enableAllTopLevelWindows(FALSE);

	HWND hwndAbout = UT_CreateWindowEx(	0L, pClassName,
										titleBuf,
										dwStyle,
										rcScreen.left + (iScreenWidth - aboutW) / 2,
										rcScreen.top + (iScreenHeight - aboutH) / 2,
										aboutW,
										aboutH,
										hWndFrame,
										nullptr,
										pWin32App->getInstance(),
										nullptr);
	if (!hwndAbout)
	{
		DeleteObject(hfontHeading);
		DeleteObject(hfontPrimary);
		DeleteObject(hfontSmall);
		UnregisterClassW(pClassName, pWin32App->getInstance());
		pWin32App->enableAllTopLevelWindows(true);
		::MessageBeep(MB_ICONEXCLAMATION);
		return;
	}

	SetWindowLongPtrW(hwndAbout, GWLP_USERDATA, (LONG_PTR)this);
	UT_Win32ApplyCaptionTheme(hwndAbout);

	RECT rcClient = {};
	int iWidth = clientW;
	int iHeight = clientH;
	if (GetClientRect(hwndAbout, &rcClient) && rcClient.right > 0 && rcClient.bottom > 0)
	{
		iWidth = rcClient.right;
		iHeight = rcClient.bottom;
	}
	const int textX = sidebarW + btnGap;
	const int actualTextW = (iWidth > textX + btnGap) ? (iWidth - textX - btnGap) : effectiveTextW;
	
	HWND hwndOK = CreateWindowW(L"BUTTON",
							   strOK.c_str(),
							   WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
							   iWidth - okBtnW - btnGap,
							   iHeight - btnH - btnGap,
							   okBtnW,
							   btnH,
							   hwndAbout,
							   (HMENU) IDOK,
							   pWin32App->getInstance(),
							   nullptr);
	UT_Win32ApplyWindowTheme(hwndOK);

	HWND hwndURL = CreateWindowW(L"BUTTON",
								kUrlLabel,
								WS_CHILD | WS_VISIBLE | WS_TABSTOP,
								iWidth - okBtnW - btnGap - urlBtnW - btnGap,
								iHeight - btnH - btnGap,
								urlBtnW,
								btnH,
								hwndAbout,
								(HMENU) ID_BUTTON_URL,
								pWin32App->getInstance(),
								nullptr);
	UT_Win32ApplyWindowTheme(hwndURL);

	HWND hwndStatic_Heading = CreateWindowW(L"STATIC",
										   strHeading.c_str(),
										   WS_CHILD | WS_VISIBLE | SS_CENTER,
										   textX,
										   yHeading,
										   actualTextW,
										   headingH,
										   hwndAbout,
										   (HMENU) 3001,
										   pWin32App->getInstance(),
										   nullptr);
	turnOffVisualTheme(hwndStatic_Heading);

	HWND hwndStatic_Version = CreateWindowW(L"STATIC",
										   strVersion.c_str(),
										   WS_CHILD | WS_VISIBLE | SS_CENTER,
										   textX,
										   yVersion,
										   actualTextW,
										   versionH,
										   hwndAbout,
										   (HMENU) 3002,
										   pWin32App->getInstance(),
										   nullptr);
	turnOffVisualTheme(hwndStatic_Version);

	HWND hwndStatic_Copyright = CreateWindowW(L"STATIC",
											 strCopyright.c_str(),
											 WS_CHILD | WS_VISIBLE | SS_LEFT,
											 textX,
											 yCopyright,
											 actualTextW,
											 copyrightH,
											 hwndAbout,
											 (HMENU) 3003,
											 pWin32App->getInstance(),
											 nullptr);
	turnOffVisualTheme(hwndStatic_Copyright);

	HWND hwndStatic_GPL = CreateWindowW(L"STATIC",
									   gplBuf,
									   WS_CHILD | WS_VISIBLE | SS_LEFT,
									   textX,
									   yGpl,
									   actualTextW,
									   gplH,
									   hwndAbout,
									   (HMENU) 3004,
									   pWin32App->getInstance(),
									   nullptr);
	turnOffVisualTheme(hwndStatic_GPL);

	HWND hwndStatic_USP_Version = nullptr;
	if (haveUSP)
	{
		hwndStatic_USP_Version = CreateWindowW(L"STATIC",
												   strUSP.c_str(),
												   WS_CHILD | WS_VISIBLE | SS_LEFT,
												   btnGap / 2,
												   iHeight - uspH - padSmall,
												   sidebarW > btnGap / 2 ? (sidebarW - btnGap / 2) : sidebarW,
												   uspH,
												   hwndAbout,
												   (HMENU) 3005,
												   pWin32App->getInstance(),
												   nullptr);
		turnOffVisualTheme(hwndStatic_USP_Version);
	}
	
	SendMessageW(hwndStatic_Heading, WM_SETFONT, (WPARAM) hfontHeading, 0);

	HWND rgFontReceivers[] =
		{ hwndOK, hwndURL, hwndStatic_Version, hwndStatic_Copyright };

	for (UT_uint32 iWnd = 0; iWnd < G_N_ELEMENTS(rgFontReceivers); ++iWnd)
	{
		SendMessageW(rgFontReceivers[iWnd], WM_SETFONT, (WPARAM) hfontPrimary, 0);
	}

	SendMessageW(hwndStatic_GPL, WM_SETFONT, (WPARAM) hfontSmall, 0);

	if(hwndStatic_USP_Version)
		SendMessageW(hwndStatic_USP_Version, WM_SETFONT, (WPARAM) hfontSmall, 0);
	
	ShowWindow(hwndAbout, SW_SHOW);
	UpdateWindow(hwndAbout);

	// the event loop
	{
		MSG msg;

		s_bEventLoopDone = false;
	
		while (!s_bEventLoopDone)
		{
			const BOOL got = GetMessageW(&msg, nullptr, 0, 0);
			if (got == 0)
			{
				// A nested loop must hand WM_QUIT back to the outer loop.
				PostQuitMessage(static_cast<int>(msg.wParam));
				break;
			}
			if (got == -1)
				break;

			if( hwndAbout && IsDialogMessageW( hwndAbout, &msg ) )
				continue;

			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}
	}

	pWin32App->enableAllTopLevelWindows(true);

	DestroyWindow(hwndAbout);
	DeleteObject(hfontHeading);
	DeleteObject(hfontPrimary);
	DeleteObject(hfontSmall);

	UnregisterClassW(pClassName, pWin32App->getInstance());

	if (GetWindow(hWndFrame, GW_HWNDFIRST) != hWndFrame)
	{
		BringWindowToTop(hWndFrame);
	}

	if (GetFocus() != hWndFrame)
	{
		SetFocus(hWndFrame);
	}
}

typedef BOOL (WINAPI *AboutAlphaBlendFn)(
	HDC, int, int, int, int,
	HDC, int, int, int, int,
	BLENDFUNCTION);

static AboutAlphaBlendFn aboutAlphaBlend(void)
{
	static AboutAlphaBlendFn fn = nullptr;
	static bool tried = false;
	if (!tried)
	{
		tried = true;
		HMODULE dll = LoadLibraryW(L"msimg32.dll");
		if (dll)
		{
			fn = reinterpret_cast<AboutAlphaBlendFn>(
				reinterpret_cast<void *>(GetProcAddress(dll, "AlphaBlend")));
			if (!fn)
				FreeLibrary(dll);
		}
	}
	return fn;
}

// StretchDIBits ignores the alpha channel, so a transparent logo would
// paint a black rectangle. Blend the straight BGRA pixels over the face.
static bool paintAboutLogo(HDC hdc, GR_Win32Image *pImage, int x, int y, int destW, int destH)
{
	BITMAPINFO *pDIB = pImage ? pImage->getDIB() : nullptr;
	if (!hdc || !pDIB || destW <= 0 || destH <= 0)
		return false;

	int srcW = pDIB->bmiHeader.biWidth;
	int srcH = pDIB->bmiHeader.biHeight;
	if (srcW <= 0 || srcH == 0)
		return false;
	const bool topDown = srcH < 0;
	if (topDown)
		srcH = -srcH;

	UT_Byte *pBits = reinterpret_cast<UT_Byte *>(pDIB) + pDIB->bmiHeader.biSize
		+ pDIB->bmiHeader.biClrUsed * sizeof(RGBQUAD);
	const int bpp = pDIB->bmiHeader.biBitCount / 8;
	if (bpp != 4 || !pImage->hasAlpha())
		return false;
	int stride = srcW * bpp;
	if (stride % 4)
		stride += 4 - (stride % 4);

	AboutAlphaBlendFn blend = aboutAlphaBlend();
	if (!blend)
		return false;

	HDC mem = CreateCompatibleDC(hdc);
	if (!mem)
		return false;

	BITMAPINFO bi;
	memset(&bi, 0, sizeof(bi));
	bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bi.bmiHeader.biWidth = srcW;
	bi.bmiHeader.biHeight = srcH;
	bi.bmiHeader.biPlanes = 1;
	bi.bmiHeader.biBitCount = 32;
	bi.bmiHeader.biCompression = BI_RGB;

	void *bits = nullptr;
	HBITMAP hbmp = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
	bool blended = false;
	if (hbmp && bits)
	{
		const int dstStride = srcW * 4;
		UT_Byte *dst = static_cast<UT_Byte *>(bits);
		for (int row = 0; row < srcH; ++row)
		{
			const int srcRow = topDown ? (srcH - 1 - row) : row;
			const UT_Byte *s = pBits + (size_t)srcRow * (size_t)stride;
			UT_Byte *d = dst + (size_t)row * (size_t)dstStride;
			for (int col = 0; col < srcW; ++col)
			{
				const UT_Byte a = s[3];
				d[0] = (UT_Byte)((s[0] * a + 127) / 255);
				d[1] = (UT_Byte)((s[1] * a + 127) / 255);
				d[2] = (UT_Byte)((s[2] * a + 127) / 255);
				d[3] = a;
				s += 4;
				d += 4;
			}
		}
		HGDIOBJ prev = SelectObject(mem, hbmp);
		BLENDFUNCTION blendfn;
		blendfn.BlendOp = AC_SRC_OVER;
		blendfn.BlendFlags = 0;
		blendfn.SourceConstantAlpha = 255;
		blendfn.AlphaFormat = AC_SRC_ALPHA;
		blended = blend(hdc, x, y, destW, destH, mem, 0, 0, srcW, srcH, blendfn) != FALSE;
		if (prev)
			SelectObject(mem, prev);
	}
	if (hbmp)
		DeleteObject(hbmp);
	DeleteDC(mem);
	return blended;
}

LRESULT CALLBACK XAP_Win32Dialog_About::s_dlgProc(HWND hWnd,UINT msg,WPARAM wParam,LPARAM lParam)
{
	// A window procedure must return LRESULT. BOOL leaves the top half
	// of RAX undefined, so WM_NCCALCSIZE comes back nonzero and Windows
	// recalculates the frame on every paint. That is the flicker.

	XAP_Win32Dialog_About * pThis = (XAP_Win32Dialog_About *)GetWindowLongPtrW(hWnd,GWLP_USERDATA);

	if (!pThis)
	{
		return UT_DefWindowProc(hWnd, msg, wParam, lParam);
	}
	
	switch (msg)
	{
	case WM_ERASEBKGND:
	{
		const UT_Win32ThemeColors tc = UT_Win32GetThemeColors();
		RECT r = {};
		if (wParam && GetClientRect(hWnd, &r))
			FillRect((HDC)wParam, &r, s_aboutBgBrush(tc.chromeBg));
		return 1;
	}

	case WM_CTLCOLORSTATIC:
	{
		const UT_Win32ThemeColors tc = UT_Win32GetThemeColors();
		HDC hdcCtrl = (HDC)wParam;
		HWND hwndCtrl = (HWND)lParam;
		RECT rcCtrl = {};
		// A zero MapWindowPoints result is also a successful move of 0 pixels.
		const bool placed = hwndCtrl && GetWindowRect(hwndCtrl, &rcCtrl);
		if (placed)
			MapWindowPoints(nullptr, hWnd, reinterpret_cast<POINT *>(&rcCtrl), 2);
		int imageWidth = pThis->m_pGrImageSidebar ? pThis->m_pGrImageSidebar->getDisplayWidth() : 0;
		SetTextColor(hdcCtrl, tc.text);
		// The version string sits on the sidebar picture. The other
		// labels use the same face color as the rest of the window.
		if (placed && rcCtrl.left < imageWidth)
		{
			SetBkMode(hdcCtrl, TRANSPARENT);
			return (LRESULT)GetStockObject(HOLLOW_BRUSH);
		}
		SetBkColor(hdcCtrl, tc.chromeBg);
		SetBkMode(hdcCtrl, OPAQUE);
		return (LRESULT)s_aboutBgBrush(tc.chromeBg);
	}

	case WM_PAINT:
	{
		const UT_Win32ThemeColors tc = UT_Win32GetThemeColors();
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hWnd, &ps);
		if (!hdc)
		{
			EndPaint(hWnd, &ps);
			return 0;
		}

		RECT r = {};
		if (!GetClientRect(hWnd, &r))
		{
			EndPaint(hWnd, &ps);
			return 0;
		}
		FillRect(hdc, &r, s_aboutBgBrush(tc.chromeBg));
		GR_Win32Image * pImage = pThis->m_pGrImageSidebar;
		BITMAPINFO * pDIB = pImage ? pImage->getDIB() : nullptr;
		if (pImage && pDIB)
		{
			int imageWidth = pImage->getDisplayWidth();
			int imageHeight = pImage->getDisplayHeight();
			int y = (r.bottom - imageHeight) / 2;
			if (!paintAboutLogo(hdc, pImage, 0, y, imageWidth, imageHeight))
			{
				UT_Byte * pBits = ((unsigned char *) pDIB) + pDIB->bmiHeader.biSize
					+ pDIB->bmiHeader.biClrUsed * sizeof(RGBQUAD);
				const int previousMode = SetStretchBltMode(hdc, HALFTONE);
				POINT previousOrg;
				SetBrushOrgEx(hdc, 0, 0, &previousOrg);
				StretchDIBits(hdc,
							  0, y, imageWidth, imageHeight,
							  0, 0, pDIB->bmiHeader.biWidth, pDIB->bmiHeader.biHeight,
							  pBits, pDIB, DIB_RGB_COLORS, SRCCOPY);
				SetBrushOrgEx(hdc, previousOrg.x, previousOrg.y, nullptr);
				SetStretchBltMode(hdc, previousMode);
			}
		}

		EndPaint(hWnd,&ps);
		return 0;
	}

	case WM_CHAR:
	{
		switch (wParam)
		{
		case 13:	// CR
		case 27:	// ESC
		case 32:	// SPACE
			s_bEventLoopDone = true;
			return 0;
		}
		
		break;
	}

	case WM_COMMAND:
		return pThis->_onCommand(hWnd,wParam,lParam);

	case WM_CLOSE:
	case WM_DESTROY:
		s_bEventLoopDone = true;
		return 0;
	}

	return UT_DefWindowProc(hWnd, msg, wParam, lParam);
}

BOOL XAP_Win32Dialog_About::_onCommand(HWND /*hWnd*/, WPARAM wParam, LPARAM /*lParam*/)
{
	WORD wId = LOWORD(wParam);

	switch (wId)
	{
	case IDCANCEL:
	case IDOK:							// also XAP_RID_DIALOG_ABOUT_BTN_OK
		s_bEventLoopDone = true;
		return 0;

	case ID_BUTTON_URL:
		XAP_App::getApp()->openURL("http://www.abisource.com/");
		return 0;
		
	default:							// we did not handle this notification
		UT_DEBUGMSG(("WM_Command for id %ld\n",wId));
		return 0;						// return zero to let windows take care of it.
	}
}
