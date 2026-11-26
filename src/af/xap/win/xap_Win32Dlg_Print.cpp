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

#include <windows.h>
#include <stdio.h>
#include <cwchar>
#include <string>
#include "ut_debugmsg.h"
#include "ut_string.h"
#include "ut_assert.h"
#include "ut_unicode.h"
#include "xap_Dialog_Id.h"
#include "xap_Win32Dlg_Print.h"
#include "xap_Win32App.h"
#include "xap_Win32FrameImpl.h"
#include "xav_View.h"
#include "gr_Win32Graphics.h"
#include "xap_Win32DialogBase.h"

namespace {

bool s_extractDevNames(HGLOBAL hDevNames, std::wstring & driver, std::wstring & device)
{
	driver.clear();
	device.clear();
	if (!hDevNames)
		return false;

	const SIZE_T bytes = GlobalSize(hDevNames);
	if (bytes < sizeof(DEVNAMES))
		return false;

	const DEVNAMES * pDevNames = static_cast<const DEVNAMES *>(GlobalLock(hDevNames));
	if (!pDevNames)
		return false;

	const SIZE_T wcharCount = bytes / sizeof(wchar_t);
	const wchar_t * base = reinterpret_cast<const wchar_t *>(pDevNames);
	if (pDevNames->wDriverOffset < wcharCount)
	{
		const wchar_t * start = base + pDevNames->wDriverOffset;
		const SIZE_T maxLen = wcharCount - pDevNames->wDriverOffset;
		const wchar_t * end = std::wmemchr(start, L'\0', maxLen);
		driver.assign(start, end ? static_cast<SIZE_T>(end - start) : maxLen);
	}
	if (pDevNames->wDeviceOffset < wcharCount)
	{
		const wchar_t * start = base + pDevNames->wDeviceOffset;
		const SIZE_T maxLen = wcharCount - pDevNames->wDeviceOffset;
		const wchar_t * end = std::wmemchr(start, L'\0', maxLen);
		device.assign(start, end ? static_cast<SIZE_T>(end - start) : maxLen);
	}
	GlobalUnlock(hDevNames);
	return !device.empty();
}

HDC s_createPrinterMetricDC(HGLOBAL hDevNames, HGLOBAL hDevMode)
{
	std::wstring driver;
	std::wstring device;
	if (!s_extractDevNames(hDevNames, driver, device))
		return nullptr;

	const wchar_t * pDriver = driver.empty() ? nullptr : driver.c_str();
	const DEVMODEW * pDevMode = hDevMode ? static_cast<const DEVMODEW *>(GlobalLock(hDevMode)) : nullptr;
	HDC hMetricDC = CreateICW(pDriver, device.c_str(), nullptr, pDevMode);
	if (!hMetricDC)
		hMetricDC = CreateDCW(pDriver, device.c_str(), nullptr, pDevMode);
	if (pDevMode)
		GlobalUnlock(hDevMode);
	return hMetricDC;
}

void s_updateScreenPrinterMetrics(HGLOBAL hDevNames, HGLOBAL hDevMode)
{
	// Give the screen view its own information context so WM_PAINT during
	// StartDocW/StartPage/EndPage never touches the active spooling HDC.
	XAP_Frame * focussed = XAP_App::getApp()->getLastFocussedFrame();
	AV_View * focussedView = focussed ? focussed->getCurrentView() : nullptr;
	GR_Win32Graphics * pG = focussedView ? static_cast<GR_Win32Graphics *>(focussedView->getGraphics()) : nullptr;
	if (!pG)
		return;

	HDC hMetricDC = s_createPrinterMetricDC(hDevNames, hDevMode);
	if (hMetricDC)
		pG->setPrintDC(hMetricDC);
}

} // namespace

/*****************************************************************/
static UINT_PTR CALLBACK s_PrintHookProc(
  HWND hdlg,      // handle to the dialog box window
  UINT uiMsg,     // message identifier
  WPARAM wParam,  // message parameter
  LPARAM lParam   // message parameter
  )                                                                             
{
	XAP_Win32Dialog_Print * pThis = nullptr;
	
	if(uiMsg == WM_INITDIALOG)
	{
		PRINTDLGW * pDlgInfo = (PRINTDLGW*)lParam;
		if (!pDlgInfo)
			return 0;
		pThis = (XAP_Win32Dialog_Print*)pDlgInfo->lCustData;
		if (!pThis)
			return 0;
		SetWindowLongPtrW(hdlg, DWLP_USER, (LONG_PTR) pThis);

		// reset the 'closed' flag which indicates that the dialog should be considered
		// 'closed' rather than aborted
		pThis->setClosed(false);

		// hide the selection radio since we do not support selection printing
		if(pDlgInfo->Flags & PD_NOSELECTION)
		{
			HWND wPrintSelectionRadio = GetDlgItem(hdlg, rad2);
			ShowWindow(wPrintSelectionRadio, SW_HIDE);
		}

		// remember the original printer selected; we use this to decide if upon closure
		// we should notify associated views to rebuild to reflect the new font metrics
		const LRESULT printer = SendDlgItemMessageW(hdlg, cmb4, CB_GETCURSEL, 0, 0);
		const UT_uint32 iPrinter = (printer == CB_ERR) ? 0 : static_cast<UT_uint32>(printer);
		pThis->setOrigPrinter(iPrinter);
		pThis->setNewPrinter(iPrinter);
	}
	else
	{
		pThis = (XAP_Win32Dialog_Print*)GetWindowLongPtrW(hdlg, DWLP_USER);
	}
	
	if (!pThis)
		return 0;

	if(uiMsg == WM_COMMAND)
	{
		// if the command indicates that the user changed printer, we change the Cancel
		// button to Close; make sure we have valid this pointer.
		if((int)LOWORD(wParam) == cmb4 && HIWORD(wParam) == CBN_SELCHANGE)
		{
			const LRESULT printer = SendDlgItemMessageW(hdlg, cmb4, CB_GETCURSEL, 0, 0);
			if (printer == CB_ERR)
				return 0;
			pThis->setNewPrinter(static_cast<UT_uint32>(printer));

			XAP_App*                    pApp      = XAP_App::getApp();
			const XAP_StringSet*  pSS         = pApp->getStringSet();
			
			if(pThis->getNewPrinter() != pThis->getOrigPrinter())
			{
			    XAP_Win32DialogBase::setDlgItemText(hdlg,IDCANCEL,pSS->getValue(XAP_STRING_ID_DLG_Close));
			}
			else
			{
				// the user set the printer back to what it used to be -- revert back to
				// cancel button
			    XAP_Win32DialogBase::setDlgItemText(hdlg,IDCANCEL,pSS->getValue(XAP_STRING_ID_DLG_Cancel));
			}
		}
		else if((int)LOWORD(wParam) == IDCANCEL && HIWORD(wParam) == 0)
		{
			// the user presed the Cancel/Close button; see if it is Cancel or Close
			if(pThis->getNewPrinter() != pThis->getOrigPrinter())
			{
				// Different printer is selected to the one we started with, this is Close
				// scenario -- eat this message and simulate click on the OK button
				// instead so that the default dlg procedure fills the PRINTDLG struct
				// with the data we need; set the m_bClosed flag, so we can differentiate
				// this from the user pressing the OK button
				pThis->setClosed(true);
				PostMessageW(hdlg, WM_COMMAND, IDOK, 0);

				return 1; // eat the message
			}
		}
	}
	
	return 0;
}

XAP_Dialog * XAP_Win32Dialog_Print::static_constructor(XAP_DialogFactory * pFactory,
													 XAP_Dialog_Id id)
{
	XAP_Win32Dialog_Print * p = new XAP_Win32Dialog_Print(pFactory,id);
	return p;
}

XAP_Win32Dialog_Print::XAP_Win32Dialog_Print(XAP_DialogFactory * pDlgFactory,
										   XAP_Dialog_Id id):
	XAP_Dialog_Print(pDlgFactory,id),
	m_iOrigPrinter(0),
	m_iNewPrinter(0),
	m_bClosed(false)
{
	m_pPersistPrintDlg = (PRINTDLGW *)UT_calloc(1,sizeof(PRINTDLGW));
	UT_return_if_fail(m_pPersistPrintDlg);

	m_pPersistPrintDlg->lStructSize = sizeof(*m_pPersistPrintDlg);
}

XAP_Win32Dialog_Print::~XAP_Win32Dialog_Print(void)
{
	if (m_pPersistPrintDlg)
	{
		if (m_pPersistPrintDlg->hDC)
		{
			DeleteDC(m_pPersistPrintDlg->hDC);
			m_pPersistPrintDlg->hDC = nullptr;
		}
		if (m_pPersistPrintDlg->hDevMode)
			GlobalFree(m_pPersistPrintDlg->hDevMode);
		if (m_pPersistPrintDlg->hDevNames)
			GlobalFree(m_pPersistPrintDlg->hDevNames);
		g_free(m_pPersistPrintDlg);
	}
}

GR_Graphics * XAP_Win32Dialog_Print::getPrinterGraphicsContext(void)
{
	UT_ASSERT(m_answer == a_OK);

	if (!m_pPersistPrintDlg || !m_pPersistPrintDlg->hDC)
		return nullptr; /* Prevents from passing nullptr to GR_Win32Graphics*/

	memset(&m_DocInfo,0,sizeof(m_DocInfo));
	m_DocInfo.cbSize = sizeof(DOCINFOW);
	m_docName.fromUTF8 (m_szDocumentPathname);
	m_DocInfo.lpszDocName = m_docName.c_str();
	
    if (m_bDoPrintToFile) {
		m_fileName.fromUTF8 (m_szPrintToFilePathname);
		m_DocInfo.lpszOutput = m_fileName.c_str();
	} else {
		m_DocInfo.lpszOutput = nullptr;	
	}	

	GR_Win32AllocInfo ai(m_pPersistPrintDlg->hDC,&m_DocInfo, m_pPersistPrintDlg->hDevMode);
	GR_Win32Graphics *pGr = (GR_Win32Graphics *)XAP_App::getApp()->newGraphics(ai);
	UT_ASSERT(pGr);
	if (pGr)
	{
		pGr->setPrintOwner(m_pPersistPrintDlg->hwndOwner);
		std::wstring driver;
		std::wstring device;
		if (s_extractDevNames(m_pPersistPrintDlg->hDevNames, driver, device))
			pGr->setPrinterName(device.c_str());
	}

	return pGr;
}

void XAP_Win32Dialog_Print::releasePrinterGraphicsContext(GR_Graphics * pGraphics)
{
	if (pGraphics)
		delete pGraphics;

	if (m_pPersistPrintDlg && m_pPersistPrintDlg->hDC)
	{
		DeleteDC(m_pPersistPrintDlg->hDC);
		m_pPersistPrintDlg->hDC = nullptr;
	}

	memset(&m_DocInfo, 0, sizeof(m_DocInfo));
}

/*****************************************************************/

void XAP_Win32Dialog_Print::runModal(XAP_Frame * pFrame)
{
	UT_return_if_fail(pFrame);
	UT_return_if_fail(m_pPersistPrintDlg);

	m_answer = a_CANCEL;
	if (m_pPersistPrintDlg->hDC)
	{
		DeleteDC(m_pPersistPrintDlg->hDC);
		m_pPersistPrintDlg->hDC = nullptr;
	}

	HWND hwnd = static_cast<XAP_Win32FrameImpl*>(pFrame->getFrameImpl())->getTopLevelWindow();

	m_pPersistPrintDlg->hwndOwner	= hwnd;
	m_pPersistPrintDlg->nFromPage	= (WORD)m_nFirstPage;
	m_pPersistPrintDlg->nToPage		= (WORD)m_nLastPage;
	m_pPersistPrintDlg->nMinPage	= (WORD)m_nFirstPage;
	m_pPersistPrintDlg->nMaxPage	= (WORD)m_nLastPage;
	// PD_RETURNDC without PD_USEDEVMODECOPIESANDCOLLATE makes PrintDlg
	// report the wrong copy count. With the flag, the user's count is
	// dmCopies and collation is dmCollate. AbiWord then prints the copies.
	m_pPersistPrintDlg->Flags		= PD_ALLPAGES | PD_RETURNDC | PD_ENABLEPRINTHOOK
		| PD_USEDEVMODECOPIESANDCOLLATE;
	m_pPersistPrintDlg->lpfnPrintHook   = s_PrintHookProc;
	m_pPersistPrintDlg->lCustData       = (INT_PTR)this;
		
	if (!m_bEnablePageRange)
		m_pPersistPrintDlg->Flags	|= PD_NOPAGENUMS;
	if (!m_bEnablePrintSelection)
		m_pPersistPrintDlg->Flags	|= PD_NOSELECTION;
	if (m_bEnablePrintToFile)
	{
		if (m_bDoPrintToFile)
			m_pPersistPrintDlg->Flags	|= PD_PRINTTOFILE;
	}
	else
	{
		m_pPersistPrintDlg->Flags	|= PD_HIDEPRINTTOFILE;
	}
	
	if (!m_bPersistValid)				// first time
	{
		// these values are either in the PRINTDLG structure or
		// in the HDEVMODE structure within it depending upon
		// the driver.  since we don't destroy and recreate the
		// structure on each use (and we don't provide our caller
		// access to these fields), we only need to load them the
		// first time.
		
		m_pPersistPrintDlg->nCopies		= (WORD)m_nCopies;
		if (m_bCollate)
			m_pPersistPrintDlg->Flags	|= PD_COLLATE;
	}

	// see if they just want the properties of the printer without
	// bothering the user.
	if (m_bPersistValid && m_bBypassActualDialog)
	{
		// Create the printer HDC before _extractResults() so any needed
		// ResetDCW operates on a valid handle instead of nullptr.
		std::wstring driver;
		std::wstring device;
		if (!m_pPersistPrintDlg->hDevMode ||
		    !s_extractDevNames(m_pPersistPrintDlg->hDevNames, driver, device))
		{
			m_answer = a_CANCEL;
			return;
		}

		DEVMODEW * pDevMode = static_cast<DEVMODEW *>(GlobalLock(m_pPersistPrintDlg->hDevMode));
		if (!pDevMode)
		{
			m_answer = a_CANCEL;
			return;
		}

		const wchar_t * pDriver = driver.empty() ? nullptr : driver.c_str();
		m_pPersistPrintDlg->hDC = CreateDCW(pDriver, device.c_str(), nullptr, pDevMode);
		GlobalUnlock(m_pPersistPrintDlg->hDevMode);
		if (!m_pPersistPrintDlg->hDC)
		{
			m_answer = a_CANCEL;
			return;
		}

		_extractResults(pFrame);
		if (m_answer != a_OK)
		{
			DeleteDC(m_pPersistPrintDlg->hDC);
			m_pPersistPrintDlg->hDC = nullptr;
			return;
		}

		// notify layout of printer using a separate IC/DC (this may redo doc layout)
		s_updateScreenPrinterMetrics(m_pPersistPrintDlg->hDevNames, m_pPersistPrintDlg->hDevMode);
	}
	else if (PrintDlgW(m_pPersistPrintDlg))		// raise the actual dialog.
	{
		// PD_RETURNDC puts a DC in hDC. PrintDlgW can still come back
		// without one; ResetDC on that handle is not valid.
		if (!m_pPersistPrintDlg->hDC)
		{
			m_answer = a_CANCEL;
			return;
		}

		// we need to differentiate between actual OK being pressed by user and simulated
		// by our hook procedure -- in the latter case we want to return a_CANCEL so that
		// the document does not get sent to the printer. We will also set m_bPersistValid
		// as if OK was clicked and update screen metrics for the newly selected printer.
		if (m_bClosed)
		{
			s_updateScreenPrinterMetrics(m_pPersistPrintDlg->hDevNames, m_pPersistPrintDlg->hDevMode);
			DeleteDC(m_pPersistPrintDlg->hDC);
			m_pPersistPrintDlg->hDC = nullptr;
			m_answer = a_CANCEL;
			m_bPersistValid = true;
			return;
		}

		_extractResults(pFrame);
		if (m_answer != a_OK)
		{
			DeleteDC(m_pPersistPrintDlg->hDC);
			m_pPersistPrintDlg->hDC = nullptr;
			return;
		}

		// notify layout of printer using a separate IC/DC (this may redo doc layout)
		s_updateScreenPrinterMetrics(m_pPersistPrintDlg->hDevNames, m_pPersistPrintDlg->hDevMode);
	}
	else
	{
		if (m_pPersistPrintDlg->hDC)
		{
			DeleteDC(m_pPersistPrintDlg->hDC);
			m_pPersistPrintDlg->hDC = nullptr;
		}
		// The user clicked the cancel button and did not change the printer, there is
		// nothing for us to do
		m_answer = a_CANCEL;
	}

	return;
}

void XAP_Win32Dialog_Print::_extractResults(XAP_Frame *pFrame)
{
	m_answer = a_CANCEL;
	UT_return_if_fail(m_pPersistPrintDlg);
	UT_return_if_fail(m_pPersistPrintDlg->hDevMode!=nullptr);

	m_bDoPrintRange		= ((m_pPersistPrintDlg->Flags & PD_PAGENUMS) != 0);
	m_bDoPrintSelection = ((m_pPersistPrintDlg->Flags & PD_SELECTION) != 0);
	m_bDoPrintToFile	= ((m_pPersistPrintDlg->Flags & PD_PRINTTOFILE) != 0);	
	if (m_bDoPrintRange)
	{
		m_nFirstPage	= m_pPersistPrintDlg->nFromPage;
		m_nLastPage		= m_pPersistPrintDlg->nToPage;
	}

	// The driver must not print the copies as well: AbiWord loops itself,
	// as on the other ports. Read the count first, then force a single copy
	// on a temporary DEVMODE copy for ResetDCW so m_pPersistPrintDlg->hDevMode
	// keeps the user's copies/collate settings across Quick Print and future dialogs.
	const SIZE_T dmBytes = GlobalSize(m_pPersistPrintDlg->hDevMode);
	DEVMODEW *pDevMode = static_cast<DEVMODEW *>(GlobalLock(m_pPersistPrintDlg->hDevMode));
	if (!pDevMode)
	{
		m_answer = a_CANCEL;
		return;
	}
	m_nCopies = UT_print_copy_count((pDevMode->dmFields & DM_COPIES) != 0,
									pDevMode->dmCopies,
									m_pPersistPrintDlg->nCopies);
	if (pDevMode->dmFields & DM_COLLATE)
		m_bCollate = (pDevMode->dmCollate == DMCOLLATE_TRUE);
	else
		m_bCollate = (m_pPersistPrintDlg->Flags & PD_COLLATE) != 0;

	const bool bNeedCopiesReset = (pDevMode->dmFields & DM_COPIES) && UT_print_temp_is_single(pDevMode->dmCopies);
	const bool bNeedCollateReset = (pDevMode->dmFields & DM_COLLATE) && pDevMode->dmCollate != DMCOLLATE_FALSE;
	const bool bNeedReset = bNeedCopiesReset || bNeedCollateReset;

	HGLOBAL hTempDevMode = nullptr;
	if (bNeedReset && dmBytes >= sizeof(DEVMODEW))
	{
		hTempDevMode = GlobalAlloc(GHND, dmBytes);
		if (hTempDevMode)
		{
			DEVMODEW *pTemp = static_cast<DEVMODEW *>(GlobalLock(hTempDevMode));
			if (pTemp)
			{
				memcpy(pTemp, pDevMode, dmBytes);
				if (bNeedCopiesReset)
				{
					pTemp->dmCopies = 1;
					pTemp->dmFields |= DM_COPIES;
				}
				if (bNeedCollateReset)
				{
					pTemp->dmCollate = DMCOLLATE_FALSE;
					pTemp->dmFields |= DM_COLLATE;
				}
				GlobalUnlock(hTempDevMode);
			}
			else
			{
				GlobalFree(hTempDevMode);
				hTempDevMode = nullptr;
			}
		}
	}
	GlobalUnlock(m_pPersistPrintDlg->hDevMode);

	if (bNeedReset)
	{
		if (!hTempDevMode)
		{
			m_answer = a_CANCEL;
			return;
		}
		std::wstring driver;
		std::wstring device;
		s_extractDevNames(m_pPersistPrintDlg->hDevNames, driver, device);

		// any changes to the DEVMODE structure must be followed by this call before the
		// modified structure can be used !!!
		GR_Win32Graphics::fixDevMode(hTempDevMode,
									 device.empty() ? nullptr : device.c_str());

		DEVMODEW *pTemp = static_cast<DEVMODEW *>(GlobalLock(hTempDevMode));
		if (!pTemp)
		{
			GlobalFree(hTempDevMode);
			m_answer = a_CANCEL;
			return;
		}
		const HDC reset = m_pPersistPrintDlg->hDC ? ResetDCW(m_pPersistPrintDlg->hDC, pTemp) : nullptr;
		GlobalUnlock(hTempDevMode);
		GlobalFree(hTempDevMode);
		if (!reset)
		{
			m_answer = a_CANCEL;
			return;
		}
	}
	
	if (m_bDoPrintToFile)
	{
		// Append .print without a fixed buffer. A document opened from a
		// very long path must not overflow the stack here.
		UT_String suggested(m_szDocumentPathname ? m_szDocumentPathname : "document");
		suggested += ".print";
		if (!_getPrintToFilePathname(pFrame, suggested.c_str()))
			goto Fail;
	}

	m_answer = a_OK;
	return;

Fail:
	m_answer = a_CANCEL;
	return;
}

