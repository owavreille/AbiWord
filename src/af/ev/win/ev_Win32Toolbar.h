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

#ifndef EV_WIN32TOOLBAR_H
#define EV_WIN32TOOLBAR_H

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commctrl.h>

#include "ut_types.h"
#include "ut_vector.h"
#include "xap_Types.h"
#include "ev_Toolbar.h"
#include "xav_Listener.h"
#include "ap_Toolbar_Id.h"
#include "ut_string_class.h"

class AP_Win32Toolbar_FontCombo;


class XAP_Win32App;
class XAP_Frame;
class EV_Win32Toolbar_ViewListener;
class EV_Toolbar_Action;

// HACK: it'd be nice to guarantee that menu and toolbar IDs don't overlap
#ifdef AP_MENU_ID__BOGUS2__
#define _ev_MENU_OFFSET		AP_MENU_ID__BOGUS2__
#else
#define _ev_MENU_OFFSET		1000
#endif

class ABI_EXPORT EV_Win32Toolbar : public EV_Toolbar
{
public:
	EV_Win32Toolbar(XAP_Win32App * pWin32App, XAP_Frame * pFrame,
				   const char * szToolbarLayoutName,
				   const char * szToolbarLabelSetName);

	virtual ~EV_Win32Toolbar(void);

	bool toolbarEvent(XAP_Toolbar_Id id,
						 UT_UCS4Char * pData = nullptr,
						 UT_uint32 dataLength = 0);
	virtual bool synthesize(void) override;
	bool bindListenerToView(AV_View * pView);
	virtual bool refreshToolbar(AV_View * pView, AV_ChangeMask mask);
	virtual bool repopulateStyles(void) override;
	bool getToolTip(LPARAM lParam);

	HWND getWindow(void) const;

	void applyChrome(void);
	void rebuildIcons(void);
	LRESULT onCustomDraw(LPNMTBCUSTOMDRAW lpcd);

	XAP_Frame * getFrame(void);

	void	onDropArrow(UINT cmd);
	void	showChevron(POINT screen);

	/*
		Note that the namespaces for toolbar and menu command ids
		do *not* overlap.
	*/
	inline XAP_Toolbar_Id	ItemIdFromWmCommand(UINT cmd)			{ return (XAP_Toolbar_Id)(cmd - WM_USER - _ev_MENU_OFFSET); };
	inline UINT				WmCommandFromItemId(XAP_Toolbar_Id id)	{ return (id + WM_USER + _ev_MENU_OFFSET); };
	inline bool				bVisible( void ) { return m_bVisible; }

protected:
	virtual void					show(void) override;
	virtual void					hide(void) override;

	void							_releaseListener(void);
	HWND							_getControlWindow(XAP_Toolbar_Id id);

	void							refillFontCombo(HWND hwndCombo);
	void							prepareFontDropdown(HWND hwndCombo);
	bool							_refreshID(XAP_Toolbar_Id id);
	bool							_refreshItem(AV_View * pView,
												 const EV_Toolbar_Action * pAction,
												 XAP_Toolbar_Id id);

private:
	int								_getBandForHwnd(HWND hToolbar) const;
	void							_addToRebar();
	void							_placeControls(void);
	static LRESULT CALLBACK			_ComboWndProc(HWND, UINT, WPARAM, LPARAM);
	static LRESULT CALLBACK			_ComboEditWndProc(HWND, UINT, WPARAM, LPARAM);
	static LRESULT CALLBACK			_ToolBarWndProc (HWND hWnd, UINT uMessage, WPARAM wParam, LPARAM lParam);

	XAP_Win32App *					m_pWin32App;
	XAP_Frame *						m_pWin32Frame;
	EV_Win32Toolbar_ViewListener *	m_pViewListener;
	AV_View *						m_pBoundView;
	AV_ListenerId					m_lid;	/* view listener id */

	HWND							m_hwnd;
	void *							m_imageList;
	int								m_iconPx;
	bool							m_iconsDark;
	UT_GenericVector<char *>		m_iconNames;
	UT_Vector						m_vecToolbarWidgets;
	AP_Win32Toolbar_FontCombo*		m_pFontCtrl;
	bool							m_fontRefilling;
	bool							m_fontDropdownReady;
	bool							m_fontPinTop;
	bool							m_bVisible;
	UT_GenericVector<UT_UTF8String *> 	m_vecOrgStylesNames;

};

#endif /* EV_WIN32TOOLBAR_H */
