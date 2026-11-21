# AbiWord 3.1 pour Windows

Portage non officiel d'AbiWord pour Windows 64 bits, y compris Windows 11.
Le programme reste sous GPL. Le nom AbiWord appartient au projet d'origine.

Installateur : [AbiWord 3.1 pour Windows](https://github.com/owavreille/AbiWord/releases/tag/v3.1-windows)

## Installateur

| | |
| --- | --- |
| Fichier | `AbiWord-3.1-windows-x64-setup.exe` |
| Taille | 26 301 917 octets |
| SHA-256 | `05bd3a7c15b028851b15ef570aa867de52340dd59673ae95fd5015cac3f65cd1` |
| Code source de ce binaire | `7d4a77c6f5de36e5bae22b7dcfb6b7ec434db1ca` |

Le même condensat est joint à la release dans `AbiWord-3.1-windows-x64-setup.exe.sha256`.

Pour le vérifier après le téléchargement :

```text
certutil -hashfile AbiWord-3.1-windows-x64-setup.exe SHA256
```

Le résultat doit être exactement le SHA-256 ci-dessus.

## Différences avec l'AbiWord officiel

La dernière version publiée par le projet AbiWord est la **3.0.5**.
Ses sources et ses paquets visent surtout Linux. Le site du projet ne
propose pas d'installateur Windows 64 bits tenu à jour pour Windows 11.

Cette version-ci s'en écarte ainsi :

- **Windows 64 bits.** L'installateur place le programme dans `Program Files`. Les anciens paquets Windows du projet étaient en 32 bits et ne sont plus entretenus.
- **Version 3.1.** Le code suit la série 3.1, après la 3.0.5 officielle.
- **Compilation MinGW.** Le binaire livré est construit avec MinGW-w64, pas avec l'ancien projet Visual C++ du portage Windows.
- **Windows 11.** Les dialogues, le défilement, les polices, le presse-papiers, le DDE et les fichiers suivent les contrats actuels de la documentation Microsoft. Le programme n'utilise pas d'API réservée à Windows 11 : il reste compilé pour un niveau d'API disponible depuis Windows 2000, et tourne ainsi sur Windows 11.
- **OpenDocument et OpenXML.** L'installateur contient ces deux greffons, ainsi que les dictionnaires.
- **Installateur bilingue.** Les pages d'installation sont en français et en anglais.
- **Icônes.** Colibre (CC0) et dessins originaux. Leur licence est affichée pendant l'installation, à part de la GPL.
- **Ce n'est pas une publication AbiSource.** Le site du projet reste [abisource.com](https://www.abisource.com/).

La compilation MinGW x64 et la génération de l'installateur ont été vérifiées. 

## Sources

L'organisation du code, la compilation et le flatpak sont décrits dans le fichier [README](README) d'origine.
