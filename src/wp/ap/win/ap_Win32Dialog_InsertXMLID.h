/* AbiWord
 * Copyright (C) 2011 Ben Martin
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

#ifndef AP_WIN32DIALOG_INSERTXMLID_H
#define AP_WIN32DIALOG_INSERTXMLID_H

#include <string>

#include "ap_Dialog_InsertXMLID.h"
#include "xap_Win32DialogBase.h"

class ABI_EXPORT AP_Win32Dialog_InsertXMLID : public XAP_Win32DialogBase, public AP_Dialog_InsertXMLID
{
public:
	AP_Win32Dialog_InsertXMLID(XAP_DialogFactory * pDlgFactory, XAP_Dialog_Id id);
	virtual ~AP_Win32Dialog_InsertXMLID(void);

	virtual void		runModal(XAP_Frame * pFrame) override;
	static XAP_Dialog *	static_constructor(XAP_DialogFactory *, XAP_Dialog_Id id);

protected:
	BOOL				_onInitDialog(HWND hWnd, WPARAM wParam, LPARAM lParam) override;
	BOOL				_onCommand(HWND hWnd, WPARAM wParam, LPARAM lParam) override;
	BOOL				_onDlgMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) override;

private:
	static LRESULT CALLBACK s_nameProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
	std::string			_nameText(void) const;
};

#endif
