#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>

#include <curl/curl.h>

#include "ABI_LLM_Client.h"

namespace {

void append_utf8(std::string & out, unsigned long code)
{
	if (code < 0x80)
	{
		out.push_back(static_cast<char>(code));
	}
	else if (code < 0x800)
	{
		out.push_back(static_cast<char>(0xC0 | (code >> 6)));
		out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
	}
	else if (code < 0x10000)
	{
		out.push_back(static_cast<char>(0xE0 | (code >> 12)));
		out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
	}
	else
	{
		out.push_back(static_cast<char>(0xF0 | (code >> 18)));
		out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
	}
}

std::string json_escape(const std::string & text)
{
	std::string out;
	out.reserve(text.size() + 8);
	for (unsigned char c : text)
	{
		switch (c)
		{
		case '"': out += "\\\""; break;
		case '\\': out += "\\\\"; break;
		case '\n': out += "\\n"; break;
		case '\r': out += "\\r"; break;
		case '\t': out += "\\t"; break;
		default:
			if (c < 0x20)
			{
				char buf[8];
				std::snprintf(buf, sizeof(buf), "\\u%04x", c);
				out += buf;
			}
			else
			{
				out.push_back(static_cast<char>(c));
			}
			break;
		}
	}
	return out;
}

int hex_value(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

std::string json_unescape(const std::string & raw)
{
	std::string out;
	unsigned long pending = 0;
	for (std::size_t i = 0; i < raw.size(); ++i)
	{
		char c = raw[i];
		unsigned long code = 0;
		if (c != '\\' || i + 1 >= raw.size())
		{
			code = static_cast<unsigned char>(c);
		}
		else
		{
			char e = raw[++i];
			switch (e)
			{
			case '"':
			case '\\':
			case '/': code = static_cast<unsigned char>(e); break;
			case 'b': code = '\b'; break;
			case 'f': code = '\f'; break;
			case 'n': code = '\n'; break;
			case 'r': code = '\r'; break;
			case 't': code = '\t'; break;
			case 'u':
				if (i + 4 < raw.size())
				{
					int h0 = hex_value(raw[i + 1]);
					int h1 = hex_value(raw[i + 2]);
					int h2 = hex_value(raw[i + 3]);
					int h3 = hex_value(raw[i + 4]);
					if (h0 >= 0 && h1 >= 0 && h2 >= 0 && h3 >= 0)
					{
						code = (h0 << 12) | (h1 << 8) | (h2 << 4) | h3;
						i += 4;
						break;
					}
				}
				code = 'u';
				break;
			default:
				code = static_cast<unsigned char>(e);
				break;
			}
		}
		if (pending)
		{
			if (code >= 0xDC00 && code <= 0xDFFF)
			{
				unsigned long full = 0x10000 + (((pending - 0xD800) << 10) | (code - 0xDC00));
				append_utf8(out, full);
				pending = 0;
				continue;
			}
			append_utf8(out, pending);
			pending = 0;
		}
		if (code >= 0xD800 && code <= 0xDBFF)
		{
			pending = code;
			continue;
		}
		append_utf8(out, code);
	}
	if (pending)
		append_utf8(out, pending);
	return out;
}

std::string last_string_value(const std::string & json, const char * key)
{
	const std::string needle = std::string("\"") + key + "\"";
	std::string last;
	for (std::size_t at = json.find(needle); at != std::string::npos; at = json.find(needle, at + needle.size()))
	{
		std::size_t i = at + needle.size();
		while (i < json.size() && (json[i] == ' ' || json[i] == '\t' || json[i] == '\r' || json[i] == '\n'))
			++i;
		if (i >= json.size() || json[i] != ':')
			continue;
		++i;
		while (i < json.size() && (json[i] == ' ' || json[i] == '\t' || json[i] == '\r' || json[i] == '\n'))
			++i;
		if (i >= json.size() || json[i] != '"')
			continue;
		++i;
		std::string raw;
		while (i < json.size())
		{
			if (json[i] == '\\' && i + 1 < json.size())
			{
				raw.push_back('\\');
				raw.push_back(json[i + 1]);
				i += 2;
				continue;
			}
			if (json[i] == '"')
				break;
			raw.push_back(json[i]);
			++i;
		}
		last = json_unescape(raw);
	}
	return last;
}

std::string provider_key(std::string provider)
{
	for (char & c : provider)
	{
		if (c >= 'A' && c <= 'Z')
			c = static_cast<char>(c - 'A' + 'a');
	}
	return provider;
}

std::string default_endpoint(const ABI_LLM_Settings & settings)
{
	const std::string provider = provider_key(settings.provider);
	if (provider == "openai")
		return "https://api.openai.com/v1/chat/completions";
	if (provider == "anthropic")
		return "https://api.anthropic.com/v1/messages";
	if (provider == "gemini")
	{
		std::string url = "https://generativelanguage.googleapis.com/v1beta/models/";
		url += settings.model.empty() ? "gemini-2.0-flash" : settings.model;
		url += ":generateContent";
		return url;
	}
	return "http://127.0.0.1:11434/v1/chat/completions";
}

std::string request_body(const ABI_LLM_Settings & settings, const std::string & user)
{
	const std::string provider = provider_key(settings.provider);
	const std::string model = json_escape(settings.model);
	const std::string content = json_escape(user);
	const std::string system = json_escape(
		"Tu es un assistant d'ecriture dans AbiWord. "
		"Reponds uniquement par le texte a inserer dans le document.");
	if (provider == "anthropic")
	{
		return std::string("{\"model\":\"") + model
			+ "\",\"max_tokens\":2048,\"system\":\"" + system
			+ "\",\"messages\":[{\"role\":\"user\",\"content\":\"" + content + "\"}]}";
	}
	if (provider == "gemini")
	{
		return std::string("{\"contents\":[{\"parts\":[{\"text\":\"")
			+ system + "\\n\\n" + content + "\"}]}]}";
	}
	return std::string("{\"model\":\"") + model
		+ "\",\"messages\":[{\"role\":\"system\",\"content\":\"" + system
		+ "\"},{\"role\":\"user\",\"content\":\"" + content + "\"}]}";
}

size_t curl_write(char * ptr, size_t size, size_t nmemb, void * userdata)
{
	const size_t bytes = size * nmemb;
	static_cast<std::string *>(userdata)->append(ptr, bytes);
	return bytes;
}

int curl_abort(void * userdata, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
{
	auto * abort = static_cast<std::atomic<bool> *>(userdata);
	return abort && abort->load() ? 1 : 0;
}

} // namespace

std::atomic<bool> abi_llm_stop_requested{false};

void abi_llm_apply_defaults(ABI_LLM_Settings & settings)
{
	if (settings.provider.empty())
		settings.provider = "ollama";
	if (settings.model.empty())
	{
		const std::string provider = provider_key(settings.provider);
		if (provider == "openai")
			settings.model = "gpt-4o-mini";
		else if (provider == "anthropic")
			settings.model = "claude-3-5-haiku-latest";
		else if (provider == "gemini")
			settings.model = "gemini-2.0-flash";
		else
			settings.model = "llama3.2";
	}
	if (settings.endpoint.empty())
		settings.endpoint = default_endpoint(settings);
	if (settings.prompt.empty())
		settings.prompt = "Reponds a la demande en tenant compte du document.";
}

std::string abi_llm_request_url(const ABI_LLM_Settings & incoming)
{
	ABI_LLM_Settings settings = incoming;
	abi_llm_apply_defaults(settings);
	std::string url = settings.endpoint;
	const std::string provider = provider_key(settings.provider);
	if (provider == "gemini"
		&& url.find("generativelanguage.googleapis.com") != std::string::npos
		&& url.find(settings.model) == std::string::npos)
		url = default_endpoint(settings);
	if (provider == "gemini" && !settings.apiKey.empty() && url.find("key=") == std::string::npos)
		url += (url.find('?') == std::string::npos ? "?key=" : "&key=") + settings.apiKey;
	return url;
}

std::string abi_llm_request_json(const ABI_LLM_Settings & incoming, const std::string & documentText)
{
	ABI_LLM_Settings settings = incoming;
	abi_llm_apply_defaults(settings);
	std::string user = settings.prompt;
	if (!documentText.empty())
	{
		user += "\n\n";
		user += documentText;
	}
	return request_body(settings, user);
}

ABI_LLM_Result abi_llm_interpret(long status, const std::string & response)
{
	ABI_LLM_Result result;
	result.ok = false;
	std::string text = last_string_value(response, "content");
	if (text.empty())
		text = last_string_value(response, "text");
	if (status >= 400 || text.empty())
	{
		std::string message = last_string_value(response, "message");
		if (message.empty())
			message = "Le modele n'a pas renvoye de texte.";
		if (status >= 400)
		{
			char buf[32];
			std::snprintf(buf, sizeof(buf), "HTTP %ld : ", status);
			message.insert(0, buf);
		}
		result.error = message;
		return result;
	}
	result.ok = true;
	result.text = text;
	return result;
}

ABI_LLM_Result abi_llm_complete(const ABI_LLM_Settings & incoming, const std::string & documentText)
{
	ABI_LLM_Result result;
	result.ok = false;
	ABI_LLM_Settings settings = incoming;
	abi_llm_apply_defaults(settings);
	const std::string url = abi_llm_request_url(settings);
	const std::string body = abi_llm_request_json(settings, documentText);
	const std::string provider = provider_key(settings.provider);
	std::string response;
	char errorBuffer[CURL_ERROR_SIZE];
	errorBuffer[0] = '\0';

	CURL * curl = curl_easy_init();
	if (!curl)
	{
		result.error = "libcurl n'a pas pu demarrer.";
		return result;
	}

	struct curl_slist * headers = nullptr;
	headers = curl_slist_append(headers, "Content-Type: application/json");
	std::string auth;
	std::string anthropicKey;
	if (provider == "anthropic")
	{
		headers = curl_slist_append(headers, "anthropic-version: 2023-06-01");
		if (!settings.apiKey.empty())
		{
			anthropicKey = "x-api-key: " + settings.apiKey;
			headers = curl_slist_append(headers, anthropicKey.c_str());
		}
	}
	else if (!settings.apiKey.empty() && provider != "gemini")
	{
		auth = "Authorization: Bearer " + settings.apiKey;
		headers = curl_slist_append(headers, auth.c_str());
	}

	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_POST, 1L);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
	curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorBuffer);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "AbiWord-LLM");
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, curl_abort);
	curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &abi_llm_stop_requested);

	const CURLcode code = curl_easy_perform(curl);
	long status = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);

	if (abi_llm_stop_requested)
	{
		result.error = "Requete interrompue.";
		return result;
	}
	if (code != CURLE_OK)
	{
		result.error = errorBuffer[0] ? errorBuffer : curl_easy_strerror(code);
		return result;
	}

	return abi_llm_interpret(status, response);
}
