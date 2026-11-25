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
 
/*
	In versions previous to Windows Vista we use owner-draw menus.
	From Vista on, the icon is hbmpItem: a 32-bit BI_RGB bitmap with premultiplied alpha.
*/

#include <wchar.h>
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include "ut_types.h"
#include <vector>
#include "ut_string.h"
#include "ut_string_class.h"
#include "ut_debugmsg.h"
#include "ut_Language.h"
#include "xap_Types.h"
#include "ev_Win32Menu.h"
#include "xap_Win32App.h"
#include "ev_Menu_Layouts.h"
#include "ev_Menu_Actions.h"
#include "ev_Menu_Labels.h"
#include "ev_EditEventMapper.h"
#include "xap_Frame.h"
#include "ap_Win32Resources.rc2"
#include "ap_Menu_Id.h"
#include "xap_App.h"
#include "xap_Win32App.h"
#include "xap_Win32Toolbar_Icons.h"
#include "ap_Win32App.h"
#include "ut_Win32OS.h"
#include "ut_Win32LocaleString.h"

#define SPACE_ICONTEXT	4	// Pixels between the icon and the text
#define BITMAP_WITDH	16
#define BITMAP_HEIGHT	16


/*
	This table assigns an icon to every menu entry
*/
static const EV_Menu_Bitmap s_bitmaps[] =
{
	{AP_MENU_ID_FILE_NEW, "Menu_AbiWord_New"},
	{AP_MENU_ID_FILE_NEW_USING_TEMPLATE, "LO_Template"},
	{AP_MENU_ID_FILE_OPEN, "Menu_AbiWord_Open"},
	{AP_MENU_ID_FILE_IMPORTSTYLES, "Menu_AbiWord_Import"},
	{AP_MENU_ID_FILE_SAVE, "Menu_AbiWord_Save"},
	{AP_MENU_ID_FILE_SAVEAS, "Menu_AbiWord_SaveAs"},
	{AP_MENU_ID_FILE_EXPORT, "Menu_AbiWord_Export"},
	{AP_MENU_ID_FILE_REVERT, "Menu_AbiWord_Revert"},
	{AP_MENU_ID_FILE_PAGESETUP, "Menu_AbiWord_Print_Setup"},
	{AP_MENU_ID_FILE_PRINT_PREVIEW, "Menu_AbiWord_Print_Preview"},
	{AP_MENU_ID_FILE_PRINT, "Menu_AbiWord_Print"},
	{AP_MENU_ID_FILE_PROPERTIES, "Menu_AbiWord_Properties"},
	{AP_MENU_ID_FILE_RECENT, "LO_Recent"},
	{AP_MENU_ID_FILE_RECENT_1, "LO_Recent"},
	{AP_MENU_ID_FILE_RECENT_2, "LO_Recent"},
	{AP_MENU_ID_FILE_RECENT_3, "LO_Recent"},
	{AP_MENU_ID_FILE_RECENT_4, "LO_Recent"},
	{AP_MENU_ID_FILE_RECENT_5, "LO_Recent"},
	{AP_MENU_ID_FILE_RECENT_6, "LO_Recent"},
	{AP_MENU_ID_FILE_RECENT_7, "LO_Recent"},
	{AP_MENU_ID_FILE_RECENT_8, "LO_Recent"},
	{AP_MENU_ID_FILE_RECENT_9, "LO_Recent"},
	{AP_MENU_ID_FILE_CLOSE, "Menu_AbiWord_Close"},
	{AP_MENU_ID_FILE_EXIT, "Menu_AbiWord_Exit"},

	{AP_MENU_ID_EDIT_UNDO, "Menu_AbiWord_Undo"},
	{AP_MENU_ID_EDIT_REDO, "Menu_AbiWord_Redo"},
	{AP_MENU_ID_EDIT_CUT, "Menu_AbiWord_Cut"},
	{AP_MENU_ID_EDIT_COPY, "Menu_AbiWord_Copy"},
	{AP_MENU_ID_EDIT_PASTE, "Menu_AbiWord_Paste"},
	{AP_MENU_ID_EDIT_PASTE_SPECIAL, "Menu_AbiWord_Paste"},
	{AP_MENU_ID_EDIT_CLEAR, "Menu_AbiWord_Clear"},
	{AP_MENU_ID_EDIT_SELECTALL, "LO_Selectall"},
	{AP_MENU_ID_EDIT_REMOVEHEADER, "LO_Removeheader"},
	{AP_MENU_ID_EDIT_REMOVEFOOTER, "LO_Removefooter"},
	{AP_MENU_ID_EDIT_FIND, "Menu_AbiWord_Search"},
	{AP_MENU_ID_EDIT_REPLACE, "Menu_AbiWord_Replace"},
	{AP_MENU_ID_EDIT_GOTO, "Menu_AbiWord_Goto"},

	{AP_MENU_ID_VIEW_NORMAL, "LO_Normal"},
	{AP_MENU_ID_VIEW_WEB, "LO_Web"},
	{AP_MENU_ID_VIEW_PRINT, "LO_Printlayout"},
	{AP_MENU_ID_WEB_WEBPREVIEW, "Menu_AbiWord_Print_Preview"},
	{AP_MENU_ID_VIEW_TOOLBARS, "LO_Toolbar"},
	{AP_MENU_ID_VIEW_TB_1, "LO_Toolbar"},
	{AP_MENU_ID_VIEW_TB_2, "LO_Toolbar"},
	{AP_MENU_ID_VIEW_TB_3, "LO_Toolbar"},
	{AP_MENU_ID_VIEW_TB_4, "LO_Toolbar"},
	{AP_MENU_ID_VIEW_RULER, "LO_Ruler"},
	{AP_MENU_ID_VIEW_STATUSBAR, "LO_Status"},
	{AP_MENU_ID_VIEW_LOCKSTYLES, "LO_Lock"},
	{AP_MENU_ID_VIEW_SHOWPARA, "LO_Showpara"},
	{AP_MENU_ID_VIEW_FULLSCREEN, "LO_Fullscreen"},
	{AP_MENU_ID_VIEW_ZOOM_MENU, "LO_Zoom"},
	{AP_MENU_ID_VIEW_ZOOM, "LO_Zoom"},
	{AP_MENU_ID_VIEW_ZOOM_WIDTH, "LO_Zoomwidth"},
	{AP_MENU_ID_VIEW_ZOOM_WHOLE, "LO_Zoompage"},
	{AP_MENU_ID_VIEW_ZOOM_200, "LO_Zoom"},
	{AP_MENU_ID_VIEW_ZOOM_100, "LO_Zoom100"},
	{AP_MENU_ID_VIEW_ZOOM_75, "LO_Zoom"},
	{AP_MENU_ID_VIEW_ZOOM_50, "LO_Zoom"},

	{AP_MENU_ID_INSERT_BREAK, "LO_Break"},
	{AP_MENU_ID_INSERT_HEADER, "LO_Header"},
	{AP_MENU_ID_INSERT_FOOTER, "LO_Footer"},
	{AP_MENU_ID_INSERT_TEXTBOX, "LO_Textbox"},
	{AP_MENU_ID_INSERT_TABLEOFCONTENTS, "LO_Toc"},
	{AP_MENU_ID_INSERT_FOOTNOTE, "LO_Footnote"},
	{AP_MENU_ID_INSERT_ENDNOTE, "LO_Endnote"},
	{AP_MENU_ID_INSERT_SYMBOL, "Menu_AbiWord_Insert_Symbol"},
	{AP_MENU_ID_INSERT_PAGENO, "LO_Pageno"},
	{AP_MENU_ID_INSERT_DATETIME, "LO_Date"},
	{AP_MENU_ID_INSERT_FIELD, "LO_Field"},
	{AP_MENU_ID_INSERT_BOOKMARK, "Menu_AbiWord_Bookmark"},
	{AP_MENU_ID_INSERT_XMLID, "LO_Xml"},
	{AP_MENU_ID_INSERT_HYPERLINK, "Menu_AbiWord_Hyperlink"},
	{AP_MENU_ID_INSERT_MAILMERGE, "LO_Mail"},
	{AP_MENU_ID_INSERT_FILE, "LO_File"},
	{AP_MENU_ID_INSERT_CLIPART, "Menu_AbiWord_Img"},
	{AP_MENU_ID_INSERT_GRAPHIC, "Menu_AbiWord_Img"},
	{AP_MENU_ID_INSERT_DIRECTIONMARKER, "LO_Direction"},
	{AP_MENU_ID_INSERT_DIRECTIONMARKER_LRM, "LO_Direction"},
	{AP_MENU_ID_INSERT_DIRECTIONMARKER_RLM, "LO_Direction"},

	{AP_MENU_ID_FMT_FONT, "Menu_AbiWord_Font"},
	{AP_MENU_ID_FMT_PARAGRAPH, "LO_Paragraph"},
	{AP_MENU_ID_FMT_BULLETS, "LO_Bullets"},
	{AP_MENU_ID_FMT_TABLE, "Menu_AbiWord_Insert_Table"},
	{AP_MENU_ID_FMT_FRAME, "LO_Frame"},
	{AP_MENU_ID_FMT_IMAGE, "LO_Image"},
	{AP_MENU_ID_FMT_BORDERS, "LO_Border"},
	{AP_MENU_ID_FMT_COLUMNS, "LO_Columns"},
	{AP_MENU_ID_FMT_TABS, "LO_Tabs"},
	{AP_MENU_ID_FMT_HDRFTR, "LO_Header"},
	{AP_MENU_ID_FMT_FOOTNOTES, "LO_Footnotes"},
	{AP_MENU_ID_FMT_TABLEOFCONTENTS, "LO_Toc"},
	{AP_MENU_ID_FMT_TOGGLECASE, "LO_Case"},
	{AP_MENU_ID_ALIGN, "Menu_AbiWord_Align_Left"},
	{AP_MENU_ID_ALIGN_LEFT, "Menu_AbiWord_Align_Left"},
	{AP_MENU_ID_ALIGN_CENTER, "Menu_AbiWord_Align_Center"},
	{AP_MENU_ID_ALIGN_RIGHT, "Menu_AbiWord_Align_Right"},
	{AP_MENU_ID_ALIGN_JUSTIFY, "Menu_AbiWord_Align_Justify"},
	{AP_MENU_ID_FMT, "Menu_AbiWord_Bold"},
	{AP_MENU_ID_FMT_BOLD, "Menu_AbiWord_Bold"},
	{AP_MENU_ID_FMT_ITALIC, "Menu_AbiWord_Italic"},
	{AP_MENU_ID_FMT_UNDERLINE, "Menu_AbiWord_Underline"},
	{AP_MENU_ID_FMT_OVERLINE, "Menu_AbiWord_Overline"},
	{AP_MENU_ID_FMT_STRIKE, "Menu_AbiWord_Strike"},
	{AP_MENU_ID_FMT_SUPERSCRIPT, "Menu_AbiWord_Superscript"},
	{AP_MENU_ID_FMT_SUBSCRIPT, "Menu_AbiWord_Subscript"},
	{AP_MENU_ID_FMT_BACKGROUND, "LO_Background"},
	{AP_MENU_ID_FMT_BACKGROUND_PAGE_IMAGE, "LO_Image"},
	{AP_MENU_ID_FMT_BACKGROUND_PAGE_COLOR, "LO_Background"},
	{AP_MENU_ID_FMT_DIRECTION, "LO_Direction"},
	{AP_MENU_ID_FMT_DIRECTION_DOCD_RTL, "LO_Direction"},
	{AP_MENU_ID_FMT_DIRECTION_SD_RTL, "LO_Direction"},
	{AP_MENU_ID_FMT_DIRECTION_DD_RTL, "LO_Direction"},
	{AP_MENU_ID_FMT_DIRECTION_DO_LTR, "LO_Direction"},
	{AP_MENU_ID_FMT_DIRECTION_DO_RTL, "LO_Direction"},
	{AP_MENU_ID_FMT_STYLE_DEFINE, "LO_Style"},
	{AP_MENU_ID_FMT_LANGUAGE, "Menu_AbiWord_Book"},
	{AP_MENU_ID_FMT_STYLIST, "LO_Style"},

	{AP_MENU_ID_TOOLS_SPELL, "Menu_AbiWord_Spellcheck"},
	{AP_MENU_ID_TOOLS_WORDCOUNT, "LO_Wordcount"},
	{AP_MENU_ID_TOOLS_HISTORY, "LO_History"},
	{AP_MENU_ID_TOOLS_HISTORY_SHOW, "LO_History"},
	{AP_MENU_ID_TOOLS_REVISIONS_COMPARE_DOCUMENTS, "LO_Compare"},
	{AP_MENU_ID_TOOLS_REVISIONS_AUTO, "LO_Revisions"},
	{AP_MENU_ID_TOOLS_REVISIONS, "LO_Revisions"},
	{AP_MENU_ID_TOOLS_REVISIONS_MARK, "LO_Revisions"},
	{AP_MENU_ID_TOOLS_REVISIONS_NEW_REVISION, "LO_Revisions"},
	{AP_MENU_ID_TOOLS_REVISIONS_SHOW, "LO_Shownotes"},
	{AP_MENU_ID_TOOLS_REVISIONS_SHOW_AFTER, "LO_Next"},
	{AP_MENU_ID_TOOLS_REVISIONS_SHOW_AFTERPREV, "LO_Prev"},
	{AP_MENU_ID_TOOLS_REVISIONS_SHOW_BEFORE, "LO_Prev"},
	{AP_MENU_ID_TOOLS_REVISIONS_SET_VIEW_LEVEL, "LO_Revisions"},
	{AP_MENU_ID_TOOLS_REVISIONS_FIND_NEXT, "LO_Next"},
	{AP_MENU_ID_TOOLS_REVISIONS_FIND_PREV, "LO_Prev"},
	{AP_MENU_ID_TOOLS_REVISIONS_ACCEPT_REVISION, "LO_Accept"},
	{AP_MENU_ID_TOOLS_REVISIONS_REJECT_REVISION, "LO_Reject"},
	{AP_MENU_ID_TOOLS_REVISIONS_PURGE, "Menu_AbiWord_Clear"},
	{AP_MENU_ID_TOOLS_ANNOTATIONS, "LO_Annotation"},
	{AP_MENU_ID_TOOLS_ANNOTATIONS_INSERT, "LO_Annotation"},
	{AP_MENU_ID_TOOLS_ANNOTATIONS_INSERT_FROMSEL, "LO_Annotation"},
	{AP_MENU_ID_TOOLS_ANNOTATIONS_TOGGLE_DISPLAY, "LO_Shownotes"},
	{AP_MENU_ID_TOOLS_PLUGINS, "LO_Plugin"},
	{AP_MENU_ID_TOOLS_SCRIPTS, "Menu_AbiWord_Execute"},
	{AP_MENU_ID_TOOLS_MAILMERGE, "LO_Mail"},
	{AP_MENU_ID_TOOLS_OPTIONS, "Menu_AbiWord_Preferences"},
	{AP_MENU_ID_TOOLS_LANGUAGE, "Menu_AbiWord_Book"},

	{AP_MENU_ID_TABLE_INSERT, "Menu_AbiWord_Insert_Table"},
	{AP_MENU_ID_TABLE_INSERT_TABLE, "Menu_AbiWord_Insert_Table"},
	{AP_MENU_ID_TABLE_INSERT_COLUMNS_BEFORE, "Menu_AbiWord_Add_Column"},
	{AP_MENU_ID_TABLE_INSERT_COLUMNS_AFTER, "Menu_AbiWord_Add_Column"},
	{AP_MENU_ID_TABLE_INSERT_ROWS_BEFORE, "Menu_AbiWord_Add_Row"},
	{AP_MENU_ID_TABLE_INSERT_ROWS_AFTER, "Menu_AbiWord_Add_Row"},
	{AP_MENU_ID_TABLE_INSERT_SUMCOLS, "LO_Sum"},
	{AP_MENU_ID_TABLE_INSERT_SUMROWS, "LO_Sum"},
	{AP_MENU_ID_TABLE_DELETE, "Menu_AbiWord_Delete_Table"},
	{AP_MENU_ID_TABLE_DELETE_TABLE, "Menu_AbiWord_Delete_Table"},
	{AP_MENU_ID_TABLE_DELETE_COLUMNS, "Menu_AbiWord_Delete_Column"},
	{AP_MENU_ID_TABLE_DELETE_ROWS, "Menu_AbiWord_Delete_Row"},
	{AP_MENU_ID_TABLE_FORMAT, "LO_Border"},
	{AP_MENU_ID_TABLE_SELECT, "LO_Selecttable"},
	{AP_MENU_ID_TABLE_SELECT_TABLE, "LO_Selecttable"},
	{AP_MENU_ID_TABLE_SELECT_COLUMN, "LO_Selectcolumn"},
	{AP_MENU_ID_TABLE_SELECT_ROW, "LO_Selectrow"},
	{AP_MENU_ID_TABLE_SELECT_CELL, "LO_Selectcell"},
	{AP_MENU_ID_TABLE_MERGE_CELLS, "Menu_AbiWord_Merge_Cells"},
	{AP_MENU_ID_TABLE_SPLIT_CELLS, "Menu_AbiWord_Split_Cells"},
	{AP_MENU_ID_TABLE_TEXTTOTABLE, "LO_Texttotable"},
	{AP_MENU_ID_TABLE_TEXTTOTABLE_TABS, "LO_Texttotable"},
	{AP_MENU_ID_TABLE_TEXTTOTABLE_COMMAS, "LO_Texttotable"},
	{AP_MENU_ID_TABLE_TEXTTOTABLE_SPACES, "LO_Texttotable"},
	{AP_MENU_ID_TABLE_TEXTTOTABLE_ALL, "LO_Texttotable"},
	{AP_MENU_ID_TABLE_SORT, "LO_Sort"},
	{AP_MENU_ID_TABLE_SORTROWSASCEND, "LO_Sort"},
	{AP_MENU_ID_TABLE_SORTROWSDESCEND, "LO_Sort"},
	{AP_MENU_ID_TABLE_SORTCOLSASCEND, "LO_Sort"},
	{AP_MENU_ID_TABLE_SORTCOLSDESCEND, "LO_Sort"},
	{AP_MENU_ID_TABLE_TABLETOTEXT, "LO_Tabletotext"},
	{AP_MENU_ID_TABLE_TABLETOTEXTCOMMAS, "LO_Tabletotext"},
	{AP_MENU_ID_TABLE_TABLETOTEXTTABS, "LO_Tabletotext"},
	{AP_MENU_ID_TABLE_TABLETOTEXTCOMMASTABS, "LO_Tabletotext"},
	{AP_MENU_ID_TABLE_AUTOFIT, "LO_Autofit"},
	{AP_MENU_ID_TABLE_HEADING_ROWS_REPEAT, "LO_Header"},
	{AP_MENU_ID_TABLE_HEADING_ROWS_REPEAT_THIS, "LO_Header"},
	{AP_MENU_ID_TABLE_HEADING_ROWS_REPEAT_REMOVE, "LO_Removeheader"},

	{AP_MENU_ID_RDF, "LO_Xml"},
	{AP_MENU_ID_RDF_HIGHLIGHT, "LO_Highlight"},
	{AP_MENU_ID_RDF_QUERY, "Menu_AbiWord_Search"},
	{AP_MENU_ID_RDF_EDITOR, "LO_Xml"},
	{AP_MENU_ID_RDF_QUERY_XMLIDS, "LO_Xml"},
	{AP_MENU_ID_RDF_SEMITEM, "LO_Contact"},
	{AP_MENU_ID_RDF_SEMITEM_CREATEREF, "Menu_AbiWord_Hyperlink"},
	{AP_MENU_ID_RDF_SEMITEM_NEW, "LO_Contact"},
	{AP_MENU_ID_RDF_SEMITEM_NEW_CONTACT, "LO_Contact"},
	{AP_MENU_ID_RDF_SEMITEM_NEW_CONTACT_FROM_FILE, "LO_File"},
	{AP_MENU_ID_RDF_SEMITEM_RELATION, "LO_Share"},
	{AP_MENU_ID_RDF_SEMITEM_SET_AS_SOURCE, "LO_Author"},
	{AP_MENU_ID_RDF_SEMITEM_RELATED_TO_SOURCE, "LO_Share"},
	{AP_MENU_ID_RDF_SEMITEM_RELATED_TO_SOURCE_FOAFKNOWS, "LO_Contact"},
	{AP_MENU_ID_RDF_SEMITEM_FIND_RELATED, "Menu_AbiWord_Search"},
	{AP_MENU_ID_RDF_SEMITEM_FIND_RELATED_FOAFKNOWS, "Menu_AbiWord_Search"},
	{AP_MENU_ID_RDF_SEMITEM_STYLESHEET, "LO_Style"},
	{AP_MENU_ID_RDF_SEMITEM_STYLESHEET_APPLY, "LO_Style"},
	{AP_MENU_ID_RDF_SEMITEM_STYLESHEET_DISASSOCIATE, "Menu_AbiWord_Clear"},
	{AP_MENU_ID_RDF_SEMITEM_STYLESHEET_CONTACT, "LO_Contact"},
	{AP_MENU_ID_RDF_SEMITEM_STYLESHEET_CONTACT_NAME, "LO_Contact"},
	{AP_MENU_ID_RDF_SEMITEM_STYLESHEET_CONTACT_NICK, "LO_Contact"},
	{AP_MENU_ID_RDF_SEMITEM_STYLESHEET_CONTACT_NAME_PHONE, "LO_Contact"},
	{AP_MENU_ID_RDF_SEMITEM_STYLESHEET_CONTACT_NICK_PHONE, "LO_Contact"},
	{AP_MENU_ID_RDF_SEMITEM_STYLESHEET_CONTACT_NAME_HOMEPAGE_PHONE, "LO_Contact"},
	{AP_MENU_ID_RDF_SEMITEM_STYLESHEET_EVENT, "LO_Date"},
	{AP_MENU_ID_RDF_SEMITEM_STYLESHEET_EVENT_NAME, "LO_Date"},
	{AP_MENU_ID_RDF_SEMITEM_STYLESHEET_EVENT_SUMMARY, "LO_Date"},
	{AP_MENU_ID_RDF_SEMITEM_STYLESHEET_EVENT_SUMMARY_LOCATION, "LO_Date"},
	{AP_MENU_ID_RDF_SEMITEM_STYLESHEET_EVENT_SUMMARY_LOCATION_TIMES, "LO_Date"},
	{AP_MENU_ID_RDF_SEMITEM_STYLESHEET_EVENT_SUMMARY_TIMES, "LO_Date"},
	{AP_MENU_ID_RDF_SEMITEM_STYLESHEET_LOCATION, "LO_Page"},
	{AP_MENU_ID_RDF_SEMITEM_STYLESHEET_LOCATION_NAME, "LO_Page"},
	{AP_MENU_ID_RDF_SEMITEM_STYLESHEET_LOCATION_NAME_LATLONG, "LO_Page"},
	{AP_MENU_ID_RDF_SEMITEM_STYLESHEET_SETTINGS, "Menu_AbiWord_Preferences"},
	{AP_MENU_ID_RDF_ADV, "LO_Plugin"},
	{AP_MENU_ID_RDF_ADV_DUMP_FOR_POINT, "LO_Xml"},
	{AP_MENU_ID_RDF_ADV_DUMP_OBJECTS, "LO_Xml"},
	{AP_MENU_ID_RDF_ADV_TEST, "LO_Plugin"},
	{AP_MENU_ID_RDF_ADV_PLAY, "Menu_AbiWord_Execute"},

	{AP_MENU_ID_WINDOW_NEW, "LO_Window"},
	{AP_MENU_ID_WINDOW_1, "LO_Window"},
	{AP_MENU_ID_WINDOW_2, "LO_Window"},
	{AP_MENU_ID_WINDOW_3, "LO_Window"},
	{AP_MENU_ID_WINDOW_4, "LO_Window"},
	{AP_MENU_ID_WINDOW_5, "LO_Window"},
	{AP_MENU_ID_WINDOW_6, "LO_Window"},
	{AP_MENU_ID_WINDOW_7, "LO_Window"},
	{AP_MENU_ID_WINDOW_8, "LO_Window"},
	{AP_MENU_ID_WINDOW_9, "LO_Window"},
	{AP_MENU_ID_WINDOW_MORE, "LO_Window"},

	{AP_MENU_ID_HELP_ABOUT, "Menu_AbiWord_About"},
	{AP_MENU_ID_HELP_CONTENTS, "Menu_AbiWord_Help"},
	{AP_MENU_ID_HELP_SEARCH, "Menu_AbiWord_Search"},
	{AP_MENU_ID_HELP_CREDITS, "Menu_AbiWord_Credits"},

	{(XAP_Menu_Id)0, ""}
};



/*

*/
static void appendMenuText(wchar_t * buf, size_t cap, const wchar_t * extra)
{
	if (!buf || !extra || !*extra)
		return;
	size_t used = wcslen(buf);
	if (used >= cap)
		return;
	wcsncpy(buf + used, extra, cap - used);
	buf[cap] = 0;
}

static const wchar_t * _ev_GetLabelName(XAP_Win32App * pWin32App,
									 XAP_Frame * /*pFrame*/, 
									 const EV_EditEventMapper * pEEM,
									 const EV_Menu_Action * pAction,
									 EV_Menu_Label * pLabel) 
{
	const char * szLabelName_utf8;
	
	if (pAction->hasDynamicLabel())
		szLabelName_utf8 = pAction->getDynamicLabel(pLabel);
	else
		szLabelName_utf8 = pLabel->getMenuLabel();

	if (!szLabelName_utf8 || !*szLabelName_utf8)
		return nullptr;

	UT_Win32LocaleString str;
	str.fromUTF8 (szLabelName_utf8);

	const char * szShortcut_ascii = nullptr;
	UT_Win32LocaleString shortcut;

	if (pEEM)
	{
		// see if this has an associated keybinding
		const char * szMethodName = pAction->getMethodName();

		if (szMethodName)
		{
			const EV_EditMethodContainer * pEMC = pWin32App->getEditMethodContainer();
			UT_return_val_if_fail(pEMC, nullptr);

			EV_EditMethod * pEM = pEMC->findEditMethodByName(szMethodName);
			UT_ASSERT(pEM);					// make sure it's bound to something

			szShortcut_ascii = pEEM->getShortcutFor(pEM);
		}
	}

	// A recent-file path can be longer than this buffer. Copy only what fits.
	static wchar_t buf[256];
	const size_t cap = G_N_ELEMENTS(buf) - 1;
	buf[0] = 0;
	wcsncpy(buf, str.c_str(), cap);
	buf[cap] = 0;

	if (pAction->raisesDialog())
		appendMenuText(buf, cap, L"...");

	if (szShortcut_ascii && *szShortcut_ascii)
	{
		shortcut.fromASCII(szShortcut_ascii);
		appendMenuText(buf, cap, L"\t");
		appendMenuText(buf, cap, shortcut.c_str());
	}

	return buf;
}



/*****************************************************************/

EV_Win32Menu::EV_Win32Menu(XAP_Win32App * pWin32App,
						   const EV_EditEventMapper * pEEM,
						   const char * szMenuLayoutName,
						   const char * szMenuLabelSetName)
:	EV_Menu(pWin32App, pWin32App->getEditMethodContainer(), szMenuLayoutName, szMenuLabelSetName),
	m_pWin32App(pWin32App),
	m_pEEM(pEEM),
	m_myMenu(nullptr),
	m_iDIR(0)
{
		    
	m_bTrack = false;
	m_hFont = nullptr;
	m_fontDpi = 0;

	m_nBitmapCX = BITMAP_WITDH + 2;
	m_nBitmapCY = BITMAP_HEIGHT + 2;

	UT_return_if_fail( m_pMenuLabelSet );
	
	UT_Language l;
	if(l.getDirFromCode(m_pMenuLabelSet->getLanguage().c_str()) == UTLANG_RTL)
		m_iDIR = DT_RTLREADING;

	HDC screen = GetDC(nullptr);
	int dpi = 96;
	if (screen)
	{
		dpi = GetDeviceCaps(screen, LOGPIXELSX);
		ReleaseDC(nullptr, screen);
	}
	refreshForDpi(dpi);
}

void EV_Win32Menu::refreshForDpi(int dpi)
{
	if (dpi < 96)
		dpi = 96;
	if (m_hFont && m_fontDpi == dpi)
		return;
	if (m_hFont)
	{
		DeleteObject(m_hFont);
		m_hFont = nullptr;
	}
	NONCLIENTMETRICSW ncm;
	if (UT_Win32GetNonClientMetrics(&ncm, dpi))
		m_hFont = CreateFontIndirectW(&ncm.lfMenuFont);
	m_fontDpi = m_hFont ? dpi : 0;
}

void	EV_Win32Menu::destroy()
{		

	UT_VECTOR_PURGEALL(EV_Menu_Item*,m_vecItems);
	m_vecItems.clear();

	for (std::vector<HBITMAP>::const_iterator i = m_vechBitmaps.begin();
		 i != m_vechBitmaps.end(); ++i)
	{
		if (*i)
			DeleteObject(*i);
	}
	m_vechBitmaps.clear();
	m_mapMenuBitmaps.clear();

	if (m_myMenu)
	{
		DestroyMenu(m_myMenu);
		m_myMenu = nullptr;
	}

	if (m_hFont)
	{
		DeleteObject(m_hFont);
		m_hFont = nullptr;
	}
	
}

EV_Win32Menu::~EV_Win32Menu()
{	
	// we let the derived classes handle destruction of m_myMenu if appropriate.
	// TODO: this is never colld
	
	for (std::vector<HBITMAP>::const_iterator i = m_vechBitmaps.begin(); 
		 i != m_vechBitmaps.end(); ++i)
	{
		if (*i)
			DeleteObject (*i);
	}
	m_vechBitmaps.clear();
	m_mapMenuBitmaps.clear();
}

bool EV_Win32Menu::onCommand(AV_View * pView,
							 HWND /*hWnd*/, WPARAM wParam)         
{
	// TODO do we need the hWnd parameter....

	// map the windows WM_COMMAND command-id into one of our XAP_Menu_Id.
	// we don't need to range check it, getAction() will puke if it's
	// out of range.
	
	XAP_Menu_Id id = MenuIdFromWmCommand(LOWORD(wParam));

	// user selected something from the menu.
	// invoke the appropriate function.
	// return true iff handled.

	const EV_Menu_ActionSet * pMenuActionSet = m_pWin32App->getMenuActionSet();
	UT_return_val_if_fail(pMenuActionSet, false);

	const EV_Menu_Action * pAction = pMenuActionSet->getAction(id);
	if (!pAction)
		return false;

	const char * szMethodName = pAction->getMethodName();
	UT_return_val_if_fail(szMethodName, false);
	
	const EV_EditMethodContainer * pEMC = m_pWin32App->getEditMethodContainer();
	UT_return_val_if_fail(pEMC, false);

	EV_EditMethod * pEM = pEMC->findEditMethodByName(szMethodName);
	UT_ASSERT(pEM);						// make sure it's bound to something

	UT_String script_name(pAction->getScriptName());
	invokeMenuMethod(pView, pEM, script_name);
	return true;
}

/*

	Create a Win32 menu from the info provided.

*/
bool EV_Win32Menu::synthesizeMenu(XAP_Frame * pFrame, HMENU menuRoot)
{	
	bool bResult = false;
	
	const EV_Menu_ActionSet * pMenuActionSet = m_pWin32App->getMenuActionSet();
	UT_ASSERT(pMenuActionSet);
	
	const UT_uint32 nrLabelItemsInLayout = m_pMenuLayout->getLayoutItemCount();
	UT_ASSERT(nrLabelItemsInLayout > 0);

	// we keep a stack of the submenus so that we can properly
	// parent the menu items and deal with nested pull-rights.
	
	std::vector<HMENU> stack;
	stack.push_back(menuRoot);
	//UT_DEBUGMSG(("Menu::synthesize [menuRoot 0x%08lx]\n",menuRoot));
	
	for (UT_uint32 k=0; (k < nrLabelItemsInLayout); k++)
	{
		
		EV_Menu_LayoutItem * pLayoutItem = m_pMenuLayout->getLayoutItem(k);
		UT_continue_if_fail(pLayoutItem);

		XAP_Menu_Id id = pLayoutItem->getMenuId();
		const EV_Menu_Action * pAction = pMenuActionSet->getAction(id);
		UT_ASSERT(pAction);
		EV_Menu_Label * pLabel = m_pMenuLabelSet->getLabel(id);
		UT_ASSERT(pLabel);

		// get the name for the menu item

		const wchar_t * szLabelName = _ev_GetLabelName(m_pWin32App,pFrame,m_pEEM,pAction,pLabel);
		
		switch (pLayoutItem->getMenuLayoutFlags())
		{
		case EV_MLF_BeginSubMenu:
			UT_ASSERT(szLabelName && *szLabelName);
			// Fall Thru Intended

		case EV_MLF_Normal:
			{
				UT_DEBUGMSG(("menu::synthesize [name %s]\n",szLabelName));

				HMENU m = nullptr;
				bResult = !stack.empty();
				if (bResult)
					m = stack.back();
				UT_ASSERT(bResult);

				// set standard flags on the item, we'll update the
				// state on an onInitMenu().
				// map our XAP_Menu_Id into a windows WM_COMMAND id.
				
				UINT flags = MF_STRING | MF_ENABLED | MF_UNCHECKED;
				UINT u = WmCommandFromMenuId(id);

				/*
					This a menu bar
				*/
				if (pLayoutItem->getMenuLayoutFlags() == EV_MLF_BeginSubMenu)
				{
					HMENU sub = CreateMenu();
					UT_ASSERT(sub);

					UT_DEBUGMSG(("menu::synthesize [name %s][subMenu 0x%08lx] %u\n",szLabelName,u, static_cast<unsigned>(stack.size())));

					flags |= MF_POPUP;
					stack.push_back(sub);
					// AppendMenuW takes UINT_PTR. A USER handle keeps 32 significant
					// bits and is sign-extended. Storing it in a UINT zero-extends it.
					const UINT_PTR popupId = reinterpret_cast<UINT_PTR>(sub);
					
					if (szLabelName && *szLabelName)					
					{
						EV_Menu_Item*	item = new EV_Menu_Item;
						item->id = id;					
						item->pMenu= this;							
						
						wcsncpy(item->szText, szLabelName, G_N_ELEMENTS(item->szText) - 1);
						item->szText[G_N_ELEMENTS(item->szText) - 1] = 0;					
						m_vecItems.addItem(item);
							
						if (!m_bTrack && stack.size()==2)
							AppendMenuW(m, flags, popupId, szLabelName);
						else {
							if (UT_IsWinVista()) {
								AppendMenuW(m, flags|MF_STRING, popupId, szLabelName);
								_setBitmapforID(m, id, popupId);
							}
							else
								AppendMenuW(m, flags|MF_OWNERDRAW, popupId, (const wchar_t*) item);
						}
					}		
						
				}
				else
				{					
					if (szLabelName && *szLabelName)
					{		  
						EV_Menu_Item*	item = new EV_Menu_Item;
						item->id = id;
						item->pMenu= this;						
						wcsncpy(item->szText, szLabelName, G_N_ELEMENTS(item->szText) - 1);
						item->szText[G_N_ELEMENTS(item->szText) - 1] = 0;
						m_vecItems.addItem(item);
						
						if (UT_IsWinVista()) {                                      //todo: code for windows vista : investigate this
							AppendMenuW(m, MF_STRING, u, szLabelName);
							_setBitmapforID (m, id, u);
						}
						else
							AppendMenuW(m, MF_OWNERDRAW, u, (const wchar_t*) item);
					}
				}

				
			}
			break;
	
		case EV_MLF_EndSubMenu:
			{				
				bResult = !stack.empty();
				if (bResult)
					stack.pop_back();
				UT_ASSERT(bResult);
			}
			break;
			
		case EV_MLF_Separator:
			{
				HMENU m = nullptr;
				bResult = !stack.empty();
				if (bResult)
					m = stack.back();
				UT_ASSERT(bResult);
				UT_ASSERT(m);

				AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
				UT_DEBUGMSG(("menu::synthesize [separator appended to submenu 0x%08lx]\n",m));
			}
			break;

		case EV_MLF_BeginPopupMenu:
		case EV_MLF_EndPopupMenu:
			break;

		default:
			UT_ASSERT_HARMLESS(0);
			break;
		}
	}

#ifdef DEBUG
	HMENU wDbg = nullptr;
	bResult = !stack.empty();
	if (bResult)
	{
		wDbg = stack.back();
		stack.pop_back();
	}
	UT_ASSERT(bResult);
	UT_ASSERT(wDbg == menuRoot);
#endif

	return true;
}

bool EV_Win32Menu::onInitMenu(XAP_Frame * pFrame, AV_View * pView, HWND /*hWnd*/, HMENU hMenuBar)
{
	// deal with WM_INITMENU.

	if (hMenuBar != m_myMenu)			// these are not different when they
		return false;				// right-click on us on the menu bar.
	
	const EV_Menu_ActionSet * pMenuActionSet = m_pWin32App->getMenuActionSet();
	UT_uint32 nrLabelItemsInLayout = m_pMenuLayout->getLayoutItemCount();
	

	UT_uintptr pos = 0;
	bool bResult = false;
	std::vector<UT_uintptr> stackPos;
	stackPos.push_back(pos);
	std::vector<HMENU> stackMenu;
	stackMenu.push_back(hMenuBar);

	HMENU mTemp;
	HMENU m = hMenuBar;
	//UT_DEBUGMSG(("menu::onInitMenu: [menubar 0x%08lx]\n",m));

	for (UT_uint32 k=0; (k < nrLabelItemsInLayout); k++)
	{
		EV_Menu_LayoutItem * pLayoutItem = m_pMenuLayout->getLayoutItem(k);
		XAP_Menu_Id id = pLayoutItem->getMenuId();
		const EV_Menu_Action * pAction = pMenuActionSet->getAction(id);
		EV_Menu_Label * pLabel = m_pMenuLabelSet->getLabel(id);

		UINT cmd = WmCommandFromMenuId(id);

		switch (pLayoutItem->getMenuLayoutFlags())
		{
		case EV_MLF_Normal:
			{
				// see if we need to enable/disable and/or check/uncheck it.
				
				UINT uEnable = MF_BYCOMMAND | MF_ENABLED;
				UINT uCheck = MF_BYCOMMAND | MF_UNCHECKED;
				UINT uBold = 0;
				if (pAction->hasGetStateFunction())
				{
 					EV_Menu_ItemState mis = pAction->getMenuItemState(pView);
					if (mis & EV_MIS_Gray)
						uEnable |= MF_GRAYED;
					if (mis & EV_MIS_Toggled)
						uCheck |= MF_CHECKED;
					if (mis & EV_MIS_Bold)
						uBold = MFS_DEFAULT;
				}

				if (!pAction->hasDynamicLabel())
				{
					// if no dynamic label, all we need to do
					// is enable/disable and/or check/uncheck it.
					pos++;

					// need to check before disabling ...
					CheckMenuItem(hMenuBar,cmd,uCheck);
					EnableMenuItem(hMenuBar,cmd,uEnable);
					break;
				}

				// get the current menu info for this item.
				
				MENUITEMINFOW mif;
				wchar_t bufMIF[128];
				mif.cbSize = sizeof(mif);
				mif.dwTypeData = bufMIF;
				mif.cch = G_N_ELEMENTS(bufMIF)-1;
				mif.fMask = MIIM_STATE | MIIM_TYPE | MIIM_ID;
				BOOL bPresent = GetMenuItemInfoW(hMenuBar,cmd,FALSE,&mif);

				// this item has a dynamic label...
				// compute the value for the label.
				// if it is blank, we remove the item from the menu.

				const wchar_t * szLabelName = _ev_GetLabelName(m_pWin32App,pFrame,m_pEEM,pAction,pLabel);

				BOOL bRemoveIt = (!szLabelName || !*szLabelName);

				if (bRemoveIt)			// we don't want it to be there
				{
					if (bPresent)
						RemoveMenu(hMenuBar,cmd,MF_BYCOMMAND);
					
					break;
				}

				// we want the item in the menu.
				pos++;			

				if (bPresent)			// just update the label on the item.
				{					
					// dynamic label has changed
					XAP_Menu_Id labelId = MenuIdFromWmCommand(cmd);
					EV_Menu_Item*	item;
					for(UT_sint32 i = 0; i< m_vecItems.getItemCount(); i++)
					{
						item = (EV_Menu_Item*)m_vecItems.getNthItem(i);
						if (labelId==item->id)
						{
							wcsncpy(item->szText, szLabelName, G_N_ELEMENTS(item->szText) - 1);
						item->szText[G_N_ELEMENTS(item->szText) - 1] = 0;					
							//UT_DEBUGMSG(("Menu changing text->%s\n",szLabelName));
							if (UT_IsWinVista()) 
							{
								mif.fState = uCheck | uEnable | uBold;
								mif.fType = MFT_STRING;
								mif.dwTypeData = (LPWSTR)szLabelName;
								SetMenuItemInfoW (hMenuBar,cmd,FALSE,&mif);
							}
							break;
						}
					}
				}
				else	
				{
					EV_Menu_Item* item = nullptr;
					for (UT_sint32 i = 0; i < m_vecItems.getItemCount(); ++i)
					{
						EV_Menu_Item* existing = (EV_Menu_Item*)m_vecItems.getNthItem(i);
						if (existing && existing->id == id)
						{
							item = existing;
							break;
						}
					}
					if (!item)
					{
						item = new EV_Menu_Item;
						item->id = id;
						item->pMenu = this;
						m_vecItems.addItem(item);
					}
					wcsncpy(item->szText, szLabelName, G_N_ELEMENTS(item->szText) - 1);
					item->szText[G_N_ELEMENTS(item->szText) - 1] = 0;
					//UT_DEBUGMSG(("Menu adding menu->%s\n",szLabelName));
										
					if (UT_IsWinVista()) {
						AppendMenuW(m, MF_STRING, cmd, szLabelName);
						_setBitmapforID(m, id, cmd);
					}
					else
						AppendMenuW(m, MF_OWNERDRAW,cmd, (const wchar_t*) item);
				}
				
				EV_Menu_ItemState mis = pAction->getMenuItemState(pView);
				
				if (mis & EV_MIS_Gray)
						uEnable |= MF_GRAYED;
						
				if (mis & EV_MIS_Toggled)
						uCheck |= MF_CHECKED;
						
				EnableMenuItem(m,cmd,uEnable);
				CheckMenuItem(m,cmd,uCheck);
				
			}
			break;
	
		case EV_MLF_Separator:
			pos++;
			break;

		case EV_MLF_BeginSubMenu:
			mTemp = m;
			pos++;
			stackMenu.push_back(mTemp);
			stackPos.push_back(pos);

			m = GetSubMenu(mTemp, pos-1);
			//UT_DEBUGMSG(("menu::onInitMenu: [menu 0x%08lx] at [pos %d] has [submenu 0x%08lx]\n",mTemp,pos-1,m));
			//UT_ASSERT(m);
			pos = 0;
			break;

		case EV_MLF_EndSubMenu:
			bResult = !stackMenu.empty();
			if (bResult)
			{
				mTemp = stackMenu.back();
				stackMenu.pop_back();
			}
			UT_ASSERT(bResult);
			bResult = !stackPos.empty();
			if (bResult)
			{
				pos = stackPos.back();
				stackPos.pop_back();
			}
			UT_ASSERT(bResult);

			//UT_DEBUGMSG(("menu::onInitMenu: endSubMenu [popping to menu 0x%08lx pos %d] from 0x%08lx\n",mTemp,pos,m));
			m = mTemp;
			break;

		case EV_MLF_BeginPopupMenu:
		case EV_MLF_EndPopupMenu:
			break;
			
		default:
			UT_ASSERT_HARMLESS(0);
			break;
		}
	}

#ifdef DEBUG
	HMENU wDbg = nullptr;
	bResult = !stackMenu.empty();
	if (bResult)
	{
		wDbg = stackMenu.back();
		stackMenu.pop_back();
	}
	UT_ASSERT(bResult);
	UT_ASSERT(wDbg == hMenuBar);
#endif
		
 	return true;
}

/*
	Caller should DeleteObject the handle returned	
*/
HBITMAP EV_Win32Menu::_loadBitmap(XAP_Menu_Id id, int width, int height, const UT_RGBColor & color)
{
	
	HBITMAP hBitmap = nullptr;
	const char * iconName = nullptr;
	const EV_Menu_Bitmap * pBitmaps = s_bitmaps;

	for (; pBitmaps->id; pBitmaps++)
	{
		if (pBitmaps->id == id)
		{
			iconName = pBitmaps->szName;
			break;
		}
	}

	if (!iconName)
	{
		const EV_Menu_ActionSet * actions = m_pWin32App ? m_pWin32App->getMenuActionSet() : nullptr;
		const EV_Menu_Action * action = actions ? actions->getAction(id) : nullptr;
		if (action)
			iconName = action->getIconName();
	}

	if (!iconName || !*iconName)
		return hBitmap;

	if (!color.isTransparent())
		XAP_Win32Toolbar_Icons::getBitmapForIcon(GetDesktopWindow(), width, height, &color, iconName, &hBitmap);
	else
		XAP_Win32Toolbar_Icons::getAlphaBitmapForIcon(GetDesktopWindow(), width, height, iconName, &hBitmap);
	
	return hBitmap; 
}

// Sets the Bitmap in Windows Vista
void EV_Win32Menu::_setBitmapforID (HMENU hMenu, XAP_Menu_Id id, UINT_PTR cmd)
{
	HBITMAP hBitmap = nullptr;
	std::map<XAP_Menu_Id, HBITMAP>::const_iterator found = m_mapMenuBitmaps.find(id);
	if (found != m_mapMenuBitmaps.end())
	{
		hBitmap = found->second;
	}
	else
	{
		UT_RGBColor Color;
		Color.m_bIsTransparent = true;
		hBitmap = EV_Win32Menu::_loadBitmap(id, BITMAP_WITDH, BITMAP_HEIGHT, Color);
		if (hBitmap != nullptr)
		{
			m_vechBitmaps.push_back(hBitmap);
			m_mapMenuBitmaps[id] = hBitmap;
		}
	}

	if (hBitmap != nullptr) {
		// SetMenuItemBitmaps draws a color bitmap as-is. Transparent pixels
		// are black, so the icon sits in a black square. hbmpItem blends them.
		MENUITEMINFOW info;
		memset(&info, 0, sizeof(info));
		info.cbSize = sizeof(info);
		info.fMask = MIIM_BITMAP;
		info.hbmpItem = hBitmap;
		SetMenuItemInfoW(hMenu, cmd, FALSE, &info);
	}
}

/*
	It tells us which are menu bars
*/
bool EV_Win32Menu::_isAMenuBar(XAP_Menu_Id id, HMENU hMenu)
{
	XAP_Menu_Id ids[] = { AP_MENU_ID_FILE, AP_MENU_ID_EDIT, AP_MENU_ID_VIEW,AP_MENU_ID_INSERT,
		AP_MENU_ID_TOOLS, AP_MENU_ID_WINDOW, AP_MENU_ID_HELP,AP_MENU_ID_TABLE, AP_MENU_ID_FORMAT, (XAP_Menu_Id)0 };

	for (UT_uint32 i=0; i<(sizeof(ids)/sizeof(XAP_Menu_Id)); i++)
	{
		if (ids[i]==id)
		{		
			
			MENUITEMINFOW menuInfo;	 
			memset (&menuInfo, 0, sizeof(MENUITEMINFOW));
			menuInfo.cbSize = sizeof(MENUITEMINFOW);
			menuInfo.fMask = MIIM_DATA;
			GetMenuItemInfoW(hMenu, 0, TRUE, &menuInfo);		
			EV_Menu_Item*	item = (EV_Menu_Item *) menuInfo.dwItemData;            			           				

			if (item && id==item->id)		
			{
				UT_DEBUGMSG(("EV_Win32Menu::_isAMenuBar->%s, 1\n", item->szText));
				return true;
			}
		}
	}

	UT_DEBUGMSG(("EV_Win32Menu::_isAMenuBar-> 0\n"));
	return false;
}



/*
	Process message WM_MEASUREITEM                          
*/
void EV_Win32Menu::onMeasureItem(HWND hwnd, WPARAM /*wParam*/, LPARAM lParam)
{
	SIZE size;
	memset(&size, 0, sizeof(size));
	LPMEASUREITEMSTRUCT lpmis = (LPMEASUREITEMSTRUCT) lParam;
	if (!lpmis)
		return;
	HDC hdc = GetDC(hwnd);
	if (!hdc)
		return;

	EV_Menu_Item*	item = (EV_Menu_Item *) lpmis->itemData;
	if (!item || !item->szText)
	{
		ReleaseDC(hwnd, hdc);
		return;
	}
	HFONT hfontOld = m_hFont ? (HFONT) SelectObject(hdc, m_hFont) : nullptr;

	// Retrieve the width and height of the item's string 
	if (!GetTextExtentPoint32W(hdc, item->szText, lstrlenW(item->szText), &size))
	{
		size.cx = 0;
		size.cy = 0;
	} 
		
	if (size.cy < static_cast<LONG>(item->pMenu->m_nBitmapCY))
		lpmis->itemHeight = item->pMenu->m_nBitmapCY;
	else
		lpmis->itemHeight = size.cy;

	lpmis->itemWidth =  size.cx + item->pMenu->m_nBitmapCX + SPACE_ICONTEXT;

	if (hfontOld)
		SelectObject(hdc, hfontOld);
	ReleaseDC(hwnd, hdc); 
}

/*
	Process message  WM_MENUCHAR. Process the hotkey messages	
*/
LPARAM EV_Win32Menu::onMenuChar(HWND /*hwnd*/, WPARAM wParam, LPARAM lParam)
{	
	HMENU hMenu = (HMENU) lParam;
	MENUITEMINFOW	menuInfo;
	int nItems = GetMenuItemCount(hMenu);

	for (int i=0; i<nItems; i++)
	{					
		memset (&menuInfo, 0, sizeof(MENUITEMINFOW));
		menuInfo.cbSize = sizeof(MENUITEMINFOW);
		menuInfo.fMask = MIIM_DATA;

		GetMenuItemInfoW(hMenu, i, TRUE, &menuInfo);		

		EV_Menu_Item*	item = (EV_Menu_Item *) menuInfo.dwItemData;            			           	

		if (item && item->szText)
		{
			const wchar_t * pHotKeyPos = wcschr(item->szText, L'&');
			if (pHotKeyPos && pHotKeyPos[1])
			{
				const wchar_t n = (wchar_t)(wParam & 0x000000ff);
				if (towupper(pHotKeyPos[1]) == towupper(n))
					return MAKELRESULT(i, MNC_EXECUTE);
			}
		}			
	}

	return MNC_IGNORE;
}

/*
	Process message  WM_DRAWITEM
*/
void EV_Win32Menu::onDrawItem(HWND /*hwnd*/, WPARAM /*wParam*/, LPARAM lParam)
{

	LPDRAWITEMSTRUCT lpdis = (LPDRAWITEMSTRUCT) lParam;
	if (!lpdis)
		return;
	EV_Menu_Item*	item = (EV_Menu_Item *) lpdis->itemData;
	if (!item || !item->szText)
		return;
    COLORREF crText;    
    COLORREF crBkgnd = 0;
	DWORD dwColor;
	RECT rect;
	HBITMAP hBitmap;	
	int colorID;
	UT_ASSERT(lpdis->CtlType==ODT_MENU); 	
    UT_Win32LocaleString sTextRight, sTextLeft;					

	/* Rect to draw the text */	
	rect.top =  lpdis->rcItem.top;
	rect.bottom = lpdis->rcItem.bottom;

	if(m_iDIR)
	{
		rect.right = lpdis->rcItem.right - 20;
		rect.left = lpdis->rcItem.left;
	}
	else
	{
		rect.right = lpdis->rcItem.right;
		rect.left = /*m_nBitmapCX*/ 20  + lpdis->rcItem.left;
	}
	
	
	if (lpdis->itemState & ODS_GRAYED) 
		crText = SetTextColor(lpdis->hDC, GetSysColor(COLOR_GRAYTEXT));	
	else
		if (lpdis->itemState & ODS_SELECTED)
			crText = SetTextColor(lpdis->hDC, GetSysColor(COLOR_HIGHLIGHTTEXT));		
		else
			crText = SetTextColor(lpdis->hDC, GetSysColor(COLOR_MENUTEXT));			

	if (lpdis->itemState & ODS_SELECTED)     
		colorID = COLOR_HIGHLIGHT;
	else
		colorID = COLOR_MENU;		
	
	dwColor = GetSysColor(colorID);
	
	UT_RGBColor Color(GetRValue(dwColor),GetGValue(dwColor),GetBValue(dwColor));
	hBitmap =  EV_Win32Menu::_loadBitmap(item->id, BITMAP_WITDH, BITMAP_HEIGHT, Color);

	if (hBitmap)	
	{
		BITMAP bitmap;		
		GetObjectA(hBitmap, sizeof(BITMAP), &bitmap);	
	}	

	/*Draw the background of the item*/
	FillRect(lpdis->hDC, &lpdis->rcItem, GetSysColorBrush(colorID));

	if(m_iDIR)
		rect.right = rect.right - SPACE_ICONTEXT;
	else
		rect.left = rect.left;
		
	
	// Select the font associated with the item into the 
    // item's device context, and then draw the string.  
    HFONT hfontOld = item->pMenu->m_hFont
		? (HFONT) SelectObject(lpdis->hDC, item->pMenu->m_hFont)
		: nullptr; 

	
	/* 
		Process tabs
	*/	
	wchar_t* pTabPos = wcschr (item->szText, L'\t');

	if (pTabPos)
	{
		wchar_t szTmp[255];
		const ptrdiff_t leftCount = pTabPos - item->szText;
		const size_t leftLen = leftCount > 0 ? static_cast<size_t>(leftCount) : 0;
		const size_t leftCopy = leftLen < 254 ? leftLen : 254;
		memcpy(szTmp, item->szText, leftCopy * sizeof(wchar_t));
		szTmp[leftCopy] = 0;
		sTextLeft.fromLocale (szTmp);

		const wchar_t * right = pTabPos + 1;
		const size_t rightLen = wcslen(right);
		const size_t rightCopy = rightLen < 254 ? rightLen : 254;
		memcpy(szTmp, right, rightCopy * sizeof(wchar_t));
		szTmp[rightCopy] = 0;
		sTextRight.fromLocale (szTmp);
		sTextRight.appendASCII ("  ");
	}
	else
		sTextLeft.fromLocale (item->szText);	
	
	if (lpdis->itemState & ODS_SELECTED)
	{
		const COLORREF previous = SetBkColor(lpdis->hDC, GetSysColor(COLOR_HIGHLIGHT));
		crBkgnd = (previous == CLR_INVALID) ? CLR_INVALID : previous;
	}

	/* Draw text*/
	if(m_iDIR)
	{
		DrawTextW(lpdis->hDC, sTextLeft.c_str(),  sTextLeft.length() , &rect,
				 DT_RIGHT | DT_VCENTER | DT_SINGLELINE | m_iDIR);
		
		if (sTextRight.length())
			DrawTextW(lpdis->hDC, sTextRight.c_str(), sTextRight.length(), &rect,
					 DT_LEFT | DT_VCENTER | DT_SINGLELINE | m_iDIR);
	}
	else
	{
		DrawTextW(lpdis->hDC, sTextLeft.c_str(),  sTextLeft.length() , &rect,
				 DT_LEFT | DT_VCENTER | DT_SINGLELINE | m_iDIR);
		
		if (sTextRight.length())
			DrawTextW(lpdis->hDC, sTextRight.c_str(), sTextRight.length(), &rect,
					 DT_RIGHT | DT_VCENTER | DT_SINGLELINE | m_iDIR);
	}
	
	if ((lpdis->itemState & ODS_SELECTED) && crBkgnd != CLR_INVALID)
		SetBkColor(lpdis->hDC, crBkgnd); 
	
	/* Draw bitmap*/
	if (hBitmap)
	{	
					  		
		HDC hdcMem = CreateCompatibleDC(lpdis->hDC);
		if (hdcMem)
		{
			HGDIOBJ previous = SelectObject(hdcMem,(void *)hBitmap);

			if(m_iDIR)
				BitBlt(lpdis->hDC, lpdis->rcItem.right - 20, lpdis->rcItem.top+1, BITMAP_WITDH, BITMAP_HEIGHT, hdcMem, 0, 0, SRCCOPY );
			else
				BitBlt(lpdis->hDC, lpdis->rcItem.left+1, lpdis->rcItem.top+1, BITMAP_WITDH, BITMAP_HEIGHT, hdcMem, 0, 0, SRCCOPY );

			if (previous)
				SelectObject(hdcMem, previous);
			DeleteDC(hdcMem);
		}
				
	}
	else 
		if (lpdis->itemState & ODS_CHECKED)
		{	
			UINT nWidth = GetSystemMetrics(SM_CXMENUCHECK);
			UINT nHeight = GetSystemMetrics(SM_CYMENUCHECK);
			RECT r;
			HBITMAP bm = CreateBitmap(nWidth, nHeight, 1, 1, nullptr );
			HDC hdcMem = bm ? CreateCompatibleDC(lpdis->hDC) : nullptr;

			const COLORREF checkBk = SetBkColor(lpdis->hDC, GetSysColor(colorID));
			if (bm && hdcMem)
			{
				HGDIOBJ previous = SelectObject(hdcMem, bm);
				SetRect(&r, 0, 0, nWidth, nHeight);
				DrawFrameControl(hdcMem, &r, DFC_MENU,DFCS_MENUCHECK);

				if(m_iDIR)
					BitBlt(lpdis->hDC, lpdis->rcItem.right - 20, lpdis->rcItem.top+2, nWidth, nHeight,
						   hdcMem, 0, 0, SRCCOPY);
				else
					BitBlt(lpdis->hDC, lpdis->rcItem.left+2, lpdis->rcItem.top+2, nWidth, nHeight,
						   hdcMem, 0, 0, SRCCOPY);

				if (previous)
					SelectObject(hdcMem, previous);
			}
			if (hdcMem)
				DeleteDC(hdcMem);
			if (bm)
				DeleteObject(bm);
			if (checkBk != CLR_INVALID)
				SetBkColor(lpdis->hDC, checkBk);
		}	

    if (hfontOld)
		SelectObject(lpdis->hDC, hfontOld); 
    if (crText != CLR_INVALID)
		SetTextColor(lpdis->hDC, crText); 	
	if (hBitmap)
		DeleteObject(hBitmap);	
}

								
bool EV_Win32Menu::onMenuSelect(XAP_Frame * pFrame, AV_View * /*pView*/,
								   HWND /*hWnd*/, HMENU hMenu, WPARAM wParam)
{
	UINT nItemID = (UINT)LOWORD(wParam);
	UINT nFlags  = (UINT)HIWORD(wParam);

	if ( (nFlags==0xffff) && (hMenu == nullptr) )
	{
		//UT_DEBUGMSG(("ClearMessage 1\n"));
		pFrame->setStatusMessage(nullptr);
		return true;
	}

	if ( (nItemID==0) || (nFlags & (MF_SEPARATOR|MF_POPUP)) )
	{
		//UT_DEBUGMSG(("ClearMessage 2\n"));
		pFrame->setStatusMessage(nullptr);
		return true;
	}

	if (nFlags & (MF_SYSMENU))
	{
		//UT_DEBUGMSG(("SysMenu [%x]\n",nItemID));
		pFrame->setStatusMessage(nullptr);
		return true;
	}
	
	XAP_Menu_Id id = MenuIdFromWmCommand(nItemID);
	EV_Menu_Label * pLabel = m_pMenuLabelSet->getLabel(id);
	if (!pLabel)
	{
		//UT_DEBUGMSG(("ClearMessage 3 [%d %d]\n",nItemID,id));
		pFrame->setStatusMessage(nullptr);
		return true;
	}

	const char * szMsg = pLabel->getMenuStatusMessage();
	if (!szMsg || !*szMsg)
		szMsg = "TODO This menu item doesn't have a StatusMessage defined.";
	
	pFrame->setStatusMessage(szMsg);
	return true;
}



/*****************************************************************/
/*****************************************************************/

EV_Win32MenuBar::EV_Win32MenuBar(XAP_Win32App * pWin32App,
								 const EV_EditEventMapper * pEEM,
								 const char * szMenuLayoutName,
								 const char * szMenuLabelSetName)
	: EV_Win32Menu(pWin32App,pEEM,szMenuLayoutName,szMenuLabelSetName)
{
}

EV_Win32MenuBar::~EV_Win32MenuBar(void)
{
	destroy();
	// TODO should we destroy m_myMenu if set ??
}

bool EV_Win32MenuBar::synthesizeMenuBar(XAP_Frame * pFrame)
{
	m_myMenu = CreateMenu();

	bool bRet = synthesizeMenu(pFrame, m_myMenu);

	// when dealing with RTL interface language, we need to tell do some tricks here to
	// make the menubar to layout RTL
	// see http://www.microsoft.com/middleeast/msdn/faq.aspx
	MENUITEMINFOW mii;

	wchar_t buff[81];
	memset(buff, 0, 80 * sizeof (wchar_t));
	buff[80] = 0;
	
	mii.cbSize = sizeof(mii);
	mii.dwTypeData = buff;
	mii.fType = MF_STRING;
	mii.cch = 80;
	mii.fState = MFS_DEFAULT;
	mii.fMask = MIIM_ID | MIIM_DATA | MIIM_TYPE | MIIM_SUBMENU;
	
	if(m_iDIR && GetMenuItemInfoW(m_myMenu, 0, 1, &mii))
	{
		mii.fType |= MFT_RIGHTORDER;
		SetMenuItemInfoW(m_myMenu, 0, 1, &mii);
	}
	
	return bRet;
}

/*****************************************************************/
/*****************************************************************/

EV_Win32MenuPopup::EV_Win32MenuPopup(XAP_Win32App * pWin32App,
									 const char * szMenuLayoutName,
									 const char * szMenuLabelSetName)
	: EV_Win32Menu(pWin32App,nullptr,szMenuLayoutName,szMenuLabelSetName)
{
}

EV_Win32MenuPopup::~EV_Win32MenuPopup(void)
{
	destroy();
	
	if (m_myMenu)
		DestroyMenu(m_myMenu);
}

bool EV_Win32MenuPopup::synthesizeMenuPopup(XAP_Frame * pFrame)
{
	m_myMenu = CreatePopupMenu();

	bool bRet = synthesizeMenu(pFrame, m_myMenu);

	// when dealing with RTL interface language, we need to tell do some tricks here to
	// make the menubar to layout RTL
	// see http://www.microsoft.com/middleeast/msdn/faq.aspx
	MENUITEMINFOW mii;
	wchar_t buff[81];
	memset(buff, 0, 80 * sizeof(wchar_t));
	buff[80] = 0;

	mii.cbSize = sizeof(mii);
	mii.dwTypeData = buff;
	mii.fType = MF_STRING;
	mii.cch = 80;
	mii.fState = MFS_DEFAULT;
	mii.fMask = MIIM_ID | MIIM_DATA | MIIM_TYPE | MIIM_SUBMENU;
	
	if(m_iDIR && GetMenuItemInfoW(m_myMenu, 0, 1, &mii))
	{
		mii.fType |= MFT_RIGHTORDER;
		SetMenuItemInfoW(m_myMenu, 0, 1, &mii);
	}
	
	return bRet;
	
}

