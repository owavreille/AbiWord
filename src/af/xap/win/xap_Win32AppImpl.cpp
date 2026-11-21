/* AbiSource Application Framework
 * Copyright (C) 1998-2000 AbiSource, Inc.
 * Copyright (C) 2004 Hubert Figuiere
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

#include "xap_Win32AppImpl.h"
#include "ut_string_class.h"
#include "ut_path.h"
#include "ut_go_file.h"
#include "xap_Frame.h"
#include "xap_App.h"
#include "xap_Win32FrameImpl.h"

#include "ut_Win32LocaleString.h"

bool XAP_Win32AppImpl::openURL(const char * szURL)
{
	if (!szURL || !*szURL)
		return false;
	if (!UT_go_url_may_open(szURL))
		return false;

	// ShellExecute with the open verb uses the user's current association
	// for http, https and file. The old http\\shell\\open\\command value
	// is not that association, and its quotes were passed as part of the path.
	UT_Win32LocaleString url;
	url.fromUTF8(szURL);
	if (!url.c_str() || !url.c_str()[0])
		return false;

	XAP_Frame * pFrame = XAP_App::getApp()->getLastFocussedFrame();
	HWND hwnd = nullptr;
	if (pFrame && pFrame->getFrameImpl())
		hwnd = static_cast<XAP_Win32FrameImpl *>(pFrame->getFrameImpl())->getTopLevelWindow();

	INT_PTR res = (INT_PTR)ShellExecuteW(hwnd, L"open", url.c_str(), nullptr, nullptr, SW_SHOW);
	if (res <= 32)
	{
		UT_String errMsg = "Error (";
		errMsg += UT_String_sprintf("%lld", static_cast<long long>(res));
		errMsg += ") displaying URL: \n [ ";
		errMsg += szURL;
		errMsg += " ] ";
		if (hwnd)
		{
			UT_Win32LocaleString str;
			str.fromUTF8(errMsg.c_str());
			MessageBoxW(hwnd, str.c_str(), L"Error displaying URL", MB_OK|MB_ICONEXCLAMATION);
		}
	}
	return res > 32;
}
