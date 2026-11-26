#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <cstdio>
#include <string>

#include "ABI_LLM_Client.h"

namespace {

int g_failures = 0;

void expect(bool condition, const char * message)
{
	if (condition)
		return;
	std::fprintf(stderr, "echec: %s\n", message);
	++g_failures;
}

void expect_has(const std::string & text, const char * fragment, const char * message)
{
	if (text.find(fragment) != std::string::npos)
		return;
	std::fprintf(stderr, "echec: %s\n  texte: %s\n  absent: %s\n", message, text.c_str(), fragment);
	++g_failures;
}

void expect_eq(const std::string & text, const char * wanted, const char * message)
{
	if (text == wanted)
		return;
	std::fprintf(stderr, "echec: %s\n  obtenu: %s\n  voulu:  %s\n", message, text.c_str(), wanted);
	++g_failures;
}

} // namespace

int main()
{
	ABI_LLM_Settings empty;
	abi_llm_apply_defaults(empty);
	expect_eq(empty.provider, "ollama", "le fournisseur vide devient Ollama");
	expect_eq(empty.model, "llama3.2", "le modele local par defaut");
	expect_eq(empty.endpoint, "http://127.0.0.1:11434/v1/chat/completions", "l'adresse locale par defaut");
	expect_eq(empty.prompt, "Reponds a la demande en tenant compte du document.", "la consigne par defaut");

	ABI_LLM_Settings openai;
	openai.provider = "OpenAI";
	openai.apiKey = "secret";
	abi_llm_apply_defaults(openai);
	expect_eq(openai.model, "gpt-4o-mini", "OpenAI recoit son modele");
	expect_eq(abi_llm_request_url(openai), "https://api.openai.com/v1/chat/completions", "l'adresse OpenAI");
	const std::string openaiBody = abi_llm_request_json(openai, "Il dit \"bonjour\"\npuis part.");
	expect_has(openaiBody, "\"model\":\"gpt-4o-mini\"", "le corps OpenAI nomme le modele");
	expect_has(openaiBody, "Il dit \\\"bonjour\\\"\\npuis part.", "les guillemets et le retour sont echappes");
	expect(openaiBody.find("secret") == std::string::npos, "la cle n'est pas dans le corps OpenAI");

	ABI_LLM_Settings kept = openai;
	kept.endpoint = "http://127.0.0.1:8080/v1/chat/completions";
	kept.model = "local-model";
	abi_llm_apply_defaults(kept);
	expect_eq(abi_llm_request_url(kept), "http://127.0.0.1:8080/v1/chat/completions", "une adresse libre est conservee");
	expect_has(abi_llm_request_json(kept, ""), "\"model\":\"local-model\"", "un modele libre est conserve");

	ABI_LLM_Settings anthropic;
	anthropic.provider = "anthropic";
	const std::string anthropicBody = abi_llm_request_json(anthropic, "Texte");
	expect_has(anthropicBody, "\"model\":\"claude-3-5-haiku-latest\"", "le modele Anthropic");
	expect_has(anthropicBody, "\"max_tokens\":2048", "Anthropic limite la reponse");
	expect_has(anthropicBody, "\"system\":", "Anthropic recoit la consigne systeme");
	expect(anthropicBody.find("\"role\":\"system\"") == std::string::npos, "Anthropic n'utilise pas le role systeme d'OpenAI");

	ABI_LLM_Settings gemini;
	gemini.provider = "gemini";
	gemini.apiKey = "clef";
	expect_eq(abi_llm_request_url(gemini),
		  "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.0-flash:generateContent?key=clef",
		  "Gemini place le modele et la cle dans l'adresse");
	const std::string geminiBody = abi_llm_request_json(gemini, "Suite");
	expect_has(geminiBody, "\"contents\":[{\"parts\":[{\"text\":", "le corps Gemini");
	expect(geminiBody.find("clef") == std::string::npos, "la cle Gemini reste hors du corps");

	ABI_LLM_Settings geminiModel;
	geminiModel.provider = "gemini";
	geminiModel.model = "gemini-2.5-pro";
	geminiModel.endpoint = "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.0-flash:generateContent";
	expect_has(abi_llm_request_url(geminiModel), "models/gemini-2.5-pro:generateContent", "un modele Gemini remplace l'adresse officielle");

	ABI_LLM_Result openaiAnswer = abi_llm_interpret(200,
		"{\"choices\":[{\"message\":{\"role\":\"assistant\",\"content\":\"Bonjour\"}}]}");
	expect(openaiAnswer.ok, "une reponse OpenAI est acceptee");
	expect_eq(openaiAnswer.text, "Bonjour", "le texte OpenAI");

	ABI_LLM_Result anthropicAnswer = abi_llm_interpret(200,
		"{\"content\":[{\"type\":\"text\",\"text\":\"Salut\"}]}");
	expect(anthropicAnswer.ok, "une reponse Anthropic est acceptee");
	expect_eq(anthropicAnswer.text, "Salut", "le texte Anthropic");

	ABI_LLM_Result anthropicMulti = abi_llm_interpret(200,
		"{\"content\":[{\"type\":\"text\",\"text\":\"Premiere\"},{\"type\":\"text\",\"text\":\"Seconde\"}]}");
	expect(anthropicMulti.ok, "une reponse Anthropic multi-blocs est acceptee");
	expect_eq(anthropicMulti.text, "Premiere\nSeconde", "les blocs Anthropic sont separes par un saut de ligne");

	ABI_LLM_Result geminiAnswer = abi_llm_interpret(200,
		"{\"candidates\":[{\"content\":{\"parts\":[{\"text\":\"Voici\"}]}}]}");
	expect(geminiAnswer.ok, "une reponse Gemini est acceptee");
	expect_eq(geminiAnswer.text, "Voici", "le texte Gemini");

	ABI_LLM_Result geminiMulti = abi_llm_interpret(200,
		"{\"candidates\":[{\"content\":{\"parts\":[{\"text\":\"Ligne 1\"},{\"text\":\"Ligne 2\"}]}}]}");
	expect(geminiMulti.ok, "une reponse Gemini multi-blocs est acceptee");
	expect_eq(geminiMulti.text, "Ligne 1\nLigne 2", "les blocs Gemini sont separes par un saut de ligne");

	ABI_LLM_Result embeddedKey = abi_llm_interpret(200,
		"{\"choices\":[{\"message\":{\"role\":\"assistant\",\"content\":\"Exemple {\\\"content\\\":\\\"faux\\\"} ok\"}}]}");
	expect(embeddedKey.ok, "une cle imbriquee dans le texte ne perturbe pas le parseur");
	expect_eq(embeddedKey.text, "Exemple {\"content\":\"faux\"} ok", "le texte contenant une cle JSON est preserve");

	ABI_LLM_Result last = abi_llm_interpret(200,
		"{\"content\":\"premier\",\"content\":\"second\"}");
	expect_eq(last.text, "second", "la derniere valeur content gagne");

	ABI_LLM_Result accent = abi_llm_interpret(200, "{\"content\":\"caf\\u00e9\"}");
	expect_eq(accent.text, "caf\xc3\xa9", "un echappement unicode devient de l'UTF-8");

	ABI_LLM_Result emoji = abi_llm_interpret(200, "{\"content\":\"\\uD83D\\uDE00\"}");
	expect_eq(emoji.text, "\xf0\x9f\x98\x80", "une paire de substituts devient un caractere UTF-8");

	ABI_LLM_Result rawUtf8 = abi_llm_interpret(200,
		"{\"content\":\"\xc3\xa9\xc3\xa0\xc3\xa8\xc3\xb9\xc3\xa7 \xe2\x80\x94 \xe2\x82\xac\"}");
	expect(rawUtf8.ok, "une reponse contenant de l'UTF-8 brut est acceptee");
	expect_eq(rawUtf8.text, "\xc3\xa9\xc3\xa0\xc3\xa8\xc3\xb9\xc3\xa7 \xe2\x80\x94 \xe2\x82\xac",
		  "l'UTF-8 brut n'est pas double-encode");

	ABI_LLM_Result mixedUtf8 = abi_llm_interpret(200,
		"{\"content\":\"Caf\xc3\xa9 \\u00e9l\xc3\xa8ve\"}");
	expect_eq(mixedUtf8.text, "Caf\xc3\xa9 \xc3\xa9l\xc3\xa8ve",
		  "l'UTF-8 brut et les echappements unicode cohabitent");

	ABI_LLM_Result denied = abi_llm_interpret(401, "{\"error\":{\"message\":\"cle refusee\"}}");
	expect(!denied.ok, "une erreur HTTP n'est pas un texte a inserer");
	expect_eq(denied.error, "HTTP 401 : cle refusee", "le message d'erreur est conserve");

	ABI_LLM_Result emptyAnswer = abi_llm_interpret(200, "{}");
	expect(!emptyAnswer.ok, "une reponse sans texte est refusee");
	expect_eq(emptyAnswer.error, "Le modele n'a pas renvoye de texte.", "l'absence de texte est expliquee");

	if (g_failures != 0)
	{
		std::fprintf(stderr, "%d echec(s)\n", g_failures);
		return 1;
	}
	return 0;
}
