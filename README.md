# AbiWord 3.1 pour Windows

Portage non officiel d'AbiWord pour Windows 64 bits, y compris Windows 11.
Le programme reste sous GPL. Le nom AbiWord appartient au projet d'origine.

Installateur : [AbiWord 3.1 pour Windows](https://github.com/owavreille/AbiWord/releases/tag/v3.1-windows)

## Installateur

| | |
| --- | --- |
| Fichier | `AbiWord-3.1-windows-x64-setup.exe` |
| Taille | 25 962 136 octets |
| SHA-256 | `13e7ec3c7a70430720a73502af7e2961591b8419448d1cc4d55479bbd84d9697` |
| Code source de ce binaire | `bff60b0c3cc15197a8069bee7d83985d0a99b6d2` |

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
