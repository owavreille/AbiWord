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

#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "ut_png.h"
#include "ut_svg.h"
#include "ut_misc.h"
#include "ut_string.h"
#include "ut_assert.h"
#include "ut_bytebuf.h"
#include "ut_debugmsg.h"
#include "ut_string_class.h"
#include "ut_Win32LocaleString.h"

#include "xap_App.h"
#include "xap_Strings.h"
#include "xap_Win32App.h"
#include "xap_Win32FrameImpl.h"

#include "xap_Dialog_Id.h"
#include "xap_Dlg_FileOpenSaveAs.h"
#include "xap_Win32Dlg_FileOpenSaveAs.h"

#include "gr_Win32Image.h"
#include "gr_Win32Graphics.h"
#include "ap_Win32App.h"

#include "ie_types.h"
#include "ie_imp.h"
#include "ie_exp.h"
#include "ie_impGraphic.h"

#include "fg_Graphic.h"

#include "xap_Win32Resources.rc2"


#define MAX_DLG_INS_PICT_STRING 1030
/*****************************************************************/
XAP_Dialog * XAP_Win32Dialog_FileOpenSaveAs::static_constructor(XAP_DialogFactory * pFactory,
															  XAP_Dialog_Id id)
{
	XAP_Win32Dialog_FileOpenSaveAs * p = new XAP_Win32Dialog_FileOpenSaveAs(pFactory,id);
	return p;
}

XAP_Win32Dialog_FileOpenSaveAs::XAP_Win32Dialog_FileOpenSaveAs(XAP_DialogFactory * pDlgFactory,
															 XAP_Dialog_Id id)
	: XAP_Dialog_FileOpenSaveAs(pDlgFactory,id)
	, m_pPreviewImage(nullptr)
{
	m_szDefaultExtension[0] = 0;
}

XAP_Win32Dialog_FileOpenSaveAs::~XAP_Win32Dialog_FileOpenSaveAs(void)
{
	_clearPreviewCache();
}

/*****************************************************************/

/*!
 Check for the presence of a file suffix in a list of suffixes

 \param haystack List of suffixes in the form "*a; *b; *c"
 \param needle Suffix to check for
 */
static bool SuffixInList(const char *haystack, const char *needle)
{
	int l = strlen(needle);
	const char *s = strstr(haystack, needle);
	if (s)
		if (s[l] == 0 || s[l] == ';')
			return true;
	return false;
}

/*!
 Retrieve the default extension to use if user does not provide one

 \param indx -- index into the filter list
 */
const wchar_t * XAP_Win32Dialog_FileOpenSaveAs::_getDefaultExtension(UT_uint32 indx)
{
	static wchar_t abw_sfx[] = L"abw";
    char defaultExtension[DEFAULT_EXT_SIZE + 1];
	UT_Win32LocaleString def_ext;
	
	UT_uint32 end = g_strv_length((gchar **) m_szDescriptions);
	if(indx >= end)
	{
		if(m_id == XAP_DIALOG_ID_FILE_SAVE_IMAGE)
			return L"png";

		return abw_sfx;
	}
	
	// Suffixes look like "*.abw". Two characters shorter than that is not an extension.
	const char * rawSuffix = m_szSuffixes[indx];
	if (!rawSuffix || strlen(rawSuffix) <= 2)
		return abw_sfx;
	strncpy(defaultExtension, rawSuffix + 2, DEFAULT_EXT_SIZE);
	
	defaultExtension[DEFAULT_EXT_SIZE] = 0;
	
	// make sure that we get rid off the semicolon if it got copied
	char * semicolon = strchr(defaultExtension, ';');
	if(semicolon)
		*semicolon = 0;
	UT_DEBUGMSG(("Default sfx [%s], (from [%s]\n", m_szDefaultExtension,m_szSuffixes[indx]));
    def_ext.fromASCII (defaultExtension);
	wcscpy (m_szDefaultExtension, def_ext.c_str());
	return m_szDefaultExtension;
}

/*!
 Build a Windows filter list from Abi's filetypes

 \param sFilter Build the list in this string
 */
void XAP_Win32Dialog_FileOpenSaveAs::_buildFilterList(UT_String& sFilter)
{
	UT_String sAllSuffixes;

	const XAP_StringSet*  pSS	= XAP_App::getApp()->getStringSet();

	UT_ASSERT(g_strv_length((gchar **) m_szSuffixes) == g_strv_length((gchar **) m_szDescriptions));

	// measure one list, they should all be the same length
	UT_uint32 end = g_strv_length((gchar **) m_szDescriptions);

	for (UT_uint32 i = 0; i < end; i++)
	{
		// HACK for inconsistent UTF-8 api
		sFilter += m_szDescriptions[i];
		sFilter += '\004';				// include the trailing 0
		sFilter += m_szSuffixes[i];
		sFilter += '\004';				// include the trailing 0

		// extract one suffix at a time
		const char *s1 = m_szSuffixes[i];
		while (1)
		{
			const char *s2 = s1;
			while (*s2 && *s2 != ';')
				++s2;
			UT_String sSuffix(s1, s2-s1);
			// build a complete list with no repeats
			if (!SuffixInList(sAllSuffixes.c_str(), sSuffix.c_str()))
			{
				if (!sAllSuffixes.empty())
					sAllSuffixes += "; ";
				sAllSuffixes += sSuffix;
			}
			if (*s2 == 0)
				break;
			s1 = s2 + 1;
			while (*s1 == ' ')
				++s1;
		}
	}

	// all supported files filter
	sFilter += m_id == XAP_DIALOG_ID_INSERT_PICTURE ? pSS->getValue(XAP_STRING_ID_DLG_FOSA_ALLIMAGES) : pSS->getValue(XAP_STRING_ID_DLG_FOSA_ALLDOCS);
	sFilter += '\004';				// include the trailing 0
	sFilter += sAllSuffixes;
	sFilter += '\004';				// include the trailing 0

	// all files filter
	sFilter += pSS->getValue(XAP_STRING_ID_DLG_FOSA_ALL);
	sFilter += '\004';				// include the trailing 0
	sFilter += "*.*";
	sFilter += '\004';				// include the trailing 0
	
	sFilter += '\004';				// double 0 at the end
}

/*****************************************************************/

/*!  
  Gets the Windows File/Save common dialog box. To be able to get
  the places bar containing icons for commonly-used folders when we
  use hooking we need to use the new OPENFILENAME structure as defined
  in WINNT5 or better. To keep backward compability, first try
  with the new size of the structure, if it does not work it means the
  the common dialog box DLL installed in the system does not support
  the new features, and then we use the old one. In this situation, we
  user does not have a places bar capable common dialog box DLL
  anyway.
*/
BOOL  XAP_Win32Dialog_FileOpenSaveAs::GetSaveFileName_Hooked(OPENFILENAME_WIN50* lpofn, BOOL bSave)
{
		BOOL  bRslt;	
		
		lpofn->lStructSize = sizeof(OPENFILENAME_WIN50);		// Size of the new structure
		lpofn->pvReserved = nullptr;
		lpofn->dwReserved = 0;
		lpofn->FlagsEx = 0;		
		
		if (bSave)				
			bRslt = GetSaveFileNameW((OPENFILENAMEW *)lpofn);
		else
			bRslt = GetOpenFileNameW((OPENFILENAMEW *)lpofn);
		
		if (!bRslt) // Error
		{			
			DWORD dwError = CommDlgExtendedError();
			
			if (dwError==CDERR_STRUCTSIZE)	// This system does not support the place bar
			{								
					//  Try with the old one
					lpofn->lStructSize = sizeof(OPENFILENAMEW);	
					if (bSave)				
						bRslt = GetSaveFileNameW((OPENFILENAMEW *)lpofn);
					else
						bRslt = GetOpenFileNameW((OPENFILENAMEW *)lpofn);
				
			}				
		}	
		
		return bRslt;
}


static bool s_copyW(wchar_t * dest, size_t destChars, const wchar_t * src)
{
	if (!dest || destChars == 0)
		return false;
	dest[0] = 0;
	if (!src)
		return false;
	const size_t n = wcslen(src);
	if (n >= destChars)
		return false;
	memcpy(dest, src, (n + 1) * sizeof(wchar_t));
	return true;
}

/*****************************************************************/

void XAP_Win32Dialog_FileOpenSaveAs::runModal(XAP_Frame * pFrame)
{
	UT_return_if_fail(pFrame);
	UT_return_if_fail(pFrame->getFrameImpl());

	XAP_Win32App* pWin32App = static_cast<XAP_Win32App*>(XAP_App::getApp());

	HWND hFrame = static_cast<XAP_Win32FrameImpl*>(pFrame->getFrameImpl())->getTopLevelWindow();

	wchar_t szFile[MAX_DLG_INS_PICT_STRING];	// buffer for filename
	wchar_t szDir[MAX_DLG_INS_PICT_STRING];	// buffer for directory
	UT_String sFilter;
	OPENFILENAME_WIN50 ofn;						// common dialog box structure
	UT_Win32LocaleString filter;

	ZeroMemory(szFile,sizeof(szFile));
	ZeroMemory(szDir,sizeof(szDir));
	ZeroMemory(&ofn, sizeof(OPENFILENAME_WIN50));

	_buildFilterList(sFilter);
	filter.fromUTF8(sFilter.c_str()/*, sFilter.size()*/);
	LPWSTR f_cur=(LPWSTR)filter.c_str();
	// \004 is a placeholder for \000 inside string
	while (*f_cur) {
		if (*f_cur == 4) *f_cur=0;
		f_cur++;
	}

	ofn.lStructSize = sizeof(OPENFILENAMEW);		// Old size
	ofn.hwndOwner = hFrame;
	ofn.lpstrFile = szFile;
	// nMaxFile is a character count. sizeof() is the byte count and
	// GetOpenFileNameW then writes past szFile.
	ofn.nMaxFile = MAX_DLG_INS_PICT_STRING;
	ofn.lpstrFilter = filter.c_str();
	ofn.nFilterIndex = 1;
	ofn.lpstrFileTitle = nullptr;
	ofn.nMaxFileTitle = 0;
	ofn.lpstrInitialDir = nullptr;
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_HIDEREADONLY;
	ofn.lpstrDefExt = _getDefaultExtension(0);
	// DWORD keeps 32 bits. The dialog lives above 4 GiB, so the hook
	// would call methods on a truncated pointer and the process dies.
	ofn.lCustData = reinterpret_cast<LPARAM>(this);
	// use the persistence info and/or the suggested filename
	// to properly seed the dialog.

	if (m_initialPathname.empty())
	{
		// the caller did not supply initial pathname
		// (or supplied an empty one).	see if we have
		// some persistent info.

		UT_ASSERT(!m_bSuggestName);
		if (!m_persistPathname.empty())
		{
			// we have a pathname from a previous use,
			// extract the directory portion and start
			// the dialog there (but without a filename).

			// use directory(m_szPersistPathname)

			gchar * szPersist = g_filename_from_uri(m_persistPathname.c_str(), nullptr, nullptr);
			UT_Win32LocaleString path;
			path.fromUTF8 (szPersist ? szPersist : m_persistPathname.c_str());
			g_free(szPersist);

			if (s_copyW(szDir, G_N_ELEMENTS(szDir), path.c_str()))
			{
				wchar_t * bslash = wcsrchr(szDir, L'\\');
				wchar_t * fslash = wcsrchr(szDir, L'/');
				wchar_t * pLastSlash = (bslash && fslash) ? (bslash > fslash ? bslash : fslash) : (bslash ? bslash : fslash);
				if (pLastSlash)
					pLastSlash[1] = 0;
				ofn.lpstrInitialDir = szDir;
			}
		}
		else
		{
			// no initial pathname given and we don't have
			// a pathname from a previous use, so just let
			// it come up in the current working directory.
			// since we set OFN_NOCHANGEDIR we don't have to
			// to worry about where this is.
		}
	}
	else
	{
		// we have an initial pathname (the name of the document
		// in the frame that we were invoked on).  if the caller
		// wanted us to suggest a name, use the initial
		// pathname as is.	if not, use the directory portion of
		// it.	either way, we need to cut the pathname into two
		// parts -- directory and file -- for the common dlg.

		UT_Win32LocaleString uri;
		const char * szURI = g_filename_from_uri(m_initialPathname.c_str(), nullptr, nullptr);
		if(!szURI)
			szURI = g_strdup(m_initialPathname.c_str());

		uri.fromUTF8 (szURI);

		const wchar_t * full = uri.c_str();
		const wchar_t * slash = nullptr;
		if (full)
		{
			slash = wcsrchr(full, L'\\');
			if (!slash)
				slash = wcsrchr(full, L'/');
		}
		if (full && s_copyW(szDir, G_N_ELEMENTS(szDir), full))
		{
			if (slash)
				szDir[slash - full + 1] = 0;
			ofn.lpstrInitialDir = szDir;
		}

		if (m_bSuggestName)
		{
			const wchar_t * name = (full && slash) ? slash + 1 : full;
			if (s_copyW(szFile, G_N_ELEMENTS(szFile), name))
			{
				// if the file name has an extension, remove it
				// (if we don't, and the document is of a different
				// type than the one initially selected in the dialogue
				// and the user just clicks OK, we get type - extension
				// mismatch)
				wchar_t * dot = wcsrchr(szFile, L'.');
				if(dot)
					*dot = 0;
			}
		}

		FREEP(szURI);
	}

	// display the appropriate dialog box.

	BOOL bDialogResult;

	if( m_nDefaultFileType != XAP_DIALOG_FILEOPENSAVEAS_FILE_TYPE_AUTO )
	{
		// Find the index of the type we were given
		UT_uint32 iCounter, iNumTypes = g_strv_length((gchar **) m_szDescriptions);

		for( iCounter = 0; iCounter < iNumTypes; iCounter++ )
		{
			if( m_nTypeList[ iCounter ] == m_nDefaultFileType )
			{
				ofn.nFilterIndex = iCounter + 1;
				ofn.lpstrDefExt = _getDefaultExtension(iCounter);
				break;
			}
		}
	}

	const XAP_StringSet* pSS = XAP_App::getApp()->getStringSet();
    UT_Win32LocaleString title;

	switch (m_id)
	{
	case XAP_DIALOG_ID_FILE_OPEN:		
		title.fromUTF8 (pSS->getValue(XAP_STRING_ID_DLG_FOSA_OpenTitle));
		ofn.lpstrTitle = title.c_str();
		ofn.Flags |= OFN_FILEMUSTEXIST;
		ofn.nFilterIndex = g_strv_length((gchar **) m_szDescriptions) + 1;
		bDialogResult = GetOpenFileNameW((OPENFILENAMEW *)&ofn);
		break;

	case XAP_DIALOG_ID_PRINTTOFILE:
	    title.fromUTF8 (pSS->getValue(XAP_STRING_ID_DLG_FOSA_PrintToFileTitle));
		ofn.lpstrTitle = title.c_str();	
        ofn.Flags |= OFN_OVERWRITEPROMPT;
		bDialogResult = GetSaveFileNameW((OPENFILENAMEW *)&ofn);
		break;

	case XAP_DIALOG_ID_FILE_SAVEAS:
	case XAP_DIALOG_ID_FILE_SAVE_IMAGE:
		title.fromUTF8 (pSS->getValue(XAP_STRING_ID_DLG_FOSA_SaveAsTitle));
		ofn.lpstrTitle = title.c_str();
		ofn.Flags |= OFN_OVERWRITEPROMPT;
		bDialogResult = GetSaveFileName_Hooked(&ofn, TRUE);
		break;

	case XAP_DIALOG_ID_INSERT_PICTURE:
		title.fromUTF8 (pSS->getValue(XAP_STRING_ID_DLG_IP_Title));
		ofn.lpstrTitle = title.c_str();
		ofn.hInstance	   = pWin32App->getInstance();
		ofn.lpTemplateName = MAKEINTRESOURCEW(XAP_RID_DIALOG_INSERT_PICTURE);
		ofn.lpfnHook	   = s_hookInsertPicProc;
		ofn.nFilterIndex   = g_strv_length((gchar **) m_szDescriptions) + 1;
		ofn.Flags |= OFN_EXPLORER;
		ofn.Flags |= OFN_ENABLETEMPLATE;
		ofn.Flags |= OFN_ENABLEHOOK;
		bDialogResult = GetSaveFileName_Hooked(&ofn, FALSE);
		break;

	case XAP_DIALOG_ID_FILE_IMPORT:
        title.fromUTF8 (pSS->getValue(XAP_STRING_ID_DLG_FOSA_ImportTitle));
		ofn.lpstrTitle = title.c_str();
		ofn.nFilterIndex = g_strv_length((gchar **) m_szDescriptions) + 1;
		ofn.Flags |= OFN_FILEMUSTEXIST;
		bDialogResult = GetOpenFileNameW((OPENFILENAMEW *)&ofn);
		break;

	case XAP_DIALOG_ID_INSERTMATHML:
		title.fromUTF8 (pSS->getValue(XAP_STRING_ID_DLG_FOSA_InsertMath));
		ofn.lpstrTitle = title.c_str();
		ofn.nFilterIndex = g_strv_length((gchar **) m_szDescriptions) + 1;
		ofn.Flags |= OFN_FILEMUSTEXIST;
		bDialogResult = GetOpenFileNameW((OPENFILENAMEW *)&ofn);
		break;

	case XAP_DIALOG_ID_FILE_EXPORT:
		title.fromUTF8 (pSS->getValue(XAP_STRING_ID_DLG_FOSA_ExportTitle));
		ofn.lpstrTitle = title.c_str();
		ofn.Flags |= OFN_OVERWRITEPROMPT;
		bDialogResult = GetSaveFileName_Hooked(&ofn, TRUE);
		break;

	case XAP_DIALOG_ID_INSERT_FILE:
		title.fromUTF8 (pSS->getValue(XAP_STRING_ID_DLG_FOSA_InsertTitle));
		ofn.lpstrTitle = title.c_str();		
		ofn.Flags |= OFN_FILEMUSTEXIST;
		ofn.nFilterIndex = g_strv_length((gchar **) m_szDescriptions) + 1;
		bDialogResult = GetOpenFileNameW((OPENFILENAMEW *)&ofn);
		break;

	default:
		bDialogResult = FALSE;
		UT_ASSERT_HARMLESS(UT_SHOULD_NOT_HAPPEN);
		break;
	}

	// TODO how do cancels get reported...
	// TODO verify that current-working-directory is not changed...

	if (bDialogResult != FALSE)
	{
		UT_uint32 end = g_strv_length((gchar **) m_szSuffixes);
		UT_Win32LocaleString sfile;
		sfile.fromLocale (szFile);
		std::string path = sfile.utf8_str().utf8_str();

		// nFilterIndex is 1-based. The stored value is the exporter or
		// importer type, which is not the position in the list.
		m_nFileType = XAP_DIALOG_FILEOPENSAVEAS_FILE_TYPE_AUTO;
		if (ofn.nFilterIndex > 0 && ofn.nFilterIndex <= end && m_nTypeList)
			m_nFileType = m_nTypeList[ofn.nFilterIndex - 1];

		const bool naming = (m_id == XAP_DIALOG_ID_FILE_SAVEAS ||
			m_id == XAP_DIALOG_ID_FILE_EXPORT);
		if (naming && m_nFileType > 0)
		{
			std::string suffix = UT_pathSuffix(path);
			IEFileType docType = IE_Exp::fileTypeForSuffix(".doc");
			if (!suffix.empty() && !g_ascii_strcasecmp(suffix.c_str(), ".doc") &&
				docType != IEFT_Unknown)
			{
				// The name says Word. Write a Word document, even if the
				// highlighted filter was still the previous format.
				m_nFileType = docType;
			}

			// The type list is not allowed to rewrite the name while the
			// dialog is open: Windows validates that change immediately and
			// reports an error as soon as .doc is chosen. Apply the chosen
			// exporter's extension here, and only on the file name. A dot in
			// a directory (C:\a.b\file) must stay put.
			IE_ExpSniffer * sniffer = IE_Exp::snifferForFileType(static_cast<IEFileType>(m_nFileType));
			if (sniffer)
			{
				const std::string want = sniffer->getPreferredSuffix().utf8_str();
				if (!path.empty() && !want.empty() &&
					(suffix.empty() || g_ascii_strcasecmp(suffix.c_str(), want.c_str())))
				{
					const std::string origPath = path;
					UT_addOrReplacePathSuffix(path, want.c_str());
					if (path != origPath)
					{
						UT_Win32LocaleString newPathW;
						newPathW.fromUTF8(path.c_str());
						if (GetFileAttributesW(newPathW.c_str()) != INVALID_FILE_ATTRIBUTES)
						{
							const char * fmt = pSS->getValue(XAP_STRING_ID_DLG_OverwriteFile);
							UT_UTF8String msgUtf8;
							UT_UTF8String_sprintf(msgUtf8,
								fmt ? fmt : "File already exists. Overwrite file '%s'?",
								path.c_str());
							UT_Win32LocaleString msgW;
							msgW.fromUTF8(msgUtf8.utf8_str());
							int res = MessageBoxW(hFrame, msgW.c_str(), title.c_str(),
								MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
							if (res != IDYES)
							{
								m_answer = a_CANCEL;
								return;
							}
						}
					}
				}
			}
		}

		char *uri = UT_go_filename_to_uri(path.c_str());
		if (uri)
		{
			m_finalPathname = uri;
			FREEP(uri);
		}
		else if (!path.empty())
			m_finalPathname = path;
		else
		{
			UT_ASSERT_HARMLESS(UT_SHOULD_NOT_HAPPEN);
			m_finalPathname.clear();
		}

		m_answer = a_OK;
	}
	else
	{
		m_answer = a_CANCEL;
		UT_DEBUGMSG(("Didn't get a file: reason=0x%x\n", CommDlgExtendedError()));
	}
}

/*!
 Used for SaveAs case of FileOpenSaveAs dialog
 \param hDlg
 \param msg
 \param wParam
 \param lParam

 Static routine
 */
UINT_PTR CALLBACK XAP_Win32Dialog_FileOpenSaveAs::s_hookSaveAsProc(HWND /*hDlg*/, UINT msg, WPARAM /*wParam*/, LPARAM lParam)
{
	switch(msg)
	{
		case WM_NOTIFY:
			{
				const OFNOTIFYW * pNotify = reinterpret_cast<OFNOTIFYW *>(lParam);
				if (!pNotify)
					break;
				switch (pNotify->hdr.code)
				{
					case CDN_TYPECHANGE:
						// Do not touch the name, and do not send
						// CDM_SETDEFEXT. Either one makes the common dialog
						// validate the name before this notification returns.
						// Choosing Microsoft Word then fails at once. The
						// chosen suffix is applied after the dialog closes.
						break;
				}
			}
			break;

		default:
			return false;
	}
	return false;
}

/*!
 Used for Insert Picture case of FileOpenSaveAs dialog
 \param hDlg
 \param msg
 \param wParam
 \param lParam

 Static routine
 */
UINT_PTR CALLBACK XAP_Win32Dialog_FileOpenSaveAs::s_hookInsertPicProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	XAP_Win32Dialog_FileOpenSaveAs* pThis = (XAP_Win32Dialog_FileOpenSaveAs *) GetWindowLongPtrW(hDlg,GWLP_USERDATA);
	bool bPreviewImage = ( IsDlgButtonChecked( hDlg, XAP_RID_DIALOG_INSERT_PICTURE_CHECK_ACTIVATE_PREVIEW )
							  == BST_CHECKED );

	switch(msg)
	{

	case WM_PAINT:
		if (pThis && bPreviewImage)
			pThis->_paintCachedPreview(hDlg);
		return false;

	case WM_NOTIFY:
		// Only bother if Preview Image is selected
		if (!bPreviewImage)
			return false;

		switch ( ((OFNOTIFYW*) lParam)->hdr.code )
		{
		case CDN_FOLDERCHANGE:
			UT_DEBUGMSG(("Folder Changed File Name Cleared\n"));
			if (pThis)
				pThis->_clearPreviewCache();
			SetDlgItemTextW( GetParent(hDlg), edt1,	nullptr );
			SetDlgItemTextW( hDlg, XAP_RID_DIALOG_INSERT_PICTURE_TEXT_HEIGHT, nullptr );
			SetDlgItemTextW( hDlg, XAP_RID_DIALOG_INSERT_PICTURE_TEXT_WIDTH, nullptr );
			return true;
		case CDN_SELCHANGE:
			UT_return_val_if_fail(pThis,false);
			return pThis->_previewPicture(hDlg);
		}
		return false;

	case WM_COMMAND:
		// Check box to Activate Image Preview
		UT_DEBUGMSG(("WM_COMMAND Received: wParam = %ld, lParam = %ld\n",wParam,lParam));
		if ( HIWORD(wParam) == BN_CLICKED ) 
		{
			switch ( LOWORD(wParam) ) 
			{
			case XAP_RID_DIALOG_INSERT_PICTURE_CHECK_ACTIVATE_PREVIEW: 
				UT_return_val_if_fail(pThis,false);
				if (bPreviewImage)
				{
					ShowWindow( GetDlgItem(hDlg,XAP_RID_DIALOG_INSERT_PICTURE_TEXT_HEIGHT),
								 SW_SHOW );
					ShowWindow( GetDlgItem(hDlg,XAP_RID_DIALOG_INSERT_PICTURE_TEXT_WIDTH),
								 SW_SHOW );
					pThis->_previewPicture(hDlg);
					return true;
				}
				else
				{
					ShowWindow( GetDlgItem(hDlg,XAP_RID_DIALOG_INSERT_PICTURE_TEXT_HEIGHT),
								 SW_HIDE );
					ShowWindow( GetDlgItem(hDlg,XAP_RID_DIALOG_INSERT_PICTURE_TEXT_WIDTH),
								 SW_HIDE );
					pThis->_clearPreviewCache();
					return pThis->_initPreviewDlg(hDlg);
				}	
			} 
		}			  
		return false;

	case WM_INITDIALOG:
		pThis = (XAP_Win32Dialog_FileOpenSaveAs *)reinterpret_cast<OPENFILENAMEW*>(lParam)->lCustData;
		if (!pThis)
			return false;
		SetWindowLongPtrW(hDlg, GWLP_USERDATA, (LONG_PTR) pThis);
		pThis->_clearPreviewCache();
		return pThis->_initPreviewDlg(hDlg);

	case WM_DESTROY:
		if (pThis)
			pThis->_clearPreviewCache();
		return false;

	default:
		return false;

	}
	// Default Dialog handles all other issues
	return false;
}

void XAP_Win32Dialog_FileOpenSaveAs::_clearPreviewCache(void)
{
	DELETEP(m_pPreviewImage);
	m_sPreviewPath.clear();
}

void XAP_Win32Dialog_FileOpenSaveAs::_paintCachedPreview(HWND hDlg)
{
	if (!m_pPreviewImage)
		return;

	HWND hThumbnail = GetDlgItem(hDlg, XAP_RID_DIALOG_INSERT_PICTURE_IMAGE_PREVIEW);
	RECT r;
	memset(&r, 0, sizeof(r));
	if (!hThumbnail || !GetClientRect(hThumbnail, &r))
		return;
	const int clientW = r.right - r.left;
	const int clientH = r.bottom - r.top;
	if (clientW <= 0 || clientH <= 0)
		return;

	// BeginPaint belongs to this window's WM_PAINT. The call arrives from
	// CDN_SELCHANGE and from the hook dialog, so GetDC is the DC to use.
	HDC hdc = GetDC(hThumbnail);
	if (!hdc)
		return;
	FillRect(hdc, &r, GetSysColorBrush(COLOR_BTNFACE));

	GR_Win32AllocInfo ai(hdc, hThumbnail);
	GR_Win32Graphics *pGr = (GR_Win32Graphics *)XAP_App::getApp()->newGraphics(ai);
	if (pGr)
	{
		const int scaledW = m_pPreviewImage->getDisplayWidth();
		const int scaledH = m_pPreviewImage->getDisplayHeight();
		pGr->drawImage(m_pPreviewImage,
					   pGr->tlu((clientW - scaledW) / 2),
					   pGr->tlu((clientH - scaledH) / 2));
		DELETEP(pGr);
	}
	ReleaseDC(hThumbnail, hdc);
	ValidateRect(hThumbnail, nullptr);
}
	
UINT XAP_Win32Dialog_FileOpenSaveAs::_previewPicture(HWND hDlg)
{
	HWND hFOSADlg	= GetParent(hDlg);
	HWND hThumbnail = GetDlgItem(hDlg,XAP_RID_DIALOG_INSERT_PICTURE_IMAGE_PREVIEW);
    UT_Win32LocaleString str;

	const XAP_StringSet*  pSS	= XAP_App::getApp()->getStringSet();
	UT_return_val_if_fail(pSS, false);

	// Check if File Name is for a file
	wchar_t buf[MAX_DLG_INS_PICT_STRING];
	buf[0] = 0;
	// A negative result means the buffer was too small and was not filled.
	const LRESULT pathChars = SendMessageW(hFOSADlg, CDM_GETFILEPATH, MAX_DLG_INS_PICT_STRING, (LPARAM)buf);
	if (pathChars <= 0 || pathChars > MAX_DLG_INS_PICT_STRING)
	{
		_clearPreviewCache();
		return false;
	}
	buf[MAX_DLG_INS_PICT_STRING - 1] = 0;
	// Attributes are a bit mask. A directory also carries archive or hidden bits.
	const DWORD attrs = GetFileAttributesW(buf);
	if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY))
	{
		_clearPreviewCache();
		return false;
	}

	if (m_pPreviewImage && m_sPreviewPath == buf)
	{
		_paintCachedPreview(hDlg);
		return true;
	}

	_clearPreviewCache();

    str.fromLocale (buf);
	UT_DEBUGMSG(("File Selected is %s\n", str.utf8_str().utf8_str()));

	// Build an Import Graphic based on file type
	UT_Error errorCode = UT_ERROR;
	FG_ConstGraphicPtr pfg;
	char *uri = UT_go_filename_to_uri(str.utf8_str().utf8_str());
	if(uri)
		errorCode = IE_ImpGraphic::loadGraphic(uri, IEGFT_Unknown, pfg);
	FREEP(uri);

	if (errorCode || !pfg)
	{
		return false;
	}

	double		scale_factor = 0.0;
	UT_sint32	scaled_width,scaled_height;
	UT_sint32	iImageWidth,iImageHeight;
	RECT		r;
	memset(&r, 0, sizeof(r));
	// GetClientRect leaves the rectangle untouched when it fails.
	if (!hThumbnail || !GetClientRect(hThumbnail, &r))
		return false;
	const int clientW = r.right - r.left;
	const int clientH = r.bottom - r.top;
	if (clientW <= 0 || clientH <= 0)
		return false;

    iImageWidth = pfg->getWidth();
    iImageHeight = pfg->getHeight();
	if (iImageWidth <= 0 || iImageHeight <= 0)
		return false;

	const std::wstring selectedPath(buf);

	// Update Height and Width Strings
    str.fromUTF8 (pSS->getValue(XAP_STRING_ID_DLG_IP_Height_Label));
	_snwprintf(buf, MAX_DLG_INS_PICT_STRING, L"%s %d", str.c_str(), iImageHeight);
	buf[MAX_DLG_INS_PICT_STRING - 1] = 0;
	SetDlgItemTextW( hDlg,
					XAP_RID_DIALOG_INSERT_PICTURE_TEXT_HEIGHT,
					buf );

    str.fromUTF8 (pSS->getValue(XAP_STRING_ID_DLG_IP_Width_Label));
	_snwprintf(buf, MAX_DLG_INS_PICT_STRING, L"%s %d", str.c_str(), iImageWidth);
	buf[MAX_DLG_INS_PICT_STRING - 1] = 0;
	SetDlgItemTextW( hDlg,
					XAP_RID_DIALOG_INSERT_PICTURE_TEXT_WIDTH,
					buf );

	// Reset String for Height and Width
	if (clientW >= iImageWidth && clientH >= iImageHeight)
		scale_factor = 1.0;
	else
		scale_factor = UT_MIN( (double) clientW/iImageWidth,
							(double) clientH/iImageHeight );

	scaled_width  = (int) (scale_factor * iImageWidth);
	scaled_height = (int) (scale_factor * iImageHeight);

	m_pPreviewImage = new GR_Win32Image(nullptr);
	if (!m_pPreviewImage->convertFromBuffer(pfg->getBuffer(), pfg->getMimeType(), scaled_width, scaled_height))
	{
		_clearPreviewCache();
		return false;
	}
	m_sPreviewPath = selectedPath;

	_paintCachedPreview(hDlg);

	return true;
}

UINT XAP_Win32Dialog_FileOpenSaveAs::_initPreviewDlg(HWND hDlg)
{
	HWND hFOSADlg	= GetParent(hDlg);
	UT_Win32LocaleString str;
	setHandle (hDlg);

	const XAP_StringSet*  pSS		  = XAP_App::getApp()->getStringSet();
	UT_return_val_if_fail(pSS, false);

    setDlgItemText( XAP_RID_DIALOG_INSERT_PICTURE_IMAGE_PREVIEW,
					pSS->getValue(XAP_STRING_ID_DLG_IP_No_Picture_Label));
  
    setDlgItemText( XAP_RID_DIALOG_INSERT_PICTURE_CHECK_ACTIVATE_PREVIEW,
					pSS->getValue(XAP_STRING_ID_DLG_IP_Activate_Label));

    setDlgItemText( XAP_RID_DIALOG_INSERT_PICTURE_TEXT_HEIGHT,
					pSS->getValue(XAP_STRING_ID_DLG_IP_Height_Label));
    
    setDlgItemText( XAP_RID_DIALOG_INSERT_PICTURE_TEXT_WIDTH,
					pSS->getValue(XAP_STRING_ID_DLG_IP_Width_Label));
    
    str.fromUTF8 (pSS->getValue(XAP_STRING_ID_DLG_IP_Button_Label));
    SetDlgItemTextW( hFOSADlg,
					IDOK,
					str.c_str());

	return true;

}
