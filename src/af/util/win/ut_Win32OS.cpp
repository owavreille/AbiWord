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
 
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <winspool.h>
#include "ut_debugmsg.h"
#include "ut_assert.h"
#include "ut_locale.h"
#include "ut_iconv.h"
#include "ut_unicode.h"
#include "ut_Win32OS.h"
#include "ut_Win32LocaleString.h"

/*!
 Returns Windows's OSVERSIONINFO structure
 */
OSVERSIONINFOW& UT_GetWinVersion(void)
{
	static bool bInitialized = false;
	static OSVERSIONINFOW os;

	if (!bInitialized)
	{
		os.dwOSVersionInfoSize = sizeof(os);
		UT_DebugOnly<BOOL> bSuccess = GetVersionExW(&os);
		UT_ASSERT(bSuccess);
		bInitialized = true;
	}

	return os;
}

/*!
 Return true if we're running on Windows Vista, false otherwise
 */
bool UT_IsWinVista(void)
{
	return (UT_GetWinVersion().dwPlatformId == VER_PLATFORM_WIN32_NT
		 && UT_GetWinVersion().dwMajorVersion >= 6);
}

/*!
 Return true if we're running on Windows NT, false otherwise
 */
bool UT_IsWinNT(void)
{
	return UT_GetWinVersion().dwPlatformId == VER_PLATFORM_WIN32_NT;
}

/*!
 Return true if we're running on Windows 2000, false otherwise
 */
bool UT_IsWin2K(void)
{
	return (UT_GetWinVersion().dwPlatformId == VER_PLATFORM_WIN32_NT
		 && UT_GetWinVersion().dwMajorVersion >= 5);
}

/*!
 Return true if we're running on Windows 95, false otherwise
 */
bool UT_IsWin95(void)
{
	return (UT_GetWinVersion().dwPlatformId == VER_PLATFORM_WIN32_WINDOWS
		 && UT_GetWinVersion().dwMajorVersion == 4 && UT_GetWinVersion().dwMinorVersion == 0);
}

/*****************************************************************/

/*!
 This function loads and locks a dialog template resource. 

 \param hinst
 \param lpszResName Name of the resource

 Returns the address of the locked resource.
 The caller is responsible for any unlocking/releasing necessary.
 This function is used by the various tabbed dialogs to load
 the sub-dialogs.
 */
DLGTEMPLATE * WINAPI UT_LockDlgRes(HINSTANCE hinst, LPCWSTR lpszResName)
{
    HRSRC hrsrc = FindResourceW(hinst, lpszResName, (LPWSTR)RT_DIALOG);
    if (!hrsrc)
        return nullptr;
    HGLOBAL hglb = LoadResource(hinst, hrsrc);
    if (!hglb)
        return nullptr;
    return (DLGTEMPLATE *) LockResource(hglb);
}

/*!
    This code is based on function by Philippe Randour <philippe_randour at hotmail dot
    com> and was found at http://bdn.borland.com/article/0,1410,28000,00.html

    (It is real pain that such an elementary task should be so hard)

    The caller must g_free the returned pointer when no longer needed
*/


#define GETDEFAULTPRINTER "GetDefaultPrinterW"


wchar_t * UT_GetDefaultPrinterName()
{
	UT_uint32 iBufferSize = 128; // will become 2x bigger immediately in the loop
	wchar_t * pPrinterName = nullptr;
	DWORD rc = ERROR_INVALID_FUNCTION;

	do
	{
		iBufferSize *= 2;

		if(pPrinterName)
			g_free(pPrinterName);

		pPrinterName = (wchar_t *) UT_calloc(sizeof(wchar_t),iBufferSize);
		UT_return_val_if_fail( pPrinterName, nullptr );

		// the method of obtaining the name is version specific ...
		OSVERSIONINFOW osvi;
		DWORD iNeeded, iReturned, iBuffSize;
		LPPRINTER_INFO_5W pPrinterInfo;
		wchar_t* p;

		osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOW);
		GetVersionExW(&osvi);

		if (osvi.dwPlatformId == VER_PLATFORM_WIN32_WINDOWS)
		{
			// get size of the buffer needed to call enum printers
			if (!EnumPrintersW(PRINTER_ENUM_DEFAULT,nullptr,5,nullptr,0,&iNeeded,&iReturned))
			{
				if ((rc = GetLastError()) != ERROR_INSUFFICIENT_BUFFER)
				{
					g_free(pPrinterName);
					return nullptr;
				}
			}

			// allocate the buffer
			if ((pPrinterInfo = (LPPRINTER_INFO_5W)LocalAlloc(LPTR,iNeeded)) == nullptr)
			{
				rc = GetLastError();
			}
			else
			{
				// now get the default printer
				if (!EnumPrintersW(PRINTER_ENUM_DEFAULT,nullptr,5,
								  (LPBYTE) pPrinterInfo,iNeeded,&iNeeded,&iReturned))
				{
					rc = GetLastError();
				}
				else
				{
					if (iReturned > 0)
					{
						// here we copy the name to our own buffer
						if ((DWORD) wcslen(pPrinterInfo->pPrinterName) > iBufferSize-1)
						{
							rc = ERROR_INSUFFICIENT_BUFFER;
						}
						else
						{
							wcscpy(pPrinterName,pPrinterInfo->pPrinterName);
							rc = ERROR_SUCCESS;
						}
					}
					else
					{
						*pPrinterName = L'\0';
						rc = ERROR_SUCCESS;
					}
				}

				LocalFree(pPrinterInfo);
			}
		}
		else if (osvi.dwPlatformId == VER_PLATFORM_WIN32_NT)
		{
			if (osvi.dwMajorVersion >= 5) /* Windows 2000 or later */
			{
				iBuffSize = iBufferSize;

				HMODULE hWinSpool = LoadLibraryW(L"winspool.drv");
				if (!hWinSpool)
				{
					g_free(pPrinterName);
					return nullptr;
				}

				// GetDefaultPrinterW returns BOOL, not HRESULT.
				// https://learn.microsoft.com/en-us/windows/win32/printdocs/getdefaultprinter
				BOOL (WINAPI * fnGetDefaultPrinter)(LPWSTR, LPDWORD) =
					reinterpret_cast<BOOL (WINAPI *)(LPWSTR, LPDWORD)>(reinterpret_cast<void *>(GetProcAddress(hWinSpool, GETDEFAULTPRINTER)));

				if (!fnGetDefaultPrinter)
				{
					FreeLibrary(hWinSpool);
					g_free(pPrinterName);
					return nullptr;
				}

                bool i =false;
				if (!fnGetDefaultPrinter(pPrinterName,&iBuffSize))
                        i = true;
                         
                if(i)
					rc = GetLastError();
				else
					rc = ERROR_SUCCESS;

				FreeLibrary(hWinSpool);
			}
			else /* Windows NT 4.0 or earlier */
			{
				if (GetProfileStringW(L"windows",L"device",L"",pPrinterName,iBufferSize) == iBufferSize-1)
				{
					rc = ERROR_INSUFFICIENT_BUFFER;
				}
				else
				{
					p = pPrinterName;
					while (*p != L'\0' && *p != L',')
						++p;
					*p = L'\0';

					rc = ERROR_SUCCESS;
				}
			}
		}
	}
	while (rc == ERROR_INSUFFICIENT_BUFFER);

	if (rc != ERROR_SUCCESS && pPrinterName)
		pPrinterName[0] = L'\0';

	return pPrinterName;
}

/*!
    This function obtains a DC for the default printer
    The caller needs to call DeleteDC when dc is no longer needed.
*/
HDC  UT_GetDefaultPrinterDC()
{
	wchar_t * pPrinterName  = UT_GetDefaultPrinterName();

	if(!pPrinterName || !*pPrinterName)
	{
		g_free(pPrinterName);
		return nullptr;
	}

	//	HANDLE hPrinter;
	//	if(!OpenPrinter(pPrinterName, &hPrinter, nullptr))
	//		return nullptr;

	const wchar_t * pDriver = UT_IsWinNT() ? L"WINSPOOL" : nullptr;
	HDC hdc = CreateDCW(pDriver, pPrinterName, nullptr, nullptr);
	g_free(pPrinterName);
	return hdc;
}



ATOM UT_RegisterClassEx(UINT style, WNDPROC wndproc, HINSTANCE hInstance,
 						HICON hIcon, HCURSOR hCursor, HBRUSH hbrBackground, HICON hIconSm,
						const wchar_t * menu, const wchar_t * name)
 
{
    ATOM atom;
	WNDCLASSEXW  wndclass;
	memset(&wndclass, 0, sizeof(wndclass));
	wndclass.cbSize        = sizeof(wndclass);
	wndclass.style         = style;
	wndclass.lpfnWndProc   = wndproc;
	wndclass.cbClsExtra    = 0;
	wndclass.cbWndExtra    = 0;
	wndclass.hInstance     = hInstance;
	wndclass.hIcon         = hIcon;
	wndclass.hCursor       = hCursor;
	wndclass.hbrBackground = hbrBackground;
	wndclass.lpszMenuName  = menu;
	wndclass.lpszClassName = name;
	wndclass.hIconSm       = hIconSm;
	
	atom = RegisterClassExW (&wndclass);	
	UT_ASSERT(atom);
	return atom;
}


LRESULT UT_DefWindowProc(HWND hWnd, UINT Msg, WPARAM wParam,LPARAM lParam)
{
	return DefWindowProcW(hWnd, Msg, wParam, lParam);	
}


BOOL UT_SetWindowText(HWND hWnd, const wchar_t * lpString)
{
	return SetWindowTextW(hWnd, lpString);
}



// The draw path calls this once per GDI call. HeapValidate plus a flush on
// every call made the ruler and typing wait on the disk. Leave the writer
// in place, but keep it off unless ABIWORD_TRACE=1.
static bool UT_Win32TraceEnabled(void)
{
	static int enabled = -1;
	if (enabled < 0)
	{
		char value[8];
		DWORD n = GetEnvironmentVariableA("ABIWORD_TRACE", value, sizeof(value));
		enabled = (n > 0 && n < sizeof(value) && value[0] == '1' && value[1] == 0) ? 1 : 0;
	}
	return enabled == 1;
}

static void UT_Win32TraceWrite(const char * step)
{
	if (!UT_Win32TraceEnabled() || !step || !*step)
		return;

	static HANDLE file = INVALID_HANDLE_VALUE;
	static unsigned long seq = 0;
	if (file == INVALID_HANDLE_VALUE)
	{
		WCHAR dir[MAX_PATH];
		DWORD n = GetTempPathW(MAX_PATH, dir);
		if (n == 0 || n >= MAX_PATH)
			return;
		WCHAR path[MAX_PATH];
		if (GetTempFileNameW(dir, L"abt", 0, path) == 0)
			return;
		file = CreateFileW(path, FILE_APPEND_DATA,
						   FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
						   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE)
			return;
	}

	char buf[800];
	int len = snprintf(buf, sizeof(buf), "%lu #%lu %s\n",
					   static_cast<unsigned long>(GetTickCount()),
					   ++seq, step);
	if (len <= 0)
		return;
	if (len >= static_cast<int>(sizeof(buf)))
		len = static_cast<int>(sizeof(buf)) - 1;
	DWORD wrote = 0;
	WriteFile(file, buf, static_cast<DWORD>(len), &wrote, nullptr);
	FlushFileBuffers(file);
}

void UT_Win32Trace(const char * step)
{
	UT_Win32TraceWrite(step);
}

void UT_Win32TraceF(const char * fmt, ...)
{
	if (!UT_Win32TraceEnabled() || !fmt)
		return;
	char msg[640];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);
	msg[sizeof(msg) - 1] = 0;
	UT_Win32TraceWrite(msg);
}

void UT_Win32TraceHeap(const char * step)
{
	if (!UT_Win32TraceEnabled())
		return;
	BOOL ok = HeapValidate(GetProcessHeap(), 0, nullptr);
	UT_Win32TraceF("%s %s", step ? step : "heap", ok ? "heap-ok" : "heap-bad");
}

BOOL UT_GetMessage(LPMSG lpMsg,HWND hWnd,UINT wMsgFilterMin,UINT wMsgFilterMax)
{
	return GetMessageW(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax);
}

LRESULT UT_DispatchMessage(const MSG *lpmsg)
{
	return DispatchMessageW(lpmsg);
}

static int s_themeGeneration = 0;

bool UT_Win32AppsUseDarkTheme(void)
{
	static int cachedGeneration = -1;
	static bool cachedDark = false;
	if (cachedGeneration == s_themeGeneration)
		return cachedDark;

	typedef LSTATUS (WINAPI *OpenFn)(HKEY, LPCWSTR, DWORD, REGSAM, PHKEY);
	typedef LSTATUS (WINAPI *QueryFn)(HKEY, LPCWSTR, LPDWORD, LPDWORD, LPBYTE, LPDWORD);
	typedef LSTATUS (WINAPI *CloseFn)(HKEY);
	static bool s_resolved = false;
	static OpenFn openKey = nullptr;
	static QueryFn queryValue = nullptr;
	static CloseFn closeKey = nullptr;
	if (!s_resolved)
	{
		s_resolved = true;
		HMODULE adv = GetModuleHandleW(L"advapi32.dll");
		if (!adv)
			adv = LoadLibraryW(L"advapi32.dll");
		if (adv)
		{
			openKey = reinterpret_cast<OpenFn>(reinterpret_cast<void *>(GetProcAddress(adv, "RegOpenKeyExW")));
			queryValue = reinterpret_cast<QueryFn>(reinterpret_cast<void *>(GetProcAddress(adv, "RegQueryValueExW")));
			closeKey = reinterpret_cast<CloseFn>(reinterpret_cast<void *>(GetProcAddress(adv, "RegCloseKey")));
		}
	}

	bool dark = false;
	HKEY key = nullptr;
	if (openKey && queryValue && closeKey &&
		openKey(HKEY_CURRENT_USER,
			L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
			0, KEY_READ, &key) == ERROR_SUCCESS)
	{
		DWORD value = 1;
		DWORD size = sizeof(value);
		DWORD type = 0;
		if (queryValue(key, L"AppsUseLightTheme", nullptr, &type,
				reinterpret_cast<LPBYTE>(&value), &size) == ERROR_SUCCESS &&
			type == REG_DWORD)
			dark = (value == 0);
		closeKey(key);
	}
	cachedDark = dark;
	cachedGeneration = s_themeGeneration;
	return dark;
}

void UT_Win32NoteThemeChanged(void)
{
	++s_themeGeneration;
}

static bool s_highContrast(void)
{
	HIGHCONTRASTW contrast;
	memset(&contrast, 0, sizeof(contrast));
	contrast.cbSize = sizeof(contrast);
	return SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0)
		&& (contrast.dwFlags & HCF_HIGHCONTRASTON);
}

void UT_Win32ApplyCaptionTheme(HWND hwnd)
{
	if (!hwnd)
		return;
	typedef HRESULT (WINAPI *SetAttrFn)(HWND, DWORD, LPCVOID, DWORD);
	static bool s_resolved = false;
	static SetAttrFn setAttr = nullptr;
	if (!s_resolved)
	{
		s_resolved = true;
		HMODULE dwm = GetModuleHandleW(L"dwmapi.dll");
		if (!dwm)
			dwm = LoadLibraryW(L"dwmapi.dll");
		if (dwm)
			setAttr = reinterpret_cast<SetAttrFn>(
				reinterpret_cast<void *>(GetProcAddress(dwm, "DwmSetWindowAttribute")));
	}
	if (!setAttr)
		return;

	// DWMWA_WINDOW_CORNER_PREFERENCE is 33 and DWMWCP_ROUND is 2.
	// https://learn.microsoft.com/en-us/windows/apps/desktop/modernize/ui/apply-rounded-corners
	const DWORD round = 2;
	setAttr(hwnd, 33, &round, sizeof(round));

	// High contrast keeps the system caption. Mica and the dark caption
	// would replace those colors.
	const bool contrast = s_highContrast();
	const BOOL dark = (!contrast && UT_Win32AppsUseDarkTheme()) ? TRUE : FALSE;
	// 20 is DWMWA_USE_IMMERSIVE_DARK_MODE. 19 is the same attribute
	// on builds that predate the renumbering.
	setAttr(hwnd, 20, &dark, sizeof(dark));
	setAttr(hwnd, 19, &dark, sizeof(dark));

	// DWMWA_SYSTEMBACKDROP_TYPE is 38. 2 is Mica on the window frame,
	// 1 turns it off. Windows 10 rejects the attribute.
	// https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/ne-dwmapi-dwm_systembackdrop_type
	const DWORD backdrop = contrast ? 1 : 2;
	setAttr(hwnd, 38, &backdrop, sizeof(backdrop));
}

void UT_Win32ApplyWindowTheme(HWND hwnd)
{
	if (!hwnd)
		return;
	typedef HRESULT (WINAPI *ThemeFn)(HWND, LPCWSTR, LPCWSTR);
	static bool s_resolved = false;
	static ThemeFn setTheme = nullptr;
	if (!s_resolved)
	{
		s_resolved = true;
		HMODULE ux = GetModuleHandleW(L"uxtheme.dll");
		if (!ux)
			ux = LoadLibraryW(L"uxtheme.dll");
		if (ux)
			setTheme = reinterpret_cast<ThemeFn>(
				reinterpret_cast<void *>(GetProcAddress(ux, "SetWindowTheme")));
	}
	if (setTheme)
	{
		if (!s_highContrast() && UT_Win32AppsUseDarkTheme())
			setTheme(hwnd, L"DarkMode_Explorer", nullptr);
		else
			setTheme(hwnd, L"", nullptr);
	}
}

static int s_systemDpi(void)
{
	typedef UINT (WINAPI *GetDpiForSystemFn)(void);
	static GetDpiForSystemFn getDpi = nullptr;
	static bool resolved = false;
	if (!resolved)
	{
		resolved = true;
		HMODULE user32 = GetModuleHandleW(L"user32.dll");
		if (user32)
			getDpi = reinterpret_cast<GetDpiForSystemFn>(
				reinterpret_cast<void *>(GetProcAddress(user32, "GetDpiForSystem")));
	}
	if (getDpi)
	{
		const UINT dpi = getDpi();
		if (dpi >= 96)
			return static_cast<int>(dpi);
	}
	HDC dc = GetDC(nullptr);
	int dpi = dc ? GetDeviceCaps(dc, LOGPIXELSX) : 96;
	if (dc)
		ReleaseDC(nullptr, dc);
	return dpi >= 96 ? dpi : 96;
}

int UT_Win32GetSystemMetricsForDpi(int index, int dpi)
{
	typedef int (WINAPI *ForDpiFn)(int, UINT);
	static ForDpiFn forDpi = nullptr;
	static bool resolved = false;
	if (!resolved)
	{
		resolved = true;
		HMODULE user32 = GetModuleHandleW(L"user32.dll");
		if (!user32)
			user32 = LoadLibraryW(L"user32.dll");
		if (user32)
			forDpi = reinterpret_cast<ForDpiFn>(
				reinterpret_cast<void *>(GetProcAddress(user32, "GetSystemMetricsForDpi")));
	}
	if (dpi < 96)
		dpi = 96;
	if (forDpi)
	{
		const int got = forDpi(index, static_cast<UINT>(dpi));
		if (got > 0)
			return got;
	}
	return GetSystemMetrics(index);
}

int UT_Win32WindowDpi(HWND hwnd)
{
	typedef UINT (WINAPI *GetDpiForWindowFn)(HWND);
	static GetDpiForWindowFn getDpi = nullptr;
	static bool resolved = false;
	if (!resolved)
	{
		resolved = true;
		HMODULE user32 = GetModuleHandleW(L"user32.dll");
		if (!user32)
			user32 = LoadLibraryW(L"user32.dll");
		if (user32)
			getDpi = reinterpret_cast<GetDpiForWindowFn>(
				reinterpret_cast<void *>(GetProcAddress(user32, "GetDpiForWindow")));
	}
	if (getDpi && hwnd)
	{
		const UINT dpi = getDpi(hwnd);
		if (dpi >= 96)
			return static_cast<int>(dpi);
	}
	HDC dc = hwnd ? GetDC(hwnd) : nullptr;
	int dpi = dc ? GetDeviceCaps(dc, LOGPIXELSX) : 96;
	if (dc)
		ReleaseDC(hwnd, dc);
	return dpi >= 96 ? dpi : 96;
}

COLORREF UT_Win32WorkspaceColor(void)
{
	if (s_highContrast())
		return GetSysColor(COLOR_APPWORKSPACE);
	if (UT_Win32AppsUseDarkTheme())
		return RGB(32, 32, 32);
	return GetSysColor(COLOR_BTNFACE);
}

void UT_Win32GetWindowTextUtf8(HWND hwnd, char * dest, int destChars)
{
	if (!dest || destChars <= 0)
		return;
	dest[0] = 0;
	if (!hwnd)
		return;

	wchar_t wide[512];
	wide[0] = 0;
	if (GetWindowTextW(hwnd, wide, 512) <= 0)
		return;

	UT_Win32LocaleString str;
	str.fromLocale(wide);
	UT_copy_utf8_bounded(dest, destChars, str.utf8_str().utf8_str());
}

static void s_scaleLogFont(LOGFONTW * font, int fromDpi, int toDpi)
{
	if (!font)
		return;
	font->lfHeight = UT_scale_by_dpi(font->lfHeight, fromDpi, toDpi);
	if (font->lfWidth)
		font->lfWidth = UT_scale_by_dpi(font->lfWidth, fromDpi, toDpi);
}

bool UT_Win32GetNonClientMetrics(NONCLIENTMETRICSW * metrics, int dpi)
{
	if (!metrics)
		return false;
	const UINT full = sizeof(*metrics);
	memset(metrics, 0, full);
	metrics->cbSize = full;

	typedef BOOL (WINAPI *ForDpiFn)(UINT, UINT, PVOID, UINT, UINT);
	static ForDpiFn forDpi = nullptr;
	static bool resolved = false;
	if (!resolved)
	{
		resolved = true;
		HMODULE user32 = GetModuleHandleW(L"user32.dll");
		if (!user32)
			user32 = LoadLibraryW(L"user32.dll");
		if (user32)
			forDpi = reinterpret_cast<ForDpiFn>(
				reinterpret_cast<void *>(GetProcAddress(user32, "SystemParametersInfoForDpi")));
	}

	bool matched = false;
	if (dpi >= 96 && forDpi && forDpi(SPI_GETNONCLIENTMETRICS, full, metrics, 0, static_cast<UINT>(dpi)))
		matched = true;
	if (!matched)
	{
		memset(metrics, 0, full);
		metrics->cbSize = full;
		if (!SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, full, metrics, 0))
			return false;
		if (dpi >= 96)
		{
			const int system = s_systemDpi();
			s_scaleLogFont(&metrics->lfCaptionFont, system, dpi);
			s_scaleLogFont(&metrics->lfSmCaptionFont, system, dpi);
			s_scaleLogFont(&metrics->lfMenuFont, system, dpi);
			s_scaleLogFont(&metrics->lfStatusFont, system, dpi);
			s_scaleLogFont(&metrics->lfMessageFont, system, dpi);
		}
	}
	return true;
}

static void s_retireFont(HFONT & current, HFONT & retired, HFONT created)
{
	const HFONT stock = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
	if (retired && retired != stock)
		DeleteObject(retired);
	retired = (current && current != stock) ? current : nullptr;
	current = created ? created : stock;
}

void UT_Win32RefreshStatusFont(HWND bar)
{
	if (!bar)
		return;

	static HFONT font = nullptr;
	static HFONT retired = nullptr;
	static int dpi = -1;
	int now = UT_Win32WindowDpi(bar);
	if (now < 96)
		now = 96;
	if (!font || dpi != now)
	{
		NONCLIENTMETRICSW metrics;
		HFONT created = nullptr;
		if (UT_Win32GetNonClientMetrics(&metrics, now))
			created = CreateFontIndirectW(&metrics.lfStatusFont);
		if (!created)
		{
			const int height = -UT_scale_by_dpi(12, 96, now);
			created = CreateFontW(height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
				DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
				CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
		}
		if (created)
		{
			// The font that was current is still selected in this bar until
			// WM_SETFONT below. Delete only the one retired on the last change.
			s_retireFont(font, retired, created);
			dpi = now;
		}
	}
	if (font)
		SendMessageW(bar, WM_SETFONT, (WPARAM)font, TRUE);
	UT_Win32ApplyWindowTheme(bar);
}

HWND UT_CreateWindowEx(DWORD dwExStyle, const wchar_t * pszClassName, const wchar_t * pszWindowName, DWORD dwStyle,
 					   int x, int y, int nWidth, int nHeight,
					   HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam)
					   
 {	
	HWND hwnd =  CreateWindowExW(dwExStyle, pszClassName, pszWindowName,
		dwStyle, x, y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam);	
			
	UT_ASSERT(hwnd);
	return hwnd;	
 }
