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
#include "fl_BlockLayout.h"
#include "ev_EditMethod.h"
#include "ev_Menu_Actions.h"
#include "ev_Menu_Layouts.h"
#include "ev_Menu_Labels.h"
#include "xap_Menu_Layouts.h"
#include "ut_growbuf.h"
#include "ut_string.h"
#include "ut_string_class.h"
#include "ut_timer.h"
#include "ut_types.h"

#include "ABI_LLM_Client.h"

#ifdef TOOLKIT_GTK_ALL
#include <gtk/gtk.h>
#endif

#ifdef TOOLKIT_WIN
#include <windows.h>
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
const char * MENU_TOOLTIP = "Reformule la selection avec un modele de langage";
const char * METHOD_NAME = "llmGenerateAction";
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

std::string ucs_to_utf8(const UT_UCS4Char * text, UT_uint32 length)
{
	if (!text || !length)
		return std::string();
	UT_UCS4String ucs(text, length);
	const char * utf8 = ucs.utf8_str();
	return utf8 ? std::string(utf8) : std::string();
}

std::string selection_text(FV_View * view, PT_DocPosition & start, PT_DocPosition & end, bool & replace)
{
	replace = false;
	start = view->getPoint();
	end = start;
	if (!view->isSelectionEmpty())
	{
		UT_UCS4Char * text = nullptr;
		view->getSelectionText(text);
		PT_DocPosition anchor = view->getSelectionAnchor();
		PT_DocPosition point = view->getPoint();
		start = anchor < point ? anchor : point;
		end = anchor < point ? point : anchor;
		replace = start != end && text;
		std::string utf8 = ucs_to_utf8(text, text ? UT_UCS4_strlen(text) : 0);
		FREEP(text);
		return utf8;
	}

	fl_BlockLayout * block = view->getCurrentBlock();
	if (!block)
		return std::string();
	UT_GrowBuf buffer;
	if (!block->getBlockBuf(&buffer) || buffer.getLength() == 0)
		return std::string();
	start = block->getPosition(false);
	end = start + buffer.getLength();
	replace = true;
	return ucs_to_utf8(reinterpret_cast<const UT_UCS4Char *>(buffer.getPointer(0)), buffer.getLength());
}

void tell(XAP_Frame * frame, const std::string & message)
{
	if (!frame)
		return;
	frame->showMessageBox(message.c_str(), XAP_Dialog_MessageBox::b_O, XAP_Dialog_MessageBox::a_OK);
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
	XAP_Frame * frame = XAP_App::getApp() ? XAP_App::getApp()->getLastFocussedFrame() : nullptr;
	if (view && pending->error.empty())
	{
		PT_DocPosition docEnd = 0;
		view->getDocument()->getBounds(true, docEnd);
		if (pending->replace && pending->end <= docEnd && pending->start <= pending->end)
			view->cmdSelect(pending->start, pending->end);
		UT_UCS4String ucs(pending->text);
		view->cmdCharInsert(ucs.ucs4_str(), static_cast<UT_uint32>(ucs.size()));
		if (frame)
			frame->setStatusMessage("");
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
		"Envoyer", GTK_RESPONSE_ACCEPT,
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

INT_PTR CALLBACK settings_dialog(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam)
{
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
		SetDlgItemTextW(dialog, 105, widen(settings->prompt).c_str());
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
	dialog.dword(WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME);
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
	dialog.item(WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 112, 142, 70, 16, IDOK, 0x0080, L"Envoyer");
	dialog.item(WS_CHILD | WS_VISIBLE | WS_TABSTOP, 190, 142, 70, 16, IDCANCEL, 0x0080, L"Annuler");

	dialog.align();
	const INT_PTR answer = DialogBoxIndirectParamW(
		GetModuleHandleW(nullptr),
		reinterpret_cast<LPCDLGTEMPLATEW>(dialog.bytes.data()),
		nullptr,
		settings_dialog,
		reinterpret_cast<LPARAM>(&settings));
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

#endif

bool llm_generate(AV_View * view, EV_EditMethodCallData *)
{
	XAP_Frame * frame = XAP_App::getApp()->getLastFocussedFrame();
	FV_View * document = static_cast<FV_View *>(view ? view : (frame ? frame->getCurrentView() : nullptr));
	if (!document || !frame)
		return false;
	if (g_busy.load())
	{
		tell(frame, "Une requete est deja en cours.");
		return true;
	}
	if (g_worker.joinable())
		g_worker.join();

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

	PT_DocPosition start = 0;
	PT_DocPosition end = 0;
	bool replace = false;
	const std::string source = selection_text(document, start, end, replace);
	if (source.empty() && settings.prompt.empty())
	{
		tell(frame, "Selectionnez un passage ou ecrivez une consigne.");
		return true;
	}

	PendingInsert * pending = new PendingInsert();
	pending->view = document;
	pending->start = start;
	pending->end = end;
	pending->replace = replace;
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
		g_worker = std::thread(request_thread, settings, source, pending);
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

void add_to_menus()
{
	XAP_App * app = XAP_App::getApp();
	EV_EditMethodContainer * methods = app->getEditMethodContainer();
	methods->addEditMethod(new EV_EditMethod(METHOD_NAME, llm_generate, 0, ""));

	XAP_Menu_Factory * factory = app->getMenuFactory();
	XAP_Menu_Id identifier = factory->addNewMenuAfter("Main", nullptr, "&Word Count", EV_MLF_Normal);
	factory->addNewLabel(nullptr, identifier, MENU_LABEL, MENU_TOOLTIP);
	factory->addNewMenuAfter("contextText", nullptr, "Bullets and &Numbering", EV_MLF_Normal, identifier);
	app->getMenuActionSet()->addAction(new EV_Menu_Action(
		identifier, 0, 1, 0, 0, METHOD_NAME, nullptr, nullptr));

	const int frames = app->getFrameCount();
	for (int i = 0; i < frames; ++i)
		app->getFrame(i)->rebuildMenus();
}

void remove_from_menus()
{
	XAP_App * app = XAP_App::getApp();
	EV_EditMethodContainer * methods = app->getEditMethodContainer();
	EV_EditMethod * method = ev_EditMethod_lookup(METHOD_NAME);
	if (method)
	{
		methods->removeEditMethod(method);
		DELETEP(method);
	}
	XAP_Menu_Factory * factory = app->getMenuFactory();
	factory->removeMenuItem("Main", nullptr, MENU_LABEL);
	factory->removeMenuItem("contextText", nullptr, MENU_LABEL);
	const int frames = app->getFrameCount();
	for (int i = 0; i < frames; ++i)
		app->getFrame(i)->rebuildMenus();
}

} // namespace

ABI_BUILTIN_FAR_CALL
int abi_plugin_register(XAP_ModuleInfo * info)
{
	info->name = "LLM plugin";
	info->desc = "Assistant de redaction branche sur Ollama, OpenAI, Anthropic ou Gemini.";
	info->version = ABI_VERSION_STRING;
	info->author = "AbiWord";
	info->usage = "Outils > Assistant LLM";
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
	if (leftover)
		apply_pending(leftover);
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
