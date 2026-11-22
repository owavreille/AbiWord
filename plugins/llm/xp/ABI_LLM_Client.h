/* AbiWord
 *
 * Plugin d'outil : appelle un modèle de langage et renvoie le texte à insérer.
 * Ce n'est pas un filtre IE_Imp / IE_Exp.
 */

#ifndef ABI_LLM_CLIENT_H
#define ABI_LLM_CLIENT_H

#include <atomic>
#include <string>

struct ABI_LLM_Settings
{
	std::string provider;
	std::string endpoint;
	std::string model;
	std::string apiKey;
	std::string prompt;
};

struct ABI_LLM_Result
{
	bool ok;
	std::string text;
	std::string error;
};

/* Remplit les champs absents avec Ollama sur la machine locale. */
void abi_llm_apply_defaults(ABI_LLM_Settings & settings);

/* Adresse et corps JSON, après application des valeurs par défaut. */
std::string abi_llm_request_url(const ABI_LLM_Settings & settings);
std::string abi_llm_request_json(const ABI_LLM_Settings & settings,
				 const std::string & documentText);

/* Lit une réponse déjà reçue. Aucun appel réseau. */
ABI_LLM_Result abi_llm_interpret(long httpStatus, const std::string & response);

/* Appel bloquant. À lancer hors du thread de l'interface. */
ABI_LLM_Result abi_llm_complete(const ABI_LLM_Settings & settings,
				const std::string & documentText);

/* Lu par libcurl pendant le transfert. Le déchargement du plugin le met à vrai. */
extern std::atomic<bool> abi_llm_stop_requested;

#endif
