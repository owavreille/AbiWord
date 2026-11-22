# AbiWord 3.1 pour Windows

Portage non officiel d'AbiWord pour Windows 64 bits, y compris Windows 11.
Le programme reste sous licence GPL-2.0-or-later. Le nom AbiWord appartient
au projet d'origine, dont le site est [abisource.com](https://www.abisource.com/).

Installateur : [AbiWord 3.1 pour Windows](https://github.com/owavreille/AbiWord/releases/tag/v3.1-windows)

## Version actuelle

Le code de cette série est la **3.1**. `configure.ac` fixe le majeur à 3,
le mineur à 1 et le micro à 0. Le paquet et la bibliothèque portent le nom
de la série, `abiword-3.1` et `libabiword-3.1`. Le manifeste Windows annonce
`3.1.0.0`, en 64 bits, avec le niveau d'exécution `asInvoker`.

La dernière publication du projet AbiWord est la **3.0.5**. Ses sources et
ses paquets visent surtout Linux. Ce dépôt continue la série 3.1 et en
produit l'installateur Windows x64.

L'installateur publié aujourd'hui est construit à partir du commit indiqué
ci-dessous. Un push sur `main` le remplace : le fichier, la taille, le
condensat, la signature et le commit sont alors réécrits dans ce tableau.

| | |
| --- | --- |
| Fichier | `AbiWord-3.1-windows-x64-setup.exe` |
| Taille | 28 542 291 octets |
| SHA-256 | `38f51a3f90333193ffd3cfae949ea884ffa97f3d3480e8a95587ebc2cdcb25d4` |
| Signature | non signée |
| Code source de ce binaire | `38f0b465fa7ca5c8ac12ba8b4d3217f9212ea854` |

Le même condensat est joint à la release dans `AbiWord-3.1-windows-x64-setup.exe.sha256`.

Pour le vérifier après le téléchargement :

```text
certutil -hashfile AbiWord-3.1-windows-x64-setup.exe SHA256
```

Le résultat doit être exactement le SHA-256 ci-dessus.

Ce binaire contient le programme, l'export PDF par Cairo, les greffons
OpenDocument, OpenXML, StarOffice (`.sdw`) et collaboration TCP, la
correction Enchant/Hunspell et les dictionnaires embarqués. L'enregistrement
`.doc` écrit du RTF sous ce nom de fichier. L'import `.doc` lit les
documents Word 97 par wv 1.2.9. Le plancher d'API reste celui de Windows
2000 : le programme n'exige pas une API réservée à Windows 11. L'installateur
demande les droits d'administrateur parce qu'il copie les fichiers dans
`Program Files`.

La signature Authenticode n'est appliquée que si les secrets
`WINDOWS_CERT_BASE64` et `WINDOWS_CERT_PASSWORD` sont présents au moment
de la publication. Ils ne l'étaient pas pour le binaire ci-dessus.

## Objectifs

Ce portage a quatre buts.

1. **Livrer un AbiWord 3.1 installable sur Windows 64 bits**, y compris Windows 11, sans dépendre des anciens paquets 32 bits du projet.
2. **Ouvrir et enregistrer les documents courants** : ABW, RTF, DOC, DOCX et ODT, et produire un PDF. La lecture et l'écriture se rapprochent des documents Word et LibreOffice. L'export `.doc` reste le RTF déjà produit par AbiWord.
3. **Garder le programme utilisable en français et en anglais**, avec les dictionnaires, les icônes et une collaboration directe entre deux AbiWord, en TCP seulement.
4. **Empêcher les régressions de format.** Chaque changement lancé sur l'intégration compile la suite native et l'installateur. La suite `core.wp.impexp.formats` vérifie les fichiers de référence ABW, RTF et DOC, puis un aller-retour de texte en ABW, RTF, DOC, DOCX et ODT.

Ce n'est pas une publication AbiSource. WordPerfect, Psion et les anciens
backends de collaboration autres que TCP ne font pas partie de
l'installateur.

## Compilation

Le fichier [README](README) d'origine décrit l'arbre des sources et le
flatpak. `docs/build/BUILD.TXT` décrit les anciennes méthodes du projet
(GTK 2, Visual C++, NSIS 2). Le binaire de ce dépôt se construit autrement,
de trois façons.

### Installateur Windows

C'est le chemin de [`.github/workflows/windows-installer.yml`](.github/workflows/windows-installer.yml).
Une pull request et un push sur `main` le lancent. Seul un push sur `main`
remplace la release `v3.1-windows`.

La compilation croisée tourne dans un conteneur Fedora 45, avec
MinGW-w64. Elle construit d'abord wv 1.2.9, corrigé par
`packaging/windows/wv-1.2.9-bounds.patch`, puis Redland pour le RDF et
SPARQL. AbiWord est ensuite configuré ainsi :

```sh
../configure \
  --host=x86_64-w64-mingw32 \
  --prefix=/usr/x86_64-w64-mingw32/sys-root/mingw \
  --with-boost=/usr/x86_64-w64-mingw32/sys-root/mingw \
  --enable-plugins="opendocument openxml sdw collab" \
  --enable-collab-backend-tcp \
  --disable-collab-backend-service \
  --disable-collab-backend-telepathy \
  --disable-collab-backend-xmpp \
  --disable-collab-backend-sugar \
  --disable-collab-backend-sip \
  --with-redland
```

`packaging/windows/stage-dist.sh` rassemble `abiword.exe`,
`libabiword-3.1.dll`, les quatre greffons, les dictionnaires Hunspell et
les bibliothèques MinGW. L'installateur est produit sur Ubuntu par NSIS
3.13 (`packaging/windows/abiword.nsi`). Ses pages sont en français et en
anglais. La licence des icônes Colibre (CC0) et des dessins originaux est
affichée à part de la GPL.

### Suite native

Le même workflow compile AbiWord en natif, dans Fedora 45, puis lance
`make check` sous Xvfb. GTK 3.24 est requis (`gtk+-3.0 >= 3.24`,
`librsvg-2.0 >= 2.59`, `pango >= 1.44`), avec GLib, libgsf, wv, libxml2,
Cairo, Enchant et les bibliothèques d'image habituelles.

```sh
NOCONFIGURE=1 ./autogen.sh
mkdir build-tests && cd build-tests
../configure --enable-plugins="opendocument openxml"
make -j"$(nproc)"
xvfb-run -a make check
```

`make check` exécute `src/wp/test/unix/testwrap.sh`, qui lance
`AbiWord-test`. Les greffons DOCX et ODT ne sont pas installés à ce
moment-là : la suite des formats les charge depuis `plugins/*/.libs`.
Le journal d'un échec est `src/wp/test/test-suite.log`.

### Compilation GTK locale

Sur une machine où ces dépendances sont déjà installées, les mêmes
commandes produisent un `abiword` Unix. `./configure` sans liste de
greffons détecte ceux qui peuvent être construits. Pour retrouver le
périmètre de l'installateur :

```sh
../configure --enable-plugins="opendocument openxml sdw collab" \
  --enable-collab-backend-tcp \
  --disable-collab-backend-service
make -j"$(nproc)"
```

Le flatpak reste celui du fichier [README](README) :

```sh
flatpak-builder --force-clean --repo=repo abiword flatpak/com.abisource.AbiWord.json
```
