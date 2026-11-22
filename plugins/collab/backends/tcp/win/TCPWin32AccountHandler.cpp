/* AbiCollab - Code to enable the modification of remote documents.
 * Copyright (C) 2007 by Rylie Pavlik <rylie@ryliepavlik.com>
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

#include <cstdlib>
#include <string>
#include <boost/lexical_cast.hpp>
#include "TCPWin32AccountHandler.h"
#include "ap_Strings.h"
#include "ut_Win32LocaleString.h"
#include "xap_App.h"

static std::wstring tcp_label(XAP_String_Id id, const wchar_t * fallback)
{
	const XAP_StringSet * strings = XAP_App::getApp() ? XAP_App::getApp()->getStringSet() : nullptr;
	const char * value = strings ? strings->getValue(id) : nullptr;
	if (value && *value)
	{
		UT_Win32LocaleString text;
		text.fromUTF8(value);
		if (text.c_str() && *text.c_str())
			return text.c_str();
	}
	return fallback ? fallback : L"";
}

#define ABI_RID_DIALOG_COLLABTCP_SERVERENTRY 201
#define ABI_RID_DIALOG_COLLABTCP_PORTENTRY 202
#define ABI_RID_DIALOG_COLLABTCP_SERVERLABEL 203
#define ABI_RID_DIALOG_COLLABTCP_PORTLABEL 204
#define ABI_RID_DIALOG_COLLABTCP_SERVERRADIO 205
#define ABI_RID_DIALOG_COLLABTCP_JOINRADIO 206
#define ABI_RID_DIALOG_COLLABTCP_AUTOCONNECTCHECK 207
#define ABI_RID_DIALOG_COLLABTCP_ALLOWALLCHECK 208

AccountHandlerConstructor TCPAccountHandlerConstructor = &TCPWin32AccountHandler::static_constructor;

AccountHandler * TCPWin32AccountHandler::static_constructor()
{
	return static_cast<AccountHandler *>(new TCPWin32AccountHandler());
}

// return true if we process the command, false otherwise
BOOL TCPWin32AccountHandler::_onCommand(HWND hWnd, WPARAM wParam, LPARAM)
{
	if (!m_hServerEntry || !m_hPortEntry)
		return false; // we are still initializing the dialog

	AP_Win32Dialog_CollaborationAddAccount* pThis = (AP_Win32Dialog_CollaborationAddAccount *)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
	UT_return_val_if_fail(pThis, false);

	WORD wId = LOWORD(wParam);
	
	bool serve = _isCheckedHwnd(m_hServerRadio);
	// NOTE: GetWindowTextLength may have a Unicode caveat, according to MSDN
	// It may return a value higher than the actual length.  This is probably OK for
	// our purposes here.
	// TODO: Input validation would be cool, but oh well.
	bool hasPort = GetWindowTextLength(m_hPortEntry)>0;
	bool hasServer = GetWindowTextLength(m_hServerEntry)>0;

	switch (wId)
	{
	// Switch on resource ID's - where do they come from for these controls?
	case ABI_RID_DIALOG_COLLABTCP_SERVERRADIO:
	case ABI_RID_DIALOG_COLLABTCP_JOINRADIO:
		// These have the ability to enable/disable input controls
		// By their doing so, they can also enable/disable the "OK" button
		// which is why there is intentionally no break statement following this section.
		if (serve)
		{
			// disable the address entry
			EnableWindow(m_hServerEntry, false);
			// enable the OK button if we have a port filled in
			pThis->setBackendValidity(hasPort);
		}
		else
		{
			// enable the address entry
			EnableWindow(m_hServerEntry, true);
			// enable the OK button if we have a port and an address filled in
			pThis->setBackendValidity(hasServer && hasPort);
		}
		return true;
		
	case ABI_RID_DIALOG_COLLABTCP_SERVERENTRY:
	case ABI_RID_DIALOG_COLLABTCP_PORTENTRY:
		{
			if (serve)
			{
				// All we need is a port.
				pThis->setBackendValidity(hasPort);
			}
			else
			{
				// We are connecting to a server: must have a server listed and have a port
				pThis->setBackendValidity(hasServer && hasPort);
			}
		return true;
		}
	default:
		return false;
	}
}

bool TCPWin32AccountHandler::shouldProcessFocus()
{
	return GetFocus() != m_hAutoconnectCheck;
}

TCPWin32AccountHandler::TCPWin32AccountHandler()
	: TCPAccountHandler(),
	m_pWin32Dialog(NULL),
	m_hInstance(NULL),
	m_hServerEntry(NULL),
	m_hPortEntry(NULL),
	m_hServerRadio(NULL),
	m_hJoinRadio(NULL),
	m_hServerLabel(NULL),
	m_hPortLabel(NULL),
	m_hAllowAllCheck(NULL),
	m_hAutoconnectCheck(NULL),
	m_hFont(nullptr)
{
	AbiCollabSessionManager * pSessionManager = AbiCollabSessionManager::getManager();
	if (pSessionManager)
	{
		m_hInstance = pSessionManager->getInstance();
	}
}

TCPWin32AccountHandler::~TCPWin32AccountHandler()
{
	if (m_hFont)
	{
		DeleteObject(m_hFont);
		m_hFont = nullptr;
	}
}

void TCPWin32AccountHandler::embedDialogWidgets(void* pEmbeddingParent)
{
	UT_DEBUGMSG(("TCPWin32AccountHandler::embedDialogWidgets()\n"));
	UT_return_if_fail(pEmbeddingParent);

	// our enclosing window must be a HWND
	HWND hBox = reinterpret_cast<HWND>(pEmbeddingParent);
	
	/* Non-Tabbable Labels */
	const std::wstring address = tcp_label(AP_STRING_ID_DLG_COLLAB_TCP_Address, L"Address:");
	const std::wstring port = tcp_label(AP_STRING_ID_DLG_COLLAB_TCP_Port, L"Port:");
	const std::wstring accept = tcp_label(AP_STRING_ID_DLG_COLLAB_TCP_Accept, L"Accept Incoming Connections");
	const std::wstring connect = tcp_label(AP_STRING_ID_DLG_COLLAB_TCP_Connect, L"Connect to Another Computer");
	const std::wstring grant = tcp_label(AP_STRING_ID_DLG_COLLAB_TCP_Grant, L"Automatically grant buddies access to shared documents");
	const std::wstring startup = tcp_label(AP_STRING_ID_DLG_COLLAB_TCP_Startup, L"Connect on Application Startup");

	// "Address:"
	m_hServerLabel = CreateWindowExW(WS_EX_NOPARENTNOTIFY, L"STATIC", address.c_str(), SS_LEFT | WS_CHILD | WS_VISIBLE,
	15, 57, 62, 15, hBox,  (HMENU) ABI_RID_DIALOG_COLLABTCP_SERVERLABEL,  m_hInstance, 0);
	UT_return_if_fail(m_hServerLabel);
	
	// "Port:"
	m_hPortLabel = CreateWindowExW(WS_EX_NOPARENTNOTIFY, L"STATIC", port.c_str(), SS_LEFT | WS_CHILD | WS_VISIBLE, 
	15, 87, 47, 15, hBox,  (HMENU) ABI_RID_DIALOG_COLLABTCP_PORTLABEL,  m_hInstance, 0);
	UT_return_if_fail(m_hPortLabel);
	
	/* Radio Button Group */
	m_hServerRadio = CreateWindowExW(WS_EX_NOPARENTNOTIFY |  WS_EX_TRANSPARENT, L"BUTTON", accept.c_str(), BS_NOTIFY | BS_AUTORADIOBUTTON | WS_CHILD | WS_VISIBLE | WS_TABSTOP,
	15, 15, 240, 15, hBox,  (HMENU) ABI_RID_DIALOG_COLLABTCP_SERVERRADIO,  m_hInstance, 0);
	UT_return_if_fail(m_hServerRadio);
	
	// Join a Server radio button
	m_hJoinRadio = CreateWindowExW(WS_EX_NOPARENTNOTIFY |  WS_EX_TRANSPARENT, L"BUTTON", connect.c_str(), BS_NOTIFY  | BS_AUTORADIOBUTTON | WS_CHILD | WS_VISIBLE | WS_TABSTOP,
	15, 35, 240, 15, hBox,  (HMENU) ABI_RID_DIALOG_COLLABTCP_JOINRADIO,  m_hInstance, 0);
	UT_return_if_fail(m_hJoinRadio);
	
	// Entry box for IP address of server
	// should be disabled when in server mode, and OK should be disabled when this is empty in join mode
	m_hServerEntry = CreateWindowExW(WS_EX_NOPARENTNOTIFY | WS_EX_CLIENTEDGE, L"EDIT", L"", ES_LEFT | ES_AUTOHSCROLL | WS_CHILD | WS_BORDER | WS_VISIBLE | WS_TABSTOP,
	80, 55, 121, 20, hBox,  (HMENU) ABI_RID_DIALOG_COLLABTCP_SERVERENTRY,  m_hInstance, 0);
	UT_return_if_fail(m_hServerEntry);
	
	// Port entry box
	m_hPortEntry = CreateWindowExA(WS_EX_NOPARENTNOTIFY | WS_EX_CLIENTEDGE, "EDIT", boost::lexical_cast<std::string>(DEFAULT_TCP_PORT).c_str(), ES_LEFT | WS_CHILD | WS_BORDER | WS_VISIBLE | WS_TABSTOP,
	80, 85, 60, 20, hBox,  (HMENU) ABI_RID_DIALOG_COLLABTCP_PORTENTRY,  m_hInstance, 0);
	UT_return_if_fail(m_hPortEntry);
	
	// Checkbox for "auto grant permission"
	m_hAllowAllCheck = CreateWindowExW(WS_EX_NOPARENTNOTIFY, L"BUTTON", grant.c_str(), BS_CHECKBOX | BS_AUTOCHECKBOX | WS_CHILD | WS_VISIBLE | WS_TABSTOP,
	14, 115, 290, 15, hBox,  (HMENU) ABI_RID_DIALOG_COLLABTCP_ALLOWALLCHECK,  m_hInstance, 0);
	UT_return_if_fail(m_hAllowAllCheck);

	// Checkbox for Connect on Startup
	m_hAutoconnectCheck = CreateWindowExW(WS_EX_NOPARENTNOTIFY, L"BUTTON", startup.c_str(), BS_CHECKBOX | BS_AUTOCHECKBOX | WS_CHILD | WS_VISIBLE | WS_TABSTOP,
	14, 135, 225, 15, hBox,  (HMENU) ABI_RID_DIALOG_COLLABTCP_AUTOCONNECTCHECK,  m_hInstance, 0);
	UT_return_if_fail(m_hAutoconnectCheck);
	
	// Font setting code borrowed from XAP_Win32Dlg_About
	LOGFONTW lf = {};
	wcscpy(lf.lfFaceName, L"MS Sans Serif");
	lf.lfHeight = 12;
	lf.lfWeight = 0;
	if (m_hFont)
	{
		DeleteObject(m_hFont);
		m_hFont = nullptr;
	}
	m_hFont = CreateFontIndirectW(&lf);
	HWND rgFontReceivers[] =
		{ m_hServerRadio, m_hJoinRadio, m_hServerLabel, m_hPortLabel, m_hServerEntry, m_hPortEntry, m_hAllowAllCheck, m_hAutoconnectCheck};
	for (UT_uint32 iWnd = 0; iWnd < G_N_ELEMENTS(rgFontReceivers); iWnd++)
		SendMessage(rgFontReceivers[iWnd], WM_SETFONT, (WPARAM) m_hFont, 0);
	
	// default to serve
	_checkButtonHwnd(m_hServerRadio, true);
	EnableWindow(m_hServerEntry, false);
	
	// default to autoconnect
	_checkButtonHwnd(m_hAutoconnectCheck, true);
}

#define DESTROY_WINDOW(M) do { if (M) { DestroyWindow(M); M = nullptr; } } while (0)

void TCPWin32AccountHandler::removeDialogWidgets(void* pEmbeddingParent)
{
	UT_DEBUGMSG(("TCPAccountHandler::removeDialogWidgets()\n"));
	UT_return_if_fail(pEmbeddingParent);

	// our enclosing window must be a HWND
	HWND hBox = reinterpret_cast<HWND>(pEmbeddingParent);
	
	DESTROY_WINDOW(m_hServerEntry);
	DESTROY_WINDOW(m_hPortEntry);
	DESTROY_WINDOW(m_hServerLabel);
	DESTROY_WINDOW(m_hPortLabel);
	DESTROY_WINDOW(m_hServerRadio);
	DESTROY_WINDOW(m_hJoinRadio);
	DESTROY_WINDOW(m_hAllowAllCheck);
	DESTROY_WINDOW(m_hAutoconnectCheck);
	if (m_hFont)
	{
		DeleteObject(m_hFont);
		m_hFont = nullptr;
	}

	// destroying a window does not repaint the area it owned, so do it here
	RedrawWindow(GetParent(hBox), NULL, NULL, RDW_ERASE | RDW_INVALIDATE | RDW_UPDATENOW);
}

void TCPWin32AccountHandler::loadProperties()
{
	UT_DEBUGMSG(("TCPWin32AccountHandler::storeProperties()\n"));

	UT_ASSERT_HARMLESS(UT_NOT_IMPLEMENTED);
}

void TCPWin32AccountHandler::storeProperties()
{
	UT_DEBUGMSG(("TCPWin32AccountHandler::storeProperties()\n"));
	char sServer[256];
	memset(sServer, 0, sizeof(sServer));
	
	char sPort[16];
	memset(sPort, 0, sizeof(sPort));
	
	bool serve = _isCheckedHwnd(m_hServerRadio);
	if (!serve)
	{
		// joining someone else's server
		if (m_hServerEntry)
		{
			
			if (_getControlTextHwnd(m_hServerEntry, sizeof(sServer), sServer) != -1)
			{
				sServer[255]=0;
				addProperty("server", sServer);
			}
		}
	}
	
	if (m_hPortEntry)
	{
		if (_getControlTextHwnd(m_hPortEntry, sizeof(sPort), sPort) != -1)
		{
			char* end = nullptr;
			const unsigned long port = strtoul(sPort, &end, 10);
			if (end && *end == '\0' && port >= 1 && port <= 65535)
				addProperty("port", sPort);
		}
	}

	if (m_hAllowAllCheck)
	{
		addProperty("allow-all", _isCheckedHwnd(m_hAllowAllCheck) ? "true" : "false" );
	}

	if (m_hAutoconnectCheck)
	{
		addProperty("autoconnect", _isCheckedHwnd(m_hAutoconnectCheck) ? "true" : "false" );
	}

}

// Win32 Helper Functions
// Since these controls aren't in a dialog resource file, the DialogHelper is no use, nor the pretty parts of the win api
// we must do some raw sendmessage instead.
void TCPWin32AccountHandler::_checkButtonHwnd(HWND hCtrl, bool bChecked)
{
	UT_return_if_fail(hCtrl);
	SendMessage(hCtrl, BM_SETCHECK, bChecked ? BST_CHECKED : BST_UNCHECKED, 0);
}

bool TCPWin32AccountHandler::_isCheckedHwnd(HWND hCtrl)
{
	// note - does not work for 3-state buttons, obviously, since it returns a bool
	UT_return_val_if_fail(hCtrl, false);
	return (SendMessage(hCtrl, BM_GETCHECK, 0, 0) == BST_CHECKED);
}

int TCPWin32AccountHandler::_getControlTextHwnd(HWND hCtrl, int iLen, const char * p_szBuf)
{
	UT_return_val_if_fail(hCtrl && p_szBuf && iLen > 1, -1);
	// WM_GETTEXT under UNICODE writes wchar_t. The destination is a UTF-8 byte buffer.
	wchar_t* wide = new wchar_t[iLen];
	wide[0] = 0;
	SendMessageW(hCtrl, WM_GETTEXT, static_cast<WPARAM>(iLen), reinterpret_cast<LPARAM>(wide));
	gchar* utf8 = g_utf16_to_utf8(reinterpret_cast<const gunichar2*>(wide), -1, nullptr, nullptr, nullptr);
	delete [] wide;
	if (!utf8)
		return -1;
	g_strlcpy(const_cast<char*>(p_szBuf), utf8, static_cast<gsize>(iLen));
	g_free(utf8);
	return 0;
}
