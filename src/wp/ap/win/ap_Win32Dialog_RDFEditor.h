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

#ifndef AP_WIN32DIALOG_RDFEDITOR_H
#define AP_WIN32DIALOG_RDFEDITOR_H

#include <list>
#include <vector>

#include "ap_Dialog_RDFEditor.h"
#include "xap_Win32DialogBase.h"

class ABI_EXPORT AP_Win32Dialog_RDFEditor : public AP_Dialog_RDFEditor, public XAP_Win32DialogBase
{
public:
	AP_Win32Dialog_RDFEditor(XAP_DialogFactory * pDlgFactory, XAP_Dialog_Id id);
	virtual ~AP_Win32Dialog_RDFEditor(void);

	virtual void			runModeless(XAP_Frame * pFrame) override;
	static XAP_Dialog *		static_constructor(XAP_DialogFactory *, XAP_Dialog_Id id);
	virtual void			activate(void) override;
	virtual void			destroy(void) override;
	virtual void			notifyActiveFrame(XAP_Frame * pFrame) override;

	virtual void			clear() override;
	virtual void			addStatement(const PD_RDFStatement& st) override;
	virtual void			setStatus(const std::string& msg) override;
	virtual void			removeStatement(const PD_RDFStatement& st) override;
	virtual std::list< PD_RDFStatement > getSelection() override;
	virtual void			setSelection(const std::list< PD_RDFStatement >& l) override;
	virtual void			hideRestrictionXMLID(bool v) override;

protected:
	BOOL					_onInitDialog(HWND hWnd, WPARAM wParam, LPARAM lParam) override;
	BOOL					_onCommand(HWND hWnd, WPARAM wParam, LPARAM lParam) override;
	BOOL					_onDlgMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) override;

private:
	void					_fillEdits(const PD_RDFStatement& st);
	void					_applyEdits(void);
	void					_deleteSelection(void);
	void					_fillLinkCombo(void);
	std::string				_editText(int controlId) const;
	int						_selectedIndex(void) const;

	std::vector< PD_RDFStatement > m_rows;
};

#endif
