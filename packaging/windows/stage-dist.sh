#!/bin/bash
# Assemble dist/windows-x86_64 from a MinGW build tree.
# Usage: stage-dist.sh BUILDDIR DEST
set -euo pipefail

BUILDDIR=${1:?répertoire de compilation}
DEST=${2:?répertoire de destination}
PREFIX=${PREFIX:-/usr/x86_64-w64-mingw32/sys-root/mingw}
OBJDUMP=${OBJDUMP:-x86_64-w64-mingw32-objdump}

rm -rf "$DEST"
mkdir -p \
	"$DEST/bin" \
	"$DEST/lib/abiword-3.1/plugins" \
	"$DEST/lib/enchant-2" \
	"$DEST/share/abiword-3.1/strings" \
	"$DEST/share/hunspell" \
	"$DEST/share/enchant-2"

cp "$BUILDDIR/src/.libs/abiword.exe" "$DEST/bin/"
cp "$BUILDDIR/src/.libs/libabiword-3.1.dll" "$DEST/bin/"
cp "$BUILDDIR/plugins/opendocument/.libs/opendocument.dll" \
	"$DEST/lib/abiword-3.1/plugins/"
cp "$BUILDDIR/plugins/openxml/.libs/openxml.dll" \
	"$DEST/lib/abiword-3.1/plugins/"
cp "$BUILDDIR"/po/*.strings "$DEST/share/abiword-3.1/strings/"
ROOT=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
cp "$ROOT/src/wp/ap/win/ToolbarIcons/LICENSE.fluent.txt" "$DEST/share/abiword-3.1/"

cp "$PREFIX/lib/enchant-2/enchant_hunspell.dll" "$DEST/lib/enchant-2/"
ordering=$(find "$PREFIX/share" -name enchant.ordering -print -quit)
if [[ -z "$ordering" ]]; then
	echo "enchant.ordering introuvable sous $PREFIX/share" >&2
	exit 1
fi
cp "$ordering" "$DEST/share/enchant-2/enchant.ordering"

shopt -s nullglob
dicts=(/usr/share/hunspell/*.dic)
if ((${#dicts[@]} == 0)); then
	echo "Aucun dictionnaire hunspell dans /usr/share/hunspell" >&2
	exit 1
fi
cp /usr/share/hunspell/*.dic /usr/share/hunspell/*.aff "$DEST/share/hunspell/"

mapfile -t search_dirs < <(
	printf '%s\n' "$PREFIX/bin" "$PREFIX/lib"
	find /usr/lib/gcc/x86_64-w64-mingw32 /usr/x86_64-w64-mingw32 -type d -name bin -o -type d -name 'lib*' 2>/dev/null \
		| sort -u
)

is_system() {
	local name=${1,,}
	case "$name" in
	kernel32.dll|user32.dll|gdi32.dll|gdiplus.dll|advapi32.dll|shell32.dll| \
	ole32.dll|oleaut32.dll|comctl32.dll|comdlg32.dll|ws2_32.dll|wsock32.dll| \
	shlwapi.dll|msvcrt.dll|ntdll.dll|imm32.dll|usp10.dll|winmm.dll| \
	winspool.drv|msimg32.dll|uxtheme.dll|dwmapi.dll|setupapi.dll|version.dll| \
	iphlpapi.dll|crypt32.dll|bcrypt.dll|secur32.dll|rpcrt4.dll|oleacc.dll| \
	psapi.dll|dbghelp.dll|mpr.dll|netapi32.dll|userenv.dll|wldap32.dll| \
	normaliz.dll|dnsapi.dll|mswsock.dll|nsi.dll|powrprof.dll|cfgmgr32.dll| \
	wintrust.dll|imagehlp.dll|opengl32.dll|glu32.dll|combase.dll|shcore.dll| \
	propsys.dll|api-ms-*.dll|ext-ms-*.dll)
		return 0
		;;
	esac
	return 1
}

find_dll() {
	local name=$1 dir
	for dir in "${search_dirs[@]}"; do
		if [[ -f "$dir/$name" ]]; then
			printf '%s\n' "$dir/$name"
			return 0
		fi
	done
	return 1
}

declare -A seen
copy_tree() {
	local bin=$1 dep found key
	[[ -f "$bin" ]] || return 0
	while read -r dep; do
		[[ -n "$dep" ]] || continue
		key=${dep,,}
		if [[ -n ${seen[$key]+x} ]]; then
			continue
		fi
		seen[$key]=1
		if is_system "$key"; then
			continue
		fi
		if [[ -f "$DEST/bin/$dep" ]]; then
			copy_tree "$DEST/bin/$dep"
			continue
		fi
		if ! found=$(find_dll "$dep"); then
			echo "Bibliothèque introuvable : $dep (demandée par $bin)" >&2
			exit 1
		fi
		cp "$found" "$DEST/bin/$dep"
		copy_tree "$DEST/bin/$dep"
	done < <("$OBJDUMP" -p "$bin" | awk '/DLL Name:/{print $3}')
}

copy_tree "$DEST/bin/abiword.exe"
copy_tree "$DEST/bin/libabiword-3.1.dll"
copy_tree "$DEST/lib/abiword-3.1/plugins/opendocument.dll"
copy_tree "$DEST/lib/abiword-3.1/plugins/openxml.dll"
copy_tree "$DEST/lib/enchant-2/enchant_hunspell.dll"

hunspell=$(find "$DEST/bin" -maxdepth 1 -name 'libhunspell*.dll' -print -quit)
if [[ -n "$hunspell" ]]; then
	cp "$hunspell" "$DEST/lib/enchant-2/"
fi

echo "Arbre prêt : $DEST"
find "$DEST/bin" -maxdepth 1 -type f | sort
