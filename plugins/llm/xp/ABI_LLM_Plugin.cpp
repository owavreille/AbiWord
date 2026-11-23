#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <atomic>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include <curl/curl.h>

#include "xap_Module.h"
#include "xap_App.h"
#include "xap_Frame.h"
#include "xap_Prefs.h"
#include "xap_Dlg_MessageBox.h"
#include "fv_View.h"
#include "pd_Document.h"
#include "pt_PieceTable.h"
#include "pf_Frag.h"
#include "pf_Frag_Text.h"
#include "pf_Frag_Strux.h"
#include "ev_EditMethod.h"
#include "ev_Menu_Actions.h"
#include "ev_Menu_Layouts.h"
#include "ev_Menu_Labels.h"
#include "xap_Menu_Layouts.h"
#include "ap_Toolbar_Id.h"
#include "xap_Toolbar_Layouts.h"
#include "ut_string_class.h"
#include "ut_timer.h"
#include "ut_types.h"

#include "ABI_LLM_Client.h"

#ifdef TOOLKIT_GTK_ALL
#include <gtk/gtk.h>
#endif

#ifdef TOOLKIT_WIN
#include <windows.h>
#include "xap_Win32FrameImpl.h"
#endif

#ifdef ABI_PLUGIN_BUILTIN
#define abi_plugin_register abipgn_llm_register
#define abi_plugin_unregister abipgn_llm_unregister
#define abi_plugin_supports_version abipgn_llm_supports_version
#define ABI_BUILTIN_FAR_CALL extern "C"
#else
#define ABI_BUILTIN_FAR_CALL ABI_FAR_CALL
ABI_PLUGIN_DECLARE("LLM")
#endif

namespace {

const char * MENU_LABEL = "Assistant &LLM";
const char * MENU_TOOLTIP = "Configurer le fournisseur, le modele et la cle";
const char * CONFIG_METHOD = "llmConfigureAction";
const char * ASK_METHOD = "llmAskAction";
const char * STANDARD_TOOLBAR = "FileEditOps";
const char * SCHEME_NAME = "AbiLLM";
const char * KEY_PROVIDER = "provider";
const char * KEY_ENDPOINT = "endpoint";
const char * KEY_MODEL = "model";
const char * KEY_API = "api-key";
const char * KEY_PROMPT = "prompt";

struct PendingInsert
{
	FV_View * view;
	PT_DocPosition start;
	PT_DocPosition end;
	bool replace;
	std::string text;
	std::string error;
};

std::atomic<bool> g_busy(false);
std::atomic<PendingInsert *> g_ready(nullptr);
std::thread g_worker;
UT_Timer * g_timer = nullptr;

XAP_PrefsScheme * scheme()
{
	XAP_Prefs * prefs = XAP_App::getApp()->getPrefs();
	if (!prefs)
		return nullptr;
	XAP_PrefsScheme * found = prefs->getPluginScheme(SCHEME_NAME);
	if (!found)
	{
		prefs->addPluginScheme(new XAP_PrefsScheme(prefs, SCHEME_NAME));
		found = prefs->getPluginScheme(SCHEME_NAME);
	}
	return found;
}

std::string pref_or_empty(XAP_PrefsScheme * prefs, const char * key)
{
	std::string value;
	if (prefs && prefs->getValue(key, value))
		return value;
	return std::string();
}

ABI_LLM_Settings load_settings()
{
	XAP_PrefsScheme * prefs = scheme();
	ABI_LLM_Settings settings;
	settings.provider = pref_or_empty(prefs, KEY_PROVIDER);
	settings.endpoint = pref_or_empty(prefs, KEY_ENDPOINT);
	settings.model = pref_or_empty(prefs, KEY_MODEL);
	settings.apiKey = pref_or_empty(prefs, KEY_API);
	settings.prompt = pref_or_empty(prefs, KEY_PROMPT);
	abi_llm_apply_defaults(settings);
	return settings;
}

void save_settings(const ABI_LLM_Settings & settings)
{
	XAP_PrefsScheme * prefs = scheme();
	XAP_Prefs * root = XAP_App::getApp()->getPrefs();
	if (!prefs || !root)
		return;
	prefs->setValue(KEY_PROVIDER, settings.provider);
	prefs->setValue(KEY_ENDPOINT, settings.endpoint);
	prefs->setValue(KEY_MODEL, settings.model);
	prefs->setValue(KEY_API, settings.apiKey);
	prefs->setValue(KEY_PROMPT, settings.prompt);
	root->savePrefsFile();
}

std::string document_text(FV_View * view)
{
	PD_Document * doc = view ? view->getDocument() : nullptr;
	pt_PieceTable * table = doc ? doc->getPieceTable() : nullptr;
	if (!table)
		return std::string();
	std::string text;
	bool started = false;
	for (pf_Frag * frag = table->getFragments().getFirst(); frag; frag = frag->getNext())
	{
		if (frag->getType() == pf_Frag::PFT_Strux)
		{
			const PTStruxType kind = static_cast<const pf_Frag_Strux *>(frag)->getStruxType();
			const bool boundary = kind == PTX_Block || kind == PTX_Section
				|| kind == PTX_SectionHdrFtr || kind == PTX_SectionFootnote
				|| kind == PTX_SectionEndnote || kind == PTX_SectionAnnotation;
			if (started && boundary)
				text.push_back('\n');
		}
		else if (frag->getType() == pf_Frag::PFT_Text)
		{
			text += static_cast<const pf_Frag_Text *>(frag)->toString();
			started = true;
		}
	}
	return text;
}

void tell(XAP_Frame * frame, const std::string & message)
{
	if (!frame)
		return;
	frame->showMessageBox(message.c_str(), XAP_Dialog_MessageBox::b_O, XAP_Dialog_MessageBox::a_OK);
}

bool review_proposal(XAP_Frame * frame, const std::string & proposal);

void insert_proposal_page(FV_View * view, const std::string & proposal)
{
	if (!view)
		return;
	PD_Document * document = view->getDocument();
	if (!document)
		return;
	const bool hasExistingText = !document_text(view).empty();
	document->beginUserAtomicGlob();
	view->cmdUnselectSelection();
	PT_DocPosition docEnd = 0;
	document->getBounds(true, docEnd);
	view->moveInsPtTo(docEnd);
	if (hasExistingText)
	{
		const UT_UCS4Char page = UCS_FF;
		view->cmdCharInsert(&page, 1);
	}
	std::string normalized;
	normalized.reserve(proposal.size());
	for (std::size_t i = 0; i < proposal.size(); ++i)
	{
		if (proposal[i] == '\r')
		{
			if (i + 1 < proposal.size() && proposal[i + 1] == '\n')
				++i;
			normalized.push_back('\n');
		}
		else
		{
			normalized.push_back(proposal[i]);
		}
	}
	std::size_t start = 0;
	while (start <= normalized.size())
	{
		const std::size_t nl = normalized.find('\n', start);
		const std::string line = (nl == std::string::npos)
			? normalized.substr(start)
			: normalized.substr(start, nl - start);
		if (!line.empty())
		{
			UT_UCS4String utext(line);
			if (utext.size() > 0)
				view->cmdCharInsert(utext.ucs4_str(), static_cast<UT_uint32>(utext.size()));
		}
		if (nl == std::string::npos)
			break;
		view->insertParagraphBreak();
		start = nl + 1;
	}
	document->endUserAtomicGlob();
}

FV_View * live_view(FV_View * candidate)
{
	XAP_App * app = XAP_App::getApp();
	if (!app || !candidate)
		return nullptr;
	const int count = app->getFrameCount();
	for (int i = 0; i < count; ++i)
	{
		XAP_Frame * frame = app->getFrame(i);
		if (frame && frame->getCurrentView() == candidate)
			return candidate;
	}
	return nullptr;
}

void apply_pending(PendingInsert * pending)
{
	FV_View * view = live_view(pending->view);
	XAP_Frame * frame = view
		? static_cast<XAP_Frame *>(view->getParentData())
		: (XAP_App::getApp() ? XAP_App::getApp()->getLastFocussedFrame() : nullptr);
	if (frame)
		frame->setStatusMessage("");
	if (view && pending->error.empty())
	{
		const bool accepted = review_proposal(frame, pending->text);
		view = live_view(pending->view);
		frame = view ? static_cast<XAP_Frame *>(view->getParentData()) : nullptr;
		if (accepted && view)
		{
			insert_proposal_page(view, pending->text);
			if (frame)
				frame->setStatusMessage("Proposition ajoutee sur une nouvelle page.");
		}
		else if (frame)
			frame->setStatusMessage("Proposition refusee.");
	}
	else if (frame)
	{
		frame->setStatusMessage("");
		tell(frame, pending->error.empty() ? "Le document a change pendant la requete." : pending->error);
	}
	delete pending;
	g_busy.store(false);
}

void on_timer(UT_Worker *)
{
	PendingInsert * pending = g_ready.exchange(nullptr);
	if (!pending)
		return;
	apply_pending(pending);
	if (g_timer)
		g_timer->stop();
}

void request_thread(ABI_LLM_Settings settings, std::string documentText, PendingInsert * pending)
{
	ABI_LLM_Result result = abi_llm_complete(settings, documentText);
	if (abi_llm_stop_requested.load())
	{
		delete pending;
		g_busy.store(false);
		return;
	}
	if (result.ok)
		pending->text = result.text;
	else
		pending->error = result.error;
	g_ready.store(pending);
}

#ifdef TOOLKIT_GTK_ALL

std::string gtk_entry_text(GtkWidget * entry)
{
	const char * text = gtk_entry_get_text(GTK_ENTRY(entry));
	return text ? std::string(text) : std::string();
}

bool ask_settings(XAP_Frame * /*frame*/, ABI_LLM_Settings & settings)
{
	GtkWidget * dialog = gtk_dialog_new_with_buttons(
		"Assistant LLM",
		nullptr,
		static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL),
		"Annuler", GTK_RESPONSE_CANCEL,
		"Enregistrer", GTK_RESPONSE_ACCEPT,
		nullptr);
	GtkWidget * content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
	GtkWidget * grid = gtk_grid_new();
	gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
	gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
	gtk_widget_set_margin_start(grid, 8);
	gtk_widget_set_margin_end(grid, 8);
	gtk_widget_set_margin_top(grid, 8);
	gtk_widget_set_margin_bottom(grid, 8);
	gtk_container_add(GTK_CONTAINER(content), grid);

	const char * labels[] = {"Fournisseur", "Adresse", "Modele", "Cle API", "Consigne"};
	GtkWidget * entries[4];
	const std::string values[] = {settings.provider, settings.endpoint, settings.model, settings.apiKey};
	for (int i = 0; i < 4; ++i)
	{
		gtk_grid_attach(GTK_GRID(grid), gtk_label_new(labels[i]), 0, i, 1, 1);
		entries[i] = gtk_entry_new();
		gtk_entry_set_text(GTK_ENTRY(entries[i]), values[i].c_str());
		if (i == 3)
			gtk_entry_set_visibility(GTK_ENTRY(entries[i]), FALSE);
		gtk_widget_set_hexpand(entries[i], TRUE);
		gtk_grid_attach(GTK_GRID(grid), entries[i], 1, i, 1, 1);
	}
	gtk_grid_attach(GTK_GRID(grid), gtk_label_new(labels[4]), 0, 4, 2, 1);
	GtkWidget * promptView = gtk_text_view_new();
	GtkTextBuffer * buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(promptView));
	gtk_text_buffer_set_text(buffer, settings.prompt.c_str(), -1);
	gtk_widget_set_size_request(promptView, 420, 80);
	gtk_grid_attach(GTK_GRID(grid), promptView, 0, 5, 2, 1);
	gtk_widget_show_all(dialog);

	const gint response = gtk_dialog_run(GTK_DIALOG(dialog));
	const bool accepted = response == GTK_RESPONSE_ACCEPT;
	if (accepted)
	{
		settings.provider = gtk_entry_text(entries[0]);
		settings.endpoint = gtk_entry_text(entries[1]);
		settings.model = gtk_entry_text(entries[2]);
		settings.apiKey = gtk_entry_text(entries[3]);
		GtkTextIter begin;
		GtkTextIter end;
		gtk_text_buffer_get_bounds(buffer, &begin, &end);
		gchar * prompt = gtk_text_buffer_get_text(buffer, &begin, &end, FALSE);
		settings.prompt = prompt ? prompt : "";
		g_free(prompt);
	}
	gtk_widget_destroy(dialog);
	return accepted;
}

bool ask_request(XAP_Frame * /*frame*/, std::string & question)
{
	GtkWidget * dialog = gtk_dialog_new_with_buttons(
		"Demande a l'assistant",
		nullptr,
		static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL),
		"Annuler", GTK_RESPONSE_CANCEL,
		"Envoyer", GTK_RESPONSE_ACCEPT,
		nullptr);
	GtkWidget * content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
	GtkWidget * box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
	gtk_widget_set_margin_start(box, 8);
	gtk_widget_set_margin_end(box, 8);
	gtk_widget_set_margin_top(box, 8);
	gtk_widget_set_margin_bottom(box, 8);
	gtk_container_add(GTK_CONTAINER(content), box);
	gtk_box_pack_start(GTK_BOX(box), gtk_label_new("Demande"), FALSE, FALSE, 0);
	GtkWidget * promptView = gtk_text_view_new();
	GtkTextBuffer * buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(promptView));
	gtk_text_buffer_set_text(buffer, question.c_str(), -1);
	gtk_widget_set_size_request(promptView, 420, 120);
	gtk_box_pack_start(GTK_BOX(box), promptView, TRUE, TRUE, 0);
	gtk_widget_show_all(dialog);

	const gint response = gtk_dialog_run(GTK_DIALOG(dialog));
	const bool accepted = response == GTK_RESPONSE_ACCEPT;
	if (accepted)
	{
		GtkTextIter begin;
		GtkTextIter end;
		gtk_text_buffer_get_bounds(buffer, &begin, &end);
		gchar * prompt = gtk_text_buffer_get_text(buffer, &begin, &end, FALSE);
		question = prompt ? prompt : "";
		g_free(prompt);
	}
	gtk_widget_destroy(dialog);
	return accepted;
}

bool review_proposal(XAP_Frame * /*frame*/, const std::string & proposal)
{
	GtkWidget * dialog = gtk_dialog_new_with_buttons(
		"Proposition de l'assistant",
		nullptr,
		static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL),
		"Refuser", GTK_RESPONSE_CANCEL,
		"Accepter", GTK_RESPONSE_ACCEPT,
		nullptr);
	gtk_window_set_default_size(GTK_WINDOW(dialog), 680, 860);
	GtkWidget * content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
	GtkWidget * scroll = gtk_scrolled_window_new(nullptr, nullptr);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_widget_set_margin_start(scroll, 12);
	gtk_widget_set_margin_end(scroll, 12);
	gtk_widget_set_margin_top(scroll, 12);
	gtk_widget_set_margin_bottom(scroll, 12);
	gtk_widget_set_vexpand(scroll, TRUE);
	gtk_container_add(GTK_CONTAINER(content), scroll);
	GtkWidget * page = gtk_text_view_new();
	gtk_text_view_set_editable(GTK_TEXT_VIEW(page), FALSE);
	gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(page), FALSE);
	gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(page), GTK_WRAP_WORD_CHAR);
	gtk_text_view_set_left_margin(GTK_TEXT_VIEW(page), 28);
	gtk_text_view_set_right_margin(GTK_TEXT_VIEW(page), 28);
	gtk_text_view_set_pixels_above_lines(GTK_TEXT_VIEW(page), 4);
	GtkTextBuffer * buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(page));
	gtk_text_buffer_set_text(buffer, proposal.c_str(), -1);
	gtk_container_add(GTK_CONTAINER(scroll), page);
	gtk_widget_show_all(dialog);

	const gint response = gtk_dialog_run(GTK_DIALOG(dialog));
	const bool accepted = response == GTK_RESPONSE_ACCEPT;
	gtk_widget_destroy(dialog);
	return accepted;
}

#elif defined(TOOLKIT_WIN)

std::wstring widen(const std::string & text)
{
	if (text.empty())
		return std::wstring();
	const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
	std::wstring wide(size > 0 ? static_cast<size_t>(size) : 1, L'\0');
	if (size > 0)
		MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, &wide[0], size);
	if (!wide.empty() && wide.back() == L'\0')
		wide.pop_back();
	return wide;
}

std::string narrow(const std::wstring & text)
{
	if (text.empty())
		return std::string();
	const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
	std::string bytes(size > 0 ? static_cast<size_t>(size) : 1, '\0');
	if (size > 0)
		WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, &bytes[0], size, nullptr, nullptr);
	if (!bytes.empty() && bytes.back() == '\0')
		bytes.pop_back();
	return bytes;
}

std::string control_text(HWND dialog, int id)
{
	wchar_t buffer[8192];
	const UINT length = GetDlgItemTextW(dialog, id, buffer, 8192);
	return narrow(std::wstring(buffer, length));
}

std::string to_crlf(const std::string & text)
{
	std::string out;
	out.reserve(text.size() + 16);
	for (std::size_t i = 0; i < text.size(); ++i)
	{
		if (text[i] == '\n' && (i == 0 || text[i - 1] != '\r'))
			out.push_back('\r');
		out.push_back(text[i]);
	}
	return out;
}

HWND parent_hwnd(XAP_Frame * frame)
{
	if (!frame || !frame->getFrameImpl())
		return nullptr;
	return static_cast<XAP_Win32FrameImpl *>(frame->getFrameImpl())->getTopLevelWindow();
}

class DialogTemplate
{
public:
	void align()
	{
		while (bytes.size() % 4)
			bytes.push_back(0);
	}
	void word(WORD value)
	{
		bytes.push_back(static_cast<unsigned char>(value & 0xFF));
		bytes.push_back(static_cast<unsigned char>((value >> 8) & 0xFF));
	}
	void dword(DWORD value)
	{
		word(static_cast<WORD>(value & 0xFFFF));
		word(static_cast<WORD>((value >> 16) & 0xFFFF));
	}
	void wide(const wchar_t * text)
	{
		for (const wchar_t * cursor = text; *cursor; ++cursor)
			word(static_cast<WORD>(*cursor));
		word(0);
	}
	void item(DWORD style, short x, short y, short width, short height, WORD id, WORD klass, const wchar_t * title)
	{
		align();
		dword(style);
		dword(0);
		word(static_cast<WORD>(x));
		word(static_cast<WORD>(y));
		word(static_cast<WORD>(width));
		word(static_cast<WORD>(height));
		word(id);
		word(0xFFFF);
		word(klass);
		wide(title);
		word(0);
	}
	std::vector<unsigned char> bytes;
};

bool handle_drag_nchittest(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam)
{
	if (message != WM_NCHITTEST)
		return false;
	const LRESULT hit = DefWindowProcW(dialog, message, wparam, lparam);
	if (hit == HTCLIENT)
	{
		SetWindowLongPtrW(dialog, DWLP_MSGRESULT, HTCAPTION);
		return true;
	}
	return false;
}

INT_PTR CALLBACK settings_dialog(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam)
{
	if (handle_drag_nchittest(dialog, message, wparam, lparam))
		return TRUE;
	ABI_LLM_Settings * settings = reinterpret_cast<ABI_LLM_Settings *>(GetWindowLongPtrW(dialog, DWLP_USER));
	switch (message)
	{
	case WM_INITDIALOG:
		SetWindowLongPtrW(dialog, DWLP_USER, lparam);
		settings = reinterpret_cast<ABI_LLM_Settings *>(lparam);
		SetDlgItemTextW(dialog, 101, widen(settings->provider).c_str());
		SetDlgItemTextW(dialog, 102, widen(settings->endpoint).c_str());
		SetDlgItemTextW(dialog, 103, widen(settings->model).c_str());
		SetDlgItemTextW(dialog, 104, widen(settings->apiKey).c_str());
		SetDlgItemTextW(dialog, 105, widen(to_crlf(settings->prompt)).c_str());
		return TRUE;
	case WM_COMMAND:
		if (LOWORD(wparam) == IDOK && settings)
		{
			settings->provider = control_text(dialog, 101);
			settings->endpoint = control_text(dialog, 102);
			settings->model = control_text(dialog, 103);
			settings->apiKey = control_text(dialog, 104);
			settings->prompt = control_text(dialog, 105);
			EndDialog(dialog, IDOK);
			return TRUE;
		}
		if (LOWORD(wparam) == IDCANCEL)
		{
			EndDialog(dialog, IDCANCEL);
			return TRUE;
		}
		break;
	default:
		break;
	}
	return FALSE;
}

bool ask_settings(XAP_Frame * frame, ABI_LLM_Settings & settings)
{
	DialogTemplate dialog;
	dialog.dword(WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_CENTER);
	dialog.dword(0);
	dialog.word(12);
	dialog.word(10);
	dialog.word(10);
	dialog.word(280);
	dialog.word(168);
	dialog.word(0);
	dialog.word(0);
	dialog.wide(L"Assistant LLM");

	const DWORD labelStyle = WS_CHILD | WS_VISIBLE;
	const DWORD editStyle = WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL;
	dialog.item(labelStyle, 8, 8, 60, 12, static_cast<WORD>(-1), 0x0082, L"Fournisseur");
	dialog.item(editStyle, 72, 6, 196, 14, 101, 0x0081, L"");
	dialog.item(labelStyle, 8, 26, 60, 12, static_cast<WORD>(-1), 0x0082, L"Adresse");
	dialog.item(editStyle, 72, 24, 196, 14, 102, 0x0081, L"");
	dialog.item(labelStyle, 8, 44, 60, 12, static_cast<WORD>(-1), 0x0082, L"Modele");
	dialog.item(editStyle, 72, 42, 196, 14, 103, 0x0081, L"");
	dialog.item(labelStyle, 8, 62, 60, 12, static_cast<WORD>(-1), 0x0082, L"Cle API");
	dialog.item(editStyle | ES_PASSWORD, 72, 60, 196, 14, 104, 0x0081, L"");
	dialog.item(labelStyle, 8, 80, 60, 12, static_cast<WORD>(-1), 0x0082, L"Consigne");
	dialog.item(editStyle | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL, 72, 78, 196, 52, 105, 0x0081, L"");
	dialog.item(WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 112, 142, 70, 16, IDOK, 0x0080, L"Enregistrer");
	dialog.item(WS_CHILD | WS_VISIBLE | WS_TABSTOP, 190, 142, 70, 16, IDCANCEL, 0x0080, L"Annuler");

	dialog.align();
	const INT_PTR answer = DialogBoxIndirectParamW(
		GetModuleHandleW(nullptr),
		reinterpret_cast<LPCDLGTEMPLATEW>(dialog.bytes.data()),
		parent_hwnd(frame),
		settings_dialog,
		reinterpret_cast<LPARAM>(&settings));
	if (answer == -1)
		tell(frame, "Le dialogue Windows n'a pas pu s'ouvrir.");
	return answer == IDOK;
}

INT_PTR CALLBACK request_dialog(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam)
{
	if (handle_drag_nchittest(dialog, message, wparam, lparam))
		return TRUE;
	std::string * question = reinterpret_cast<std::string *>(GetWindowLongPtrW(dialog, DWLP_USER));
	switch (message)
	{
	case WM_INITDIALOG:
		SetWindowLongPtrW(dialog, DWLP_USER, lparam);
		question = reinterpret_cast<std::string *>(lparam);
		if (question)
			SetDlgItemTextW(dialog, 201, widen(to_crlf(*question)).c_str());
		return TRUE;
	case WM_COMMAND:
		if (LOWORD(wparam) == IDOK && question)
		{
			*question = control_text(dialog, 201);
			EndDialog(dialog, IDOK);
			return TRUE;
		}
		if (LOWORD(wparam) == IDCANCEL)
		{
			EndDialog(dialog, IDCANCEL);
			return TRUE;
		}
		break;
	default:
		break;
	}
	return FALSE;
}

bool ask_request(XAP_Frame * frame, std::string & question)
{
	DialogTemplate dialog;
	dialog.dword(WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_CENTER);
	dialog.dword(0);
	dialog.word(4);
	dialog.word(10);
	dialog.word(10);
	dialog.word(280);
	dialog.word(128);
	dialog.word(0);
	dialog.word(0);
	dialog.wide(L"Demande a l'assistant");

	const DWORD labelStyle = WS_CHILD | WS_VISIBLE;
	const DWORD editStyle = WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL
		| ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | ES_WANTRETURN;
	dialog.item(labelStyle, 8, 8, 80, 12, static_cast<WORD>(-1), 0x0082, L"Demande");
	dialog.item(editStyle, 8, 22, 260, 68, 201, 0x0081, L"");
	dialog.item(WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 112, 100, 70, 16, IDOK, 0x0080, L"Envoyer");
	dialog.item(WS_CHILD | WS_VISIBLE | WS_TABSTOP, 190, 100, 70, 16, IDCANCEL, 0x0080, L"Annuler");

	dialog.align();
	const INT_PTR answer = DialogBoxIndirectParamW(
		GetModuleHandleW(nullptr),
		reinterpret_cast<LPCDLGTEMPLATEW>(dialog.bytes.data()),
		parent_hwnd(frame),
		request_dialog,
		reinterpret_cast<LPARAM>(&question));
	if (answer == -1)
		tell(frame, "Le dialogue Windows n'a pas pu s'ouvrir.");
	return answer == IDOK;
}

INT_PTR CALLBACK proposal_dialog(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam)
{
	if (handle_drag_nchittest(dialog, message, wparam, lparam))
		return TRUE;
	const std::string * proposal = reinterpret_cast<const std::string *>(GetWindowLongPtrW(dialog, DWLP_USER));
	switch (message)
	{
	case WM_INITDIALOG:
		SetWindowLongPtrW(dialog, DWLP_USER, lparam);
		proposal = reinterpret_cast<const std::string *>(lparam);
		if (proposal)
		{
			SetDlgItemTextW(dialog, 301, widen(to_crlf(*proposal)).c_str());
		}
		return TRUE;
	case WM_COMMAND:
		if (LOWORD(wparam) == IDOK)
		{
			EndDialog(dialog, IDOK);
			return TRUE;
		}
		if (LOWORD(wparam) == IDCANCEL)
		{
			EndDialog(dialog, IDCANCEL);
			return TRUE;
		}
		break;
	default:
		break;
	}
	return FALSE;
}

bool review_proposal(XAP_Frame * frame, const std::string & proposal)
{
	DialogTemplate dialog;
	dialog.dword(WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | DS_MODALFRAME | DS_CENTER);
	dialog.dword(0);
	dialog.word(3);
	dialog.word(20);
	dialog.word(16);
	dialog.word(320);
	dialog.word(420);
	dialog.word(0);
	dialog.word(0);
	dialog.wide(L"Proposition de l'assistant");

	const DWORD pageStyle = WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | WS_TABSTOP
		| ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | ES_WANTRETURN;
	dialog.item(pageStyle, 8, 8, 304, 380, 301, 0x0081, L"");
	dialog.item(WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 168, 396, 70, 16, IDOK, 0x0080, L"Accepter");
	dialog.item(WS_CHILD | WS_VISIBLE | WS_TABSTOP, 244, 396, 70, 16, IDCANCEL, 0x0080, L"Refuser");

	dialog.align();
	const INT_PTR answer = DialogBoxIndirectParamW(
		GetModuleHandleW(nullptr),
		reinterpret_cast<LPCDLGTEMPLATEW>(dialog.bytes.data()),
		parent_hwnd(frame),
		proposal_dialog,
		reinterpret_cast<LPARAM>(&proposal));
	if (answer == -1)
		tell(frame, "Le dialogue Windows n'a pas pu s'ouvrir.");
	return answer == IDOK;
}

#else

bool ask_settings(XAP_Frame * frame, ABI_LLM_Settings &)
{
	tell(frame, "Le dialogue de l'assistant existe pour GTK et Windows.");
	return false;
}

bool ask_request(XAP_Frame * frame, std::string &)
{
	tell(frame, "Le dialogue de l'assistant existe pour GTK et Windows.");
	return false;
}

bool review_proposal(XAP_Frame * frame, const std::string &)
{
	tell(frame, "Le dialogue de l'assistant existe pour GTK et Windows.");
	return false;
}

#endif

bool llm_configure(AV_View *, EV_EditMethodCallData *)
{
	XAP_Frame * frame = XAP_App::getApp()->getLastFocussedFrame();
	if (!frame)
		return false;
	ABI_LLM_Settings settings = load_settings();
	const ABI_LLM_Settings previous = settings;
	if (!ask_settings(frame, settings))
		return true;
	if (settings.provider != previous.provider)
	{
		if (settings.endpoint == previous.endpoint)
			settings.endpoint.clear();
		if (settings.model == previous.model)
			settings.model.clear();
	}
	abi_llm_apply_defaults(settings);
	save_settings(settings);
	return true;
}

bool start_request(FV_View * document, XAP_Frame * frame, const ABI_LLM_Settings & settings, const std::string & payload)
{
	if (g_busy.load())
	{
		tell(frame, "Une requete est deja en cours.");
		return true;
	}
	if (g_worker.joinable())
		g_worker.join();

	PendingInsert * pending = new PendingInsert();
	pending->view = document;
	pending->start = document->getPoint();
	pending->end = pending->start;
	pending->replace = false;
	if (!g_timer)
		g_timer = UT_Timer::static_constructor(on_timer, nullptr);
	if (!g_timer || g_timer->set(200) != 0)
	{
		delete pending;
		tell(frame, "Le minuteur de l'interface n'a pas pu demarrer.");
		return true;
	}
	abi_llm_stop_requested.store(false);
	g_busy.store(true);
	frame->setStatusMessage("Assistant LLM : envoi...");
	try
	{
		g_worker = std::thread(request_thread, settings, payload, pending);
	}
	catch (const std::system_error &)
	{
		delete pending;
		g_busy.store(false);
		if (g_timer)
			g_timer->stop();
		frame->setStatusMessage("");
		tell(frame, "Le thread de la requete n'a pas pu demarrer.");
	}
	return true;
}

bool llm_ask(AV_View * view, EV_EditMethodCallData *)
{
	XAP_Frame * frame = XAP_App::getApp()->getLastFocussedFrame();
	FV_View * document = static_cast<FV_View *>(view ? view : (frame ? frame->getCurrentView() : nullptr));
	if (!document || !frame)
		return false;
	std::string question;
	if (!ask_request(frame, question))
		return true;
	document = live_view(document);
	if (!document)
		return false;
	frame = static_cast<XAP_Frame *>(document->getParentData());
	if (!frame)
		return false;
	if (question.empty())
	{
		tell(frame, "Ecrivez la demande a envoyer.");
		return true;
	}
	ABI_LLM_Settings settings = load_settings();
	std::string payload = "Demande:\n";
	payload += question;
	payload += "\n\nDocument:\n";
	payload += document_text(document);
	return start_request(document, frame, settings, payload);
}

void remove_method(const char * name)
{
	EV_EditMethodContainer * methods = XAP_App::getApp()->getEditMethodContainer();
	EV_EditMethod * method = ev_EditMethod_lookup(name);
	if (!method)
		return;
	methods->removeEditMethod(method);
	DELETEP(method);
}

void add_to_menus()
{
	XAP_App * app = XAP_App::getApp();
	EV_EditMethodContainer * methods = app->getEditMethodContainer();
	methods->addEditMethod(new EV_EditMethod(CONFIG_METHOD, llm_configure, 0, ""));
	methods->addEditMethod(new EV_EditMethod(ASK_METHOD, llm_ask, 0, ""));

	XAP_Menu_Factory * factory = app->getMenuFactory();
	XAP_Menu_Id identifier = factory->addNewMenuAfter("Main", nullptr, "&Word Count", EV_MLF_Normal);
	factory->addNewLabel(nullptr, identifier, MENU_LABEL, MENU_TOOLTIP);
	EV_Menu_Action * configure = new EV_Menu_Action(
		identifier, 0, 1, 0, 0, CONFIG_METHOD, nullptr, nullptr);
	configure->setIconName("Menu_AbiWord_Help");
	app->getMenuActionSet()->addAction(configure);
	app->getToolbarFactory()->addIconAfter(STANDARD_TOOLBAR, AP_TOOLBAR_ID_LLM, AP_TOOLBAR_ID_ZOOM);
	app->getToolbarFactory()->addSpacerAfter(STANDARD_TOOLBAR, AP_TOOLBAR_ID_ZOOM);

	const int frames = app->getFrameCount();
	for (int i = 0; i < frames; ++i)
	{
		XAP_Frame * frame = app->getFrame(i);
		frame->rebuildMenus();
		frame->rebuildAllToolbars();
	}
}

void remove_from_menus()
{
	remove_method(CONFIG_METHOD);
	remove_method(ASK_METHOD);
	XAP_App * app = XAP_App::getApp();
	XAP_Menu_Factory * factory = app->getMenuFactory();
	factory->removeMenuItem("Main", nullptr, MENU_LABEL);
	app->getToolbarFactory()->removeSpacerBefore(STANDARD_TOOLBAR, AP_TOOLBAR_ID_LLM);
	app->getToolbarFactory()->removeIcon(STANDARD_TOOLBAR, AP_TOOLBAR_ID_LLM);
	const int frames = app->getFrameCount();
	for (int i = 0; i < frames; ++i)
	{
		XAP_Frame * frame = app->getFrame(i);
		frame->rebuildMenus();
		frame->rebuildAllToolbars();
	}
}

} // namespace

ABI_BUILTIN_FAR_CALL
int abi_plugin_register(XAP_ModuleInfo * info)
{
	info->name = "LLM plugin";
	info->desc = "Assistant de redaction branche sur Ollama, OpenAI, Anthropic ou Gemini.";
	info->version = ABI_VERSION_STRING;
	info->author = "AbiWord";
	info->usage = "Outils > Assistant LLM pour la configuration, bouton de la barre standard pour envoyer une demande.";
	curl_global_init(CURL_GLOBAL_DEFAULT);
	add_to_menus();
	return 1;
}

ABI_BUILTIN_FAR_CALL
int abi_plugin_unregister(XAP_ModuleInfo * info)
{
	info->name = nullptr;
	info->desc = nullptr;
	info->version = nullptr;
	info->author = nullptr;
	info->usage = nullptr;
	abi_llm_stop_requested.store(true);
	if (g_worker.joinable())
		g_worker.join();
	PendingInsert * leftover = g_ready.exchange(nullptr);
	delete leftover;
	if (g_timer)
	{
		g_timer->stop();
		DELETEP(g_timer);
	}
	g_busy.store(false);
	remove_from_menus();
	curl_global_cleanup();
	return 1;
}

ABI_BUILTIN_FAR_CALL
int abi_plugin_supports_version(UT_uint32, UT_uint32, UT_uint32)
{
	return 1;
}
