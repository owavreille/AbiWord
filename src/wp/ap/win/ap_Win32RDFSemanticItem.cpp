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

#include <windows.h>
#include <commdlg.h>

#include <vector>

#include "ut_assert.h"
#include "ut_Win32LocaleString.h"

#include "xap_App.h"
#include "xap_Frame.h"
#include "xap_Strings.h"
#include "xap_Win32App.h"
#include "xap_Win32FrameImpl.h"

#include "ap_Strings.h"
#include "pd_Document.h"
#include "ap_RDFContact.h"
#include "ap_RDFEvent.h"
#include "ap_RDFLocation.h"
#include "fv_View.h"
#include "ap_Win32Resources.rc2"

namespace {

std::string windowText(HWND dlg, int id)
{
	HWND edit = GetDlgItem(dlg, id);
	const int n = edit ? GetWindowTextLengthW(edit) : 0;
	std::wstring buf(n > 0 ? n + 1 : 1, L'\0');
	if (edit && n > 0)
		GetWindowTextW(edit, &buf[0], n + 1);
	buf.resize(n > 0 ? n : 0);
	UT_Win32LocaleString str;
	str.fromLocale(buf.c_str());
	return std::string(str.utf8_str().utf8_str());
}

void setLabel(HWND dlg, int id, const char* utf8)
{
	UT_Win32LocaleString str;
	str.fromUTF8(utf8 ? utf8 : "");
	SetDlgItemTextW(dlg, id, str.c_str());
}

void setEdit(HWND dlg, int id, const std::string& utf8)
{
	setLabel(dlg, id, utf8.c_str());
}

HWND parentWindow()
{
	XAP_Frame* frame = XAP_App::getApp()->getLastFocussedFrame();
	if (!frame)
		return nullptr;
	XAP_Win32FrameImpl* impl = static_cast<XAP_Win32FrameImpl*>(frame->getFrameImpl());
	return impl ? impl->getTopLevelWindow() : nullptr;
}

std::string askVCardPath(bool save, const char* titleUtf8)
{
	wchar_t file[MAX_PATH];
	file[0] = 0;
	static const wchar_t filter[] = L"vCard (*.vcf)\0*.vcf;*.vcard\0";
	UT_Win32LocaleString title;
	if (titleUtf8)
		title.fromUTF8(titleUtf8);
	OPENFILENAMEW ofn;
	memset(&ofn, 0, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = parentWindow();
	ofn.lpstrFile = file;
	ofn.nMaxFile = MAX_PATH;
	ofn.lpstrFilter = filter;
	ofn.nFilterIndex = 1;
	ofn.Flags = OFN_NOCHANGEDIR | OFN_HIDEREADONLY | OFN_EXPLORER
		| (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
	ofn.lpstrDefExt = L"vcf";
	if (titleUtf8)
		ofn.lpstrTitle = title.c_str();
	const BOOL ok = save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
	if (!ok)
		return "";
	UT_Win32LocaleString path;
	path.fromLocale(file);
	return std::string(path.utf8_str().utf8_str());
}

struct RefChoice
{
	FV_View* view;
	std::vector< std::string > names;
};

INT_PTR CALLBACK refDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (msg == WM_INITDIALOG)
	{
		SetWindowLongPtrW(hwnd, DWLP_USER, lParam);
		RefChoice* choice = reinterpret_cast<RefChoice*>(lParam);
		const XAP_StringSet* strings = XAP_App::getApp()->getStringSet();
		{
			UT_Win32LocaleString title;
			title.fromUTF8(strings->getValue(AP_STRING_ID_DLG_RDF_SemanticItemInsert_Title));
			SetWindowTextW(hwnd, title.c_str());
		}
		setLabel(hwnd, AP_RID_DIALOG_RDFREFERENCE_LBL,
				 strings->getValue(AP_STRING_ID_DLG_RDF_SemanticItemInsert_Column_Refdlg));
		setLabel(hwnd, IDOK, strings->getValue(AP_STRING_ID_DLG_RDF_SemanticItemInsert_Ok));
		setLabel(hwnd, IDCANCEL, strings->getValue(XAP_STRING_ID_DLG_Cancel));
		HWND list = GetDlgItem(hwnd, AP_RID_DIALOG_RDFREFERENCE_LIST);
		for (size_t i = 0; i < choice->names.size(); ++i)
		{
			UT_Win32LocaleString item;
			item.fromUTF8(choice->names[i].c_str());
			const LRESULT index = SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)item.c_str());
			if (index != LB_ERR)
				SendMessageW(list, LB_SETITEMDATA, (WPARAM)index, (LPARAM)i);
		}
		if (!choice->names.empty())
			SendMessageW(list, LB_SETCURSEL, 0, 0);
		return TRUE;
	}
	RefChoice* choice = reinterpret_cast<RefChoice*>(GetWindowLongPtrW(hwnd, DWLP_USER));
	if (!choice || msg != WM_COMMAND)
		return FALSE;
	const WORD id = LOWORD(wParam);
	const WORD note = HIWORD(wParam);
	if (id == IDCANCEL)
	{
		EndDialog(hwnd, IDCANCEL);
		return TRUE;
	}
	if (id == IDOK || (id == AP_RID_DIALOG_RDFREFERENCE_LIST && note == LBN_DBLCLK))
	{
		HWND list = GetDlgItem(hwnd, AP_RID_DIALOG_RDFREFERENCE_LIST);
		const LRESULT sel = SendMessageW(list, LB_GETCURSEL, 0, 0);
		if (sel == LB_ERR)
		{
			EndDialog(hwnd, IDCANCEL);
			return TRUE;
		}
		const LRESULT index = SendMessageW(list, LB_GETITEMDATA, (WPARAM)sel, 0);
		if (index >= 0 && static_cast<size_t>(index) < choice->names.size() && choice->view)
		{
			PD_Document* doc = choice->view->getDocument();
			PD_DocumentRDFHandle rdf = doc ? doc->getDocumentRDF() : PD_DocumentRDFHandle();
			if (rdf)
			{
				const std::string& wanted = choice->names[static_cast<size_t>(index)];
				PD_RDFContacts contacts = rdf->getContacts();
				for (PD_RDFContacts::iterator it = contacts.begin(); it != contacts.end(); ++it)
				{
					if ((*it)->name() == wanted)
					{
						(*it)->insert(choice->view);
						break;
					}
				}
			}
		}
		EndDialog(hwnd, IDOK);
		return TRUE;
	}
	return FALSE;
}

void reflowContactName(const PD_RDFSemanticItemHandle& contact)
{
	if (!contact)
		return;
	XAP_Frame* frame = XAP_App::getApp()->getLastFocussedFrame();
	if (!frame)
		return;
	FV_View* view = static_cast<FV_View*>(frame->getCurrentView());
	if (!view)
		return;
	PD_RDFSemanticStylesheetHandle sheet = contact->findStylesheetByName(
		PD_RDFSemanticStylesheet::stylesheetTypeSystem(),
		RDF_SEMANTIC_STYLESHEET_CONTACT_NAME);
	if (!sheet)
		return;
	const std::set< std::string > ids = contact->getXMLIDs();
	for (std::set< std::string >::const_iterator it = ids.begin(); it != ids.end(); ++it)
	{
		PD_RDFSemanticItemViewSite site(contact, *it);
		site.applyStylesheet(view, sheet);
		return;
	}
}

}

class AP_Win32RDFContact : public AP_RDFContact
{
	std::string m_inName;
	std::string m_inNick;
	std::string m_inEmail;
	std::string m_inHome;
	std::string m_inPhone;

	void initDialog(HWND dlg);
	void readDialog(HWND dlg);
	static INT_PTR CALLBACK dlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

public:
	AP_Win32RDFContact(PD_DocumentRDFHandle rdf, PD_ResultBindings_t::iterator& it)
		: AP_RDFContact(rdf, it)
	{
	}

	virtual void* createEditor() override
	{
		return nullptr;
	}

	virtual void updateFromEditorData(PD_DocumentRDFMutationHandle m) override;
	virtual void showEditorWindow(const PD_RDFSemanticItemHandle& c) override;
	virtual void showEditorWindow(const PD_RDFSemanticItems& cl) override;
	virtual std::string getImportFromFileName(const std::string& filename_const,
		std::list< std::pair< std::string, std::string > > types) const override;
	virtual std::string getExportToFileName(const std::string& filename_const,
		std::string defaultExtension,
		std::list< std::pair< std::string, std::string > > types) const override;
};

class AP_Win32RDFEvent : public AP_RDFEvent
{
public:
	AP_Win32RDFEvent(PD_DocumentRDFHandle rdf, PD_ResultBindings_t::iterator& it)
		: AP_RDFEvent(rdf, it)
	{
	}

	virtual void* createEditor() override
	{
		return nullptr;
	}

	virtual void updateFromEditorData(PD_DocumentRDFMutationHandle) override
	{
	}
};

class AP_Win32RDFLocation : public AP_RDFLocation
{
public:
	AP_Win32RDFLocation(PD_DocumentRDFHandle rdf, PD_ResultBindings_t::iterator& it, bool isGeo84)
		: AP_RDFLocation(rdf, it, isGeo84)
	{
	}

	virtual void* createEditor() override
	{
		return nullptr;
	}

	virtual void updateFromEditorData(PD_DocumentRDFMutationHandle) override
	{
	}
};

void AP_Win32RDFContact::initDialog(HWND dlg)
{
	const XAP_StringSet* strings = XAP_App::getApp()->getStringSet();
	{
		UT_Win32LocaleString title;
		title.fromUTF8(strings->getValue(AP_STRING_ID_DLG_RDF_Insert_NewContact));
		SetWindowTextW(dlg, title.c_str());
	}
	setLabel(dlg, AP_RID_DIALOG_RDFCONTACT_LBL_NAME,
			 strings->getValue(AP_STRING_ID_MENU_LABEL_RDF_SEMITEM_STYLESHEET_CONTACT_NAME));
	setLabel(dlg, AP_RID_DIALOG_RDFCONTACT_LBL_NICK,
			 strings->getValue(AP_STRING_ID_MENU_LABEL_RDF_SEMITEM_STYLESHEET_CONTACT_NICK));
	setLabel(dlg, AP_RID_DIALOG_RDFCONTACT_LBL_PHONE,
			 strings->getValue(AP_STRING_ID_DLG_RDF_Contact_Phone));
	setLabel(dlg, AP_RID_DIALOG_RDFCONTACT_LBL_EMAIL,
			 strings->getValue(AP_STRING_ID_DLG_RDF_Contact_Email));
	setLabel(dlg, AP_RID_DIALOG_RDFCONTACT_LBL_HOME,
			 strings->getValue(AP_STRING_ID_DLG_RDF_Contact_HomePage));
	setLabel(dlg, IDOK, strings->getValue(XAP_STRING_ID_DLG_OK));
	setLabel(dlg, IDCANCEL, strings->getValue(XAP_STRING_ID_DLG_Cancel));
	setEdit(dlg, AP_RID_DIALOG_RDFCONTACT_EBX_NAME, m_name);
	setEdit(dlg, AP_RID_DIALOG_RDFCONTACT_EBX_NICK, m_nick);
	setEdit(dlg, AP_RID_DIALOG_RDFCONTACT_EBX_PHONE, m_phone);
	setEdit(dlg, AP_RID_DIALOG_RDFCONTACT_EBX_EMAIL, m_email);
	setEdit(dlg, AP_RID_DIALOG_RDFCONTACT_EBX_HOME, m_homePage);
}

void AP_Win32RDFContact::readDialog(HWND dlg)
{
	m_inName = windowText(dlg, AP_RID_DIALOG_RDFCONTACT_EBX_NAME);
	m_inNick = windowText(dlg, AP_RID_DIALOG_RDFCONTACT_EBX_NICK);
	m_inPhone = windowText(dlg, AP_RID_DIALOG_RDFCONTACT_EBX_PHONE);
	m_inEmail = windowText(dlg, AP_RID_DIALOG_RDFCONTACT_EBX_EMAIL);
	m_inHome = windowText(dlg, AP_RID_DIALOG_RDFCONTACT_EBX_HOME);
}

INT_PTR CALLBACK AP_Win32RDFContact::dlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (msg == WM_INITDIALOG)
	{
		SetWindowLongPtrW(hwnd, DWLP_USER, lParam);
		AP_Win32RDFContact* self = reinterpret_cast<AP_Win32RDFContact*>(lParam);
		if (self)
			self->initDialog(hwnd);
		return TRUE;
	}
	AP_Win32RDFContact* self = reinterpret_cast<AP_Win32RDFContact*>(GetWindowLongPtrW(hwnd, DWLP_USER));
	if (!self || msg != WM_COMMAND)
		return FALSE;
	switch (LOWORD(wParam))
	{
	case IDOK:
		self->readDialog(hwnd);
		EndDialog(hwnd, IDOK);
		return TRUE;
	case IDCANCEL:
		EndDialog(hwnd, IDCANCEL);
		return TRUE;
	default:
		return FALSE;
	}
}

void AP_Win32RDFContact::updateFromEditorData(PD_DocumentRDFMutationHandle m)
{
	if (m_linkingSubject.toString().empty())
		m_linkingSubject = PD_URI(XAP_App::getApp()->createUUIDString());
	const std::string base = "http://xmlns.com/foaf/0.1/";
	setRDFType(m, base + "Person");
	updateTriple(m, m_name, m_inName, PD_URI(base + "name"));
	updateTriple(m, m_nick, m_inNick, PD_URI(base + "nick"));
	updateTriple(m, m_email, m_inEmail, PD_URI(base + "mbox"));
	updateTriple(m, m_homePage, m_inHome, PD_URI(base + "homepage"));
	updateTriple(m, m_phone, m_inPhone, PD_URI(base + "phone"));
}

void AP_Win32RDFContact::showEditorWindow(const PD_RDFSemanticItemHandle& c)
{
	XAP_Win32App* app = static_cast<XAP_Win32App*>(XAP_App::getApp());
	HWND parent = nullptr;
	if (XAP_Frame* frame = app->getLastFocussedFrame())
	{
		XAP_Win32FrameImpl* impl = static_cast<XAP_Win32FrameImpl*>(frame->getFrameImpl());
		if (impl)
			parent = impl->getTopLevelWindow();
	}
	const INT_PTR answer = DialogBoxParamW(app->getInstance(),
		MAKEINTRESOURCEW(AP_RID_DIALOG_RDFCONTACT),
		parent, dlgProc, reinterpret_cast<LPARAM>(this));
	if (answer != IDOK)
		return;
	PD_RDFSemanticItem::updateFromEditorData();
	reflowContactName(c);
}

void AP_Win32RDFContact::showEditorWindow(const PD_RDFSemanticItems& cl)
{
	for (PD_RDFSemanticItems::const_iterator it = cl.begin(); it != cl.end(); ++it)
	{
		if (it->get() == this)
		{
			showEditorWindow(*it);
			return;
		}
	}
}

std::string AP_Win32RDFContact::getImportFromFileName(const std::string& filename_const,
	std::list< std::pair< std::string, std::string > >) const
{
	if (!filename_const.empty())
		return filename_const;
	const XAP_StringSet* strings = XAP_App::getApp()->getStringSet();
	return askVCardPath(false, strings->getValue(AP_STRING_ID_MENU_LABEL_RDF_SEMITEM_NEW_CONTACT_FROM_FILE));
}

std::string AP_Win32RDFContact::getExportToFileName(const std::string& filename_const,
	std::string,
	std::list< std::pair< std::string, std::string > >) const
{
	if (!filename_const.empty())
		return filename_const;
	const XAP_StringSet* strings = XAP_App::getApp()->getStringSet();
	return askVCardPath(true, strings->getValue(AP_STRING_ID_MENU_LABEL_RDF_SEMITEM_NEW_CONTACT_FROM_FILE));
}

class AP_Win32SemanticItemFactory : public PD_SemanticItemFactory
{
public:
	AP_Win32SemanticItemFactory()
	{
		PD_DocumentRDF::setSemanticItemFactory(this);
	}

	virtual PD_RDFContact* createContact(PD_DocumentRDFHandle rdf, PD_ResultBindings_t::iterator it) override
	{
		return new AP_Win32RDFContact(rdf, it);
	}

	virtual PD_RDFEvent* createEvent(PD_DocumentRDFHandle rdf, PD_ResultBindings_t::iterator it) override
	{
		return new AP_Win32RDFEvent(rdf, it);
	}

	virtual PD_RDFLocation* createLocation(PD_DocumentRDFHandle rdf, PD_ResultBindings_t::iterator it, bool isGeo84 = false) override
	{
		return new AP_Win32RDFLocation(rdf, it, isGeo84);
	}
};

static AP_Win32SemanticItemFactory s_win32SemanticItems;

class AP_Win32RDFDialogs : public PD_RDFDialogs
{
public:
	AP_Win32RDFDialogs()
	{
		PD_DocumentRDF::setRDFDialogs(this);
	}

	virtual void runSemanticStylesheetsDialog(FV_View*) override
	{
	}

	virtual std::pair< PT_DocPosition, PT_DocPosition > runInsertReferenceDialog(FV_View* view) override
	{
		std::pair< PT_DocPosition, PT_DocPosition > none;
		if (!view)
			return none;
		PD_Document* doc = view->getDocument();
		PD_DocumentRDFHandle rdf = doc ? doc->getDocumentRDF() : PD_DocumentRDFHandle();
		if (!rdf)
			return none;
		RefChoice choice;
		choice.view = view;
		PD_RDFContacts contacts = rdf->getContacts();
		for (PD_RDFContacts::iterator it = contacts.begin(); it != contacts.end(); ++it)
			choice.names.push_back((*it)->name());
		XAP_Win32App* app = static_cast<XAP_Win32App*>(XAP_App::getApp());
		DialogBoxParamW(app->getInstance(),
			MAKEINTRESOURCEW(AP_RID_DIALOG_RDFREFERENCE),
			parentWindow(), refDlgProc, reinterpret_cast<LPARAM>(&choice));
		return none;
	}
};

static AP_Win32RDFDialogs s_win32RDFDialogs;
