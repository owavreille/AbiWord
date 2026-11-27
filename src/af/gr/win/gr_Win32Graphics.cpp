/* AbiWord
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
#define NOWINABLE
#define NOMETAFILE
#define NOSERVICE
#define NOIME
#define NOMCX

#include <windows.h>
#include <winspool.h>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <map>
#include <string>

#include "gr_Win32Graphics.h"
#include "gr_Win32Image.h"
#include "gr_Painter.h"
#include "ut_unicode.h"

#include <xap_Win32App.h>
#include <xap_Win32Res_Cursors.rc2>

#include "xap_Prefs.h"
#include "xap_Frame.h"
#include "xap_Dialog_Id.h"
#include "xap_Win32Dlg_Print.h"
#include "xap_EncodingManager.h"

#include "ut_debugmsg.h"
#include "ut_assert.h"
#include "ut_string.h"
#include "ut_std_string.h"
#include "ut_Win32OS.h"
#include "ut_Win32Timer.h"
#include "ut_Win32LocaleString.h"

#ifdef __MINGW32__
#include <w32api.h>
#endif

UT_uint32 GR_Win32Graphics::s_iInstanceCount = 0;
HDC GR_Win32Graphics::m_defPrintHDC = nullptr;

#define LOG_WIN32_EXCPT(msg)                                                  \
{                                                                             \
    UT_String __s;                                                            \
    UT_String_sprintf(__s, "%s (%d): %s",__FILE__, __LINE__, msg);            \
	UT_DEBUGMSG(("%s",__s.c_str()));                                          \
    if(XAP_App::getApp()->getPrefs())                                         \
    {                                                                         \
	    XAP_App::getApp()->getPrefs()->log("gr_Win32Graphics", __s.c_str());  \
    }                                                                         \
}

//#define GR_GRAPHICS_DEBUG	1

/*****************************************************************/

// A small helper class
class ABI_EXPORT private_FontReverter
{
public:
	private_FontReverter(GR_Win32Graphics& gr, GR_Font* pOldFont)
	:	m_gr(gr),
		m_pOldFont(pOldFont)
	{
	}
	~private_FontReverter()
	{
		if (m_pOldFont)
			m_gr.setFont(m_pOldFont);
		// else, Oh joy, no old font to revert to... :-<
	}

private:
	private_FontReverter(const private_FontReverter&);	// no impl
	void operator=(const private_FontReverter&);		// no impl

	GR_Win32Graphics&	m_gr;
	GR_Font*			m_pOldFont;
};

/*****************************************************************/

void GR_Win32Graphics::_constructorCommonCode(HDC hdc)
{
	UT_ASSERT(hdc);

	m_iDCFontAllocNo = 0;
	m_iPrintDCFontAllocNo = 0;
	m_hdc = hdc;
	m_bOwnHDC = false;
	m_bOwnDocInfo = false;
	m_bOwnDevMode = false;
	m_bufferHdc = nullptr;
	m_bufferBitmap = nullptr;
	m_bufferPrevBitmap = nullptr;
	m_bufferWidth = 0;
	m_bufferHeight = 0;
	m_bufferCoherent = false;
	m_bufferPersist = false;
	m_omitPresent = false;
	m_presentFull = false;
	m_presentAny = false;
	m_presentLeft = 0;
	m_presentTop = 0;
	m_presentRight = 0;
	m_presentBottom = 0;
	m_dummyHdc = nullptr;
	m_dummyBitmap = nullptr;
	m_dummyPrevBitmap = nullptr;
	m_printHDC = nullptr;
	m_nPrintLogPixelsY = 0;
	m_hwnd = nullptr;
	m_iLineWidth = 0;	// default to a hairline
	m_bPrint = false;
	m_bStartPrint = false;
	m_bStartPage = false;
	m_bPrintCancelled = false;
	m_bPrintTimersPaused = false;
	m_bPrevTimersPaused = false;
	m_hwndPrintOwner = nullptr;
	m_hwndPrintDisabled = nullptr;
	m_pFont = nullptr;
	m_pFontGUI = nullptr;
	m_bIsPreview = false;

	m_cs = GR_Graphics::GR_COLORSPACE_COLOR;
	m_cursor = GR_CURSOR_INVALID;

	m_clrXorPen = 0;
	m_hXorPen = nullptr;

	setCursor(GR_CURSOR_DEFAULT);

	m_remapBuffer = nullptr;
	m_remapBufferSize = 0;
	m_remapIndices = nullptr;

	m_eJoinStyle = JOIN_MITER;
	m_eCapStyle  = CAP_PROJECTING;
	m_eLineStyle = LINE_SOLID;

	setBrush((HBRUSH) GetStockObject(WHITE_BRUSH));	// Default brush

	m_nLogPixelsY = GetDeviceCaps(m_hdc, LOGPIXELSY);
	int nLogPixelsX = GetDeviceCaps(m_hdc, LOGPIXELSX);
	if(m_nLogPixelsY)
	{
		m_fXYRatio = (double)nLogPixelsX / (double) m_nLogPixelsY;
	}
	else
	{
		UT_ASSERT_HARMLESS( UT_SHOULD_NOT_HAPPEN );
		m_fXYRatio = 1;
	}
	
	m_hDevMode = nullptr;
	
	
	typedef struct
	{
		HPEN hPen;
		int	 nStyle;
		int  nWidth;
		int	 nColour;
	
	} CachedPen;
	
	m_pArPens = new GR_Win32Graphics::CACHE_PEN [_MAX_CACHE_PENS];
	memset (m_pArPens, 0, _MAX_CACHE_PENS*sizeof(CachedPen));
	m_nArPenPos = 0;

	_DoubleBuffering_SetUpDummyBuffer();
}

GR_Win32Graphics::GR_Win32Graphics(HDC hdc, HWND hwnd)
{
	_constructorCommonCode(hdc);
	m_hwnd = hwnd;

	// init the print HDC with one for the default printer
	
	if (m_defPrintHDC == nullptr) {
		m_defPrintHDC = UT_GetDefaultPrinterDC();
	}
	
	m_printHDC = m_defPrintHDC;
	s_iInstanceCount++;

	if(m_printHDC)
	{
		m_nPrintLogPixelsY = GetDeviceCaps(getPrintDC(), LOGPIXELSY);
		int nLogPixelsX = GetDeviceCaps(getPrintDC(), LOGPIXELSX);
		if(m_nPrintLogPixelsY)
		{
			m_fXYRatioPrint = (double)nLogPixelsX / (double) m_nPrintLogPixelsY;
		}
		else
		{
			UT_ASSERT_HARMLESS( UT_SHOULD_NOT_HAPPEN );
			m_fXYRatioPrint = 1;
		}
	}
}

void GR_Win32Graphics::setPrintDC(HDC dc)
{
	if(!m_bPrint && dc != m_printHDC)
	{
		if(m_printHDC && m_printHDC != m_defPrintHDC)
			DeleteDC(m_printHDC);
		m_printHDC = dc;
	}
}


GR_Win32Graphics::GR_Win32Graphics(HDC hdc, const DOCINFOW * pDocInfo, HGLOBAL hDevMode)
{
	_constructorCommonCode(hdc);
 	m_bPrint = true;
	m_pDocInfo = pDocInfo;
	m_hDevMode = hDevMode;
	m_bIsPreview = (hDevMode == nullptr);
}

HDC GR_Win32Graphics::swapDC(HDC hdc)
{
	HDC previous = m_hdc;
	if (hdc)
		m_hdc = hdc;
	return previous;
}

bool GR_Win32Graphics::getScreenCanvas(UT_RGBColor & color)
{
	if (m_bPrint)
		return false;
	if (UT_Win32AppsUseDarkTheme())
		color = UT_RGBColor(32, 32, 32);
	else
		color = UT_RGBColor(243, 243, 243);
	return true;
}

void GR_Win32Graphics::refreshDeviceScale()
{
	if (!m_hdc || m_bPrint)
		return;
	int logY = GetDeviceCaps(m_hdc, LOGPIXELSY);
	int logX = GetDeviceCaps(m_hdc, LOGPIXELSX);
	if (!logY)
		return;
	if (logY == m_nLogPixelsY && logX == (int)(m_fXYRatio * m_nLogPixelsY + 0.5))
		return;
	_releasePersistentBuffer();
	SelectObject(m_hdc, GetStockObject(BLACK_PEN));
	CACHE_PEN * pens = m_pArPens;
	for (int n = 0; n < m_nArPenPos; ++n, ++pens)
	{
		if (pens->hPen)
			DeleteObject(pens->hPen);
		pens->hPen = nullptr;
	}
	m_nArPenPos = 0;
	m_nLogPixelsY = logY;
	m_fXYRatio = (double)logX / (double)logY;
}

void GR_Win32Graphics::setWindow(HWND hwnd)
{
	if (!hwnd || m_bPrint || hwnd == m_hwnd)
		return;
	HDC hdc = GetDC(hwnd);
	if (!hdc)
		return;
	_releasePersistentBuffer();
	if (m_hdc && m_hwnd && !m_bOwnHDC)
		ReleaseDC(m_hwnd, m_hdc);
	m_hwnd = hwnd;
	m_hdc = hdc;
	int logY = GetDeviceCaps(m_hdc, LOGPIXELSY);
	int logX = GetDeviceCaps(m_hdc, LOGPIXELSX);
	if (logY)
	{
		m_nLogPixelsY = logY;
		m_fXYRatio = (double)logX / (double)logY;
	}
}

void GR_Win32Graphics::layoutToClient(UT_sint32 x, UT_sint32 y, int & cx, int & cy) const
{
	cx = (int)((double)_tduX(x) * m_fXYRatio);
	cy = _tduY(y);
}

GR_Win32Graphics::~GR_Win32Graphics()
{
	if (m_bPrint)
	{
		if (!m_bIsPreview && m_bStartPrint && m_hdc)
		{
			AbortDoc(m_hdc);
			m_bStartPage = false;
			m_bStartPrint = false;
		}
		closePrintAbort();
	}
	// Deselect the buffer before the fonts it may still hold are destroyed.
	_releasePersistentBuffer();
	_destroyFonts ();
	UT_VECTOR_SPARSEPURGEALL( UT_Rect*, m_vSaveRect);
	if (!m_bPrint)
		s_iInstanceCount--;
		
	/* Release saved bitmaps */
	HBITMAP hBit;
	for (UT_sint32 i = 0; i < m_vSaveRectBuf.size (); i++)
	{
		hBit = (HBITMAP)m_vSaveRectBuf.getNthItem (i);
		DeleteObject(hBit);			
	}

	if (m_hXorPen)
		DeleteObject(m_hXorPen);

	delete [] m_remapBuffer;
	delete [] m_remapIndices;
	
	/*Release created pens*/
	CACHE_PEN * pArPens =  m_pArPens;
	for (int n = 0; n<m_nArPenPos; n++, pArPens++)
		DeleteObject(pArPens->hPen);	

	DELETEPV(m_pArPens);

	DELETEP(m_pFontGUI);
	
	_DoubleBuffering_ReleaseDummyBuffer();

	if(m_printHDC && m_printHDC != m_defPrintHDC)
		DeleteDC(m_printHDC);
	
	if (!m_bPrint && s_iInstanceCount == 0 && m_defPrintHDC)
	{
		DeleteDC(m_defPrintHDC);
		m_defPrintHDC = nullptr;
	}

	if (m_bOwnHDC && m_hdc)
	{
		DeleteDC(m_hdc);
		m_hdc = nullptr;
	}
	if (m_bOwnDocInfo && m_pDocInfo)
		FREEP(m_pDocInfo);
	if (m_bOwnDevMode && m_hDevMode)
	{
		GlobalFree(m_hDevMode);
		m_hDevMode = nullptr;
	}
}

void GR_Win32Graphics::takeOwnershipOfDCAndDocInfo()
{
	m_bOwnHDC = (m_hdc != nullptr);
	m_bOwnDocInfo = (m_pDocInfo != nullptr);
	// The DEVMODE from CreateDC belongs with this object. The print dialog
	// keeps its own handle and does not call this.
	m_bOwnDevMode = (m_hDevMode != nullptr);
}

bool GR_Win32Graphics::queryProperties(GR_Graphics::Properties gp) const
{
	switch (gp)
	{
	case DGP_SCREEN:
		return !m_bPrint;
	case DGP_PAPER:
		return m_bPrint;
	case DGP_OPAQUEOVERLAY:
		return true;
	default:
		UT_ASSERT_HARMLESS(0);
		return false;
	}
}

GR_Win32Font * GR_Win32Graphics::_newFont(LOGFONTW & lf, double fPoints, HDC hdc, HDC printHDC)
{
	return GR_Win32Font::newFont(lf, fPoints, hdc, printHDC);
}

GR_Font* GR_Win32Graphics::getGUIFont(void)
{
	if (!m_pFontGUI)
	{
		// Prefer the modern system status font (Segoe UI) scaled to 8pt
		// at the current window DPI with ClearType antialiasing, falling
		// back to DEFAULT_GUI_FONT if unavailable.
		int dpi = m_hwnd ? UT_Win32WindowDpi(m_hwnd) : m_nLogPixelsY;
		if (dpi <= 0)
			dpi = 96;

		NONCLIENTMETRICSW ncm = {};
		if (UT_Win32GetNonClientMetrics(&ncm, dpi))
		{
			LOGFONTW lf = ncm.lfStatusFont;
			lf.lfHeight = -MulDiv(8, dpi, 72);
#ifdef CLEARTYPE_QUALITY
			lf.lfQuality = CLEARTYPE_QUALITY;
#endif
			HFONT hProbe = CreateFontIndirectW(&lf);
			if (hProbe)
			{
				DeleteObject(hProbe);
				m_pFontGUI = _newFont(lf, 0, m_hdc, m_hdc);
			}
		}

		if (!m_pFontGUI)
		{
			// DEFAULT_GUI_FONT is a stock object. GetObjectW copies its
			// LOGFONT; DeleteObject must not be called on the stock handle.
			// A short or failed GetObjectW leaves the structure unwritten.
			HFONT stock = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
			LOGFONTW lf;
			memset(&lf, 0, sizeof(lf));
			if (stock && GetObjectW(stock, sizeof(lf), &lf) == sizeof(lf))
				m_pFontGUI = _newFont(lf, 0, m_hdc, m_hdc);
		}
		UT_ASSERT(m_pFontGUI);
	}
	if(m_pFontGUI)
		m_pFontGUI->markGUIFont();
	
	return m_pFontGUI;
}

extern "C"
struct win32FontCharset
{
	BYTE value;
	int rank;
};

int CALLBACK
win32Internal_fontEnumProcedure(ENUMLOGFONTW* pLogFont,
								NEWTEXTMETRICEXW* /*pTextMetric*/,
								int /*Font_type*/,
								LPARAM lParam)
{
	win32FontCharset *choice = reinterpret_cast<win32FontCharset *>(lParam);
	if (!choice || !pLogFont)
		return 1;
	const BYTE charset = pLogFont->elfLogFont.lfCharSet;
	int rank = 1;
	if (charset == ANSI_CHARSET)
		rank = 2;
	if (charset == DEFAULT_CHARSET)
		rank = 3;
	if (rank > choice->rank)
	{
		choice->rank = rank;
		choice->value = charset;
	}
	return 1;
}

GR_Font* GR_Win32Graphics::_findFont(const char* pszFontFamily,
									 const char* pszFontStyle,
									 const char* /*pszFontVariant*/,
									 const char* pszFontWeight,
									 const char* /*pszFontStretch*/,
									 const char* pszFontSize,
									 const char* /*pszLang*/)
{	
	#ifdef GR_GRAPHICS_DEBUG
	UT_DEBUGMSG(("GR_Win32Graphics::findFont %s %s %s\n", pszFontFamily, pszFontStyle, pszFontSize));	
	#endif
	
	LOGFONTW lf;
	memset(&lf, 0, sizeof(lf));

	HDC hPrintDC = m_printHDC ? m_printHDC : m_hdc;

	/*
		TODO we need to fill out the LOGFONT object such that
		we'll get the closest possible matching font.  For
		now, we're hard-coding a hack.
	*/

	// we need to get the size in logpixels for the current DC, which
	// simply means to divide points by 72 and multiply by device Y resolution

	// See: http://support.microsoft.com/support/kb/articles/Q74/2/99.asp
	double fPointSize = UT_convertToPoints(pszFontSize);
	lf.lfHeight = (int)(-fPointSize * (double)GetDeviceCaps(m_hdc, LOGPIXELSY) / 72.0);		

	// TODO note that we don't support all those other ways of expressing weight.
	if (0 == g_ascii_strcasecmp(pszFontWeight, "bold"))
		lf.lfWeight = 700;

	// TODO -- remove this block entirely, since oblique is no longer a valid style
	// We squash oblique into italic
	if (0 == g_ascii_strcasecmp(pszFontStyle, "italic") || 0 == g_ascii_strcasecmp(pszFontStyle, "oblique"))
		lf.lfItalic = TRUE;

	// TODO note that we currently think pszFontFamily is a single name, not a list!
	// TODO why don't these generic family names work?!?
	if (0 == g_ascii_strcasecmp(pszFontFamily, "serif"))
		lf.lfPitchAndFamily = DEFAULT_PITCH | FF_ROMAN;
	else if (0 == g_ascii_strcasecmp(pszFontFamily, "sans-serif"))
		lf.lfPitchAndFamily = DEFAULT_PITCH | FF_SWISS;
	else if (0 == g_ascii_strcasecmp(pszFontFamily, "cursive"))
		lf.lfPitchAndFamily = DEFAULT_PITCH | FF_SCRIPT;
	else if (0 == g_ascii_strcasecmp(pszFontFamily, "fantasy"))
		lf.lfPitchAndFamily = DEFAULT_PITCH | FF_DECORATIVE;
	else if (0 == g_ascii_strcasecmp(pszFontFamily, "monospace"))
		lf.lfPitchAndFamily = DEFAULT_PITCH | FF_MODERN;
	else
	{
		// NB: lf.lfFaceName is a statically allocated char buffer of len 
		// LF_FACESIZE. strToNative() will truncate the string at 500 bytes,
		// but as LF_FACESIZE is generally 32, this shouldn't matter.
		//
		// This convertion is needed because the windows API expects fontnames
		// in the native encoding and later lf will be passing to a windows
		// API funcion (CreateFontIndirect()).
    	UT_Win32LocaleString str;
		str.fromUTF8 (pszFontFamily);
		lstrcpynW(lf.lfFaceName, 
		        str.c_str(),
		        LF_FACESIZE);
	}

	// Get character set value from the font itself
	LOGFONTW enumlf;
	memset(&enumlf, 0, sizeof(enumlf));

	enumlf.lfCharSet = DEFAULT_CHARSET;
	wcscpy(enumlf.lfFaceName, lf.lfFaceName);
	win32FontCharset charset = { DEFAULT_CHARSET, 0 };
	EnumFontFamiliesExW(m_hdc, &enumlf,
		(FONTENUMPROCW)win32Internal_fontEnumProcedure, (LPARAM)&charset, 0);
	lf.lfCharSet = charset.rank ? charset.value : DEFAULT_CHARSET;

	lf.lfOutPrecision = OUT_TT_ONLY_PRECIS;		// Choose only True Type fonts.
	lf.lfQuality = PROOF_QUALITY;

	return _newFont(lf, fPointSize, m_hdc, hPrintDC);
}

// ExtTextOutW consumes UTF-16. U+10000 and above need a surrogate pair.
// U+200B and U+FEFF are omitted. A lone surrogate or a value past U+10FFFF
// becomes U+FFFD.
static int s_utf16Units(UT_UCS4Char ch, UT_UCS2Char out[2])
{
	return UT_Unicode::UCS4_to_UTF16(ch, out, true);
}

void GR_Win32Graphics::drawGlyph(UT_uint32 /*Char*/, UT_sint32 /*xoff*/, UT_sint32 /*yoff*/)
{
	UT_ASSERT_HARMLESS(UT_TODO);
}

void GR_Win32Graphics::drawChar(UT_UCS4Char Char, UT_sint32 xoff, UT_sint32 yoff)
{
	#ifdef GR_GRAPHICS_DEBUG
	UT_DEBUGMSG(("GR_Win32Graphics::drawChar %c %u %u\n", Char, xoff, yoff));	
	#endif	
	
	xoff = (UT_sint32)((double)_tduX(xoff) * m_fXYRatio);
	yoff = _tduY(yoff);

#if 0 // this bypassesthe font handling mechanism
	HFONT hFont = m_pFont->getDisplayFont(this);
	SelectObject(m_hdc, hFont);
#else
	m_pFont->selectFontIntoDC(this, m_hdc);
#endif
	
	SetTextAlign(m_hdc, TA_LEFT | TA_TOP);
	SetBkMode(m_hdc, TRANSPARENT);		// TODO: remember and reset?

	if(Char == 0x200B || Char == 0xFEFF
	   /*|| Char == UCS_LIGATURE_PLACEHOLDER*/)
		return;

	UT_UCS2Char units[2];
	const int nUnits = s_utf16Units(Char, units);
	if (nUnits <= 0)
		return;
	// Windows NT and Windows 95 support the Unicode Font file.
	// All of the Unicode glyphs can be rendered if the glyph is found in
	// the font file. However, Windows 95 does  not support the Unicode
	// characters other than the characters for which the particular codepage
	// of the font file is defined.
	// Reference Microsoft knowledge base:
	// Q145754 - PRB ExtTextOutW or TextOutW Unicode Text Output Is Blank
	bool bWin9xSymbol = false;
	if (!UT_IsWinNT())
	{
		LOGFONTW lf;
		memset(&lf, 0, sizeof(lf));
		bWin9xSymbol = (GetObjectW(m_pFont->getFontHandle(), sizeof(lf), &lf) == sizeof(lf)
			&& lf.lfCharSet == SYMBOL_CHARSET);
	}
	if (bWin9xSymbol && nUnits == 1)
	{
		// Symbol character handling for Win9x
		char str[sizeof(UT_UCS2Char)];

		int iConverted = WideCharToMultiByte(CP_ACP, 0,
			(LPCWSTR) units, 1,
			str, sizeof(str), nullptr, nullptr);
		if (iConverted > 0)
			ExtTextOutA(m_hdc, xoff, yoff, 0, nullptr, str, iConverted, nullptr);
	}
	else
	{
		// ExtTextOutW takes UTF-16. A character above U+FFFF is a surrogate pair.
		ExtTextOutW(m_hdc, xoff, yoff, 0/*ETO_GLYPH_INDEX*/, nullptr, (LPCWSTR) units, nUnits, nullptr);
	}
}

UT_uint16*	GR_Win32Graphics::_remapGlyphs(const UT_UCS4Char* pChars, int iCharOffset, int &iLength)
{
	const int capacity = iLength * 2;
	if (capacity > (int)m_remapBufferSize)
	{
		delete [] m_remapBuffer;

		if(XAP_App::getApp()->theOSHasBidiSupport() != XAP_App::BIDI_SUPPORT_NONE)
		{
			delete [] m_remapIndices;
			m_remapIndices = new UT_UCS2Char[capacity];
		}

		m_remapBuffer = new UT_UCS2Char[capacity];
		m_remapBufferSize = capacity;
	}

	int j = 0;
	for (int i = 0; i < iLength; ++i)
	{
		UT_UCS2Char units[2];
		const int n = s_utf16Units(pChars[iCharOffset + i], units);
		for (int u = 0; u < n; ++u)
			m_remapBuffer[j++] = units[u];
	}

	iLength = j;

	return m_remapBuffer;
}

void GR_Win32Graphics::drawChars(const UT_UCS4Char* pChars,
								 int iCharOffset, int iLengthOrig,
								 UT_sint32 xoff, UT_sint32 yoff,
								 int * pCharWidths)
{
	UT_ASSERT(pChars);

	if(!iLengthOrig)
	{
		// I do not want to assert here, as this makes debugging
		// virtually impossible
		UT_DEBUGMSG(("GR_Win32Graphics::drawChars: asked to draw zero chars !!!\n"));
		return;
	}
	
	#ifdef GR_GRAPHICS_DEBUG
	UT_DEBUGMSG(("GR_Win32Graphics::drawChars %c %u %u\n", pChars, xoff, yoff));	
	#endif
	
	xoff = (UT_sint32)((double)_tduX(xoff) * m_fXYRatio);
	yoff = _tduY(yoff);
	int *pCharAdvances = nullptr;
	
	// iLength can be modified by _remapGlyphs
	int iLength = iLengthOrig;
	HFONT hFont = m_pFont->getDisplayFont(this);
	SelectObject(m_hdc, hFont);
	m_iDCFontAllocNo = m_pFont->getAllocNumber();
	SetTextAlign(m_hdc, TA_LEFT | TA_TOP);
	SetBkMode(m_hdc, TRANSPARENT);		// TODO: remember and reset?

	UT_uint16* currentChars = _remapGlyphs(pChars, iCharOffset, iLength);

	// Windows NT and Windows 95 support the Unicode Font file.
	// All of the Unicode glyphs can be rendered if the glyph is found in
	// the font file. However, Windows 95 does  not support the Unicode
	// characters other than the characters for which the particular codepage
	// of the font file is defined.
	// Reference Microsoft knowledge base:
	// Q145754 - PRB ExtTextOutW or TextOutW Unicode Text Output Is Blank
	bool bWin9xSymbol = false;
	if (iLength > 0 && !UT_IsWinNT())
	{
		LOGFONTW lf;
		memset(&lf, 0, sizeof(lf));
		bWin9xSymbol = (GetObjectW(hFont, sizeof(lf), &lf) == sizeof(lf)
			&& lf.lfCharSet == SYMBOL_CHARSET);
	}

	if (bWin9xSymbol)
	{
		// Symbol character handling for Win9x. cbMultiByte is the
		// allocated byte count, not a wider guess.
		const int bytes = iLength * static_cast<int>(sizeof(UT_UCS2Char));
		char* str = new char[bytes];
		int iConverted = WideCharToMultiByte(CP_ACP, 0,
			(LPCWSTR) currentChars, iLength,
			str, bytes, nullptr, nullptr);

		if (iConverted > 0)
			ExtTextOutA(m_hdc, xoff, yoff, 0, nullptr, str, iConverted, nullptr);
		delete [] str;
	}
	else
	{
		int duCharWidths [256];

		if (pCharWidths)
		{
			const int advanceCap = iLengthOrig * 2;
			if (advanceCap > static_cast<int>(sizeof(duCharWidths)/sizeof(int)) )
				pCharAdvances = new int [advanceCap];
			else
				pCharAdvances = duCharWidths;
			UT_ASSERT(pCharAdvances != nullptr);

			// One advance per UTF-16 unit. A surrogate pair keeps the whole
			// width on the first unit. Zero-width characters are absent.
			UT_sint32 iwidth = 0;
			UT_sint32 iadvance = 0;
			int j = 0;
			for (int i = 0; i < iLengthOrig; i++)
			{
				UT_UCS2Char units[2];
				const int n = s_utf16Units(pChars[iCharOffset + i], units);
				if (n <= 0)
					continue;
				iwidth += pCharWidths[iCharOffset + i];
				const UT_sint32 inextAdvance = (UT_sint32)((double)_tduX(iwidth) * m_fXYRatio);
				pCharAdvances[j++] = UT_utf16_unit_advance(0, inextAdvance - iadvance);
				iadvance = inextAdvance;
				if (n == 2)
					pCharAdvances[j++] = UT_utf16_unit_advance(1, inextAdvance - iadvance);
			}
		}
		else
		  {
			pCharAdvances=nullptr;
		  }

		// Unicode font and default character set handling for WinNT and Win9x

		// The bidi aspect of the drawing is handled as follows:
		//     1. The OS has no bidi support: use simple ExTextOut
		//        call, since all the bidi processing has been already
		//        done in the layout engine
		//
		//     2. If the OS has bidi support: we will not use it for
		//        drawing in the main window, only let the system to
		//        handle processing for the GUI. This requires that we
		//        call GetCharacterPlacement function without
		//        requesting reordering and then feed the indices to
		//        ExTextOut (direct call to ExTextOut automatically reorders)
		if(XAP_App::getApp()->theOSHasBidiSupport() != XAP_App::BIDI_SUPPORT_NONE)
		{
			UT_ASSERT(m_remapIndices);
			GCP_RESULTSW gcpResult;
			gcpResult.lStructSize = sizeof(GCP_RESULTSW);
			gcpResult.lpOutString = nullptr;			// Output string
			gcpResult.lpOrder = nullptr;				// Ordering indices
			// we must set here lpDx to nullptr so that
			// GetCharacterPlacement does not change our values ...
			gcpResult.lpDx = nullptr;	                // Distances between character cells
			gcpResult.lpCaretPos = nullptr;			// Caret positions	
			gcpResult.lpClass = nullptr;				// Character classifications
// w32api changed lpGlyphs from UINT * to LPWSTR to match MS PSDK in w32api v2.4
#if defined(__MINGW32__) && (__W32API_MAJOR_VERSION == 2 && __W32API_MINOR_VERSION < 4)
			gcpResult.lpGlyphs = (UINT *) m_remapIndices;	// Character glyphs
#else			
			gcpResult.lpGlyphs = (LPWSTR) m_remapIndices;	// Character glyphs
#endif			
			gcpResult.nGlyphs = m_remapBufferSize;  // Array size

			DWORD placementResult;

			if(XAP_App::getApp()->theOSHasBidiSupport() == XAP_App::BIDI_SUPPORT_GUI)
				placementResult = GetCharacterPlacementW(m_hdc, (LPCWSTR) currentChars, iLength, 0, &gcpResult, 0);
			else
				placementResult = GetCharacterPlacementW(m_hdc, (LPCWSTR) currentChars, iLength, 0, &gcpResult, GCP_REORDER);
			// now we set the character advances ...
			gcpResult.lpDx = pCharAdvances;	    // Distances between character cells
			
			if(placementResult)
			{
				ExtTextOutW(m_hdc, xoff, yoff, ETO_GLYPH_INDEX, nullptr, (LPCWSTR) m_remapIndices, gcpResult.nGlyphs, pCharAdvances);
			}
			else
			{
				UT_ASSERT_HARMLESS(UT_SHOULD_NOT_HAPPEN);
				goto simple_exttextout;
			}
		}
		else
		{
            simple_exttextout:
			ExtTextOutW(m_hdc, xoff, yoff, 0, nullptr, (LPCWSTR) currentChars, iLength, pCharAdvances);
		}

		if (pCharAdvances && pCharAdvances != duCharWidths)
			delete[] pCharAdvances;
	}
}

void GR_Win32Graphics::setFont(const GR_Font* pFont)
{
	UT_ASSERT(pFont);	// TODO should we allow pFont == nullptr?

	const GR_Win32Font* pWin32Font = static_cast<const GR_Win32Font*>(pFont);

// TMN: This currently won't work since there is no way to tell
// GR_Win32Graphics to "drop" a font. If a user of this class:
// 1. Creates a new font on the heap.
// 2. Calls this setFont()
// 3. Deletes the font.
// 4. Creates a new font on the heap.
// 5. Calls this setFont()
// it is possible (and it has happened) that the pointers are the same,
// but it's two different fonts.
//

	// this should work though, the allocation number is unique, even
	// if the pointers are identical
	if (m_pFont == nullptr || pFont->getAllocNumber() != m_iFontAllocNo
		|| pWin32Font->getPrimaryHDC() != m_hdc)
	{
		m_pFont = const_cast<GR_Win32Font*>(pWin32Font);
		m_iFontAllocNo = pFont->getAllocNumber();
		m_pFont->selectFontIntoDC(this, m_hdc);
	}
}

UT_uint32 GR_Win32Graphics::getFontHeight(const GR_Font * fnt)
{
	private_FontReverter janitor_(*this, m_pFont);

	setFont(fnt);

	return getFontHeight();
}

UT_uint32 GR_Win32Graphics::getFontHeight()
{
	UT_return_val_if_fail( m_pFont, 0 );

	return (UT_uint32)(m_pFont->getHeight(m_hdc, m_printHDC));
}

UT_uint32 GR_Win32Graphics::getFontAscent(const GR_Font* fnt)
{
	private_FontReverter janitor_(*this, m_pFont);

	setFont(fnt);

	return getFontAscent();
}

UT_uint32 GR_Win32Graphics::getFontAscent()
{
	UT_return_val_if_fail( m_pFont, 0 );
	return (UT_uint32)(m_pFont->getAscent(m_hdc, m_printHDC));
}

UT_uint32 GR_Win32Graphics::getFontDescent(const GR_Font* fnt)
{
	private_FontReverter janitor_(*this, m_pFont);

	setFont(fnt);

	return getFontDescent();
}

UT_uint32 GR_Win32Graphics::getFontDescent()
{
	UT_return_val_if_fail( m_pFont, 0 );
	return (UT_uint32)(m_pFont->getDescent(m_hdc, m_printHDC));
}

void GR_Win32Graphics::getCoverage(UT_NumberVector& coverage)
{
	coverage.clear();
	coverage.push_back(' ');
	coverage.push_back((255 - ' '));
	
	//UT_ASSERT(UT_TODO);
}

UT_sint32 GR_Win32Graphics::measureUnRemappedChar(const UT_UCS4Char c, UT_uint32 * /*height*/)
{
	#ifdef GR_GRAPHICS_DEBUG
	UT_DEBUGMSG(("GR_Win32Graphics::measureUnRemappedChar\n"));	
	#endif

	UT_return_val_if_fail(m_pFont,0);
	UT_sint32 iWidth = m_pFont->measureUnRemappedChar(c);

	if (iWidth==GR_CW_UNKNOWN || iWidth==GR_CW_ABSENT)
		return iWidth;
	
	iWidth *= (UT_sint32)getResolution();
	iWidth /= (UT_sint32)getDeviceResolution();
	iWidth = (UT_sint32)((double)iWidth * m_fXYRatio);
	return iWidth;	
}

UT_uint32 GR_Win32Graphics::getDeviceResolution(void) const
{	
	return m_nLogPixelsY; // NOTE: assumes square pixels
}

void GR_Win32Graphics::setColor(const UT_RGBColor& clr)
{
	m_curColor = clr;
	_setColor(RGB(clr.m_red, clr.m_grn, clr.m_blu));
}

void GR_Win32Graphics::getColor(UT_RGBColor& clr)
{
	clr = m_curColor;
}

void GR_Win32Graphics::_setColor(DWORD dwColor)
{
	m_clrCurrent = dwColor;
	SetTextColor(m_hdc, m_clrCurrent);
}

#ifdef GR_GRAPHICS_DEBUG
static nCacheHit = 0;
static nCacheFailed = 0;
#endif

void GR_Win32Graphics::drawLine(UT_sint32 x1, UT_sint32 y1, UT_sint32 x2, UT_sint32 y2)
{	
	#ifdef GR_GRAPHICS_DEBUG
	UT_DEBUGMSG(("GR_Win32Graphics::drawLine %u %u %u %u\n", x1,  y1, x2,  y2));	
	#endif			   	
	
	x1 = (UT_sint32)((double)_tduX(x1) * m_fXYRatio);
	x2 = (UT_sint32)((double)_tduX(x2) * m_fXYRatio);
	y1 = _tduY(y1);
	y2 = _tduY(y2);	

	int penStyle;
	HPEN hPen = nullptr;
	bool bCached = false;
	UT_sint32 iLineWidth = (UT_sint32)((double)_tduR(m_iLineWidth) * m_fXYRatio);
	
	switch(m_eLineStyle)
	{
		case LINE_DOUBLE_DASH:
		case LINE_ON_OFF_DASH:      penStyle = PS_DASH;  break;
		case LINE_SOLID:            penStyle = PS_SOLID; break;
		case LINE_DOTTED:           penStyle = PS_DOT;   break;

		default:
			UT_ASSERT_HARMLESS(UT_NOT_IMPLEMENTED);
			penStyle = PS_SOLID;
	}
	
	/*Look for a cached pen*/
	CACHE_PEN * pArPens =  m_pArPens;
	for (int n = 0; n<m_nArPenPos; n++, pArPens++)
	{
		if (pArPens->nStyle==penStyle &&
			pArPens->nWidth==iLineWidth &&
			pArPens->dwColour==m_clrCurrent)
			{
				hPen = pArPens->hPen;
				bCached = true;
#ifdef GR_GRAPHICS_DEBUG
				nCacheHit++;
#endif
				break;
			}		
	}
	
	/*If not cached, let's create it*/
	if (!hPen)
	{
		// CreatePen returns NULL when GDI has no pen. Selecting that
		// leaves the previous pen in the DC and still draws.
		hPen = CreatePen(penStyle, iLineWidth, m_clrCurrent);
		if (!hPen)
			return;

		if (m_nArPenPos<_MAX_CACHE_PENS)
		{			
			pArPens =  m_pArPens + m_nArPenPos;
			pArPens->nStyle=penStyle;
			pArPens->nWidth=iLineWidth;
			pArPens->dwColour=m_clrCurrent;
			pArPens->hPen = hPen;
			bCached = true;
			m_nArPenPos++;
#ifdef GR_GRAPHICS_DEBUG
			nCacheFailed++;
#endif
		}
	}	

	#ifdef GR_GRAPHICS_DEBUG
	UT_DEBUGMSG(("GR_Win32Graphics::drawline: cached elements %u cached hits %u  failed %u\n", m_nArPenPos, 
		 nCacheHit, nCacheFailed));	
	#endif
	
	UT_Win32TraceF("line %d,%d %d,%d pen=%p w=%d cached=%d",
				  x1, y1, x2, y2, (void*)hPen, iLineWidth, bCached ? 1 : 0);
	// SelectObject returns NULL when the pen was not selected. The
	// previous pen must stay in the DC.
	HPEN hOldPen = (HPEN) SelectObject(m_hdc, hPen);
	if (!hOldPen)
	{
		if (!bCached)
			DeleteObject(hPen);
		return;
	}

	MoveToEx(m_hdc, x1, y1, nullptr);
	LineTo(m_hdc, x2, y2);

	(void) SelectObject(m_hdc, hOldPen);
	
	if (!bCached)
		DeleteObject(hPen);
	UT_Win32TraceHeap("line");
}

void GR_Win32Graphics::setLineProperties(double iLineWidth,
										 JoinStyle inJoinStyle,
										 CapStyle inCapStyle,
										 LineStyle inLineStyle)
{
	m_eJoinStyle = inJoinStyle;
	m_eCapStyle  = inCapStyle;
	m_eLineStyle = inLineStyle;
	m_iLineWidth = static_cast<UT_sint32>(iLineWidth);
}

void GR_Win32Graphics::setLineWidth(UT_sint32 iLineWidth)
{
	m_iLineWidth = iLineWidth;
}

void GR_Win32Graphics::xorLine(UT_sint32 x1, UT_sint32 y1, UT_sint32 x2, UT_sint32 y2)
{
	#ifdef GR_GRAPHICS_DEBUG
	UT_DEBUGMSG(("GR_Win32Graphics::xorLine %u %u %u %u\n", x1,  y1, x2,  y2));	
	#endif
	
	/*
	  Note that we always use a pixel width of 1 for xorLine, since
	  this should always be done to the screen.
	*/

	x1 = (UT_sint32)((double)_tduX(x1) * m_fXYRatio);
	x2 = (UT_sint32)((double)_tduX(x2) * m_fXYRatio);
	y1 = _tduY(y1);
	y2 = _tduY(y2);

	if (m_clrCurrent != m_clrXorPen || !m_hXorPen)
	{
		HPEN created = CreatePen(PS_SOLID, (UT_sint32)((double)_tduR(m_iLineWidth) * m_fXYRatio), m_clrCurrent);
		if (!created)
			return;
		if (m_hXorPen)
			DeleteObject(m_hXorPen);
		m_hXorPen = created;
		m_clrXorPen = m_clrCurrent;
	}

	int iROP = SetROP2(m_hdc, R2_XORPEN);
	HPEN hOldPen = (HPEN)SelectObject(m_hdc, m_hXorPen);
	if (!hOldPen)
	{
		SetROP2(m_hdc, iROP);
		return;
	}

	MoveToEx(m_hdc, x1, y1, nullptr);
	LineTo(m_hdc, x2, y2);

	SelectObject(m_hdc, hOldPen);
	SetROP2(m_hdc, iROP);
}

void GR_Win32Graphics::polyLine(const UT_Point * pts, UT_uint32 nPoints)
{
	#ifdef GR_GRAPHICS_DEBUG
	UT_DEBUGMSG(("GR_Win32Graphics::polyLine %u\n", nPoints));	
	#endif
	
	if (!pts || nPoints == 0 || !m_hdc)
		return;

	HPEN hPen = CreatePen(PS_SOLID, (UT_sint32)((double)_tduR(m_iLineWidth) * m_fXYRatio), m_clrCurrent);
	if (!hPen)
		return;

	POINT * points = (POINT *)UT_calloc(nPoints, sizeof(POINT));
	if (!points)
	{
		DeleteObject(hPen);
		return;
	}
	HPEN hOldPen = (HPEN) SelectObject(m_hdc, hPen);
	if (!hOldPen)
	{
		DeleteObject(hPen);
		FREEP(points);
		return;
	}

	for (UT_uint32 i = 0; i < nPoints; i++)
	{
		points[i].x = (UT_sint32)((double)_tduX(pts[i].x) * m_fXYRatio);
		points[i].y = _tduY(pts[i].y);
	}

	Polyline(m_hdc, points, nPoints);

	(void) SelectObject(m_hdc, hOldPen);
	DeleteObject(hPen);
	FREEP(points);
}

void GR_Win32Graphics::fillRect(const UT_RGBColor& c, UT_sint32 x, UT_sint32 y, UT_sint32 w, UT_sint32 h)
{
	
	RECT r;
	r.left = (UT_sint32)((double)_tduX(x) * m_fXYRatio);
	r.top = _tduY(y);
	r.right = (UT_sint32)(_tduX(x+w) * m_fXYRatio);
	r.bottom = _tduY(y+h);

	COLORREF clr = RGB(c.m_red, c.m_grn, c.m_blu);

	#ifdef GR_GRAPHICS_DEBUG
	w=(UT_sint32)((double)_tduR(w) * m_fXYRatio);
	h=_tduR(h);
	UT_DEBUGMSG(("GR_Win32Graphics::fillRect %x %u %u %u %u\n",  clr, r.left, r.top, w, h));	
	#endif
		
	// This might look wierd (and I think it is), but it's MUCH faster.
	// CreateSolidBrush is dog slow.
	HDC hdc = m_hdc;
	UT_Win32TraceF("fill %d,%d %d,%d hdc=%p", r.left, r.top, r.right, r.bottom, (void*)hdc);
	// SetBkColor returns CLR_INVALID and leaves the DC unchanged.
	// Writing that value back is not the previous color.
	const COLORREF cr = ::SetBkColor(hdc,  clr);
	if (cr == CLR_INVALID)
		return;
	::ExtTextOutW(hdc, 0, 0, ETO_OPAQUE, &r, nullptr, 0, nullptr);
	::SetBkColor(hdc, cr);
	UT_Win32TraceHeap("fill");
}

namespace {

bool s_printAbort = false;
HWND s_printAbortWnd = nullptr;

LRESULT CALLBACK s_printAbortWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (msg == WM_COMMAND && (LOWORD(wParam) == IDCANCEL || LOWORD(wParam) == IDOK))
	{
		s_printAbort = true;
		HWND btn = GetDlgItem(hwnd, IDCANCEL);
		if (btn)
			EnableWindow(btn, FALSE);
		return 0;
	}
	if (msg == WM_CLOSE)
	{
		s_printAbort = true;
		return 0;
	}
	if (msg == WM_NCDESTROY && hwnd == s_printAbortWnd)
	{
		s_printAbortWnd = nullptr;
	}
	return DefWindowProcW(hwnd, msg, wParam, lParam);
}

BOOL CALLBACK s_printAbortProc(HDC, int)
{
	// Bound the pump per callback so a busy message queue cannot starve
	// the spooler or hang printing.
	constexpr int kMaxMessagesPerCall = 64;
	MSG msg;
	int processed = 0;
	while (!s_printAbort && processed < kMaxMessagesPerCall &&
	       PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
	{
		++processed;
		if (msg.message == WM_QUIT)
		{
			PostQuitMessage(static_cast<int>(msg.wParam));
			s_printAbort = true;
			return FALSE;
		}
		if (!s_printAbortWnd || !IsDialogMessageW(s_printAbortWnd, &msg))
		{
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}
	}
	return s_printAbort ? FALSE : TRUE;
}

short s_matchStandardPaperSize(short widthTenthsMm, short lengthTenthsMm)
{
	struct PaperSpec
	{
		short id;
		short width;
		short length;
	};
	static const PaperSpec kStandardPapers[] = {
		{ DMPAPER_A4,        2100, 2970 },
		{ DMPAPER_LETTER,    2159, 2794 },
		{ DMPAPER_LEGAL,     2159, 3556 },
		{ DMPAPER_A3,        2970, 4200 },
		{ DMPAPER_A5,        1480, 2100 },
		{ DMPAPER_EXECUTIVE, 1842, 2667 },
		{ DMPAPER_TABLOID,   2794, 4318 },
		{ DMPAPER_STATEMENT, 1397, 2159 },
		{ DMPAPER_B4,        2570, 3640 },
		{ DMPAPER_B5,        1820, 2570 },
		{ DMPAPER_ENV_DL,    1100, 2200 },
		{ DMPAPER_ENV_C5,    1620, 2290 },
		{ DMPAPER_ENV_10,    1048, 2413 },
	};
	constexpr int kToleranceTenthsMm = 15; // 1.5 mm
	for (const PaperSpec & spec : kStandardPapers)
	{
		if (std::abs(static_cast<int>(widthTenthsMm) - spec.width) <= kToleranceTenthsMm &&
		    std::abs(static_cast<int>(lengthTenthsMm) - spec.length) <= kToleranceTenthsMm)
		{
			return spec.id;
		}
	}
	return 0;
}

} // namespace

void GR_Win32Graphics::closePrintAbort(void)
{
	// Win32 modality rule: re-enable the owner before destroying the owned
	// popup so Windows returns activation to the owner instead of another app.
	HWND hwndToRestore = m_hwndPrintDisabled;
	m_hwndPrintDisabled = nullptr;
	if (hwndToRestore && IsWindow(hwndToRestore))
		EnableWindow(hwndToRestore, TRUE);

	if (s_printAbortWnd)
	{
		if (IsWindow(s_printAbortWnd))
			DestroyWindow(s_printAbortWnd);
		s_printAbortWnd = nullptr;
	}

	if (hwndToRestore && IsWindow(hwndToRestore))
		SetForegroundWindow(hwndToRestore);

	if (m_bPrintTimersPaused)
	{
		UT_Win32Timer::pauseAllTimers(m_bPrevTimersPaused);
		m_bPrintTimersPaused = false;
	}
}

void GR_Win32Graphics::_showPrintAbort(void)
{
	s_printAbort = false;
	if (s_printAbortWnd)
	{
		if (IsWindow(s_printAbortWnd))
			DestroyWindow(s_printAbortWnd);
		s_printAbortWnd = nullptr;
	}

	HWND owner = (m_hwndPrintOwner && IsWindow(m_hwndPrintOwner)) ? m_hwndPrintOwner : nullptr;

	static bool registered = false;
	if (!registered)
	{
		WNDCLASSW wc;
		memset(&wc, 0, sizeof(wc));
		wc.lpfnWndProc = s_printAbortWndProc;
		wc.hInstance = GetModuleHandleW(nullptr);
		wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
		wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
		wc.lpszClassName = L"AbiWordPrintAbort";
		registered = RegisterClassW(&wc) != 0;
	}

	const XAP_StringSet * strings = XAP_App::getApp() ? XAP_App::getApp()->getStringSet() : nullptr;
	UT_Win32LocaleString title;
	UT_Win32LocaleString button;
	title.fromUTF8(strings ? strings->getValue(XAP_STRING_ID_DLG_UP_PrintTitle) : "Print");
	button.fromUTF8(strings ? strings->getValue(XAP_STRING_ID_DLG_Cancel) : "Cancel");
	if (!title.c_str() || !title.c_str()[0])
		title.fromASCII("Print");
	if (!button.c_str() || !button.c_str()[0])
		button.fromASCII("Cancel");

	const int width = 280;
	const int height = 120;
	int x = CW_USEDEFAULT;
	int y = CW_USEDEFAULT;
	if (owner)
	{
		// GetWindowRect leaves the rectangle untouched on failure.
		// Keep CW_USEDEFAULT so Windows places the popup itself.
		RECT frame;
		memset(&frame, 0, sizeof(frame));
		if (GetWindowRect(owner, &frame))
		{
			x = frame.left + ((frame.right - frame.left) - width) / 2;
			y = frame.top + ((frame.bottom - frame.top) - height) / 2;
		}
	}

	s_printAbortWnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"AbiWordPrintAbort", title.c_str(),
		WS_POPUP | WS_CAPTION | WS_SYSMENU,
		x, y, width, height, owner, nullptr, GetModuleHandleW(nullptr), nullptr);
	if (!s_printAbortWnd)
		return;

	CreateWindowExW(0, L"BUTTON", button.c_str(),
		WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
		90, 36, 100, 26, s_printAbortWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDCANCEL)),
		GetModuleHandleW(nullptr), nullptr);
	ShowWindow(s_printAbortWnd, SW_SHOW);
	UpdateWindow(s_printAbortWnd);
	if (owner && IsWindowEnabled(owner))
	{
		EnableWindow(owner, FALSE);
		m_hwndPrintDisabled = owner;
	}
}

bool GR_Win32Graphics::startPrint(void)
{
	UT_ASSERT(m_bPrint);
	UT_ASSERT(!m_bStartPrint);

	if (!m_bIsPreview)
	  {
	    m_bPrintCancelled = false;
	    s_printAbort = false;
	    if (!m_bPrintTimersPaused)
	      {
	        m_bPrevTimersPaused = UT_Win32Timer::timersPaused();
	        UT_Win32Timer::pauseAllTimers(true);
	        m_bPrintTimersPaused = true;
	      }
	    // SetAbortProc must run before StartDoc. Returning FALSE cancels the job.
	    SetAbortProc(m_hdc, s_printAbortProc);
	    // StartDocW may display a driver-owned modal dialog (e.g. Microsoft Print to PDF
	    // "Save Print Output As" on PORTPROMPT:). Defer the abort popup and disabling
	    // the owner window until after StartDocW returns so the two modal flows do not
	    // conflict on Windows 11.
	    m_bStartPrint = ( StartDocW(m_hdc, m_pDocInfo) > 0 );
	    if (m_bStartPrint)
	      {
	        _showPrintAbort();
	      }
	    else
	      {
	        closePrintAbort();
	      }
	  }
	else
	  {
	    m_bStartPrint = true;
	  }

	return m_bStartPrint;
}

bool GR_Win32Graphics::startPage(const char * /*szPageLabel*/, UT_uint32 /*pageNumber*/,
									bool bPortrait, UT_uint32 iWidth, UT_uint32 iHeight)
{
	// dmPaperWidth and dmPaperLength are both tenths of a millimetre.
	// Layout units are square, so the pixel aspect ratio must not scale one side.
	const double resolution = getResolution() ? static_cast<double>(getResolution()) : 1.0;
	iWidth = static_cast<UT_uint32>(static_cast<double>(iWidth) * 254.0 / resolution + 0.5);
	iHeight = static_cast<UT_uint32>(static_cast<double>(iHeight) * 254.0 / resolution + 0.5);

	if (s_printAbort)
	{
		m_bPrintCancelled = true;
		return false;
	}

	if (m_bStartPage)
	{
		if (!m_bIsPreview && EndPage(m_hdc) <= 0)
		{
			m_bStartPage = false;
			m_bPrintCancelled = true;
			return false;
		}
		if (m_bIsPreview)
			EndPage(m_hdc);
		// EndPage has closed the page. A later failure must not leave the flag set,
		// or endPrint() calls EndPage a second time.
		m_bStartPage = false;
	}

	// Correct for Portrait vs Landscape mode and paper size, only when needed.
	if (m_hDevMode)
	{
		const short desiredOrientation = bPortrait ? DMORIENT_PORTRAIT : DMORIENT_LANDSCAPE;
		const short paperWidth = static_cast<short>(bPortrait ? iWidth : iHeight);
		const short paperLength = static_cast<short>(bPortrait ? iHeight : iWidth);
		const bool haveValidSize = (paperWidth > 0 && paperLength > 0);
		const short matchedPaperSize = haveValidSize ? s_matchStandardPaperSize(paperWidth, paperLength) : 0;

		bool needReset = false;
		std::vector<BYTE> savedDevMode;

		DEVMODEW * pDevMode = static_cast<DEVMODEW *>(GlobalLock(m_hDevMode));
		if (pDevMode)
		{
			const bool orientationMatches =
				((pDevMode->dmFields & DM_ORIENTATION) != 0) &&
				(pDevMode->dmOrientation == desiredOrientation);

			bool paperMatches = !haveValidSize;
			if (haveValidSize)
			{
				if (matchedPaperSize != 0)
				{
					if (((pDevMode->dmFields & DM_PAPERSIZE) != 0) &&
					    (pDevMode->dmPaperSize == matchedPaperSize))
					{
						const bool widthOk = ((pDevMode->dmFields & DM_PAPERWIDTH) == 0) ||
							(std::abs(static_cast<int>(pDevMode->dmPaperWidth) - paperWidth) <= 15);
						const bool lengthOk = ((pDevMode->dmFields & DM_PAPERLENGTH) == 0) ||
							(std::abs(static_cast<int>(pDevMode->dmPaperLength) - paperLength) <= 15);
						paperMatches = widthOk && lengthOk;
					}
				}
				else if ((pDevMode->dmFields & (DM_PAPERWIDTH | DM_PAPERLENGTH)) == (DM_PAPERWIDTH | DM_PAPERLENGTH))
				{
					paperMatches =
						(std::abs(static_cast<int>(pDevMode->dmPaperWidth) - paperWidth) <= 15) &&
						(std::abs(static_cast<int>(pDevMode->dmPaperLength) - paperLength) <= 15);
				}
			}

			if (!orientationMatches || !paperMatches)
			{
				const SIZE_T dmBytes = GlobalSize(m_hDevMode);
				const SIZE_T structBytes = static_cast<SIZE_T>(pDevMode->dmSize) + static_cast<SIZE_T>(pDevMode->dmDriverExtra);
				const SIZE_T copyBytes = (structBytes > 0 && structBytes <= dmBytes) ? structBytes : dmBytes;
				if (copyBytes > 0)
				{
					savedDevMode.resize(copyBytes);
					memcpy(savedDevMode.data(), pDevMode, copyBytes);
				}

				// Preserve unrelated dmFields (duplex, color, tray, quality, etc.).
				pDevMode->dmFields |= DM_ORIENTATION;
				pDevMode->dmOrientation = desiredOrientation;
				if (haveValidSize)
				{
					if (matchedPaperSize != 0)
					{
						pDevMode->dmFields |= DM_PAPERSIZE;
						pDevMode->dmFields &= ~(DM_PAPERLENGTH | DM_PAPERWIDTH);
						pDevMode->dmPaperSize = matchedPaperSize;
						pDevMode->dmPaperLength = 0;
						pDevMode->dmPaperWidth = 0;
					}
					else
					{
						pDevMode->dmFields &= ~DM_PAPERSIZE;
						pDevMode->dmFields |= DM_PAPERLENGTH | DM_PAPERWIDTH;
						pDevMode->dmPaperSize = 0;
						pDevMode->dmPaperLength = paperLength;
						pDevMode->dmPaperWidth = paperWidth;
					}
				}
				needReset = true;
			}
			GlobalUnlock(m_hDevMode);
		}

		if (needReset)
		{
			bool resetOk = false;
			const wchar_t * printerName = m_printerName.empty() ? nullptr : m_printerName.c_str();
			// Call DocumentProperties() on the DEVMODE to propagate public fields
			// into the driver's private part before ResetDCW.
			if (fixDevMode(m_hDevMode, printerName))
			{
				pDevMode = static_cast<DEVMODEW *>(GlobalLock(m_hDevMode));
				if (pDevMode)
				{
					resetOk = (ResetDCW(m_hdc, pDevMode) != nullptr);
					GlobalUnlock(m_hDevMode);
				}
			}

			if (resetOk)
			{
				// ResetDCW resets all selected GDI objects on m_hdc back to defaults.
				m_pFont = nullptr;
				m_iFontAllocNo = 0;
				m_iDCFontAllocNo = 0;
				const int logY = GetDeviceCaps(m_hdc, LOGPIXELSY);
				const int logX = GetDeviceCaps(m_hdc, LOGPIXELSX);
				if (logY > 0)
				{
					m_nLogPixelsY = logY;
					m_fXYRatio = static_cast<double>(logX) / static_cast<double>(logY);
				}
			}
			else
			{
				// Restore DEVMODE and fall back to the existing DC configuration
				// instead of aborting the job when a driver rejects a page change.
				if (!savedDevMode.empty())
				{
					pDevMode = static_cast<DEVMODEW *>(GlobalLock(m_hDevMode));
					if (pDevMode && GlobalSize(m_hDevMode) >= savedDevMode.size())
						memcpy(pDevMode, savedDevMode.data(), savedDevMode.size());
					if (pDevMode)
						GlobalUnlock(m_hDevMode);
				}
				LOG_WIN32_EXCPT("fixDevMode/ResetDCW rejected page change; keeping existing DC settings");
			}
		}
	}

	if (s_printAbort)
	{
		m_bPrintCancelled = true;
		return false;
	}

	const int iRet = StartPage(m_hdc);
	if (s_printAbort)
		m_bPrintCancelled = true;

	if (!m_bIsPreview) {
	  m_bStartPage = iRet > 0;

	  if (m_bStartPage) {
	      // PHYSICALOFFSETX returns offset of printable area from the left edge
	      // of the physical printer paper. The value returned is in device units.
	      // Since the current mapping mode is MM_TEXT, this code _should_ work.
	      const POINT ptNew = {
		-GetDeviceCaps(m_hdc, PHYSICALOFFSETX),
		-GetDeviceCaps(m_hdc, PHYSICALOFFSETY)
	      };
	      SetViewportOrgEx(m_hdc, ptNew.x, ptNew.y, nullptr);
	    }
	}
	else {
	  m_bStartPage = true;
	}

	return m_bStartPage;
}

bool GR_Win32Graphics::endPrint(void)
{
  if (m_bIsPreview)
  {
    if (m_bStartPage)
      EndPage(m_hdc);
    m_bStartPage = false;
    m_bStartPrint = false;
    return true;
  }

  // After the abort procedure returns FALSE, AbortDoc replaces EndPage and EndDoc.
  bool ok = true;
  if (m_bStartPrint)
  {
    if (s_printAbort || m_bPrintCancelled)
      ok = AbortDoc(m_hdc) > 0;
    else
    {
      if (m_bStartPage && EndPage(m_hdc) <= 0)
        ok = false;
      if (ok)
        ok = EndDoc(m_hdc) > 0;
      else
        AbortDoc(m_hdc);
    }
  }
  m_bStartPage = false;
  m_bStartPrint = false;
  closePrintAbort();
  s_printAbort = false;
  return ok;
}

/**
 **  The View calls this when it receives a SetX/YScrollOffset
 **  call.  Move the contents of the window appropriately.  View
 **  will draw after you exit from this call.  View will only
 **  draw into the "exposed" area.
 **
 ** dx & dy are the change in x/y from the current scrolled position
 ** negative values indcate left/up movement, positive right/down movement
 **/
void GR_Win32Graphics::scroll(UT_sint32 dx, UT_sint32 dy)
{
	UT_sint32 oldDY = tdu(getPrevYOffset());
	UT_sint32 oldDX = (UT_sint32)((double)tdu(getPrevXOffset()) * m_fXYRatio);
	UT_sint32 newY = getPrevYOffset() + dy;
	UT_sint32 newX = getPrevXOffset() + dx;
	UT_sint32 ddx = -(UT_sint32)((double)(tdu(newX) - oldDX) * m_fXYRatio);
	UT_sint32 ddy = -(tdu(newY) - oldDY);
	setPrevYOffset(newY);
	setPrevXOffset(newX);
	if(ddx == 0 && ddy == 0)
	{
		return;
	}
	GR_Painter caretDisablerPainter(this); // not an elegant way to disable all carets, but it works beautifully - MARCM

	// ScrollWindowEx moves pixels on the window, not in the retained bitmap.
	m_bufferCoherent = false;
	// A clip left on the CS_OWNDC window would scroll only that rectangle.
	clearScreenClip();
	if (m_hwnd)
		ScrollWindowEx(m_hwnd, ddx, ddy, nullptr, nullptr, nullptr, nullptr, SW_INVALIDATE);
}

void GR_Win32Graphics::scroll(UT_sint32 x_dest, UT_sint32 y_dest,
							  UT_sint32 x_src, UT_sint32 y_src,
							  UT_sint32 width, UT_sint32 height)
{
	x_dest = (UT_sint32)((double)tdu(x_dest) * m_fXYRatio);
	y_dest = tdu(y_dest);
	x_src = (UT_sint32)((double)tdu(x_src) * m_fXYRatio);
	y_src = tdu(y_src);
	width = (UT_sint32)((double)tdu(width) * m_fXYRatio);
	height = tdu(height);
	RECT r;
	r.left = x_src;
	r.top = y_src;
	r.right = r.left + width;
	r.bottom = r.top + height;
	
	GR_Painter caretDisablerPainter(this); // not an elegant way to disable all carets, but it works beautifully - MARCM

	m_bufferCoherent = false;
	clearScreenClip();
	if (m_hwnd)
		ScrollWindowEx(m_hwnd, (x_dest - x_src), (y_dest - y_src),
					   &r, nullptr, nullptr, nullptr, SW_INVALIDATE | SW_ERASE);
}

void GR_Win32Graphics::clearArea(UT_sint32 x, UT_sint32 y, UT_sint32 width, UT_sint32 height)
{
	#ifdef GR_GRAPHICS_DEBUG
	UT_DEBUGMSG(("GR_Win32Graphics::clearArea %u %u %u %u\n",  x, y, width, height));	
	#endif
	
	x = (UT_sint32)((double)_tduX(x) * m_fXYRatio);
	y = _tduY(y);
	width = (UT_sint32)((double)_tduR(width) * m_fXYRatio);
	height = _tduR(height);	
	
	RECT r;
	r.left = x;
	r.top = y;
	r.right = r.left + width;
	r.bottom = r.top + height;

#if 1
	// for the sake of consistency we should be using the same method as the fillRect()
	// does; however, we already have brush created, so FillRect is probably quicker here
	// (someone should do some profiling on this one day)
	FillRect(m_hdc, &r, m_hClearBrush);
#else
	// this is the method used by fillRect(); if we were to use it, it we should
	// cache lb.lbColor from inside setBrush() to avoid calling GetObject() all the time
	LOGBRUSHW lb;
	GetObjectW(m_hClearBrush, sizeof(LOGBRUSHW), &lb);
	
	const COLORREFW cr = ::SetBkColor(m_hdc,  lb.lbColor);
	::ExtTextOutW(m_hdc, 0, 0, ETO_OPAQUE, &r, nullptr, 0, nullptr);
	::SetBkColor(m_hdc, cr);
#endif
}

void GR_Win32Graphics::invertRect(const UT_Rect* pRect)
{
	#ifdef GR_GRAPHICS_DEBUG
	UT_DEBUGMSG(("GR_Win32Graphics::invertRect\n"));	
	#endif

	if (!pRect || !m_hdc)
		return;

	RECT r;

	r.left = (UT_sint32)((double)_tduX(pRect->left) * m_fXYRatio);
	r.top = _tduY(pRect->top);
	r.right = (UT_sint32)((double)_tduX(pRect->left + pRect->width) * m_fXYRatio);
	r.bottom = _tduY(pRect->top + pRect->height);

	InvertRect(m_hdc, &r);
}

void GR_Win32Graphics::queueDraw(const UT_Rect* pRect)
{
	if (!m_hwnd)
		return;

	if (!pRect)
	{
		InvalidateRect(m_hwnd, nullptr, FALSE);
		return;
	}

	RECT r;
	r.left = (UT_sint32)((double)_tduX(pRect->left) * m_fXYRatio);
	r.top = _tduY(pRect->top);
	r.right = (UT_sint32)((double)_tduX(pRect->left + pRect->width) * m_fXYRatio);
	r.bottom = _tduY(pRect->top + pRect->height);
	InvalidateRect(m_hwnd, &r, FALSE);
}

void GR_Win32Graphics::setClipRect(const UT_Rect* pRect)
{
	// This causes a lot of drawing and screen refresh problems.
	// It can be removed from here without problems, but may
	// not work for other things.  For now leave code in place.
	// return;

	UT_DebugOnly<int> res;
	// m_pRect owns its rectangle. The caller passes a stack UT_Rect, or the
	// pointer returned by getClipRect(). Copy the values before releasing the
	// previous rectangle: deleting that pointer first is a use-after-free and
	// the restored clip no longer matches the selection.
	UT_Rect copy;
	bool haveRect = false;
	if (pRect)
	{
		copy = *pRect;
		haveRect = true;
	}
	if (haveRect)
		m_pRect.reset(new UT_Rect(copy));
	else
		m_pRect.reset();
	if (haveRect)
	{
		int l = (UT_sint32)((double)_tduX(copy.left) * m_fXYRatio);
		int t = _tduY(copy.top);
		int r = (UT_sint32)((double)_tduX(copy.left + copy.width) * m_fXYRatio);
		int b = _tduY(copy.top + copy.height);
		UT_Win32TraceF("clip l=%d t=%d r=%d b=%d hdc=%p", l, t, r, b, (void*)m_hdc);
		// CreateRectRgn fails when the rectangle is empty. SelectClipRgn
		// of NULL would drop clipping and paint the whole DC.
		HRGN hrgn = (r > l && b > t) ? CreateRectRgn(l, t, r, b) : nullptr;
		UT_Win32TraceF("clip-rgn %p", (void*)hrgn);
		int selected;
		if (hrgn)
		{
			selected = SelectClipRgn(m_hdc, hrgn);
			DeleteObject(hrgn);
		}
		else
		{
			SelectClipRgn(m_hdc, nullptr);
			selected = IntersectClipRect(m_hdc, 0, 0, 0, 0);
		}
		res = selected;
		UT_Win32TraceF("clip-select %d", selected);
		UT_Win32TraceHeap("clip");
		_noteBufferClip(l, t, r, b, false);
	}
	else		// stop clipping
	{
		UT_Win32TraceF("clip-clear hdc=%p", (void*)m_hdc);
		int selected = SelectClipRgn(m_hdc, nullptr);
		res = selected;
		UT_Win32TraceF("clip-clear-select %d", selected);
		UT_Win32TraceHeap("clip-clear");
		_noteBufferClip(0, 0, 0, 0, true);
	}

	UT_ASSERT_HARMLESS(res != ERROR);
}

void GR_Win32Graphics::clearScreenClip(void)
{
	// The document window is CS_OWNDC, so BeginPaint and setClipRect leave
	// their region on the window DC. ScrollWindowEx then moves only that
	// rectangle: a strip of text slides and the caret no longer matches it.
	// GetDC returns that same owned DC even while drawing goes to the buffer.
	if (!m_hwnd || m_bPrint)
		return;
	HDC screen = GetDC(m_hwnd);
	if (!screen)
		return;
	SelectClipRgn(screen, nullptr);
	ReleaseDC(m_hwnd, screen);
}

GR_Image* GR_Win32Graphics::createNewImage(const char* pszName, const UT_ConstByteBufPtr & pBB, const std::string& mimetype,
					   UT_sint32 iDisplayWidth, UT_sint32 iDisplayHeight,
					   GR_Image::GRType iType)
{
	iDisplayWidth = (UT_sint32)((double)_tduR(iDisplayWidth) * m_fXYRatio);
	iDisplayHeight = _tduR(iDisplayHeight);
	
	GR_Image* pImg = nullptr;
	if (iType == GR_Image::GRT_Raster)
		pImg = new GR_Win32Image(pszName);
	else
		pImg = new GR_VectorImage(pszName);

	if (!pImg->convertFromBuffer(pBB, mimetype, iDisplayWidth, iDisplayHeight))
	{
		delete pImg;
		return nullptr;
	}

	return pImg;
}

typedef BOOL (WINAPI *GR_Win32AlphaBlendFn)(
	HDC, int, int, int, int,
	HDC, int, int, int, int,
	BLENDFUNCTION);

static GR_Win32AlphaBlendFn s_alphaBlendFn(void)
{
	static GR_Win32AlphaBlendFn fn = nullptr;
	static bool tried = false;
	if (!tried)
	{
		tried = true;
		HMODULE dll = LoadLibraryW(L"msimg32.dll");
		if (dll)
		{
			fn = reinterpret_cast<GR_Win32AlphaBlendFn>(reinterpret_cast<void *>(GetProcAddress(dll, "AlphaBlend")));
			if (!fn)
				FreeLibrary(dll);
		}
	}
	return fn;
}

static void s_premultiplyStraight(UT_Byte *pixels, UT_uint32 count)
{
	for (UT_uint32 i = 0; i < count; ++i)
	{
		const UT_Byte a = pixels[3];
		pixels[0] = (UT_Byte)((pixels[0] * a + 127) / 255);
		pixels[1] = (UT_Byte)((pixels[1] * a + 127) / 255);
		pixels[2] = (UT_Byte)((pixels[2] * a + 127) / 255);
		pixels += 4;
	}
}

/* Screen: blend a cached premultiplied 32-bit HBITMAP over the page. */
static bool s_alphaBlendBitmap(HDC hdc, int xDest, int yDest, int destW, int destH,
	HBITMAP hbmp, int srcW, int srcH, int constantAlpha)
{
	if (!hdc || !hbmp || destW <= 0 || destH <= 0 || srcW <= 0 || srcH <= 0)
		return false;
	GR_Win32AlphaBlendFn blend = s_alphaBlendFn();
	if (!blend)
		return false;

	HDC mem = CreateCompatibleDC(hdc);
	if (!mem)
		return false;

	HGDIOBJ prev = SelectObject(mem, hbmp);
	BLENDFUNCTION blendfn;
	blendfn.BlendOp = AC_SRC_OVER;
	blendfn.BlendFlags = 0;
	blendfn.SourceConstantAlpha = (BYTE)constantAlpha;
	blendfn.AlphaFormat = AC_SRC_ALPHA;
	const BOOL ok = blend(hdc, xDest, yDest, destW, destH, mem, 0, 0, srcW, srcH, blendfn);
	if (prev)
		SelectObject(mem, prev);
	DeleteDC(mem);
	return ok != FALSE;
}

/* Screen: blend the straight BGRA DIB over the page that was already painted. */
static bool s_alphaBlendImage(HDC hdc, int xDest, int yDest, int destW, int destH,
	const UT_Byte *src, int srcW, int srcH, int constantAlpha)
{
	if (!hdc || !src || destW <= 0 || destH <= 0 || srcW <= 0 || srcH <= 0)
		return false;
	GR_Win32AlphaBlendFn blend = s_alphaBlendFn();
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
	if (!hbmp || !bits)
	{
		if (hbmp)
			DeleteObject(hbmp);
		DeleteDC(mem);
		return false;
	}

	const UT_uint32 count = (UT_uint32)srcW * (UT_uint32)srcH;
	memcpy(bits, src, (size_t)count * 4);
	s_premultiplyStraight(static_cast<UT_Byte *>(bits), count);

	HGDIOBJ prev = SelectObject(mem, hbmp);
	BLENDFUNCTION blendfn;
	blendfn.BlendOp = AC_SRC_OVER;
	blendfn.BlendFlags = 0;
	blendfn.SourceConstantAlpha = (BYTE)constantAlpha;
	blendfn.AlphaFormat = AC_SRC_ALPHA;
	const BOOL ok = blend(hdc, xDest, yDest, destW, destH, mem, 0, 0, srcW, srcH, blendfn);
	if (prev)
		SelectObject(mem, prev);
	DeleteObject(hbmp);
	DeleteDC(mem);
	return ok != FALSE;
}

/* Printer and metafile DCs do not record AlphaBlend. Composite on white paper. */
static bool s_stretchAlphaOnWhite(HDC hdc, int xDest, int yDest, int destW, int destH,
	const UT_Byte *src, int srcW, int srcH)
{
	if (!hdc || !src || destW <= 0 || destH <= 0 || srcW <= 0 || srcH <= 0)
		return false;

	const int stride = (srcW * 3 + 3) & ~3;
	const size_t bytes = sizeof(BITMAPINFOHEADER) + (size_t)stride * (size_t)srcH;
	if (bytes > 0xffffffffu)
		return false;

	UT_Byte *block = static_cast<UT_Byte *>(UT_calloc(1, (UT_uint32)bytes));
	if (!block)
		return false;

	BITMAPINFOHEADER *hdr = reinterpret_cast<BITMAPINFOHEADER *>(block);
	hdr->biSize = sizeof(BITMAPINFOHEADER);
	hdr->biWidth = srcW;
	hdr->biHeight = srcH;
	hdr->biPlanes = 1;
	hdr->biBitCount = 24;
	hdr->biCompression = BI_RGB;

	UT_Byte *dst = block + sizeof(BITMAPINFOHEADER);
	for (int y = 0; y < srcH; ++y)
	{
		const UT_Byte *srow = src + (size_t)y * (size_t)srcW * 4;
		UT_Byte *drow = dst + (size_t)y * (size_t)stride;
		for (int x = 0; x < srcW; ++x)
		{
			const int a = srow[x * 4 + 3];
			const int inv = 255 - a;
			drow[x * 3 + 0] = (UT_Byte)((srow[x * 4 + 0] * a + 255 * inv + 127) / 255);
			drow[x * 3 + 1] = (UT_Byte)((srow[x * 4 + 1] * a + 255 * inv + 127) / 255);
			drow[x * 3 + 2] = (UT_Byte)((srow[x * 4 + 2] * a + 255 * inv + 127) / 255);
		}
	}

	const int res = StretchDIBits(hdc, xDest, yDest, destW, destH,
		0, 0, srcW, srcH, dst, reinterpret_cast<BITMAPINFO *>(block),
		DIB_RGB_COLORS, SRCCOPY);
	FREEP(block);
	return res != (int)GDI_ERROR;
}

void GR_Win32Graphics::drawImage(GR_Image* pImg, UT_sint32 xDest, UT_sint32 yDest)
{
	UT_return_if_fail(pImg);
	
	xDest = (UT_sint32)((double)_tduX(xDest) * m_fXYRatio);
	yDest = UT_image_page_y(_tduY(yDest));
	
	if (pImg->getType() != GR_Image::GRT_Raster)
	{
		pImg->render(this, xDest, yDest);
		return;
	}

	// startPage() already opens one physical page, and the view draws that
	// page in page-local coordinates. Text is not folded with PHYSICALHEIGHT.
	// Doing it here sent a picture whose device Y passed the paper edge back
	// to the top of the page.

	GR_Win32Image* pWin32Img = static_cast<GR_Win32Image*>(pImg);

	BITMAPINFO* pDIB = pWin32Img->getDIB();

	if (pDIB == nullptr)
		return;
		
	UT_uint32 iSizeOfColorData = pDIB->bmiHeader.biClrUsed * sizeof(RGBQUAD);
	UT_Byte* pBits = ((unsigned char*) pDIB) + pDIB->bmiHeader.biSize + iSizeOfColorData;

	if (pWin32Img->hasAlpha() && pDIB->bmiHeader.biBitCount == 32
		&& pDIB->bmiHeader.biWidth > 0 && pDIB->bmiHeader.biHeight > 0)
	{
		const DWORD kind = GetObjectType(m_hdc);
		const bool paper = queryProperties(GR_Graphics::DGP_PAPER)
			|| kind == OBJ_ENHMETADC || kind == OBJ_METADC;
		const int srcW = pDIB->bmiHeader.biWidth;
		const int srcH = pDIB->bmiHeader.biHeight;
		const int destW = pImg->getDisplayWidth();
		const int destH = pImg->getDisplayHeight();
		if (!paper)
		{
			HBITMAP hPremul = pWin32Img->getPremultipliedBitmap(m_hdc);
			if (hPremul && s_alphaBlendBitmap(m_hdc, xDest, yDest, destW, destH, hPremul, srcW, srcH, 255))
				return;
			if (!hPremul && s_alphaBlendImage(m_hdc, xDest, yDest, destW, destH, pBits, srcW, srcH, 255))
				return;
		}
		if (s_stretchAlphaOnWhite(m_hdc, xDest, yDest, destW, destH, pBits, srcW, srcH))
			return;
	}

	int iRes = 0;

	if(!iRes)
	{
		const int previousMode = SetStretchBltMode(m_hdc, HALFTONE);
		POINT previousOrg;
		SetBrushOrgEx(m_hdc, 0, 0, &previousOrg);
		iRes = StretchDIBits(m_hdc,
							 xDest, yDest,
							 pImg->getDisplayWidth(), pImg->getDisplayHeight(),
							 0, 0,
							 pDIB->bmiHeader.biWidth, pDIB->bmiHeader.biHeight,
							 pBits, pDIB, DIB_RGB_COLORS, SRCCOPY);
		SetBrushOrgEx(m_hdc, previousOrg.x, previousOrg.y, nullptr);
		SetStretchBltMode(m_hdc, previousMode);
	}
	
	if (iRes == static_cast<int>(GDI_ERROR))
	{
		UT_DebugOnly<DWORD> err = GetLastError();
		UT_DEBUGMSG(("StretchDIBits failed with err %d\n", err));
	}
}

void GR_Win32Graphics::drawImage(GR_Image* pImg, UT_sint32 xDest, UT_sint32 yDest, const GR_ImageTune& tune)
{
	if (!pImg || !tune.isAdjusted() || pImg->getType() != GR_Image::GRT_Raster)
	{
		drawImage(pImg, xDest, yDest);
		return;
	}

	GR_Win32Image* pWin32Img = static_cast<GR_Win32Image*>(pImg);
	BITMAPINFO* pDIB = pWin32Img->getDIB();
	if (!pDIB || pDIB->bmiHeader.biWidth == 0 || pDIB->bmiHeader.biHeight == 0)
	{
		drawImage(pImg, xDest, yDest);
		return;
	}

	const int srcW = pDIB->bmiHeader.biWidth > 0 ? pDIB->bmiHeader.biWidth : -pDIB->bmiHeader.biWidth;
	const int srcH = pDIB->bmiHeader.biHeight > 0 ? pDIB->bmiHeader.biHeight : -pDIB->bmiHeader.biHeight;
	const bool bottomUp = pDIB->bmiHeader.biHeight > 0;
	const int bpp = pDIB->bmiHeader.biBitCount / 8;
	if ((bpp != 3 && bpp != 4) || srcW <= 0 || srcH <= 0)
	{
		drawImage(pImg, xDest, yDest);
		return;
	}

	int left = (int)((int64_t)tune.cropLeft * srcW / 100);
	int right = (int)((int64_t)tune.cropRight * srcW / 100);
	int top = (int)((int64_t)tune.cropTop * srcH / 100);
	int bottom = (int)((int64_t)tune.cropBottom * srcH / 100);
	if (left < 0) left = 0;
	if (left > srcW - 1) left = srcW - 1;
	if (top < 0) top = 0;
	if (top > srcH - 1) top = srcH - 1;
	if (right < 0) right = 0;
	if (right > srcW - 1 - left) right = srcW - 1 - left;
	if (bottom < 0) bottom = 0;
	if (bottom > srcH - 1 - top) bottom = srcH - 1 - top;
	const int cropW = srcW - left - right;
	const int cropH = srcH - top - bottom;
	if (cropW <= 0 || cropH <= 0)
		return;

	const int srcStride = ((srcW * bpp + 3) & ~3);
	UT_uint32 colorBytes = pDIB->bmiHeader.biClrUsed * sizeof(RGBQUAD);
	const UT_Byte* srcBits = reinterpret_cast<const UT_Byte*>(pDIB) + pDIB->bmiHeader.biSize + colorBytes;

	const size_t outBytes = (size_t)cropW * (size_t)cropH * 4;
	if (outBytes == 0 || outBytes > 0x7fffffffu)
		return;
	UT_Byte* out = static_cast<UT_Byte*>(UT_calloc(1, (UT_uint32)outBytes));
	if (!out)
		return;

	const int bright = tune.brightness;
	const int opacity = tune.opacity < 0 ? 0 : (tune.opacity > 100 ? 100 : tune.opacity);
	for (int y = 0; y < cropH; ++y)
	{
		const int visualY = top + y;
		int memY = bottomUp ? (srcH - 1 - visualY) : visualY;
		if (memY < 0)
			memY = 0;
		else if (memY > srcH - 1)
			memY = srcH - 1;
		const UT_Byte* srow = srcBits + (size_t)memY * (size_t)srcStride + (size_t)left * (size_t)bpp;
		UT_Byte* drow = out + (size_t)(cropH - 1 - y) * (size_t)cropW * 4;
		for (int x = 0; x < cropW; ++x)
		{
			int b = srow[x * bpp + 0];
			int g = srow[x * bpp + 1];
			int r = srow[x * bpp + 2];
			int a = bpp == 4 ? srow[x * bpp + 3] : 255;
			if (bright != 0)
			{
				const int delta = bright * 255 / 100;
				b += delta; g += delta; r += delta;
				if (b < 0) b = 0; else if (b > 255) b = 255;
				if (g < 0) g = 0; else if (g > 255) g = 255;
				if (r < 0) r = 0; else if (r > 255) r = 255;
			}
			a = a * opacity / 100;
			drow[x * 4 + 0] = (UT_Byte)b;
			drow[x * 4 + 1] = (UT_Byte)g;
			drow[x * 4 + 2] = (UT_Byte)r;
			drow[x * 4 + 3] = (UT_Byte)a;
		}
	}

	xDest = (UT_sint32)((double)_tduX(xDest) * m_fXYRatio);
	yDest = UT_image_page_y(_tduY(yDest));

	const int destW = pImg->getDisplayWidth();
	const int destH = pImg->getDisplayHeight();
	const DWORD kind = GetObjectType(m_hdc);
	const bool paper = queryProperties(GR_Graphics::DGP_PAPER)
		|| kind == OBJ_ENHMETADC || kind == OBJ_METADC;
	bool drawn = false;
	if (!paper)
		drawn = s_alphaBlendImage(m_hdc, xDest, yDest, destW, destH, out, cropW, cropH, 255);
	if (!drawn)
		s_stretchAlphaOnWhite(m_hdc, xDest, yDest, destW, destH, out, cropW, cropH);
	FREEP(out);
}

HWND GR_Win32Graphics::getHwnd(void) const
{
	return m_hwnd;
}

void GR_Win32Graphics::setColorSpace(GR_Graphics::ColorSpace /*c*/)
{
	// TODO:  maybe?
	UT_ASSERT_HARMLESS(UT_SHOULD_NOT_HAPPEN);
}

GR_Graphics::ColorSpace GR_Win32Graphics::getColorSpace(void) const
{
	return m_cs;
}

void GR_Win32Graphics::setCursor(GR_Graphics::Cursor c)
{
	// set the cursor type, but wait for a WM_SETCURSOR
	// to do anything about it.
	m_cursor = c;
}

GR_Graphics::Cursor GR_Win32Graphics::getCursor(void) const
{
	return m_cursor;
}

void GR_Win32Graphics::handleSetCursorMessage(void)
{
	// deal with WM_SETCURSOR message.

	XAP_Win32App * pWin32App = static_cast<XAP_Win32App *>(XAP_App::getApp());
	HINSTANCE hinst = pWin32App->getInstance();
	LPCTSTR cursor_name;                //TODO : CHECK

	switch (m_cursor)
	{
	case GR_CURSOR_CROSSHAIR:	
		cursor_name = IDC_CROSS;
		hinst = nullptr;
		break;

	default:
	{
		// this assert makes debugging virtuall impossible !!!
		static bool bDoneThisAlready = false;
		if(!bDoneThisAlready)
		{
			bDoneThisAlready = true;
			UT_ASSERT_HARMLESS(UT_NOT_IMPLEMENTED);
		}
		
	}
		/*FALLTHRU*/
	case GR_CURSOR_DEFAULT:
		cursor_name = IDC_ARROW;		// top-left arrow
		hinst = nullptr;
		break;

	case GR_CURSOR_LINK:
	case GR_CURSOR_GRAB:
#ifndef IDC_HAND
		cursor_name = MAKEINTRESOURCE(IDC_ABIHAND);
#else
		if (UT_IsWin95())
			cursor_name = MAKEINTRESOURCE(IDC_ABIHAND);
		else
		{
			cursor_name = IDC_HAND;
			hinst = nullptr;
		}
#endif
		break;

	case GR_CURSOR_EXCHANGE:
		cursor_name = MAKEINTRESOURCE(IDC_EXCHANGE);
		break;

	case GR_CURSOR_IBEAM:
		cursor_name = IDC_IBEAM;
		hinst = nullptr;
		break;	

	case GR_CURSOR_RIGHTARROW:
		cursor_name = MAKEINTRESOURCE(IDC_ABIRIGHTARROW);
		break;

	case GR_CURSOR_LEFTARROW:
		cursor_name = IDC_ARROW;		// TODO change this
		hinst = nullptr;
		break;

	case GR_CURSOR_DOWNARROW:
		cursor_name = MAKEINTRESOURCE(IDC_ABIDOWNARROW);
		break;

	case GR_CURSOR_IMAGE:
		cursor_name = IDC_SIZEALL;
		hinst = nullptr;
		break;

	case GR_CURSOR_IMAGESIZE_NW:
	case GR_CURSOR_IMAGESIZE_SE:
		cursor_name = IDC_SIZENWSE;
		hinst = nullptr;
		break;

	case GR_CURSOR_HLINE_DRAG:
	case GR_CURSOR_UPDOWN:
	case GR_CURSOR_IMAGESIZE_N:
	case GR_CURSOR_IMAGESIZE_S:
		cursor_name = IDC_SIZENS;
		hinst = nullptr;
		break;

	case GR_CURSOR_IMAGESIZE_NE:
	case GR_CURSOR_IMAGESIZE_SW:
		cursor_name = IDC_SIZENESW;
		hinst = nullptr;
		break;

	case GR_CURSOR_VLINE_DRAG:
	case GR_CURSOR_LEFTRIGHT:
	case GR_CURSOR_IMAGESIZE_E:
	case GR_CURSOR_IMAGESIZE_W:
		cursor_name = IDC_SIZEWE;
		hinst = nullptr;
		break;

	case GR_CURSOR_WAIT:
		cursor_name = IDC_WAIT;
		hinst = nullptr;
		break;
	}

	HCURSOR hCursor = LoadCursor(hinst,cursor_name); //TODO: Leaking resource
	if (hCursor != nullptr)
		SetCursor(hCursor);
}

void GR_Win32Graphics::setColor3D(GR_Color3D c)
{
	UT_ASSERT(c < COUNT_3D_COLORS);
	_setColor(m_3dColors[c]);
}

bool GR_Win32Graphics::getColor3D(GR_Color3D c, UT_RGBColor & color)
{
	UT_return_val_if_fail(c < COUNT_3D_COLORS, false);
	color.m_red = GetRValue(m_3dColors[c]);
	color.m_grn = GetGValue(m_3dColors[c]);
	color.m_blu = GetBValue(m_3dColors[c]);
	return true;
}

void GR_Win32Graphics::init3dColors(void)
{
	HIGHCONTRASTW hc = {};
	hc.cbSize = sizeof(hc);
	const bool bHC = SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0) &&
					 (hc.dwFlags & HCF_HIGHCONTRASTON);

	if (bHC)
	{
		m_3dColors[CLR3D_Foreground] = GetSysColor(COLOR_BTNTEXT);
		m_3dColors[CLR3D_Background] = GetSysColor(COLOR_3DFACE);
		m_3dColors[CLR3D_BevelUp]    = GetSysColor(COLOR_3DHIGHLIGHT);
		m_3dColors[CLR3D_BevelDown]  = GetSysColor(COLOR_3DSHADOW);
		m_3dColors[CLR3D_Highlight]  = GetSysColor(COLOR_WINDOW);
	}
	else if (UT_Win32AppsUseDarkTheme())
	{
		m_3dColors[CLR3D_Background] = RGB(32, 32, 32);
		m_3dColors[CLR3D_Highlight]  = RGB(54, 54, 54);
		m_3dColors[CLR3D_BevelDown]  = RGB(42, 42, 42);
		m_3dColors[CLR3D_BevelUp]    = RGB(68, 68, 68);
		m_3dColors[CLR3D_Foreground] = RGB(190, 190, 190);
	}
	else
	{
		m_3dColors[CLR3D_Background] = RGB(243, 243, 243);
		m_3dColors[CLR3D_Highlight]  = RGB(255, 255, 255);
		m_3dColors[CLR3D_BevelDown]  = RGB(224, 224, 224);
		m_3dColors[CLR3D_BevelUp]    = RGB(250, 250, 250);
		m_3dColors[CLR3D_Foreground] = RGB(96, 96, 96);
	}
}

void GR_Win32Graphics::fillRect(GR_Color3D c, UT_sint32 x, UT_sint32 y, UT_sint32 w, UT_sint32 h)
{
	UT_ASSERT(c < COUNT_3D_COLORS);

	#ifdef GR_GRAPHICS_DEBUG
	UT_DEBUGMSG(("GR_Win32Graphics::fillRect GR_Color3D  %x %u %u %u %u\n",  c, x, y, w, h));	
	#endif
	
	RECT r;
	r.left = (UT_sint32)((double)_tduX(x) * m_fXYRatio);
	r.top = _tduY(y);
	r.right = (UT_sint32)((double)_tduX(x+w) * m_fXYRatio);
	r.bottom = _tduY(y+h);

	// This might look wierd (and I think it is), but it's MUCH faster.
	// CreateSolidBrush is dog slow.
	HDC hdc = m_hdc;
	COLORREF clr = RGB(GetRValue(m_3dColors[c]), GetGValue(m_3dColors[c]), GetBValue(m_3dColors[c]));
	
	const COLORREF cr = ::SetBkColor(hdc,  clr);
	if (cr == CLR_INVALID)
		return;
	::ExtTextOutW(hdc, 0, 0, ETO_OPAQUE, &r, nullptr, 0, nullptr);
	::SetBkColor(hdc, cr);
}

void GR_Win32Graphics::fillRect(GR_Color3D c, UT_Rect &r)
{
	UT_ASSERT(c < COUNT_3D_COLORS);
	fillRect(c,r.left,r.top,r.width,r.height);
}

//////////////////////////////////////////////////////////////////
// This is a static method in the GR_Font base class implemented
// in platform code.
//////////////////////////////////////////////////////////////////

void GR_Font::s_getGenericFontProperties(const char * szFontName,
										 FontFamilyEnum * pff,
										 FontPitchEnum * pfp,
										 bool * pbTrueType)
{
	// describe in generic terms the named font.

	// we borrow some code from GR_Win32Graphics::findFont()

	LOGFONTW lf;
	memset(&lf, 0, sizeof(lf));

	TEXTMETRICW tm;
	memset(&tm, 0, sizeof(tm));

	// TODO i'm not sure why we special case these, but the other
	// TODO code did, so i'm going to here.

	if (g_ascii_strcasecmp(szFontName, "serif") == 0)
		lf.lfPitchAndFamily = DEFAULT_PITCH | FF_ROMAN;
	else if (g_ascii_strcasecmp(szFontName, "sans-serif") == 0)
		lf.lfPitchAndFamily = DEFAULT_PITCH | FF_SWISS;
	else if (g_ascii_strcasecmp(szFontName, "cursive") == 0)
		lf.lfPitchAndFamily = DEFAULT_PITCH | FF_SCRIPT;
	else if (g_ascii_strcasecmp(szFontName, "fantasy") == 0)
		lf.lfPitchAndFamily = DEFAULT_PITCH | FF_DECORATIVE;
	else if (g_ascii_strcasecmp(szFontName, "monospace") == 0)
		lf.lfPitchAndFamily = DEFAULT_PITCH | FF_MODERN;
	else
	{
		UT_Win32LocaleString str;
		str.fromUTF8 (szFontName);
			lstrcpynW(lf.lfFaceName, 
				    str.c_str(),
			        LF_FACESIZE);
	}

	// let the system create the font with the given name or
	// properties and then query it and see what was actually
	// created.  hopefully, this will let us more accurately
	// reflect what is being seen on screen.

	// CreateFontIndirect and CreateDC return NULL on failure. GetTextMetrics
	// leaves TEXTMETRIC untouched, so a failed call must not be read.
	HFONT hFont = CreateFontIndirectW(&lf);
	HDC hdc = CreateDCW(L"DISPLAY",nullptr,nullptr,nullptr);
	bool measured = false;
	if (hFont && hdc)
	{
		HFONT hFontOld = (HFONT) SelectObject(hdc,hFont);
		if (hFontOld && GetTextMetricsW(hdc,&tm))
			measured = true;
		if (hFontOld)
			SelectObject(hdc,hFontOld);
	}
	if (hFont)
		DeleteObject(hFont);
	if (hdc)
		DeleteDC(hdc);
	if (!measured)
	{
		if (pff) *pff = FF_Unknown;
		if (pfp) *pfp = FP_Variable;
		if (pbTrueType) *pbTrueType = false;
		return;
	}

	BYTE xx = tm.tmPitchAndFamily;

	switch (xx & 0xf0)
	{
	default:
	case FF_DONTCARE:	*pff = FF_Unknown;		break;
	case FF_ROMAN:		*pff = FF_Roman;		break;
	case FF_SWISS:		*pff = FF_Swiss;		break;
	case FF_MODERN:		*pff = FF_Modern;		break;
	case FF_SCRIPT:		*pff = FF_Script;		break;
	case FF_DECORATIVE:	*pff = FF_Decorative;	break;
	}

	if ((xx & TMPF_FIXED_PITCH) == TMPF_FIXED_PITCH)
		*pfp = FP_Variable;				// yes these look backwards
	else								// but that is how windows
		*pfp = FP_Fixed;				// defines the bits...

	*pbTrueType = ((xx & TMPF_TRUETYPE) == TMPF_TRUETYPE);

	return;
}

/*!
    hdc - the primary hdc on which we are expected to draw
    printHDC - the hdc for which to retrive metrics
*/
GR_Win32Font::GR_Win32Font(LOGFONTW & lf, double fPoints, HDC hdc, HDC printHDC)
:	m_hdc(hdc),
	m_xhdc(nullptr), // once all the processing is done, this is changed to printHDC
	m_yhdc(nullptr), // once all the processing is done, this is changed to printHDC
	m_defaultCharWidth(0),
	m_layoutFont(nullptr),
	m_tm(TEXTMETRICW()),
	m_bGUIFont(false),
	m_fPointSize(fPoints)
{
	m_iHeight = abs(lf.lfHeight);

	m_layoutFont = CreateFontIndirectW(&lf); // this is what we see to start with

	if(!m_layoutFont)
	{
		LOG_WIN32_EXCPT("CreateFontIndirectFailed");
		return;
	}
	
	insertFontInCache (m_iHeight, m_layoutFont);

	HFONT printFont = m_layoutFont;
	
	if(hdc != printHDC)
	{
		int nPrintLogPixelsY = GetDeviceCaps(printHDC, LOGPIXELSY);

		// use the point size rather than the screen pixel size to minimise rounding error
		// (lfHeight already carries a rounding error from conversion pts -> screen pixels)
		// lf.lfHeight = MulDiv(lf.lfHeight, nPrintLogPixelsY, nLogPixelsY);
		lf.lfHeight =  (int)(-fPoints * (double)nPrintLogPixelsY / 72.00 + 0.5);
		printFont = CreateFontIndirectW(&lf);
		
		if(!printFont)
		{
			LOG_WIN32_EXCPT("CreateFontIndirectFailed");
			return;
		}

		insertFontInCache(abs(lf.lfHeight), printFont);

	}
	
	//
	// TMN: We need to initialize 'this' to _something_, why we use the
	// screen DC. Note that this is a *bad hack* forced by bad design. :-(
	//
	if (!hdc || !printHDC)
	{
		// NOW what? We can't throw an exeption, and this object isn't
		// legal yet...
		LOG_WIN32_EXCPT("No DC")
		m_layoutFont = nullptr;
		return;
	}
	else
	{
		// now we need to retrieve the y-axis metrics
		HFONT hOldFont = (HFONT)SelectObject(printHDC, printFont);
		// A font selection fails with NULL. GDI_ERROR is the region failure.
		if (!hOldFont || hOldFont == (HFONT)GDI_ERROR)
		{
			LOG_WIN32_EXCPT("Could not select font into DC.")
			m_layoutFont = nullptr;
			return;
		}
		else
		{
			// Setting the m_hashKey. GetTextFaceW leaves the buffer untouched
			// when it fails, so it must start empty.
			wchar_t lpFaceName[LF_FACESIZE];
			lpFaceName[0] = 0;
			if (GetTextFaceW(printHDC, LF_FACESIZE, lpFaceName) == 0)
				lpFaceName[0] = 0;

			_updateFontYMetrics(hdc, printHDC);

			m_hashKey = UT_std_string_sprintf("%ls-%ld-%ld-%d-%d-%d-%d-%d",
							  lpFaceName,
							  m_tm.tmHeight, m_tm.tmWeight, m_tm.tmItalic, m_tm.tmUnderlined,
							  m_tm.tmStruckOut,
							  GetDeviceCaps(hdc, LOGPIXELSX),
							  GetDeviceCaps(hdc, LOGPIXELSY));

			// now we measure the default character
			// 
			// when we are called, the char widths might not exist yet; we
			// will force initialisation by a bogus call to
			// getCharWidthFromCache(); by measuring the space, we will
			// preload the entire Latin1 page
			getCharWidthFromCache(' ');
			
			UINT d = m_tm.tmDefaultChar;

			if (!_getCharWidths())
			{
				SelectObject(printHDC, hOldFont);
				return;
			}
			_getCharWidths()->setCharWidthsOfRange(printHDC, d, d);
			m_defaultCharWidth = getCharWidthFromCache(d);

			// this is not good SelectObject() is very expensive, and chances are this
			// call is unnecessary, but because of the design of the graphics class we
			// have no way of knowing
			SelectObject(printHDC, hOldFont);
		}

		m_xhdc = printHDC;
		m_yhdc = printHDC;
	}
}

GR_Win32Font * GR_Win32Font::newFont(LOGFONTW &lf, double fPoints, HDC hdc, HDC printHDC)
{
	GR_Win32Font * f = new GR_Win32Font(lf, fPoints, hdc, printHDC);

	if(!f || !f->getFontHandle())
	{
		delete f;
		f = nullptr;
	}

	return f;
}


GR_Win32Font::~GR_Win32Font()
{
	// We currently can't delete the font, since it might
	// be selected into a DC. Also, we don't keep track of previously
	// selected font [into the DC] why it's impossible to revert to that
	// one! :-<
	// But, let's try it like this...
	//
	// the GetObjectType call with 0 throws a first chance exception
	// (this whole thing is rather messy)

	DWORD dwObjType;
	bool bIsDC = false;

	if(m_hdc) 
	{
		dwObjType = GetObjectType((HGDIOBJ)m_hdc);
		bIsDC = (dwObjType == OBJ_DC || dwObjType == OBJ_MEMDC);
	}

	for (UT_sint32 i = 0; i < m_allocFonts.getItemCount(); ++i)
	  {
		  allocFont *p = (allocFont *)m_allocFonts.getNthItem(i);

		  if (p && p->hFont)
		  {
			  if (bIsDC && p->hFont == (HFONT)::GetCurrentObject(m_hdc, OBJ_FONT))
				  SelectObject(m_hdc, GetStockObject(SYSTEM_FONT));
			  DeleteObject(p->hFont);
		  }

		  delete p;
	  }
}

/*!
    hdc -- handle to the primary DC on which we draw
    printHDC -- handle to the DC on which we want to measure the font (typically the printer)
                can be nullptr (in which case hdc will be used

    NB: the caller must ensure that the corresponding HFONT is already selected onto the
    DC represented by printHDC (or hdc if nullptr).
*/
void GR_Win32Font::_updateFontYMetrics(HDC hdc, HDC printHDC)
{
	// have to have at least the prinary DC
	UT_return_if_fail( hdc );
	
	// only do this if we have reason to believe the cached values are stale
	if(!printHDC)
	{
		printHDC = hdc;
	}

	// we have to remeasure if (a) the printer changed, or (b) the primary device changed
	if(printHDC != m_yhdc)
	{
		// A failed GetTextMetrics leaves TEXTMETRIC untouched. Caching that
		// as the printer face makes every later line use a zero height.
		TEXTMETRICW tm;
		memset(&tm, 0, sizeof(tm));
		if (!GetTextMetricsW(printHDC, &tm))
			return;
		m_tm = tm;

		// now we have to scale the metrics down from the printHDC to our layout units
		// 
		// (we used to scale the metrics down to the device resolution, but that since we
		// then scale it back up to layout units all the time, this caused us huge
		// rounding error) Tomas, 16/9/05
		// 
		// NB: we do not want to obtain the metrics directly for the primary dc
		// Windows is designed to get slightly bigger font for the screen than the
		// user asks for (MS say to improve readibility), and this causes us
		// non-WYSIWYG behaviour (basically our characters are wider on screen than on
		// the printer and our lines are further apart)
		int nLogPixelsY      = GR_Graphics::getResolution();
		int nPrintLogPixelsY = GetDeviceCaps(printHDC, LOGPIXELSY);

		if(nLogPixelsY != nPrintLogPixelsY)
		{
			m_tm.tmAscent  = MulDiv(m_tm.tmAscent, nLogPixelsY, nPrintLogPixelsY);
			m_tm.tmDescent = MulDiv(m_tm.tmDescent, nLogPixelsY, nPrintLogPixelsY);
			m_tm.tmHeight  = MulDiv(m_tm.tmHeight, nLogPixelsY, nPrintLogPixelsY);
		}

		// now remember what HDC these values are for
		m_yhdc = printHDC;
	}
}


/*! hdc - handle to device context for which the measurment is required */
UT_uint32 GR_Win32Font::getAscent(HDC hdc, HDC printHDC)
{
	if(!m_bGUIFont)
		_updateFontYMetrics(hdc, printHDC);
	return m_tm.tmAscent;
}

/*! hdc - handle to device context for which the measurment is required */
UT_uint32 GR_Win32Font::getDescent(HDC hdc, HDC printHDC)
{
	if(!m_bGUIFont)
		_updateFontYMetrics(hdc, printHDC);
	return m_tm.tmDescent;
}

/*! hdc - handle to device context for which the measurment is required */
UT_uint32 GR_Win32Font::getHeight(HDC hdc, HDC printHDC)
{
	if(!m_bGUIFont)
		_updateFontYMetrics(hdc, printHDC);
	return m_tm.tmHeight;
}


namespace {

struct Win32WidthKey
{
	std::wstring face;
	LONG height;
	LONG weight;
	BYTE italic;
	BYTE charset;
	UT_UCS4Char base;

	bool operator<(const Win32WidthKey & other) const
	{
		if (face != other.face)
			return face < other.face;
		if (height != other.height)
			return height < other.height;
		if (weight != other.weight)
			return weight < other.weight;
		if (italic != other.italic)
			return italic < other.italic;
		if (charset != other.charset)
			return charset < other.charset;
		return base < other.base;
	}
};

struct Win32WidthPage
{
	UT_sint32 width[256];
};

std::map<Win32WidthKey, Win32WidthPage> & widthPages()
{
	static std::map<Win32WidthKey, Win32WidthPage> pages;
	return pages;
}

bool recallWidthPage(const Win32WidthKey & key, GR_CharWidths * widths)
{
	std::map<Win32WidthKey, Win32WidthPage>::const_iterator found = widthPages().find(key);
	if (found == widthPages().end())
		return false;
	for (UT_uint32 i = 0; i < 256; ++i)
		widths->setWidth(key.base + i, found->second.width[i]);
	return true;
}

void storeWidthPage(const Win32WidthKey & key, const GR_CharWidths * widths)
{
	Win32WidthPage page;
	for (UT_uint32 i = 0; i < 256; ++i)
		page.width[i] = widths->getWidth(key.base + i);
	widthPages()[key] = page;
}

} // namespace

UT_sint32 GR_Win32Font::measureUnremappedCharForCache(UT_UCS4Char cChar) const
{
#ifndef ABI_GRAPHICS_PLUGIN_NO_WIDTHS
	// first of all, handle 0-width spaces ...
	if(cChar == 0xFEFF || cChar == 0x200b || cChar == UCS_LIGATURE_PLACEHOLDER)
		return 0;

	// this function is only called when there is no value in the
	// cache; we will set not only the value for the character
	// required, but also for everything on the same char-width page
    UT_return_val_if_fail(_getCharWidths(),0);

	// calculate the limits of the 256-char page
	UT_UCS4Char base = (cChar & 0xffffff00);
	UT_UCS4Char limit = (cChar | 0x000000ff);
	LOGFONTW face;
	memset(&face, 0, sizeof(face));
	Win32WidthKey key{};
	key.base = base;
	const bool haveFace = m_layoutFont && GetObjectW(m_layoutFont, static_cast<int>(sizeof(face)), &face) == static_cast<int>(sizeof(face));
	if (haveFace)
	{
		key.face = face.lfFaceName;
		key.height = face.lfHeight;
		key.weight = face.lfWeight;
		key.italic = face.lfItalic;
		key.charset = face.lfCharSet;
		if (recallWidthPage(key, _getCharWidths()))
			return _getCharWidths()->getWidth(cChar);
	}
	// One screen DC for the process. Creating and deleting a DC for every
	// 256-glyph page made the first characters of a font wait on GDI.
	static HDC measureDC = nullptr;
	if (!measureDC)
		measureDC = CreateDCW(L"DISPLAY", nullptr, nullptr, nullptr);
	UT_Win32TraceF("widths-dc %p char=%u base=%u limit=%u",
				   (void*)measureDC, (unsigned)cChar, (unsigned)base, (unsigned)limit);
	if (!measureDC)
		return 0;
	// Restore the previous font before returning. Leaving it selected is
	// safe on this reused DC; deleting the DC while it is selected is not.
	HGDIOBJ previous = SelectObject(measureDC, m_layoutFont);
	UT_Win32TraceF("widths-font prev=%p", previous);
	if (!previous || previous == HGDI_ERROR)
		return 0;
	_getCharWidths()->setCharWidthsOfRange(measureDC, base, limit);
	UT_Win32TraceHeap("widths-range");
	if (haveFace)
		storeWidthPage(key, _getCharWidths());
	if (previous)
		SelectObject(measureDC, previous);
	return _getCharWidths()->getWidth(cChar);
#else
	UT_return_val_if_fail(UT_NOT_IMPLEMENTED,0);
#endif
}

/*!
    UT_Rect of glyph in Logical units.
	rec.left = bearing Left (distance from origin to start)
	rec.width = width of the glyph
	rec.top = distance from the origin to the top of the glyph
	rec.height = total height of the glyph

    This function will only work on win NT -- check the return value before doing anything
    with the rectangle (win9x implementation would have to use GetGlyphOutlineA() and
    convert the UT_UCS4Char to an appropriate ansi value 
*/
bool GR_Win32Font::glyphBox(UT_UCS4Char g, UT_Rect & rec, GR_Graphics * pG)
{
	UT_return_val_if_fail( pG, false );
	GR_Win32Graphics * pWin32Gr = (GR_Win32Graphics *)pG;

	// we do all measurements on the printer DC and only later scale down to display to
	// ensure WYSIWYG behaviour
	HDC hdc = getPrimaryHDC();
	HDC printDC = pWin32Gr->getPrintDC() ? pWin32Gr->getPrintDC() : hdc;
	
	GLYPHMETRICS gm;
	MAT2 m = {{0,1}, {0,0}, {0,0}, {0,1}};

	int nPrintLogPixelsY = GetDeviceCaps(printDC, LOGPIXELSY);// resolution of printer
	int nPrintLogPixelsX = GetDeviceCaps(printDC, LOGPIXELSX);

	UT_uint32 pixels = getUnscaledHeight();

	// scale the pixel size to the printer resolution and get the font
	if(!m_bGUIFont)
	{
		// use point size to reduce rounding errors
		// pixels = MulDiv(pixels, nPrintLogPixelsY, 72);
		pixels = (int)(m_fPointSize * (double)nPrintLogPixelsY / 72.0 + 0.5);
	}
	
	HFONT pFont = getFontFromCache(pixels, false, 100);
	if (!pFont)
	{
		fetchFont(pixels);
		pFont = getFontFromCache(pixels, false, 100);
	}

	// select our font into the printer DC
	HGDIOBJ hRet = SelectObject(printDC, pFont);
	UT_return_val_if_fail( hRet != (void*)GDI_ERROR, false);

	// remember which font we selected into the dc
	if(printDC == pWin32Gr->getPrintDC())
		pWin32Gr->setPrintDCFontAllocNo(getAllocNumber());

	if(printDC == pWin32Gr->getPrimaryDC())
		pWin32Gr->setDCFontAllocNo(getAllocNumber());
	
	DWORD iRet = GDI_ERROR;
	
	if (UT_IsWinNT())
	{
		iRet = GetGlyphOutlineW(printDC, (UINT)g, GGO_METRICS, &gm, 0, nullptr, &m);
	}
	else
	{
		// GetGlyphOutlineA() ... using the GGO_GLYPH_INDEX = 0x0080 to get the ttf glyph 
		// for the unicode character set in ANSI

		// first of all, we need to translate the ucs4 value to the index; we use
		// GetCharacterPlacementW() for this. Event though the docs state that this
		// function is only available on win9x via unicows, this is in fact incorrect
		// (whether the 9x version of the function is fully functional I do not know, but
		// it does glyph index lookup).
		UINT iIndx;
		GCP_RESULTSW gcpResult;
		memset(&gcpResult, 0, sizeof(gcpResult));

		gcpResult.lStructSize = sizeof(GCP_RESULTS);
		
		// w32api changed lpGlyphs from UINT * to LPWSTR to match MS PSDK in w32api v2.4
#if defined(__MINGW32__) && (__W32API_MAJOR_VERSION == 2 && __W32API_MINOR_VERSION < 4)
		gcpResult.lpGlyphs = (UINT *) &iIndx;	// Character glyphs
#else			
		gcpResult.lpGlyphs = (LPWSTR) &iIndx;	// Character glyphs
#endif			
		gcpResult.nGlyphs = 1;  // Array size

		if(GetCharacterPlacementW(printDC, (LPCWSTR)&g, 1, 0, &gcpResult, 0))
		{
			iRet = GetGlyphOutlineA(printDC, iIndx, 0x0080 | GGO_METRICS, &gm, 0, nullptr, &m);
		}
		else
		{
			// OK, the GetCharacterPlacementW function probably not present, we just do
			// our best with the ANSI stuff
			iRet = GetGlyphOutlineA(printDC, g, GGO_METRICS, &gm, 0, nullptr, &m);
		}
	}
	
	if(GDI_ERROR == iRet)
		return false;

	rec.left   = gm.gmptGlyphOrigin.x;
	rec.top    = gm.gmptGlyphOrigin.y;
	rec.width  = gm.gmBlackBoxX;
	rec.height = gm.gmBlackBoxY;
	
	// the metrics are in device units, scale them to layout
	int iResolution = pG->getResolution();
	rec.height  = MulDiv(rec.height, iResolution, nPrintLogPixelsY);
	rec.top  = MulDiv(rec.top, iResolution, nPrintLogPixelsY);

	rec.width  = MulDiv(rec.width, iResolution, nPrintLogPixelsX);
	rec.left  = MulDiv(rec.left, iResolution, nPrintLogPixelsX);

	UT_DEBUGMSG(("gr_Win32Graphics::glyphBox(l=%d, t=%d, w=%d, h=%d\n", rec.left, rec.top, rec.width, rec.height));
	return true;
}

/*!
	Implements a GR_CharWidths.
	Override if you which to instanciate a subclass.
 */
GR_CharWidths* GR_Win32Font::newFontWidths(void) const
{
#ifndef ABI_GRAPHICS_PLUGIN_NO_WIDTHS
	return new GR_Win32CharWidths();
#else
	UT_return_val_if_fail(UT_NOT_IMPLEMENTED,nullptr);
#endif
}

HFONT GR_Win32Font::getFontFromCache(UT_uint32 pixelsize, bool /*bIsLayout*/, UT_uint32 /*zoomPercentage*/) const
{
	UT_uint32 l = 0;
	UT_uint32 count = m_allocFonts.getItemCount();
	allocFont *entry;

	while (l < count)
	  {
		  entry = (allocFont *)m_allocFonts.getNthItem(l);
		  if (entry && entry->pixelSize == pixelsize)
			  return entry->hFont;
		  l++;
	  }

	return nullptr;
}

void GR_Win32Font::insertFontInCache(UT_uint32 pixelsize, HFONT pFont) const
{
	// Each zoom creates another HFONT. Keep a few sizes and release the rest,
	// never evicting m_layoutFont and deselecting any evicted font from m_hdc.
	bool bIsDC = false;
	if (m_hdc)
	{
		DWORD dwObjType = GetObjectType((HGDIOBJ)m_hdc);
		bIsDC = (dwObjType == OBJ_DC || dwObjType == OBJ_MEMDC);
	}

	while (m_allocFonts.getItemCount() >= 8)
	{
		UT_sint32 evictIdx = -1;
		for (UT_sint32 i = 0; i < m_allocFonts.getItemCount(); ++i)
		{
			allocFont *cand = (allocFont *)m_allocFonts.getNthItem(i);
			if (!cand || cand->hFont != m_layoutFont)
			{
				evictIdx = i;
				break;
			}
		}
		if (evictIdx < 0)
			break;

		allocFont *old = (allocFont *)m_allocFonts.getNthItem(evictIdx);
		if (old)
		{
			if (old->hFont)
			{
				if (bIsDC && old->hFont == (HFONT)::GetCurrentObject(m_hdc, OBJ_FONT))
					SelectObject(m_hdc, GetStockObject(SYSTEM_FONT));
				DeleteObject(old->hFont);
			}
			delete old;
		}
		m_allocFonts.deleteNthItem(evictIdx);
	}

	allocFont *entry = new allocFont;
	entry->pixelSize = pixelsize;
	entry->hFont = pFont;

	m_allocFonts.push_back(static_cast<void *>(entry));
}

void GR_Win32Font::fetchFont(UT_uint32 pixelsize) const
{
	LOGFONTW lf;
	memset(&lf, 0, sizeof(lf));
	// GetObjectW writes the LOGFONT only when it returns the full size.
	if (!m_layoutFont || GetObjectW(m_layoutFont, sizeof(lf), &lf) != sizeof(lf))
		return;

	lf.lfHeight = pixelsize;

	if (lf.lfHeight>0) lf.lfHeight = - lf.lfHeight;

	HFONT pFont = CreateFontIndirectW(&lf);
	if (!pFont)
		return;

	insertFontInCache(pixelsize, pFont);
}

HFONT GR_Win32Font::getDisplayFont(GR_Graphics * pG)
{
	UT_uint32 zoom = m_bGUIFont ? 100 : pG->getZoomPercentage();
	UT_uint32 pixels = m_bGUIFont ? m_iHeight : m_iHeight*zoom/100;
		
	HFONT pFont = getFontFromCache(pixels, false, zoom);
	if (pFont)
		return pFont;

	fetchFont(pixels);
	return getFontFromCache(pixels, false, zoom);
}

UT_sint32 GR_Win32Font::measureUnRemappedChar(UT_UCS4Char c, UT_uint32 * /*height*/)
{
#ifndef ABI_GRAPHICS_PLUGIN_NO_WIDTHS
	// first of all, handle 0-width spaces ...
	if(c == 0xFEFF || c == 0x200b || c == UCS_LIGATURE_PLACEHOLDER)
		return 0;

	UT_sint32 iWidth = getCharWidthFromCache(c);
	return iWidth;
#else
	UT_return_val_if_fail(UT_NOT_IMPLEMENTED,0);
#endif
}

void GR_Win32Font::selectFontIntoDC(GR_Graphics * pGr, HDC hdc)
{
	HFONT hFont = getDisplayFont(pGr);
	UT_DebugOnly<HGDIOBJ> hRet = SelectObject(hdc, hFont);

	// hate having to do the cast, here
	UT_ASSERT_HARMLESS( hRet != (void*)GDI_ERROR);
}

void GR_Win32Graphics::polygon(const UT_RGBColor& c, const UT_Point *pts,UT_uint32 nPoints)
{
	if (!pts || nPoints == 0 || !m_hdc)
		return;

	HPEN hPen = CreatePen(PS_SOLID, (UT_sint32)((double)_tduR(m_iLineWidth) * m_fXYRatio), RGB(c.m_red, c.m_grn, c.m_blu));
	HBRUSH hBrush = CreateSolidBrush(RGB(c.m_red, c.m_grn, c.m_blu));
	if (!hPen || !hBrush)
	{
		if (hPen)
			DeleteObject(hPen);
		if (hBrush)
			DeleteObject(hBrush);
		return;
	}
	HPEN hOldPen = (HPEN)SelectObject(m_hdc,hPen);
	HBRUSH hOldBrush = (HBRUSH)SelectObject(m_hdc, hBrush);

	POINT * points = new POINT[nPoints];
	UT_ASSERT(points);

	for (UT_uint32 i = 0; i < nPoints; ++i)
	{
		points[i].x = (UT_sint32)((double)_tduX(pts[i].x) * m_fXYRatio);
		points[i].y = _tduY(pts[i].y);
	}

	Polygon(m_hdc, points, nPoints);

	if (hOldBrush)
		SelectObject(m_hdc, hOldBrush);
	DeleteObject(hBrush);

	if (hOldPen)
		SelectObject(m_hdc, hOldPen);
	DeleteObject(hPen);

	delete[] points;
}

bool GR_Win32Graphics::_setTransform(const GR_Transform & /*tr*/)
{
#if 0
	if(UT_IsWinNT())
	{
		XFORM xForm;
		float fScale = static_cast<float>(static_cast<float>(tlu(1)) * static_cast<float>(getZoomPercentage()) / 100.0);
	
		xForm.eM11 = (float)(tr.getM11() * fScale);
		xForm.eM12 = (float)tr.getM12();
		xForm.eM21 = (float)tr.getM21();
		xForm.eM22 = (float)(tr.getM22() * fScale);
		xForm.eDx  = (float)(tr.getDx());
		xForm.eDy  = (float)(tr.getDy());

		bool ret = (SetWorldTransform(m_hdc, & xForm) != 0);
		UT_ASSERT( ret );
		return ret;
	}
	else
	{
		// world tranforms are not supported, fiddle with the view
		// port, etc
		UT_sint32 iScale = static_cast<UT_sint32>((static_cast<float>(tlu(1)) * static_cast<float>(getZoomPercentage())/100.0);
		
		bool res = (SetWindowExtEx(m_hdc, iScale, iScale, nullptr) != 0);
		UT_ASSERT( res );

		if(res)
		{
			res = (SetViewportExtEx(m_hdc,
									GetDeviceCaps(m_hdc, LOGPIXELSX), 
									GetDeviceCaps(m_hdc, LOGPIXELSY),
									nullptr) != 0);
			UT_ASSERT( res );
		}
		
		// change of origins is not implemented yet
		UT_ASSERT(!tr.getDx() && !tr.getDy());

		return res;
	}
#else
	return true;
#endif
}


void GR_Win32Graphics::saveRectangle(UT_Rect & r, UT_uint32 iIndx) 
{		
	UT_Rect * oldR = nullptr;
	m_vSaveRect.setNthItem(iIndx, (void*)new UT_Rect(r),(const void **)&oldR);
	DELETEP(oldR);

	UT_uint32 iWidth = (UT_sint32)((double)_tduR(r.width) * m_fXYRatio);
	UT_uint32 iHeight = _tduR(r.height);
	UT_sint32 x = (UT_sint32)((double)_tduX(r.left) * m_fXYRatio);
	UT_sint32 y = _tduY(r.top);

	// A caret rectangle with a wrapped coordinate is larger than the window.
	// Saving it and blitting it back copies a block of text to another place.
	int clientW = 0;
	int clientH = 0;
	if (m_hwnd)
		getWidthAndHeightFromHWND(m_hwnd, clientW, clientH);
	bool reject = (r.width <= 0 || r.height <= 0 || iWidth == 0 || iHeight == 0);
	if (!reject && clientW > 0 && (iWidth > static_cast<UT_uint32>(clientW) || iHeight > static_cast<UT_uint32>(clientH)))
		reject = true;
	if (reject)
	{
		HBITMAP hOld = nullptr;
		m_vSaveRectBuf.setNthItem(iIndx, nullptr, (const void**)&hOld);
		if (hOld)
			DeleteObject(hOld);
		return;
	}

	#ifdef GR_GRAPHICS_DEBUG	
	UT_DEBUGMSG(("GR_Win32Graphics::saveRectangle %u, %u %u %u %u\n", iIndx,
		x,y, iWidth, iHeight));	
	#endif

	HDC hMemDC = CreateCompatibleDC(m_hdc);
	if (!hMemDC)
		return;

	BITMAPINFO bmi;
	memset(&bmi, 0, sizeof(bmi));
	bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth = iWidth;
	bmi.bmiHeader.biHeight = iHeight;
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biBitCount = 24; // as we want true-color
	bmi.bmiHeader.biCompression = BI_RGB; // no compression
	bmi.bmiHeader.biSizeImage = (((iWidth * bmi.bmiHeader.biBitCount + 31) & ~31) >> 3) * iHeight;

	BYTE *imagedata = nullptr;
	HBITMAP hBit = CreateDIBSection(hMemDC, &bmi, DIB_RGB_COLORS, (void**)&imagedata, nullptr, 0);
	if (!hBit || !imagedata)
	{
		if (hBit)
			DeleteObject(hBit);
		DeleteDC(hMemDC);
		return;
	}
	GdiFlush();

	HGDIOBJ hOld = SelectObject(hMemDC, hBit);
	BitBlt(hMemDC, 0, 0, iWidth, iHeight, m_hdc, x, y, SRCCOPY);
	// DeleteDC destroys the bitmap still selected in a memory DC.
	if (hOld)
		SelectObject(hMemDC, hOld);
	DeleteDC(hMemDC);

	HBITMAP hPrev = nullptr;
	m_vSaveRectBuf.setNthItem(iIndx, (void*) hBit, (const void**)&hPrev);
	if (hPrev)
		DeleteObject(hPrev);
}

void GR_Win32Graphics::restoreRectangle(UT_uint32 iIndx) 
{
	UT_Rect * r = (UT_Rect*)m_vSaveRect.getNthItem(iIndx);
	HBITMAP hBit = (HBITMAP)m_vSaveRectBuf.getNthItem(iIndx);
	
	UT_return_if_fail(r);
	UT_return_if_fail(hBit);

	UT_uint32 iWidth = (UT_sint32)((double)_tduR(r->width) * m_fXYRatio);
	UT_uint32 iHeight = _tduR(r->height);
	if (iWidth == 0 || iHeight == 0)
		return;
	if (m_hwnd)
	{
		int clientW = 0;
		int clientH = 0;
		getWidthAndHeightFromHWND(m_hwnd, clientW, clientH);
		if (clientW > 0 && (iWidth > static_cast<UT_uint32>(clientW) || iHeight > static_cast<UT_uint32>(clientH)))
			return;
	}
	UT_sint32 x = (UT_sint32)((double)_tduX(r->left) * m_fXYRatio);
	UT_sint32 y = _tduY(r->top);			

	#ifdef GR_GRAPHICS_DEBUG
	UT_DEBUGMSG(("GR_Win32Graphics::restoreRectangle %u, %u %u %u %u\n", iIndx,
		x,y, iWidth, iHeight));	
	#endif	

	HDC hMemDC = CreateCompatibleDC(m_hdc);
	if (!hMemDC)
		return;
	HGDIOBJ hOld = SelectObject(hMemDC, hBit);
	if (!hOld)
	{
		DeleteDC(hMemDC);
		return;
	}

	BitBlt(m_hdc, x, y, iWidth, iHeight, hMemDC, 0, 0, SRCCOPY);
	SelectObject(hMemDC, hOld);
	DeleteDC(hMemDC);
}

void GR_Win32Graphics::flush(void)
{	
	GdiFlush();	
}

//--------------------------------------------------------------------------/
//--------------------------------------------------------------------------/

BITMAPINFO * GR_Win32Graphics::ConvertDDBToDIB(HBITMAP bitmap, HPALETTE hPal, DWORD dwCompression) {

	BITMAP				bm;	
	BITMAPINFOHEADER	bi;	
	LPBITMAPINFOHEADER 	lpbi;	
	DWORD				dwLen;
	HANDLE				hDIB;	
	HANDLE				handle;	
	HDC 				hDC;	
	
	if (dwCompression == BI_BITFIELDS)
		return nullptr;

	if (hPal == nullptr)
		hPal = (HPALETTE)GetStockObject(DEFAULT_PALETTE);

	// Get bitmap information. A failed call leaves bm uninitialized.
	memset(&bm, 0, sizeof(bm));
	if (!bitmap || ::GetObjectW(bitmap, sizeof(bm), &bm) != sizeof(bm))
		return nullptr;
	if (bm.bmWidth <= 0 || bm.bmHeight <= 0)
		return nullptr;	
	
	// Initialize the bitmapinfoheader
	bi.biSize			= sizeof(BITMAPINFOHEADER);	
	bi.biWidth			= bm.bmWidth;
	bi.biHeight 		= bm.bmHeight;	
	bi.biPlanes 		= 1;
	bi.biBitCount		= bm.bmPlanes * bm.bmBitsPixel;
	bi.biCompression	= dwCompression;	
	bi.biSizeImage		= 0;	
	bi.biXPelsPerMeter	= 0;
	bi.biYPelsPerMeter	= 0;	
	bi.biClrUsed		= 0;	
	bi.biClrImportant	= 0;
	
	// A 16-bit or 32-bit DIB has no color table. Shifting by the bit count
	// is undefined once the count reaches the width of an int.
	int nColors = 0;
	if (bi.biBitCount > 0 && bi.biBitCount <= 8)
		nColors = 1 << bi.biBitCount;
	dwLen  = bi.biSize + nColors * sizeof(RGBQUAD);
	
	// We need a device context to get the DIB from
	hDC = CreateCompatibleDC(m_hdc);
	if (!hDC)
		return nullptr;
	hPal = SelectPalette(hDC, hPal, TRUE);
	RealizePalette(hDC);
	
	// Allocate enough memory to hold bitmapinfoheader and color table
	hDIB = g_try_malloc(dwLen);	
	if (!hDIB){
		SelectPalette(hDC, hPal, TRUE);		
		DeleteDC(hDC);
		return nullptr;	
		}
	lpbi = (LPBITMAPINFOHEADER)hDIB;	
	*lpbi = bi;

	// Call GetDIBits with a nullptr lpBits param, so the device driver
	// will calculate the biSizeImage field
	GetDIBits(
		hDC,
		bitmap,
		0L,
		(DWORD)bi.biHeight,
		(LPBYTE)nullptr,
		(LPBITMAPINFO)lpbi,
		(DWORD)DIB_RGB_COLORS
		);
	bi = *lpbi;

	// If the driver did not fill in the biSizeImage field, then compute it
	// Each scan line of the image is aligned on a DWORD (32bit) boundary
	if (bi.biSizeImage == 0) {
		bi.biSizeImage = ((((bi.biWidth * bi.biBitCount) + 31) & ~31) / 8) * bi.biHeight;
	
		// If a compression scheme is used the result may infact be larger
		// Increase the size to account for this.		
		if (dwCompression != BI_RGB)
			bi.biSizeImage = (bi.biSizeImage * 3) / 2;
		}
	
	// Realloc the buffer so that it can hold all the bits	
	dwLen += bi.biSizeImage;
	handle = g_try_realloc(hDIB, dwLen);
	if (handle)
		hDIB = handle;	
	else{
		g_free(hDIB);		
		// Reselect the original palette
		SelectPalette(hDC,hPal,TRUE);		
		DeleteDC(hDC);
		return nullptr;	
		}
	
	// Get the bitmap bits	
	lpbi = (LPBITMAPINFOHEADER)hDIB;	
	
	// FINALLY get the DIB
	BOOL bGotBits = GetDIBits( 
		hDC, 
		bitmap, 
		0L,							// Start scan line				
		(DWORD)bi.biHeight,			// # of scan lines
		(LPBYTE)lpbi 				// address for bitmap bits
		+ (bi.biSize + nColors * sizeof(RGBQUAD)),
		(LPBITMAPINFO)lpbi,			// address of bitmapinfo
		(DWORD)DIB_RGB_COLORS		// Use RGB for color table
		);
	
	if (!bGotBits)	{
		g_free(hDIB);				
		SelectPalette(hDC,hPal,TRUE);		
		DeleteDC(hDC);
		return nullptr;	
		}	
		
	SelectPalette(hDC,hPal,TRUE);	
	DeleteDC(hDC);

	return (BITMAPINFO*)hDIB;
	}

//--------------------------------------------------------------------------/
//--------------------------------------------------------------------------/

GR_Image * GR_Win32Graphics::genImageFromRectangle(const UT_Rect & r) {

	// Same conversion as drawImage, so a scrolled page does not shift the
	// snapshot relative to the image being dragged.
	UT_sint32 x = (UT_sint32)((double)_tduX(r.left) * m_fXYRatio);
	UT_sint32 y = _tduY(r.top);
	UT_sint32 iWidth = (UT_sint32)((double)_tduR(r.width) * m_fXYRatio);
	UT_sint32 iHeight = _tduR(r.height);
	if (x < 0)
	{
		iWidth += x;
		x = 0;
	}
	if (y < 0)
	{
		iHeight += y;
		y = 0;
	}
	if (iWidth < 1 || iHeight < 1)
		return nullptr;

	HDC hMemDC = CreateCompatibleDC(m_hdc);
	if (!hMemDC)
		return nullptr;

	BITMAPINFO bmi;
	memset(&bmi, 0, sizeof(bmi));
	bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth = iWidth;
	bmi.bmiHeader.biHeight = iHeight;
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biBitCount = 24;
	bmi.bmiHeader.biCompression = BI_RGB;

	void *imagedata = nullptr;
	HBITMAP hBit = CreateDIBSection(hMemDC, &bmi, DIB_RGB_COLORS, &imagedata, nullptr, 0);
	if (!hBit || !imagedata)
	{
		if (hBit)
			DeleteObject(hBit);
		DeleteDC(hMemDC);
		return nullptr;
	}

	HBITMAP hOld = (HBITMAP) SelectObject(hMemDC, hBit);
	// The source DC may be clipped to a drag strip. Read the whole rectangle.
	SelectClipRgn(m_hdc, nullptr);
	BitBlt(hMemDC, 0, 0, iWidth, iHeight, m_hdc, x, y, SRCCOPY);
	if (getClipRect())
		setClipRect(getClipRect());
	GdiFlush();
	// DeleteObject on a bitmap still selected in the DC, then DeleteDC,
	// frees that bitmap twice.
	// https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-deleteobject
	if (hOld)
		SelectObject(hMemDC, hOld);

	const UT_uint64 stride64 = (static_cast<UT_uint64>(iWidth) * 3u + 3u) & ~UT_uint64(3);
	if (stride64 == 0 || stride64 > 0xffffffffu
		|| stride64 > 0xffffffffu / static_cast<UT_uint32>(iHeight))
	{
		DeleteObject(hBit);
		DeleteDC(hMemDC);
		return nullptr;
	}
	const UT_uint32 stride = static_cast<UT_uint32>(stride64);
	const UT_uint32 imageBytes = stride * static_cast<UT_uint32>(iHeight);
	BITMAPINFO *dib = (BITMAPINFO *)g_try_malloc(sizeof(BITMAPINFOHEADER) + imageBytes);
	if (!dib)
	{
		DeleteObject(hBit);
		DeleteDC(hMemDC);
		return nullptr;
	}
	memset(&dib->bmiHeader, 0, sizeof(BITMAPINFOHEADER));
	dib->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	dib->bmiHeader.biWidth = iWidth;
	dib->bmiHeader.biHeight = iHeight;
	dib->bmiHeader.biPlanes = 1;
	dib->bmiHeader.biBitCount = 24;
	dib->bmiHeader.biCompression = BI_RGB;
	dib->bmiHeader.biSizeImage = imageBytes;
	memcpy(((UT_Byte *)dib) + sizeof(BITMAPINFOHEADER), imagedata, imageBytes);

	DeleteObject(hBit);
	DeleteDC(hMemDC);

	GR_Win32Image *img = new GR_Win32Image("Screenshot");
	img->setDIB(dib);
	return img;
	}


GR_Graphics * GR_Win32Graphics::graphicsAllocator(GR_AllocInfo &info)
{
#ifndef ABI_GRAPHICS_PLUGIN
	UT_return_val_if_fail(info.getType() == GRID_WIN32, nullptr);
	
	GR_Win32AllocInfo &AI = (GR_Win32AllocInfo&)info;

	if(AI.m_pDocInfo)
	{
		// printer graphics required
		return new GR_Win32Graphics(AI.m_hdc, AI.m_pDocInfo, AI.m_hDevMode);
	}
	else
	{
		// screen graphics required
		return new GR_Win32Graphics(AI.m_hdc, AI.m_hwnd);
	}
#else
	UT_return_val_if_fail(UT_NOT_IMPLEMENTED,nullptr);
#endif
}

#define _test_and_cleanup(x)                    \
if(!(x))                                        \
{                                               \
    UT_ASSERT_HARMLESS( UT_SHOULD_NOT_HAPPEN ); \
	goto cleanup;                               \
}

GR_Graphics * GR_Win32Graphics::getPrinterGraphics(const wchar_t * pPrinterName,
												   const wchar_t * pDocName)
{
	UT_return_val_if_fail( pDocName, nullptr );
	GR_Win32Graphics *pGr = nullptr;
	HGLOBAL hDM = nullptr;
	HANDLE hPrinter = nullptr;
	LONG iBuffSize = 0;
	HDC hPDC = nullptr;
	DOCINFOW * pDI = nullptr;
	PDEVMODEW pDM = nullptr;
	
	bool bFreePN = false;

	// we will use '-' as a shortcut for default printer (the --print command has to have
	// a parameter)
	wchar_t * pPN = wcscmp(L"-", pPrinterName) == 0 ? nullptr : (wchar_t *) pPrinterName;
	const wchar_t * pDriver = UT_IsWinNT() ? L"WINSPOOL" : nullptr;
	
	if(!pPN)
	{
		pPN = UT_GetDefaultPrinterName();
		bFreePN = true;
	}

	_test_and_cleanup(pPN);

	hPDC = CreateDCW(pDriver, pPN, nullptr, nullptr);

	_test_and_cleanup(hPDC);
	
	_test_and_cleanup( OpenPrinterW(pPN, &hPrinter, nullptr));
		
	// first, get the size of the buffer needed for the document mode
	// DocumentPropertiesW returns the byte count, or a negative value on failure.
	// A non-zero test would treat that failure as a size and pass it to GlobalAlloc.
	iBuffSize = DocumentPropertiesW(nullptr, hPrinter,	pPN, nullptr, nullptr, 0);
	_test_and_cleanup( iBuffSize > 0 );

	// must be global movable memory. Unlock before any failure jump: GlobalFree
	// fails while the lock count is non-zero.
	hDM = GlobalAlloc(GHND, static_cast<SIZE_T>(iBuffSize));
	pDM = hDM ? (PDEVMODEW)GlobalLock(hDM) : nullptr;
	if (!pDM || DocumentPropertiesW(nullptr, hPrinter, pPN, pDM, nullptr, DM_OUT_BUFFER) <= 0)
	{
		if (pDM)
			GlobalUnlock(hDM);
		UT_ASSERT_HARMLESS(UT_SHOULD_NOT_HAPPEN);
		goto cleanup;
	}
	GlobalUnlock(hDM);

	// we have all we need to fill in the doc info structure ...
	pDI = (DOCINFOW*) UT_calloc(1, sizeof(DOCINFOW));
	_test_and_cleanup(pDI);

	memset(pDI, 0, sizeof(DOCINFOW));
	
	pDI->cbSize = sizeof(DOCINFOW);
	pDI->lpszDocName = pDocName;
	pDI->lpszOutput  = nullptr; // for now we do not support printing into file from cmd line
	pDI->lpszDatatype = nullptr;
	pDI->fwType = 0;
	
	{
		GR_Win32AllocInfo ai(hPDC, pDI, hDM);
	
		pGr = (GR_Win32Graphics *)XAP_App::getApp()->newGraphics(ai);
		UT_ASSERT_HARMLESS(pGr);
		if (pGr && pPN)
			pGr->setPrinterName(pPN);
	}
	
 cleanup:
	if(bFreePN)
		g_free(pPN);

	if (pGr)
		static_cast<GR_Win32Graphics *>(pGr)->takeOwnershipOfDCAndDocInfo();
	else
	{
		// CreateDC and GlobalAlloc are ours until the graphics object
		// accepts them. A later failure must not leave the printer DC open.
		if (hPDC)
			DeleteDC(hPDC);
		if (hDM)
			GlobalFree(hDM);
		if (pDI)
			g_free(pDI);
	}

	if(hPrinter)
		ClosePrinter(hPrinter);
	
	return pGr;
}


/*!
    This function absolutely must be called anytime the DEVMODE structure is modified in
    any way; it also must be called on the DEVMODE handle returned by PringDlg. Those who
    will modify DEVMODE structure and not call this function on it will be shot, without
    exception.

    The issue with the DEVMODE structure is this: it consists of two parts, a public part
    declared by the win32 headers, and a private part, the size and contents of which 
    depend on the printer. Before a printer can be passed the DEVMODE structure, two
    things have to be satisfied:

    1. The memory allocated for the structure has to be large enough to hold both the
       public and private parts of the structure; we obtain the handle from the
       PrintDlg, and it _should_ allocate the necessary size correctly. In any case, we
       cannot meddle with the size, since reallocating means loosing the private part.

    2. The settings represented by the public part have to be transferred into the private
       part; this is achieved by calling the DocumentProperties() function on the
       structure. So any time we modify the contents of DEVMODE in any way we have to call
       DocumentProperties() before we can pass the pointer / handle to any other
       function. In addition, I have a strong suspicion that the PrintDlg function only
       sets the public section, but does not call DocumentProperties(), so we have to do
       that ourselves once we get the handle back.
*/
bool GR_Win32Graphics::fixDevMode(HGLOBAL hDevMode, const wchar_t * pPrinterName)
{
	if (!hDevMode)
		return false;

	DEVMODEW * pDM = (DEVMODEW*)GlobalLock(hDevMode);
	UT_return_val_if_fail( pDM, false );

	// dmDeviceName is CCHDEVICENAME (32) characters, is truncated on long or
	// network printer names, and is not NUL-terminated when full. Prefer the
	// full printer name from DEVNAMES when available.
	std::wstring printerName;
	if (pPrinterName && pPrinterName[0] != L'\0')
	{
		printerName = pPrinterName;
	}
	else
	{
		const wchar_t * end = std::wmemchr(pDM->dmDeviceName, L'\0', CCHDEVICENAME);
		const size_t len = end ? static_cast<size_t>(end - pDM->dmDeviceName) : CCHDEVICENAME;
		printerName.assign(pDM->dmDeviceName, len);
	}

	std::string printerUtf8;
	if (!printerName.empty())
	{
		const int utf8Len = WideCharToMultiByte(CP_UTF8, 0, printerName.c_str(), -1, nullptr, 0, nullptr, nullptr);
		if (utf8Len > 1)
		{
			printerUtf8.resize(static_cast<size_t>(utf8Len));
			WideCharToMultiByte(CP_UTF8, 0, printerName.c_str(), -1, &printerUtf8[0], utf8Len, nullptr, nullptr);
			printerUtf8.resize(static_cast<size_t>(utf8Len - 1));
		}
	}

	// find out how big structure this particular printer really needs
	HANDLE      hPrinter = nullptr;
	wchar_t *   printerNameBuf = printerName.empty() ? nullptr : &printerName[0];

	//Start by opening the printer
	if(!printerNameBuf || !OpenPrinterW(printerNameBuf, &hPrinter, nullptr))
	{
		UT_String msg;
		UT_String_sprintf(msg,"Unable to open printer [%s]", printerUtf8.c_str());
		LOG_WIN32_EXCPT(msg.c_str());
		GlobalUnlock(hDevMode);
		UT_ASSERT_HARMLESS( UT_SHOULD_NOT_HAPPEN );
		return false;
	}

	// A failure is a negative LONG. Storing it in a DWORD turns it into a
	// huge size, and casting that back to int hides the failure.
	const LONG needed = DocumentPropertiesW(nullptr, hPrinter, printerNameBuf, nullptr, nullptr, 0);
	const LONG have = static_cast<LONG>(pDM->dmSize) + static_cast<LONG>(pDM->dmDriverExtra);
	const SIZE_T allocSize = GlobalSize(hDevMode);

	if( needed <= 0 || needed > have || static_cast<SIZE_T>(needed) > allocSize )
	{
		// we are knees deep in crap here ... log it into the users profile, so we have
		// some debug info to go on
		UT_String msg;
		UT_String_sprintf(msg,"DEVMODE too small [%s, needs %ld, got %d + %d]",
						  printerUtf8.c_str(), static_cast<long>(needed), pDM->dmSize, pDM->dmDriverExtra);
		LOG_WIN32_EXCPT(msg.c_str());
		ClosePrinter(hPrinter);
		GlobalUnlock(hDevMode);
		UT_ASSERT_HARMLESS( UT_SHOULD_NOT_HAPPEN );
		return false;
	}
	
	// now get the printer driver to merge the data in the public section into its private part
	const LONG dwRet = DocumentPropertiesW(nullptr, hPrinter, printerNameBuf, pDM, pDM, DM_OUT_BUFFER | DM_IN_BUFFER);

	// g_free what needs be ...
	ClosePrinter(hPrinter);
	GlobalUnlock(hDevMode);

	UT_return_val_if_fail( dwRet == IDOK, false );
	return true;
}

DOCINFOW *GR_Win32Graphics::getDocInfo()
{
  DOCINFOW *di;
  
  di = (DOCINFOW*) UT_calloc(1, sizeof(DOCINFOW));
  memset( di, 0, sizeof(DOCINFOW) );
  di->cbSize = sizeof(DOCINFOW);
  di->lpszDocName = L"AbiWord Print Preview";
  di->lpszOutput = (LPWSTR) nullptr;
  di->lpszDatatype = (LPWSTR) nullptr;
  di->fwType = 0;
  
  return di;
}

/*
 *  Creates a HDC to the best printer to be used as a reference for CreateEnhMetaFile
 */
HDC GR_Win32Graphics::createbestmetafilehdc()
{
  // A zero-size EnumPrintersW call fails and reports the buffer size.
  // Treating that failure as "no printer" returned a screen DC from GetDC
  // that nothing released.
  DWORD neededsize = 0;
  DWORD noprinters = 0;
  HDC besthdc = CreateDCW(L"DISPLAY", nullptr, nullptr, nullptr);
  int bestres = 0;
  if (besthdc)
    bestres = GetDeviceCaps(besthdc, LOGPIXELSX) + GetDeviceCaps(besthdc, LOGPIXELSY);

  EnumPrintersW(PRINTER_ENUM_CONNECTIONS | PRINTER_ENUM_LOCAL, nullptr, 5,
		nullptr, 0, &neededsize, &noprinters);
  if (neededsize == 0)
    return besthdc;

  LPPRINTER_INFO_5W printerinfo = static_cast<LPPRINTER_INFO_5W>(malloc(neededsize));
  if (!printerinfo)
    return besthdc;

  if (EnumPrintersW(PRINTER_ENUM_CONNECTIONS | PRINTER_ENUM_LOCAL, nullptr, 5,
		    reinterpret_cast<LPBYTE>(printerinfo), neededsize, &neededsize, &noprinters))
  {
    for (DWORD i = 0; i < noprinters; i++)
    {
      if (!printerinfo[i].pPrinterName)
        continue;
      HDC curhdc = CreateDCW(L"WINSPOOL", printerinfo[i].pPrinterName, nullptr, nullptr);
      if (!curhdc)
        continue;
      int curres = GetDeviceCaps(curhdc, LOGPIXELSX) + GetDeviceCaps(curhdc, LOGPIXELSY);
      if (curres > bestres)
      {
        if (besthdc)
          DeleteDC(besthdc);
        bestres = curres;
        besthdc = curhdc;
      }
      else
        DeleteDC(curhdc);
    }
  }

  free(printerinfo);
  return besthdc;
}

void GR_Win32Graphics::getWidthAndHeightFromHWND(HWND h, int &width, int &height)
{
	// GetClientRect leaves the RECT untouched when it fails.
	RECT clientRect;
	memset(&clientRect, 0, sizeof(clientRect));
	if (!h || !GetClientRect(h, &clientRect))
	{
		width = 0;
		height = 0;
		return;
	}

	width = clientRect.right - clientRect.left;
	height = clientRect.bottom - clientRect.top;

	UT_ASSERT(clientRect.left == 0 && clientRect.top == 0);
}

void GR_Win32Graphics::_DeviceContext_MeasureBitBltCopySpeed(HDC sourceHdc, HDC destHdc, int width, int height)
{
	LARGE_INTEGER t1, t2, freq;
	
	QueryPerformanceCounter(&t1);
	BitBlt(destHdc, 0, 0, width, height, sourceHdc, 0, 0, SRCCOPY);
	QueryPerformanceCounter(&t2);
	
	QueryPerformanceFrequency(&freq);
#if DEBUG
	double blitSpeed = ((double)(t2.QuadPart - t1.QuadPart)) / ((double)freq.QuadPart);

	UT_DEBUGMSG(("ASFRENT: measured BitBlt speed: %lfs [client rectangle W = %d, H = %d]\n", 
			blitSpeed, width, height));
#endif
}

void GR_Win32Graphics::_releasePersistentBuffer()
{
	if (!m_bufferHdc)
		return;
	if (m_hdc == m_bufferHdc)
	{
		_DeviceContext_RestorePrevHDCFromStack();
		if (m_hdc == m_bufferHdc)
			m_hdc = nullptr;
	}
	_DoubleBuffering_ReleaseBuffer(m_bufferHdc, m_bufferBitmap, m_bufferPrevBitmap);
	m_bufferHdc = nullptr;
	m_bufferBitmap = nullptr;
	m_bufferPrevBitmap = nullptr;
	m_bufferWidth = 0;
	m_bufferHeight = 0;
	m_bufferCoherent = false;
	m_bufferPersist = false;
}

void GR_Win32Graphics::_noteBufferClip(int left, int top, int right, int bottom, bool cleared)
{
	if (!m_bufferHdc || m_hdc != m_bufferHdc)
		return;
	if (cleared)
	{
		// Later drawing is unclipped, so the whole buffer may change.
		m_presentFull = true;
		return;
	}
	if (right < left)
	{
		int swap = left;
		left = right;
		right = swap;
	}
	if (bottom < top)
	{
		int swap = top;
		top = bottom;
		bottom = swap;
	}
	if (!m_presentAny)
	{
		m_presentLeft = left;
		m_presentTop = top;
		m_presentRight = right;
		m_presentBottom = bottom;
		m_presentAny = true;
		return;
	}
	if (left < m_presentLeft)
		m_presentLeft = left;
	if (top < m_presentTop)
		m_presentTop = top;
	if (right > m_presentRight)
		m_presentRight = right;
	if (bottom > m_presentBottom)
		m_presentBottom = bottom;
}

void GR_Win32Graphics::_DeviceContext_SwitchToBuffer()
{
	// Draw into a memory bitmap, then copy it to the window in one BitBlt.
	// Painting each ruler tick and each character straight to the screen
	// made the numbers appear one by one and typing wait on GDI.
	UT_Win32TraceF("buffer-enter print=%d hwnd=%p hdc=%p",
				   m_bPrint ? 1 : 0, (void*)m_hwnd, (void*)m_hdc);
	UT_Win32TraceHeap("buffer-enter");
	UT_Win32Trace("buffer-on");
	// get client area size
	int height(0), width(0);
	if (m_hwnd)
		getWidthAndHeightFromHWND(m_hwnd, width, height);
	UT_Win32TraceF("buffer-size w=%d h=%d", width, height);

	// Keep the bitmap for the screen. Recreate it only when the window
	// changes size. Printer DCs have no window and stay one-shot.
	bool persist = (m_hwnd && !m_bPrint && width > 0 && height > 0);
	bool sizeChanged = (!m_bufferHdc || width != m_bufferWidth || height != m_bufferHeight);
	if (!persist || sizeChanged)
	{
		_releasePersistentBuffer();
		m_bufferHdc = _DoubleBuffering_CreateBuffer(m_hdc, width, height, m_bufferBitmap, m_bufferPrevBitmap);
		m_bufferWidth = width;
		m_bufferHeight = height;
		m_bufferCoherent = false;
		m_bufferPersist = persist;
	}
	UT_Win32TraceF("buffer-created hdc=%p bm=%p prev=%p",
				   (void*)m_bufferHdc, (void*)m_bufferBitmap, (void*)m_bufferPrevBitmap);
	UT_Win32TraceHeap("buffer-created");
	if (!m_bufferHdc)
		return;

	// A drag or a previous paint may already have clipped either DC to a
	// thin strip. ScrollWindowEx and the following BitBlt would then move
	// or copy only that strip, and the next click draws into the leftover.
	SelectClipRgn(m_hdc, nullptr);
	if (m_bufferHdc)
		SelectClipRgn(m_bufferHdc, nullptr);
	if (!m_bufferCoherent)
	{
		// A failed BitBlt leaves the memory bitmap empty. Marking it coherent
		// would copy that empty bitmap over the window on the next present.
		BOOL blt = BitBlt(m_bufferHdc, 0, 0, width, height, m_hdc, 0, 0, SRCCOPY);
		UT_Win32TraceF("buffer-blt %d", blt ? 1 : 0);
		if (!blt)
		{
			if (!persist)
				_releasePersistentBuffer();
			return;
		}
		m_bufferCoherent = true;
	}

	// save the current hdc & switch
	_HDCSwitchStack.push_back(new _HDCSwitchRecord(m_hdc));
	m_hdc = m_bufferHdc;
	m_omitPresent = false;
	m_presentFull = false;
	m_presentAny = false;
	// The clip was selected into the screen DC. The memory DC starts unclipped,
	// so a selection rectangle would not limit drawing into the buffer.
	if (getClipRect())
		setClipRect(getClipRect());
	UT_Win32TraceF("buffer-switched hdc=%p", (void*)m_hdc);
	UT_Win32TraceHeap("buffer-switched");
}

void GR_Win32Graphics::_DeviceContext_SwitchToScreen()
{
	UT_Win32TraceF("screen-enter hdc=%p buf=%p", (void*)m_hdc, (void*)m_bufferHdc);
	UT_Win32TraceHeap("screen-enter");
	_DeviceContext_RestorePrevHDCFromStack();
	UT_Win32TraceF("screen-restored hdc=%p", (void*)m_hdc);
	UT_Win32TraceHeap("screen-restored");

	// get client area size
	int height = m_bufferHeight > 0 ? m_bufferHeight : 1;
	int width = m_bufferWidth > 0 ? m_bufferWidth : 1;
	if (m_hwnd)
	{
		int clientWidth = 0;
		int clientHeight = 0;
		getWidthAndHeightFromHWND(m_hwnd, clientWidth, clientHeight);
		if (clientWidth > 0 && clientWidth < width)
			width = clientWidth;
		if (clientHeight > 0 && clientHeight < height)
			height = clientHeight;
	}
	UT_Win32TraceF("screen-size w=%d h=%d", width, height);
	
	// copy any modifications back to the screen
	if (m_bufferHdc && !m_omitPresent)
	{
		// The document window is CS_OWNDC, so a clip region stays on its DC.
		// BitBlt honors that clip and would leave the selection highlight in
		// the buffer. Clear it, copy the changed rectangle, and leave the
		// screen DC unclipped. Putting the clip back here makes the following
		// scroll move only the selection rectangle.
		SelectClipRgn(m_hdc, nullptr);
		BOOL blt = FALSE;
		if (!m_presentFull && m_presentAny)
		{
			int left = m_presentLeft;
			int top = m_presentTop;
			int right = m_presentRight;
			int bottom = m_presentBottom;
			if (left < 0)
				left = 0;
			if (top < 0)
				top = 0;
			if (right > width)
				right = width;
			if (bottom > height)
				bottom = height;
			if (right > left && bottom > top)
				blt = BitBlt(m_hdc, left, top, right - left, bottom - top, m_bufferHdc, left, top, SRCCOPY);
		}
		else
			blt = BitBlt(m_hdc, 0, 0, width, height, m_bufferHdc, 0, 0, SRCCOPY);
		UT_Win32TraceF("screen-blt %d", blt ? 1 : 0);
		UT_Win32TraceHeap("screen-blt");
		m_bufferCoherent = true;
	}

	m_omitPresent = false;
	if (!m_bufferPersist)
	{
		UT_Win32Trace("screen-release");
		_releasePersistentBuffer();
	}
	UT_Win32TraceHeap("screen-done");
}

void GR_Win32Graphics::_DeviceContext_RestorePrevHDCFromStack()
{
	_HDCSwitchRecord *switchRecord = nullptr;
	UT_Win32Trace("hdc-pop");
	if (_HDCSwitchStack.empty())
	{
		UT_Win32Trace("hdc-pop-empty");
		return;
	}
	switchRecord = _HDCSwitchStack.back();
	_HDCSwitchStack.pop_back();
	if (!switchRecord)
	{
		UT_Win32Trace("hdc-pop-empty");
		return;
	}
	UT_Win32TraceF("hdc-pop-old %p", (void*)switchRecord->oldHdc);
	m_hdc = switchRecord->oldHdc;
	delete switchRecord;
	UT_Win32TraceHeap("hdc-pop");
}

void GR_Win32Graphics::_DeviceContext_SuspendDrawing()
{
	// Drawing goes to the dummy DC, so the retained bitmap is unchanged
	// and must not be copied back over the window.
	if (m_bufferHdc && m_hdc == m_bufferHdc)
		m_omitPresent = true;
	// save the current hdc & switch them!
	_HDCSwitchStack.push_back(new _HDCSwitchRecord(m_hdc));
	m_hdc = m_dummyHdc;
}

void GR_Win32Graphics::_DeviceContext_ResumeDrawing()
{
	_DeviceContext_RestorePrevHDCFromStack();
}

void GR_Win32Graphics::_DoubleBuffering_SetUpDummyBuffer()
{
	m_dummyHdc = _DoubleBuffering_CreateBuffer(m_hdc, 0, 0, m_dummyBitmap, m_dummyPrevBitmap);
}

void GR_Win32Graphics::_DoubleBuffering_ReleaseDummyBuffer()
{
	_DoubleBuffering_ReleaseBuffer(m_dummyHdc, m_dummyBitmap, m_dummyPrevBitmap);
	m_dummyHdc = nullptr;
	m_dummyBitmap = nullptr;
	m_dummyPrevBitmap = nullptr;
}

HDC GR_Win32Graphics::_DoubleBuffering_CreateBuffer(HDC compatibleWith, int width, int height, HBITMAP & bitmap, HBITMAP & previous)
{
	bitmap = nullptr;
	previous = nullptr;
	UT_Win32TraceF("buf-create src=%p w=%d h=%d", (void*)compatibleWith, width, height);
	if (width < 1)
		width = 1;
	if (height < 1)
		height = 1;
	HDC resultingHdc = CreateCompatibleDC(compatibleWith);
	UT_Win32TraceF("buf-dc %p err=%lu", (void*)resultingHdc, (unsigned long)GetLastError());
	if (!resultingHdc)
		return nullptr;
	bitmap = CreateCompatibleBitmap(compatibleWith, width, height);
	UT_Win32TraceF("buf-bm %p err=%lu", (void*)bitmap, (unsigned long)GetLastError());
	UT_Win32TraceHeap("buf-bm");
	if (!bitmap)
	{
		DeleteDC(resultingHdc);
		UT_Win32TraceHeap("buf-dc-deleted");
		return nullptr;
	}
	previous = static_cast<HBITMAP>(SelectObject(resultingHdc, bitmap));
	UT_Win32TraceF("buf-select prev=%p", (void*)previous);
	UT_Win32TraceHeap("buf-select");
	return resultingHdc;
}

void GR_Win32Graphics::_DoubleBuffering_ReleaseBuffer(HDC hdc, HBITMAP bitmap, HBITMAP previous)
{
	if (!hdc)
	{
		UT_Win32Trace("buffer-off-null");
		return;
	}
	UT_Win32TraceF("buffer-off hdc=%p bm=%p prev=%p",
				   (void*)hdc, (void*)bitmap, (void*)previous);
	UT_Win32TraceHeap("buffer-off");
	// https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-deletedc
	// DeleteDC destroys every application object still selected in the DC.
	// Drawing the ruler selects its font into this memory DC and leaves it
	// there. On Windows 11 that is reported as heap corruption 0xC0000374.
	HGDIOBJ font = SelectObject(hdc, GetStockObject(SYSTEM_FONT));
	UT_Win32TraceF("buffer-font %p", font);
	HGDIOBJ pen = SelectObject(hdc, GetStockObject(BLACK_PEN));
	UT_Win32TraceF("buffer-pen %p", pen);
	HGDIOBJ brush = SelectObject(hdc, GetStockObject(WHITE_BRUSH));
	UT_Win32TraceF("buffer-brush %p", brush);
	if (UT_gdi_restores(previous))
	{
		HGDIOBJ back = SelectObject(hdc, previous);
		UT_Win32TraceF("buffer-bm-back %p", back);
	}
	HGDIOBJ current = GetCurrentObject(hdc, OBJ_BITMAP);
	UT_Win32TraceF("buffer-current %p", current);
	if (bitmap && current != bitmap)
	{
		BOOL deleted = DeleteObject(bitmap);
		UT_Win32TraceF("buffer-delete-bm %d err=%lu", deleted ? 1 : 0, (unsigned long)GetLastError());
		UT_Win32TraceHeap("buffer-delete-bm");
	}
	else
		UT_Win32Trace("buffer-bm-kept");
	BOOL dc = DeleteDC(hdc);
	UT_Win32TraceF("buffer-deletedc %d err=%lu", dc ? 1 : 0, (unsigned long)GetLastError());
	UT_Win32TraceHeap("buffer-deletedc");
}
