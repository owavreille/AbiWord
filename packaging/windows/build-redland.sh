#!/bin/bash
# Cross-compile raptor, rasqal and redland for MinGW.
# RDF/XML and SPARQL stay inside the process. The memory store is
# linked into librdf so Windows does not have to load Redland modules.
set -euo pipefail

PREFIX=${PREFIX:-/usr/x86_64-w64-mingw32/sys-root/mingw}
export PKG_CONFIG=${PKG_CONFIG:-/usr/bin/x86_64-w64-mingw32-pkg-config}
export PATH="$PREFIX/bin:$PATH"

if "$PKG_CONFIG" --exists 'redland >= 1.0.10' 'rasqal >= 0.9.17'; then
	echo "Redland déjà installé : $("$PKG_CONFIG" --modversion redland)"
	exit 0
fi

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

fetch() {
	local url=$1
	local name=$2
	local sha256=$3
	curl -fsSL -o "$work/$name" "$url"
	echo "$sha256  $work/$name" | sha256sum -c -
	tar -xzf "$work/$name" -C "$work"
}

# Libtool cherche winepath pour convertir le chemin des exécutables non
# installés. On ne les lance pas, donc le chemin de construction suffit.
common=(
	--build=x86_64-pc-linux-gnu
	--host=x86_64-w64-mingw32
	--prefix="$PREFIX"
	--disable-static
	--enable-shared
	lt_cv_to_host_file_cmd=func_convert_file_noop
	ac_cv_prog_FILE=/usr/bin/file
	FGREP="grep -F"
	EGREP="grep -E"
)

# Neutralise les avertissements inoffensifs des anciens scripts configure
# (gtk-doc absent, WWW désactivé volontairement, BDB non utilisé, fgrep obsolète).
silence_configure_warnings() {
	python3 - << 'PY'
from pathlib import Path
cfg = Path("configure")
text = cfg.read_text(errors="replace")
text = text.replace("fgrep", "grep -F")
for warn in (
    "No WWW library in use - only file: URLs will work",
    "Install libcurl, libxml2 or BSD libfetch for WWW access",
    "because gtk-doc >=",
    "Assuming ceil, floor and round are in default libs for cross compiling",
    "Cannot find the BDB library name from the files in",
    "Assuming it is 'db' and will link like -ldb",
    "Use --with-bdb-dbname=NAME if this is wrong",
):
    text = text.replace(f"WARNING: {warn}", f"NOTE: {warn}")
text = text.replace(
    'as_fn_ schedule_warning "You will not be able',
    ': "You will not be able',
)
text = text.replace(
    '{ $as_echo "$as_me:${as_lineno-$LINENO}: WARNING:\n  You will not be able',
    'if false; then { $as_echo "$as_me:${as_lineno-$LINENO}: NOTE:\n  You will not be able',
).replace(
    'because gtk-doc >= 1.3 is not found." >&2;}',
    'because gtk-doc >= 1.3 is not found." >&2;}; fi',
)
cfg.write_text(text)
PY
}

# libtool refuse une DLL MinGW tant que chaque symbole n'est pas résolu.
mark_defined() {
	find . -name Makefile -exec sed -i 's/_la_LDFLAGS = /_la_LDFLAGS = -no-undefined /' {} +
}

if ! "$PKG_CONFIG" --exists raptor2; then
fetch https://ftp.debian.org/debian/pool/main/r/raptor2/raptor2_2.0.16.orig.tar.gz raptor2.tar.gz 089db78d7ac982354bdbf39d973baf09581e6904ac4c92a98c5caadb3de44680
(
	cd "$work"/raptor2-2.0.16
	# libxml2 2.11 a retiré le champ xmlEntity.checked.
	python3 - << 'PY'
from pathlib import Path
path = Path("src/raptor_libxml.c")
text = path.read_text()
old = "#if LIBXML_VERSION >= 20627\n"
new = "#if LIBXML_VERSION >= 20627 && LIBXML_VERSION < 21100\n"
if old not in text:
    raise SystemExit("garde libxml2 introuvable dans raptor_libxml.c")
path.write_text(text.replace(old, new, 1))
sort = Path("src/sort_r.h")
header = sort.read_text()
needle = "#include <stdlib.h>\n"
insert = needle + "\n#if defined(__MINGW32__)\n#define NESTED_QSORT 1\n#endif\n"
if needle not in header:
    raise SystemExit("sort_r.h inattendu")
sort.write_text(header.replace(needle, insert, 1))
old_type = "#define YYSTYPE date_ll\n"
new_type = "#undef YYSTYPE\n#define YYSTYPE date_ll\n"
for path in (Path("src/parsedate.y"), Path("src/parsedate.c")):
    text = path.read_text()
    if old_type not in text:
        raise SystemExit(f"YYSTYPE introuvable dans {path}")
    path.write_text(text.replace(old_type, new_type, 1))
PY
	silence_configure_warnings
	./configure "${common[@]}" --with-www=none
	mark_defined
	make -j"$(nproc)"
	make install
)
fi

# PCRE 8.45 est la dernière publication. Fedora 45 la fournit en
# mingw64-pcre ; le téléchargement ne sert que si ce paquet est absent.
if ! "$PKG_CONFIG" --exists libpcre; then
fetch https://downloads.sourceforge.net/project/pcre/pcre/8.45/pcre-8.45.tar.gz pcre.tar.gz 4e6ce03e0336e8b4a3d6c2b70b1c5e18590a5673a98186da90d4f33c23defc09
(
	cd "$work"/pcre-8.45
	# config.h définit déjà ces macros via la visibilité GCC. Le bloc
	# Windows les redéfinit avec __declspec, ce qui avertit sous MinGW.
	python3 - << 'PY'
from pathlib import Path
path = Path("pcreposix.c")
text = path.read_text()
old = "#if defined(_WIN32) && !defined(PCRE_STATIC)\n"
new = "#if defined(_WIN32) && !defined(PCRE_STATIC) && !defined(PCREPOSIX_EXP_DECL)\n"
if old not in text:
    raise SystemExit("garde PCREPOSIX_EXP_DECL introuvable dans pcreposix.c")
path.write_text(text.replace(old, new, 1))
PY
	silence_configure_warnings
	./configure "${common[@]}" --enable-utf --enable-unicode-properties
	mark_defined
	make -j"$(nproc)"
	make install
)
fi

if ! "$PKG_CONFIG" --exists rasqal; then
fetch https://ftp.debian.org/debian/pool/main/r/rasqal/rasqal_0.9.33.orig.tar.gz rasqal.tar.gz 6924c9ac6570bd241a9669f83b467c728a322470bf34f4b2da4f69492ccfd97c
(
	cd "$work"/rasqal-0.9.33
	python3 - << 'PY'
from pathlib import Path
old = "#ifdef WIN32\n#include <win32_rasqal_config.h>\n#endif\n"
new = "#if defined(WIN32) && !defined(__MINGW32__)\n#include <win32_rasqal_config.h>\n#endif\n"
changed = 0
for path in Path(".").rglob("*"):
    if path.suffix not in {".c", ".h", ".in"}:
        continue
    text = path.read_text(errors="replace")
    if old in text:
        path.write_text(text.replace(old, new))
        changed += 1
if changed == 0:
    raise SystemExit("aucune inclusion win32_rasqal_config.h")
service = Path("src/rasqal_service.c")
text = service.read_text()
old_accept = """  if(svc->format)
    raptor_www_set_http_accept(svc->www, svc->format);
  else
    raptor_www_set_http_accept(svc->www, DEFAULT_FORMAT);
"""
new_accept = """  if(svc->format)
    raptor_www_set_http_accept2(svc->www, svc->format, strlen(svc->format));
  else
    raptor_www_set_http_accept2(svc->www, DEFAULT_FORMAT, strlen(DEFAULT_FORMAT));
"""
if old_accept not in text:
    raise SystemExit("appel raptor_www_set_http_accept introuvable")
service.write_text(text.replace(old_accept, new_accept, 1))
PY
	silence_configure_warnings
	./configure "${common[@]}" \
		--with-regex-library=pcre \
		--with-decimal=none \
		--with-uuid-library=internal \
		--with-digest-library=internal
	mark_defined
	make -C src -j"$(nproc)"
	make -C src install
	mkdir -p "$PREFIX/lib/pkgconfig"
	cp -a rasqal.pc "$PREFIX/lib/pkgconfig/"
)
fi

if ! "$PKG_CONFIG" --exists redland; then
fetch https://ftp.debian.org/debian/pool/main/r/redland/redland_1.0.17.orig.tar.gz redland.tar.gz de1847f7b59021c16bdc72abb4d8e2d9187cd6124d69156f3326dd34ee043681
(
	cd "$work"/redland-1.0.17
	python3 - << 'PY'
from pathlib import Path
old = "#ifdef WIN32\n#include <win32_rdf_config.h>\n#endif\n"
new = "#if defined(WIN32) && !defined(__MINGW32__)\n#include <win32_rdf_config.h>\n#endif\n"
changed = 0
for path in Path(".").rglob("*"):
    if path.suffix not in {".c", ".h"}:
        continue
    text = path.read_text(errors="replace")
    if old in text:
        path.write_text(text.replace(old, new))
        changed += 1
if changed == 0:
    raise SystemExit("aucune inclusion win32_rdf_config.h")
parser = Path("src/rdf_parser_raptor.c")
text = parser.read_text()
old_accept = "      raptor_www_set_http_accept(pcontext->www, accept_h);\n"
new_accept = "      raptor_www_set_http_accept2(pcontext->www, accept_h, strlen(accept_h));\n"
if old_accept not in text:
    raise SystemExit("appel raptor_www_set_http_accept introuvable")
parser.write_text(text.replace(old_accept, new_accept, 1))
results = Path("src/rdf_query_results.c")
text = results.read_text()
old_check = "rasqal_query_results_formats_check("
new_check = "rasqal_query_results_formats_check2("
if old_check not in text:
    raise SystemExit("appel rasqal_query_results_formats_check introuvable")
results.write_text(text.replace(old_check, new_check, 1))
PY
	silence_configure_warnings
	./configure "${common[@]}" \
		--disable-modular \
		--with-bdb=no \
		--with-sqlite=no \
		--with-mysql=no \
		--with-postgresql=no \
		--with-virtuoso=no \
		--with-threads=win32
	mark_defined
	make -C src -j"$(nproc)"
	make -C src install
	mkdir -p "$PREFIX/lib/pkgconfig"
	cp -a redland.pc "$PREFIX/lib/pkgconfig/"
)
fi

"$PKG_CONFIG" --exists 'redland >= 1.0.10' 'rasqal >= 0.9.17'
echo "Redland $("$PKG_CONFIG" --modversion redland) installé"
