/* AbiSource Drag and Drop
 * Copyright (C) 2002 Jordi Mas i Hernàndez jmas@softcatala.org
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
#include <richedit.h>


#include "ut_types.h"
#include "ut_string_class.h"
#include "ut_assert.h"
#include "ut_debugmsg.h"
#include "xap_Win32App.h"
#include "xap_Win32FrameImpl.h"
#include "xav_View.h"
#include "xad_Document.h"
#include "ap_FrameData.h"
#include "ap_Win32FrameImpl.h"
#include "ie_impexp_Register.h"
#include "ie_imp.h"
#include "xap_EncodingManager.h"
#include "xap_Win32DragAndDrop.h"
#include "fl_DocLayout.h"

// OLE text need not be null-terminated. GlobalSize is the only bound,
// the same rule the clipboard paste path already applies.
static UT_uint32 s_dropTextBytes(const void * data, SIZE_T size, bool wide)
{
	if (!data || size == 0)
		return 0;
	if (size > 0xffffffffu)
		size = 0xffffffffu;
	const UT_uint32 bytes = static_cast<UT_uint32>(size);

	if (wide)
	{
		const UT_uint32 chars = bytes / sizeof(wchar_t);
		const wchar_t * text = static_cast<const wchar_t *>(data);
		UT_uint32 n = 0;
		while (n < chars && text[n] != L'\0')
			++n;
		return n * sizeof(wchar_t);
	}

	const unsigned char * text = static_cast<const unsigned char *>(data);
	UT_uint32 n = 0;
	while (n < bytes && text[n] != 0)
		++n;
	return n;
}

static void s_pasteDrop(XAP_Frame * frame, const unsigned char * data, UT_uint32 len,
			 const char * suffix, const char * encoding)
{
	if (!frame || !data || !len)
		return;

	AP_FrameData * pFrameData = static_cast<AP_FrameData *>(frame->getFrameData());
	if (!pFrameData || !pFrameData->m_pDocLayout)
		return;
	FV_View * pView = pFrameData->m_pDocLayout->getView();
	if (!pView || !pView->getDocument())
		return;

	PD_DocumentRange dr(pView->getDocument(), pView->getPoint(), pView->getPoint());
	IE_Imp * pImp = nullptr;
	IE_Imp::constructImporter(dr.m_pDoc, IE_Imp::fileTypeForSuffix(suffix), &pImp, nullptr);
	if (!pImp)
		return;

	pImp->pasteFromBuffer(&dr, data, len, encoding);
	delete pImp;
	pView->_generalUpdate();
}

static bool s_dropTextFormat(IDataObject * dataObj, CLIPFORMAT format, bool wide,
			     const char * suffix, const char * encoding, XAP_Frame * frame)
{
	FORMATETC formatetc;
	memset(&formatetc, 0, sizeof(formatetc));
	formatetc.cfFormat = format;
	formatetc.dwAspect = DVASPECT_CONTENT;
	formatetc.lindex = -1;
	formatetc.tymed = TYMED_HGLOBAL;

	STGMEDIUM medium;
	memset(&medium, 0, sizeof(medium));
	if (!SUCCEEDED(dataObj->GetData(&formatetc, &medium)))
		return false;

	void * locked = GlobalLock(medium.hGlobal);
	if (!locked)
	{
		ReleaseStgMedium(&medium);
		return true;
	}

	const UT_uint32 len = s_dropTextBytes(locked, GlobalSize(medium.hGlobal), wide);
	s_pasteDrop(frame, static_cast<const unsigned char *>(locked), len, suffix, encoding);
	GlobalUnlock(medium.hGlobal);
	ReleaseStgMedium(&medium);
	return true;
}

XAP_Win32DropTarget::XAP_Win32DropTarget()
{
#ifdef TEXT
#undef TEXT
#endif
#define TEXT(z) L##z
	m_uCF_RTF = RegisterClipboardFormatW(CF_RTF);
#undef TEXT
	m_nCount = 0;
	
}

STDMETHODIMP XAP_Win32DropTarget::QueryInterface(REFIID riid, LPVOID FAR* ppvObj)
{
	if (!ppvObj) return E_POINTER;
	if (riid==IID_IDropTarget) {
		*ppvObj=(IDropTarget*)this;
		AddRef();
		return S_OK;
	} else if (riid==IID_IUnknown) {
		*ppvObj=(IUnknown*)this;
		AddRef();
		return S_OK;
	}
	return E_NOINTERFACE;
}


STDMETHODIMP_(ULONG) XAP_Win32DropTarget::AddRef()
{
        ++m_nCount;
        return (ULONG)m_nCount;
}


STDMETHODIMP_(ULONG) XAP_Win32DropTarget::Release()
{
        if (m_nCount > 0)
		--m_nCount;
	return (ULONG)m_nCount;
}

//
//      Called when the mouse first enters our DropTarget window
//	We check which formats we really support
//
STDMETHODIMP XAP_Win32DropTarget::DragEnter (LPDATAOBJECT pDataObj, DWORD /*grfKeyState*/, POINTL /*pointl*/, LPDWORD pdwEffect)
{	
	FORMATETC 	fmte = {CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
	
	m_bSupportedFormat = false;	

	// Dropping files
	if (NOERROR == pDataObj->QueryGetData(&fmte))	
		m_bSupportedFormat = true;				
	
	// RTF
	fmte.cfFormat = m_uCF_RTF;
	
	if (NOERROR == pDataObj->QueryGetData(&fmte))	
		m_bSupportedFormat = true;

	// Plain text uses the same clipboard format as paste.
	fmte.cfFormat = CF_UNICODETEXT;
	if (NOERROR == pDataObj->QueryGetData(&fmte))
		m_bSupportedFormat = true;
	
	*pdwEffect = (m_bSupportedFormat) ? DROPEFFECT_COPY : DROPEFFECT_NONE;
		
	return S_OK;
}

STDMETHODIMP XAP_Win32DropTarget::DragOver(DWORD /*grfKeyState*/, POINTL pointl, LPDWORD pdwEffect)
{
	*pdwEffect = (m_bSupportedFormat) ? DROPEFFECT_COPY : DROPEFFECT_NONE;

	if (!m_bSupportedFormat || !m_pFrame)
		return S_OK;

	// The frame client starts at the rebar and ends at the status bar,
	// so a 24px band there never covers the page. Measure the document.
	AP_Win32FrameImpl * impl = static_cast<AP_Win32FrameImpl *>(m_pFrame->getFrameImpl());
	HWND doc = impl->getHwndDocument();
	if (!doc)
		return S_OK;

	POINT client;
	client.x = pointl.x;
	client.y = pointl.y;
	ScreenToClient(doc, &client);
	RECT rc;
	GetClientRect(doc, &rc);
	if (client.x < rc.left || client.x >= rc.right
	    || client.y < rc.top || client.y >= rc.bottom)
		return S_OK;

	const int edge = 24;
	WPARAM code = 0;
	if (client.y < rc.top + edge)
		code = SB_LINEUP;
	else if (client.y > rc.bottom - edge)
		code = SB_LINEDOWN;
	if (code)
		SendMessageW(impl->getTopLevelWindow(), WM_VSCROLL, code, 0);

	return S_OK;
}


STDMETHODIMP XAP_Win32DropTarget::DragLeave()
{
	return S_OK;
}


//
// Called when the user drops an object
//
STDMETHODIMP XAP_Win32DropTarget::Drop (LPDATAOBJECT pDataObj, DWORD /*grfKeyState*/, POINTL /*pointl*/, LPDWORD /*pdwEffect*/)
{
	int count = 0;
	STGMEDIUM medium;
	memset(&medium, 0, sizeof(medium));

	FORMATETC fmte = {CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};

	//
	// Is the user dropping files?
	// lParam 1 tells the frame that this HDROP belongs to the OLE medium,
	// so it must not call DragFinish. ReleaseStgMedium frees it once.
	//
	if (NOERROR == pDataObj->QueryGetData(&fmte))
	{
		if (pDataObj && SUCCEEDED (pDataObj->GetData (&fmte, &medium)))
		{
			count = DragQueryFileW((HDROP)medium.hGlobal, 0xFFFFFFFF, nullptr, 0);
			if (count)
				SendMessageW(static_cast<XAP_Win32FrameImpl*>(m_pFrame->getFrameImpl())->getTopLevelWindow(), WM_DROPFILES, (WPARAM)medium.hGlobal, 1);
			ReleaseStgMedium(&medium);
		}
		return S_OK;
	}

	// RTF first, then the same plain-text format paste already accepts.
	const char * encoding = XAP_EncodingManager::get_instance()->getNative8BitEncodingName();
	if (s_dropTextFormat(pDataObj, static_cast<CLIPFORMAT>(m_uCF_RTF), false, ".rtf", encoding, m_pFrame))
		return S_OK;

	s_dropTextFormat(pDataObj, CF_UNICODETEXT, true, ".txt", "UTF-16LE", m_pFrame);
	return S_OK;
}




