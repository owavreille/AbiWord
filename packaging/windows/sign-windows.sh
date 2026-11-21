#!/bin/bash
# Signe des exécutables et DLL Windows (Authenticode SHA-256, horodaté).
# Variables : WINDOWS_CERT_BASE64 (PKCS#12), WINDOWS_CERT_PASSWORD.
# Sans certificat, le script ne modifie pas les fichiers.
# Usage : sign-windows.sh fichier-ou-dossier ...
set -euo pipefail

if [[ $# -lt 1 ]]; then
	echo "usage: sign-windows.sh fichier-ou-dossier ..." >&2
	exit 2
fi

mark_signed() {
	if [[ -n "${GITHUB_ENV:-}" ]]; then
		echo "INSTALLER_SIGNED=$1" >> "$GITHUB_ENV"
	fi
}

if [[ -z "${WINDOWS_CERT_BASE64:-}" ]]; then
	echo "Aucun certificat WINDOWS_CERT_BASE64 : les fichiers restent non signés."
	mark_signed no
	exit 0
fi

if ! command -v osslsigncode >/dev/null 2>&1; then
	echo "osslsigncode est absent." >&2
	exit 1
fi

PFX=$(mktemp)
trap 'rm -f "$PFX"' EXIT
umask 077
	printf '%s' "$WINDOWS_CERT_BASE64" | tr -d ' \t\r\n' | base64 -d > "$PFX"

help=$(osslsigncode sign -h 2>&1 || true)
if grep -q -- '-ts ' <<<"$help"; then
	timestamp_flag=-ts
else
	timestamp_flag=-t
fi

timestamps=(
	https://timestamp.sectigo.com
	http://timestamp.digicert.com
	http://timestamp.globalsign.com/tsa/r6advanced1
)

sign_one() {
	local src=$1
	local tmp ts ok=0
	tmp=$(mktemp "${src}.signed.XXXXXX")
	for ts in "${timestamps[@]}"; do
		if osslsigncode sign \
			-pkcs12 "$PFX" \
			-pass "${WINDOWS_CERT_PASSWORD:-}" \
			-n "AbiWord" \
			-i "https://github.com/owavreille/AbiWord" \
			-h sha256 \
			"$timestamp_flag" "$ts" \
			-in "$src" \
			-out "$tmp"
		then
			ok=1
			break
		fi
		rm -f "$tmp"
		tmp=$(mktemp "${src}.signed.XXXXXX")
	done
	if [[ $ok -ne 1 ]]; then
		rm -f "$tmp"
		echo "signature échouée : $src" >&2
		exit 1
	fi
	verify_out=$(osslsigncode verify -in "$tmp" 2>&1 || true)
	if ! grep -Eq 'Succeeded|Signature verification: ok' <<<"$verify_out"; then
		rm -f "$tmp"
		printf '%s\n' "$verify_out" >&2
		echo "vérification de la signature échouée : $src" >&2
		exit 1
	fi
	mv "$tmp" "$src"
	echo "signé : $src"
}

declare -a targets=()
for arg in "$@"; do
	if [[ -d $arg ]]; then
		while IFS= read -r -d '' path; do
			targets+=("$path")
		done < <(find "$arg" -type f \( -name '*.exe' -o -name '*.dll' \) -print0)
	elif [[ -f $arg ]]; then
		targets+=("$arg")
	else
		echo "introuvable : $arg" >&2
		exit 1
	fi
done

signed_any=0
for path in "${targets[@]}"; do
	if file -b "$path" | grep -q 'PE32'; then
		sign_one "$path"
		signed_any=1
	fi
done

if [[ $signed_any -eq 1 ]]; then
	mark_signed yes
else
	echo "aucun exécutable PE à signer" >&2
	exit 1
fi
