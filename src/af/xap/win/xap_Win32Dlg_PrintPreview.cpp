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
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <string>
#include <vector>

#include "ut_assert.h"
#include "ut_string.h"
#include "ut_debugmsg.h"
#include "ut_Win32OS.h"
#include "ut_Win32LocaleString.h"
#include "xap_Dialog_Id.h"
#include "xap_Win32Dlg_PrintPreview.h"
#include "xap_Frame.h"
#include "xap_App.h"
#include "xap_Win32App.h"
#include "xap_Win32FrameImpl.h"
#include "xap_DialogFactory.h"
#include "gr_Win32Graphics.h"
#include "fp_PageSize.h"
#include "fv_View.h"

namespace {

struct PreviewPage
{
	HENHMETAFILE hmf;
	RECT rectHiMetric; // in 0.01 mm units
};

static DOCINFOW * s_getDocInfo()
{
	DOCINFOW * pDocInfo = new DOCINFOW;
	memset(pDocInfo, 0, sizeof(DOCINFOW));
	pDocInfo->cbSize = sizeof(DOCINFOW);
	pDocInfo->lpszDocName = L"AbiWord Print Preview";
	return pDocInfo;
}

static HDC s_previewReferenceDC()
{
	HDC dc = CreateDCW(L"DISPLAY", nullptr, nullptr, nullptr);
	if (!dc)
		dc = GR_Win32Graphics::createbestmetafilehdc();
	return dc;
}

static COLORREF s_blendColor(COLORREF base, COLORREF target, double alpha)
{
	const int r = static_cast<int>(GetRValue(base) * alpha + GetRValue(target) * (1.0 - alpha) + 0.5);
	const int g = static_cast<int>(GetGValue(base) * alpha + GetGValue(target) * (1.0 - alpha) + 0.5);
	const int b = static_cast<int>(GetBValue(base) * alpha + GetBValue(target) * (1.0 - alpha) + 0.5);
	return RGB(std::max(0, std::min(255, r)),
	           std::max(0, std::min(255, g)),
	           std::max(0, std::min(255, b)));
}

static HBRUSH s_previewBgBrush(COLORREF color)
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

enum PreviewControlId
{
	IDC_PREV_PAGE = 4001,
	IDC_PAGE_LABEL = 4002,
	IDC_NEXT_PAGE = 4003,
	IDC_ZOOM_OUT = 4004,
	IDC_ZOOM_LABEL = 4005,
	IDC_ZOOM_IN = 4006,
	IDC_ZOOM_FIT = 4007,
	IDC_ZOOM_100 = 4008,
	IDC_CLOSE_PREVIEW = 4009
};

static const int kZoomSteps[] = { 25, 50, 75, 100, 125, 150, 200, 300 };

struct PrintPreviewState
{
	const std::vector<PreviewPage> * pages;
	size_t currentPage;
	bool fitPage;
	int zoomPercent;
	int scrollX;
	int scrollY;
	int toolbarHeight;
	int dpi;
	bool done;

	HWND hwnd;
	HWND btnPrev;
	HWND lblPage;
	HWND btnNext;
	HWND btnZoomOut;
	HWND lblZoom;
	HWND btnZoomIn;
	HWND btnFit;
	HWND btn100;
	HWND btnClose;
	HFONT uiFont;

	PrintPreviewState()
		: pages(nullptr)
		, currentPage(0)
		, fitPage(true)
		, zoomPercent(100)
		, scrollX(0)
		, scrollY(0)
		, toolbarHeight(44)
		, dpi(96)
		, done(false)
		, hwnd(nullptr)
		, btnPrev(nullptr)
		, lblPage(nullptr)
		, btnNext(nullptr)
		, btnZoomOut(nullptr)
		, lblZoom(nullptr)
		, btnZoomIn(nullptr)
		, btnFit(nullptr)
		, btn100(nullptr)
		, btnClose(nullptr)
		, uiFont(nullptr)
	{
	}
};

static void s_computePaperMetrics(const PrintPreviewState & st, int canvasW, int canvasH,
                                  int & outPaperW, int & outPaperH, int & outEffectiveZoom)
{
	double widthMm = 210.0;
	double heightMm = 297.0;
	if (st.pages && st.currentPage < st.pages->size())
	{
		const RECT & r = (*st.pages)[st.currentPage].rectHiMetric;
		if (r.right - r.left > 100)
			widthMm = static_cast<double>(r.right - r.left) / 100.0;
		if (r.bottom - r.top > 100)
			heightMm = static_cast<double>(r.bottom - r.top) / 100.0;
	}

	const double baseW = (widthMm / 25.4) * static_cast<double>(st.dpi);
	const double baseH = (heightMm / 25.4) * static_cast<double>(st.dpi);

	if (st.fitPage)
	{
		const int margin = UT_ui_px_at_dpi(24, st.dpi);
		const double availW = static_cast<double>(std::max(64, canvasW - 2 * margin));
		const double availH = static_cast<double>(std::max(64, canvasH - 2 * margin));
		const double scaleW = availW / baseW;
		const double scaleH = availH / baseH;
		const double scale = std::min(scaleW, scaleH);
		outPaperW = std::max(1, static_cast<int>(baseW * scale + 0.5));
		outPaperH = std::max(1, static_cast<int>(baseH * scale + 0.5));
		outEffectiveZoom = std::max(10, static_cast<int>(scale * 100.0 + 0.5));
	}
	else
	{
		const double scale = static_cast<double>(st.zoomPercent) / 100.0;
		outPaperW = std::max(1, static_cast<int>(baseW * scale + 0.5));
		outPaperH = std::max(1, static_cast<int>(baseH * scale + 0.5));
		outEffectiveZoom = st.zoomPercent;
	}
}

static void s_updateToolbarControls(PrintPreviewState & st)
{
	if (!st.hwnd || !st.pages)
		return;

	const size_t total = st.pages->size();
	wchar_t pageBuf[64];
	_snwprintf(pageBuf, G_N_ELEMENTS(pageBuf), L"Page %u / %u",
		static_cast<unsigned>(st.currentPage + 1),
		static_cast<unsigned>(total));
	pageBuf[G_N_ELEMENTS(pageBuf) - 1] = 0;
	if (st.lblPage)
		SetWindowTextW(st.lblPage, pageBuf);

	if (st.btnPrev)
		EnableWindow(st.btnPrev, st.currentPage > 0 ? TRUE : FALSE);
	if (st.btnNext)
		EnableWindow(st.btnNext, (st.currentPage + 1 < total) ? TRUE : FALSE);

	RECT rc = {};
	GetClientRect(st.hwnd, &rc);
	const int canvasW = std::max(1, static_cast<int>(rc.right - rc.left));
	const int canvasH = std::max(1, static_cast<int>(rc.bottom - rc.top - st.toolbarHeight));
	int paperW = 0, paperH = 0, effZoom = 100;
	s_computePaperMetrics(st, canvasW, canvasH, paperW, paperH, effZoom);

	wchar_t zoomBuf[32];
	_snwprintf(zoomBuf, G_N_ELEMENTS(zoomBuf), L"%d %%", effZoom);
	zoomBuf[G_N_ELEMENTS(zoomBuf) - 1] = 0;
	if (st.lblZoom)
		SetWindowTextW(st.lblZoom, zoomBuf);
}

static void s_updateScrollbars(PrintPreviewState & st)
{
	if (!st.hwnd)
		return;
	RECT rc = {};
	GetClientRect(st.hwnd, &rc);
	const int canvasW = std::max(1, static_cast<int>(rc.right - rc.left));
	const int canvasH = std::max(1, static_cast<int>(rc.bottom - rc.top - st.toolbarHeight));
	const int margin = UT_ui_px_at_dpi(24, st.dpi);

	int paperW = 0, paperH = 0, effZoom = 100;
	s_computePaperMetrics(st, canvasW, canvasH, paperW, paperH, effZoom);

	const int totalW = paperW + 2 * margin;
	const int totalH = paperH + 2 * margin;
	const int maxScrollX = std::max(0, totalW - canvasW);
	const int maxScrollY = std::max(0, totalH - canvasH);

	st.scrollX = std::max(0, std::min(st.scrollX, maxScrollX));
	st.scrollY = std::max(0, std::min(st.scrollY, maxScrollY));

	SCROLLINFO si;
	memset(&si, 0, sizeof(si));
	si.cbSize = sizeof(si);
	si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
	si.nMin = 0;
	si.nMax = (maxScrollX > 0) ? (totalW - 1) : 0;
	si.nPage = (maxScrollX > 0) ? static_cast<UINT>(canvasW) : 0;
	si.nPos = st.scrollX;
	SetScrollInfo(st.hwnd, SB_HORZ, &si, TRUE);

	si.nMax = (maxScrollY > 0) ? (totalH - 1) : 0;
	si.nPage = (maxScrollY > 0) ? static_cast<UINT>(canvasH) : 0;
	si.nPos = st.scrollY;
	SetScrollInfo(st.hwnd, SB_VERT, &si, TRUE);
}

static void s_layoutToolbar(PrintPreviewState & st)
{
	if (!st.hwnd)
		return;
	RECT rc = {};
	GetClientRect(st.hwnd, &rc);
	const int w = rc.right - rc.left;
	const int pad = UT_ui_px_at_dpi(8, st.dpi);
	const int btnH = UT_ui_px_at_dpi(28, st.dpi);
	const int y = (st.toolbarHeight - btnH) / 2;

	int x = pad;
	const int prevW = UT_ui_px_at_dpi(96, st.dpi);
	const int pageW = UT_ui_px_at_dpi(96, st.dpi);
	const int nextW = UT_ui_px_at_dpi(96, st.dpi);
	const int smallBtnW = UT_ui_px_at_dpi(32, st.dpi);
	const int zoomLblW = UT_ui_px_at_dpi(56, st.dpi);
	const int fitW = UT_ui_px_at_dpi(74, st.dpi);
	const int z100W = UT_ui_px_at_dpi(54, st.dpi);
	const int closeW = UT_ui_px_at_dpi(80, st.dpi);

	if (st.btnPrev)    MoveWindow(st.btnPrev, x, y, prevW, btnH, TRUE);
	x += prevW + pad / 2;
	if (st.lblPage)    MoveWindow(st.lblPage, x, y + UT_ui_px_at_dpi(4, st.dpi), pageW, btnH - UT_ui_px_at_dpi(4, st.dpi), TRUE);
	x += pageW + pad / 2;
	if (st.btnNext)    MoveWindow(st.btnNext, x, y, nextW, btnH, TRUE);
	x += nextW + pad * 2;
	if (st.btnZoomOut) MoveWindow(st.btnZoomOut, x, y, smallBtnW, btnH, TRUE);
	x += smallBtnW + pad / 2;
	if (st.lblZoom)    MoveWindow(st.lblZoom, x, y + UT_ui_px_at_dpi(4, st.dpi), zoomLblW, btnH - UT_ui_px_at_dpi(4, st.dpi), TRUE);
	x += zoomLblW + pad / 2;
	if (st.btnZoomIn)  MoveWindow(st.btnZoomIn, x, y, smallBtnW, btnH, TRUE);
	x += smallBtnW + pad;
	if (st.btnFit)     MoveWindow(st.btnFit, x, y, fitW, btnH, TRUE);
	x += fitW + pad / 2;
	if (st.btn100)     MoveWindow(st.btn100, x, y, z100W, btnH, TRUE);

	const int closeX = std::max(x + z100W + pad, w - closeW - pad);
	if (st.btnClose)   MoveWindow(st.btnClose, closeX, y, closeW, btnH, TRUE);
}

static void s_redrawCanvas(PrintPreviewState & st)
{
	s_updateToolbarControls(st);
	s_updateScrollbars(st);
	if (st.hwnd)
		InvalidateRect(st.hwnd, nullptr, FALSE);
}

static void s_stepZoom(PrintPreviewState & st, bool zoomIn)
{
	RECT rc = {};
	GetClientRect(st.hwnd, &rc);
	const int canvasW = std::max(1, static_cast<int>(rc.right - rc.left));
	const int canvasH = std::max(1, static_cast<int>(rc.bottom - rc.top - st.toolbarHeight));
	int paperW = 0, paperH = 0, curZoom = st.zoomPercent;
	s_computePaperMetrics(st, canvasW, canvasH, paperW, paperH, curZoom);

	int nextZoom = curZoom;
	const size_t nSteps = G_N_ELEMENTS(kZoomSteps);
	if (zoomIn)
	{
		nextZoom = kZoomSteps[nSteps - 1];
		for (size_t i = 0; i < nSteps; ++i)
		{
			if (kZoomSteps[i] > curZoom)
			{
				nextZoom = kZoomSteps[i];
				break;
			}
		}
	}
	else
	{
		nextZoom = kZoomSteps[0];
		for (size_t i = nSteps; i > 0; --i)
		{
			if (kZoomSteps[i - 1] < curZoom)
			{
				nextZoom = kZoomSteps[i - 1];
				break;
			}
		}
	}

	st.fitPage = false;
	st.zoomPercent = nextZoom;
	s_redrawCanvas(st);
}

static void s_goToPage(PrintPreviewState & st, size_t target)
{
	if (!st.pages || st.pages->empty())
		return;
	if (target >= st.pages->size())
		target = st.pages->size() - 1;
	if (target != st.currentPage)
	{
		st.currentPage = target;
		st.scrollX = 0;
		st.scrollY = 0;
		s_redrawCanvas(st);
	}
}

static void s_paintPreview(PrintPreviewState & st, HDC hdc)
{
	RECT rcClient = {};
	if (!GetClientRect(st.hwnd, &rcClient))
		return;
	const int w = rcClient.right - rcClient.left;
	const int h = rcClient.bottom - rcClient.top;
	if (w <= 0 || h <= 0)
		return;

	const UT_Win32ThemeColors tc = UT_Win32GetThemeColors();

	HDC memDc = CreateCompatibleDC(hdc);
	HBITMAP memBmp = memDc ? CreateCompatibleBitmap(hdc, w, h) : nullptr;
	if (!memDc || !memBmp)
	{
		if (memBmp)
			DeleteObject(memBmp);
		if (memDc)
			DeleteDC(memDc);
		return;
	}

	HGDIOBJ prevBmp = SelectObject(memDc, memBmp);
	HBRUSH dcBrush = static_cast<HBRUSH>(GetStockObject(DC_BRUSH));
	COLORREF prevBrush = SetDCBrushColor(memDc, tc.chromeBg);

	// Fill entire window (top bar + canvas backdrop).
	FillRect(memDc, &rcClient, dcBrush);

	// 1px bottom hairline under the top bar.
	RECT dividerRc = { 0, st.toolbarHeight - 1, w, st.toolbarHeight };
	SetDCBrushColor(memDc, tc.border);
	FillRect(memDc, &dividerRc, dcBrush);

	// Clip page rendering strictly to the canvas below the top bar.
	const int canvasY = st.toolbarHeight;
	const int canvasW = w;
	const int canvasH = std::max(0, h - canvasY);

	if (canvasH > 0 && st.pages && st.currentPage < st.pages->size())
	{
		HRGN clipRgn = CreateRectRgn(0, canvasY, w, h);
		if (clipRgn)
			SelectClipRgn(memDc, clipRgn);

		int paperW = 0, paperH = 0, effZoom = 100;
		s_computePaperMetrics(st, canvasW, canvasH, paperW, paperH, effZoom);
		const int margin = UT_ui_px_at_dpi(24, st.dpi);

		int paperX = (paperW + 2 * margin <= canvasW)
			? (canvasW - paperW) / 2
			: (margin - st.scrollX);
		int paperY = (paperH + 2 * margin <= canvasH)
			? (canvasY + (canvasH - paperH) / 2)
			: (canvasY + margin - st.scrollY);

		// Soft 4-layer elevation shadow around the paper sheet.
		if (!tc.highContrast)
		{
			const COLORREF shadowBase = RGB(0, 0, 0);
			const double lightAlphas[4] = { 0.22, 0.13, 0.07, 0.03 };
			const double darkAlphas[4]  = { 0.45, 0.28, 0.15, 0.07 };
			const double * alphas = tc.dark ? darkAlphas : lightAlphas;

			for (int layer = 4; layer >= 1; --layer)
			{
				const int off = UT_ui_px_at_dpi(layer, st.dpi);
				RECT shRc = {
					paperX - (layer == 1 ? 1 : 0),
					paperY - (layer == 1 ? 1 : 0),
					paperX + paperW + off + 1,
					paperY + paperH + off + 1
				};
				SetDCBrushColor(memDc, s_blendColor(shadowBase, tc.chromeBg, alphas[layer - 1]));
				FillRect(memDc, &shRc, dcBrush);
			}
		}

		// Subtle 1px hairline border + white paper background.
		RECT borderRc = { paperX - 1, paperY - 1, paperX + paperW + 1, paperY + paperH + 1 };
		SetDCBrushColor(memDc, tc.dark ? RGB(64, 64, 64) : RGB(210, 210, 210));
		FillRect(memDc, &borderRc, dcBrush);

		RECT paperRc = { paperX, paperY, paperX + paperW, paperY + paperH };
		SetDCBrushColor(memDc, RGB(255, 255, 255));
		FillRect(memDc, &paperRc, dcBrush);

		// Replay the vector enhanced metafile crisply onto the paper rectangle.
		const PreviewPage & page = (*st.pages)[st.currentPage];
		if (page.hmf)
		{
			const int saved = SaveDC(memDc);
			SetStretchBltMode(memDc, HALFTONE);
			SetBrushOrgEx(memDc, 0, 0, nullptr);
			PlayEnhMetaFile(memDc, page.hmf, &paperRc);
			if (saved)
				RestoreDC(memDc, saved);
		}

		if (clipRgn)
		{
			SelectClipRgn(memDc, nullptr);
			DeleteObject(clipRgn);
		}
	}

	SetDCBrushColor(memDc, prevBrush);
	BitBlt(hdc, 0, 0, w, h, memDc, 0, 0, SRCCOPY);
	SelectObject(memDc, prevBmp);
	DeleteObject(memBmp);
	DeleteDC(memDc);
}

static LRESULT CALLBACK s_previewWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	PrintPreviewState * st = reinterpret_cast<PrintPreviewState *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
	if (!st)
		return DefWindowProcW(hwnd, msg, wParam, lParam);

	switch (msg)
	{
	case WM_ERASEBKGND:
		return 1;

	case WM_CTLCOLORSTATIC:
	{
		const UT_Win32ThemeColors tc = UT_Win32GetThemeColors();
		HDC hdcCtrl = reinterpret_cast<HDC>(wParam);
		SetTextColor(hdcCtrl, tc.text);
		SetBkColor(hdcCtrl, tc.chromeBg);
		SetBkMode(hdcCtrl, OPAQUE);
		return reinterpret_cast<LRESULT>(s_previewBgBrush(tc.chromeBg));
	}

	case WM_SIZE:
		s_layoutToolbar(*st);
		s_redrawCanvas(*st);
		return 0;

	case WM_PAINT:
	{
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hwnd, &ps);
		if (hdc)
			s_paintPreview(*st, hdc);
		EndPaint(hwnd, &ps);
		return 0;
	}

	case WM_COMMAND:
	{
		switch (LOWORD(wParam))
		{
		case IDC_PREV_PAGE:
			if (st->currentPage > 0)
				s_goToPage(*st, st->currentPage - 1);
			return 0;
		case IDC_NEXT_PAGE:
			if (st->pages && st->currentPage + 1 < st->pages->size())
				s_goToPage(*st, st->currentPage + 1);
			return 0;
		case IDC_ZOOM_OUT:
			s_stepZoom(*st, false);
			return 0;
		case IDC_ZOOM_IN:
			s_stepZoom(*st, true);
			return 0;
		case IDC_ZOOM_FIT:
			st->fitPage = true;
			st->scrollX = 0;
			st->scrollY = 0;
			s_redrawCanvas(*st);
			return 0;
		case IDC_ZOOM_100:
			st->fitPage = false;
			st->zoomPercent = 100;
			s_redrawCanvas(*st);
			return 0;
		case IDC_CLOSE_PREVIEW:
		case IDCANCEL:
			st->done = true;
			return 0;
		}
		break;
	}

	case WM_VSCROLL:
	{
		SCROLLINFO si;
		memset(&si, 0, sizeof(si));
		si.cbSize = sizeof(si);
		si.fMask = SIF_ALL;
		GetScrollInfo(hwnd, SB_VERT, &si);
		int pos = si.nPos;
		const int step = UT_ui_px_at_dpi(48, st->dpi);
		switch (LOWORD(wParam))
		{
		case SB_LINEUP:        pos -= step; break;
		case SB_LINEDOWN:      pos += step; break;
		case SB_PAGEUP:        pos -= static_cast<int>(si.nPage); break;
		case SB_PAGEDOWN:      pos += static_cast<int>(si.nPage); break;
		case SB_THUMBTRACK:    pos = si.nTrackPos; break;
		case SB_THUMBPOSITION: pos = si.nTrackPos; break;
		default: break;
		}
		st->scrollY = pos;
		s_updateScrollbars(*st);
		InvalidateRect(hwnd, nullptr, FALSE);
		return 0;
	}

	case WM_HSCROLL:
	{
		SCROLLINFO si;
		memset(&si, 0, sizeof(si));
		si.cbSize = sizeof(si);
		si.fMask = SIF_ALL;
		GetScrollInfo(hwnd, SB_HORZ, &si);
		int pos = si.nPos;
		const int step = UT_ui_px_at_dpi(48, st->dpi);
		switch (LOWORD(wParam))
		{
		case SB_LINELEFT:      pos -= step; break;
		case SB_LINERIGHT:     pos += step; break;
		case SB_PAGELEFT:      pos -= static_cast<int>(si.nPage); break;
		case SB_PAGERIGHT:     pos += static_cast<int>(si.nPage); break;
		case SB_THUMBTRACK:    pos = si.nTrackPos; break;
		case SB_THUMBPOSITION: pos = si.nTrackPos; break;
		default: break;
		}
		st->scrollX = pos;
		s_updateScrollbars(*st);
		InvalidateRect(hwnd, nullptr, FALSE);
		return 0;
	}

	case WM_MOUSEWHEEL:
	{
		const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
		if (GET_KEYSTATE_WPARAM(wParam) & MK_CONTROL)
		{
			s_stepZoom(*st, delta > 0);
			return 0;
		}
		SCROLLINFO si;
		memset(&si, 0, sizeof(si));
		si.cbSize = sizeof(si);
		si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
		GetScrollInfo(hwnd, SB_VERT, &si);
		if (si.nMax > 0 && static_cast<int>(si.nPage) <= si.nMax)
		{
			st->scrollY -= (delta / WHEEL_DELTA) * UT_ui_px_at_dpi(72, st->dpi);
			s_updateScrollbars(*st);
			InvalidateRect(hwnd, nullptr, FALSE);
		}
		else if (delta < 0 && st->pages && st->currentPage + 1 < st->pages->size())
		{
			s_goToPage(*st, st->currentPage + 1);
		}
		else if (delta > 0 && st->currentPage > 0)
		{
			s_goToPage(*st, st->currentPage - 1);
		}
		return 0;
	}

	case WM_KEYDOWN:
	{
		switch (wParam)
		{
		case VK_ESCAPE:
			st->done = true;
			return 0;
		case VK_PRIOR:
		case VK_LEFT:
			if (st->currentPage > 0)
				s_goToPage(*st, st->currentPage - 1);
			return 0;
		case VK_NEXT:
		case VK_RIGHT:
			if (st->pages && st->currentPage + 1 < st->pages->size())
				s_goToPage(*st, st->currentPage + 1);
			return 0;
		case VK_HOME:
			s_goToPage(*st, 0);
			return 0;
		case VK_END:
			if (st->pages && !st->pages->empty())
				s_goToPage(*st, st->pages->size() - 1);
			return 0;
		case VK_ADD:
		case VK_OEM_PLUS:
			s_stepZoom(*st, true);
			return 0;
		case VK_SUBTRACT:
		case VK_OEM_MINUS:
			s_stepZoom(*st, false);
			return 0;
		case '0':
		case VK_NUMPAD0:
			st->fitPage = true;
			st->scrollX = 0;
			st->scrollY = 0;
			s_redrawCanvas(*st);
			return 0;
		case '1':
		case VK_NUMPAD1:
			st->fitPage = false;
			st->zoomPercent = 100;
			s_redrawCanvas(*st);
			return 0;
		}
		break;
	}

	case WM_CLOSE:
	case WM_DESTROY:
		st->done = true;
		return 0;
	}

	return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static bool s_runPrintPreviewWindow(XAP_Frame * pFrame, const std::wstring & docTitle,
                                    const std::vector<PreviewPage> & pages)
{
	if (!pFrame || pages.empty())
		return false;

	XAP_Win32App * pWin32App = static_cast<XAP_Win32App *>(XAP_App::getApp());
	if (!pWin32App)
		return false;

	XAP_Win32FrameImpl * pFrameImpl = static_cast<XAP_Win32FrameImpl *>(pFrame->getFrameImpl());
	HWND hWndParent = pFrameImpl ? pFrameImpl->getTopLevelWindow() : nullptr;

	const wchar_t * const kClassName = L"AbiWord_PrintPreview";
	WNDCLASSEXW wc;
	memset(&wc, 0, sizeof(wc));
	wc.cbSize = sizeof(wc);
	wc.style = CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc = s_previewWndProc;
	wc.hInstance = pWin32App->getInstance();
	wc.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
	wc.lpszClassName = kClassName;
	RegisterClassExW(&wc);

	RECT rcWork = {};
	bool haveWork = false;
	if (hWndParent)
	{
		HMONITOR mon = MonitorFromWindow(hWndParent, MONITOR_DEFAULTTONEAREST);
		if (mon)
		{
			MONITORINFO mi;
			mi.cbSize = sizeof(mi);
			if (GetMonitorInfoW(mon, &mi))
			{
				rcWork = mi.rcWork;
				haveWork = true;
			}
		}
	}
	if (!haveWork && !SystemParametersInfoW(SPI_GETWORKAREA, 0, &rcWork, 0))
		SetRect(&rcWork, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));

	const int workW = rcWork.right - rcWork.left;
	const int workH = rcWork.bottom - rcWork.top;
	const int winW = std::max(640, (workW * 82) / 100);
	const int winH = std::max(480, (workH * 86) / 100);
	const int winX = rcWork.left + (workW - winW) / 2;
	const int winY = rcWork.top + (workH - winH) / 2;

	std::wstring title = L"Aper\u00e7u avant impression";
	if (!docTitle.empty())
	{
		title += L" \u2014 ";
		title += docTitle;
	}

	PrintPreviewState st;
	st.pages = &pages;
	st.dpi = hWndParent ? UT_Win32WindowDpi(hWndParent) : 96;
	st.toolbarHeight = UT_ui_px_at_dpi(44, st.dpi);

	pWin32App->enableAllTopLevelWindows(FALSE);

	HWND hwnd = CreateWindowExW(
		0,
		kClassName,
		title.c_str(),
		WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
		winX, winY, winW, winH,
		hWndParent,
		nullptr,
		pWin32App->getInstance(),
		nullptr);
	if (!hwnd)
	{
		pWin32App->enableAllTopLevelWindows(TRUE);
		UnregisterClassW(kClassName, pWin32App->getInstance());
		return false;
	}

	st.hwnd = hwnd;
	SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&st));
	UT_Win32ApplyCaptionTheme(hwnd);

	LOGFONTW lf;
	memset(&lf, 0, sizeof(lf));
	wcscpy(lf.lfFaceName, L"Segoe UI");
	lf.lfHeight = -MulDiv(9, st.dpi, 72);
	lf.lfWeight = FW_NORMAL;
	lf.lfQuality = CLEARTYPE_QUALITY;
	st.uiFont = CreateFontIndirectW(&lf);

	HINSTANCE hInst = pWin32App->getInstance();
	st.btnPrev = CreateWindowW(L"BUTTON", L"\u25c0 Pr\u00e9c\u00e9dent",
		WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd,
		reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_PREV_PAGE)), hInst, nullptr);
	st.lblPage = CreateWindowW(L"STATIC", L"Page 1 / 1",
		WS_CHILD | WS_VISIBLE | SS_CENTER, 0, 0, 0, 0, hwnd,
		reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_PAGE_LABEL)), hInst, nullptr);
	st.btnNext = CreateWindowW(L"BUTTON", L"Suivant \u25b6",
		WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd,
		reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_NEXT_PAGE)), hInst, nullptr);
	st.btnZoomOut = CreateWindowW(L"BUTTON", L"\u2212",
		WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd,
		reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_ZOOM_OUT)), hInst, nullptr);
	st.lblZoom = CreateWindowW(L"STATIC", L"100 %",
		WS_CHILD | WS_VISIBLE | SS_CENTER, 0, 0, 0, 0, hwnd,
		reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_ZOOM_LABEL)), hInst, nullptr);
	st.btnZoomIn = CreateWindowW(L"BUTTON", L"+",
		WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd,
		reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_ZOOM_IN)), hInst, nullptr);
	st.btnFit = CreateWindowW(L"BUTTON", L"Ajuster",
		WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd,
		reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_ZOOM_FIT)), hInst, nullptr);
	st.btn100 = CreateWindowW(L"BUTTON", L"100 %",
		WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd,
		reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_ZOOM_100)), hInst, nullptr);
	st.btnClose = CreateWindowW(L"BUTTON", L"Fermer",
		WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 0, 0, 0, 0, hwnd,
		reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_CLOSE_PREVIEW)), hInst, nullptr);

	HWND controls[] = {
		st.btnPrev, st.lblPage, st.btnNext,
		st.btnZoomOut, st.lblZoom, st.btnZoomIn,
		st.btnFit, st.btn100, st.btnClose
	};
	for (size_t i = 0; i < G_N_ELEMENTS(controls); ++i)
	{
		if (controls[i])
		{
			UT_Win32ApplyWindowTheme(controls[i]);
			if (st.uiFont)
				SendMessageW(controls[i], WM_SETFONT, reinterpret_cast<WPARAM>(st.uiFont), FALSE);
		}
	}

	s_layoutToolbar(st);
	s_updateToolbarControls(st);
	s_updateScrollbars(st);

	ShowWindow(hwnd, SW_SHOW);
	UpdateWindow(hwnd);
	SetFocus(hwnd);

	MSG msg;
	while (!st.done)
	{
		const BOOL got = GetMessageW(&msg, nullptr, 0, 0);
		if (got == 0)
		{
			PostQuitMessage(static_cast<int>(msg.wParam));
			break;
		}
		if (got == -1)
			break;
		if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE)
		{
			st.done = true;
			continue;
		}
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}

	SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
	pWin32App->enableAllTopLevelWindows(TRUE);
	DestroyWindow(hwnd);
	if (st.uiFont)
		DeleteObject(st.uiFont);
	UnregisterClassW(kClassName, pWin32App->getInstance());

	if (hWndParent && IsWindow(hWndParent))
	{
		BringWindowToTop(hWndParent);
		SetFocus(hWndParent);
	}
	return true;
}

} // namespace

class ABI_EXPORT GR_Win32PrintPreviewGraphics : public GR_Win32Graphics
{
public:

	GR_Win32PrintPreviewGraphics(const RECT & rect, XAP_Frame * pFrame, const std::wstring & docTitle)
		: GR_Win32Graphics(s_previewReferenceDC(), s_getDocInfo())
		, m_pGraphics(nullptr)
		, m_pFrame(pFrame)
		, m_docTitle(docTitle)
		, page_rect(rect)
		, cur_page_rect(rect)
		, metafile_dc(nullptr)
	{
		takeOwnershipOfDCAndDocInfo();
	}

	virtual ~GR_Win32PrintPreviewGraphics()
	{
		DELETEP(m_pGraphics);
		if (metafile_dc != nullptr)
		{
			HENHMETAFILE hmf = CloseEnhMetaFile(metafile_dc);
			if (hmf)
				DeleteEnhMetaFile(hmf);
			metafile_dc = nullptr;
		}
		_clearPages();
	}

	virtual UT_uint32 getClassId() override { return s_getClassId(); }

	virtual GR_Capability getCapability() override { return GRCAP_PRINTER_ONLY; }

	static const char * graphicsDescriptor() { return "Win32 Print Preview"; }

	virtual void drawGlyph(UT_uint32 glyph_idx, UT_sint32 xoff, UT_sint32 yoff) override
	{
		UT_return_if_fail(m_pGraphics != nullptr);
		m_pGraphics->drawGlyph(glyph_idx, xoff, yoff);
	}

	virtual void drawChar(UT_UCS4Char Char, UT_sint32 xoff, UT_sint32 yoff) override
	{
		UT_return_if_fail(m_pGraphics != nullptr);
		m_pGraphics->drawChar(Char, xoff, yoff);
	}

	virtual void drawChars(const UT_UCS4Char * pChars,
	                       int iCharOffset, int iLength,
	                       UT_sint32 xoff, UT_sint32 yoff,
	                       int * pCharWidth) override
	{
		UT_return_if_fail(m_pGraphics != nullptr);
		m_pGraphics->drawChars(pChars, iCharOffset, iLength, xoff, yoff, pCharWidth);
	}

	virtual void setFont(const GR_Font * pFont) override
	{
		if (m_pGraphics)
			m_pGraphics->setFont(pFont);
		else
			GR_Win32Graphics::setFont(pFont);
	}

	virtual void clearFont(void) override
	{
		if (m_pGraphics)
			m_pGraphics->clearFont();
		else
			GR_Win32Graphics::clearFont();
	}

	virtual void setColor(const UT_RGBColor & clr) override
	{
		UT_return_if_fail(m_pGraphics != nullptr);
		m_pGraphics->setColor(clr);
	}

	virtual void getColor(UT_RGBColor & clr) override
	{
		UT_return_if_fail(m_pGraphics != nullptr);
		m_pGraphics->getColor(clr);
	}

	virtual void drawLine(UT_sint32 a, UT_sint32 b, UT_sint32 c, UT_sint32 d) override
	{
		UT_return_if_fail(m_pGraphics != nullptr);
		m_pGraphics->drawLine(a, b, c, d);
	}

	virtual void xorLine(UT_sint32 a, UT_sint32 b, UT_sint32 c, UT_sint32 d) override
	{
		UT_return_if_fail(m_pGraphics != nullptr);
		m_pGraphics->xorLine(a, b, c, d);
	}

	virtual void setLineWidth(UT_sint32 w) override
	{
		UT_return_if_fail(m_pGraphics != nullptr);
		m_pGraphics->setLineWidth(w);
	}

	virtual void setLineProperties(double inWidthPixels,
	                               JoinStyle inJoinStyle = JOIN_MITER,
	                               CapStyle inCapStyle   = CAP_BUTT,
	                               LineStyle inLineStyle = LINE_SOLID) override
	{
		UT_return_if_fail(m_pGraphics != nullptr);
		m_pGraphics->setLineProperties(inWidthPixels, inJoinStyle, inCapStyle, inLineStyle);
	}

	virtual void polyLine(const UT_Point * pts, UT_uint32 nPoints) override
	{
		UT_return_if_fail(m_pGraphics != nullptr);
		m_pGraphics->polyLine(pts, nPoints);
	}

	virtual void fillRect(const UT_RGBColor & c,
	                      UT_sint32 x, UT_sint32 y,
	                      UT_sint32 w, UT_sint32 h) override
	{
		UT_return_if_fail(m_pGraphics != nullptr);
		m_pGraphics->fillRect(c, x, y, w, h);
	}

	virtual void invertRect(const UT_Rect * pRect) override
	{
		UT_return_if_fail(m_pGraphics != nullptr);
		m_pGraphics->invertRect(pRect);
	}

	virtual void setClipRect(const UT_Rect * pRect) override
	{
		UT_return_if_fail(m_pGraphics != nullptr);
		m_pGraphics->setClipRect(pRect);
	}

	virtual void clearArea(UT_sint32 a, UT_sint32 b, UT_sint32 c, UT_sint32 d) override
	{
		UT_return_if_fail(m_pGraphics != nullptr);
		m_pGraphics->clearArea(a, b, c, d);
	}

	virtual GR_Image * createNewImage(const char * pszName, const UT_ConstByteBufPtr & pBB,
	                                  const std::string & mimetype,
	                                  UT_sint32 iDisplayWidth, UT_sint32 iDisplayHeight,
	                                  GR_Image::GRType iType) override
	{
		if (m_pGraphics)
			return m_pGraphics->createNewImage(pszName, pBB, mimetype, iDisplayWidth, iDisplayHeight, iType);
		return GR_Win32Graphics::createNewImage(pszName, pBB, mimetype, iDisplayWidth, iDisplayHeight, iType);
	}

	virtual void drawImage(GR_Image * pImg, UT_sint32 xDest, UT_sint32 yDest) override
	{
		UT_return_if_fail(m_pGraphics != nullptr);
		m_pGraphics->drawImage(pImg, xDest, yDest);
	}

	virtual bool queryProperties(GR_Graphics::Properties gp) const override
	{
		return (gp == DGP_PAPER);
	}

	virtual void polygon(const UT_RGBColor & c, const UT_Point * pts, UT_uint32 nPoints) override
	{
		UT_return_if_fail(m_pGraphics != nullptr);
		m_pGraphics->polygon(c, pts, nPoints);
	}

	virtual GR_Image * genImageFromRectangle(const UT_Rect & r) override
	{
		UT_return_val_if_fail(m_pGraphics != nullptr, nullptr);
		return m_pGraphics->genImageFromRectangle(r);
	}

	virtual void saveRectangle(UT_Rect & r, UT_uint32 iIndx) override
	{
		UT_return_if_fail(m_pGraphics != nullptr);
		m_pGraphics->saveRectangle(r, iIndx);
	}

	virtual void restoreRectangle(UT_uint32 iIndx) override
	{
		UT_return_if_fail(m_pGraphics != nullptr);
		m_pGraphics->restoreRectangle(iIndx);
	}

	virtual void flush(void) override
	{
		UT_return_if_fail(m_pGraphics != nullptr);
		m_pGraphics->flush();
	}

	virtual bool startPrint(void) override
	{
		_clearPages();
		return true;
	}

	virtual bool startPage(const char * /*szPageLabel*/, UT_uint32 /*pageNumber*/,
	                       bool bPortrait, UT_uint32 /*iWidth*/, UT_uint32 /*iHeight*/) override
	{
		endPage();

		if (!getPrimaryDC())
			return false;

		cur_page_rect = page_rect;
		const LONG curW = cur_page_rect.right - cur_page_rect.left;
		const LONG curH = cur_page_rect.bottom - cur_page_rect.top;
		if ((bPortrait && curW > curH) || (!bPortrait && curH > curW))
		{
			cur_page_rect.right = cur_page_rect.left + curH;
			cur_page_rect.bottom = cur_page_rect.top + curW;
		}

		metafile_dc = CreateEnhMetaFileW(getPrimaryDC(), nullptr, &cur_page_rect,
			L"AbiWord\0Print Preview\0\0");
		if (!metafile_dc)
			return false;

		GR_Win32AllocInfo ai(metafile_dc, m_pDocInfo, nullptr);
		m_pGraphics = static_cast<GR_Win32Graphics *>(XAP_App::getApp()->newGraphics(ai));
		if (!m_pGraphics)
		{
			HENHMETAFILE dead = CloseEnhMetaFile(metafile_dc);
			if (dead)
				DeleteEnhMetaFile(dead);
			metafile_dc = nullptr;
			return false;
		}

		return true;
	}

	virtual bool endPrint(void) override
	{
		endPage();

		if (m_pages.empty())
			return false;

		return s_runPrintPreviewWindow(m_pFrame, m_docTitle, m_pages);
	}

	void endPage()
	{
		invalidateCache();

		if (metafile_dc)
		{
			DELETEP(m_pGraphics);
			HENHMETAFILE metafile = CloseEnhMetaFile(metafile_dc);
			metafile_dc = nullptr;
			if (metafile)
			{
				PreviewPage page;
				page.hmf = metafile;
				page.rectHiMetric = cur_page_rect;
				m_pages.push_back(page);
			}
		}
	}

protected:

	void _clearPages()
	{
		for (size_t i = 0; i < m_pages.size(); ++i)
		{
			if (m_pages[i].hmf)
				DeleteEnhMetaFile(m_pages[i].hmf);
		}
		m_pages.clear();
	}

	virtual GR_Font * _findFont(const char * pszFontFamily,
	                            const char * pszFontStyle,
	                            const char * pszFontVariant,
	                            const char * pszFontWeight,
	                            const char * pszFontStretch,
	                            const char * pszFontSize,
	                            const char * pszLang) override
	{
		if (!m_pGraphics)
			return GR_Win32Graphics::_findFont(pszFontFamily, pszFontStyle, pszFontVariant,
				pszFontWeight, pszFontStretch, pszFontSize, pszLang);
		return m_pGraphics->findFont(pszFontFamily, pszFontStyle, pszFontVariant,
			pszFontWeight, pszFontStretch, pszFontSize, pszLang);
	}

	GR_Win32Graphics * m_pGraphics;
	XAP_Frame * m_pFrame;
	std::wstring m_docTitle;
	RECT page_rect;
	RECT cur_page_rect;
	HDC metafile_dc;
	std::vector<PreviewPage> m_pages;
};

XAP_Win32Dialog_PrintPreview::XAP_Win32Dialog_PrintPreview(XAP_DialogFactory * pDlgFactory, XAP_Dialog_Id id)
	: XAP_Dialog_PrintPreview(pDlgFactory, id), m_pPrintGraphics(nullptr), m_emfFilename(nullptr)
{
}

XAP_Win32Dialog_PrintPreview::~XAP_Win32Dialog_PrintPreview(void)
{
	DELETEP(m_pPrintGraphics);
	g_free(m_emfFilename);
}

XAP_Dialog * XAP_Win32Dialog_PrintPreview::static_constructor(XAP_DialogFactory * pFactory,
                                                              XAP_Dialog_Id id)
{
	return new XAP_Win32Dialog_PrintPreview(pFactory, id);
}

GR_Graphics * XAP_Win32Dialog_PrintPreview::getPrinterGraphicsContext(void)
{
	return m_pPrintGraphics;
}

void XAP_Win32Dialog_PrintPreview::releasePrinterGraphicsContext(GR_Graphics * /*pGraphics*/)
{
	DELETEP(m_pPrintGraphics);
}

void XAP_Win32Dialog_PrintPreview::runModal(XAP_Frame * pFrame)
{
	UT_return_if_fail(pFrame);
	FV_View * pView = static_cast<FV_View *>(pFrame->getCurrentView());
	UT_return_if_fail(pView);

	RECT rect;
	rect.left = 0;
	rect.right = static_cast<LONG>(pView->getPageSize().Width(DIM_MM) * 100);
	rect.top = 0;
	rect.bottom = static_cast<LONG>(pView->getPageSize().Height(DIM_MM) * 100);

	std::wstring docTitle;
	if (m_szDocumentTitle && m_szDocumentTitle[0])
	{
		UT_Win32LocaleString titleStr;
		titleStr.fromUTF8(m_szDocumentTitle);
		docTitle = titleStr.c_str();
	}

	DELETEP(m_pPrintGraphics);
	try
	{
		m_pPrintGraphics = new GR_Win32PrintPreviewGraphics(rect, pFrame, docTitle);
	}
	catch (...)
	{
		m_pPrintGraphics = nullptr;
	}
}
