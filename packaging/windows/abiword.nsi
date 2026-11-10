; Installateur Windows x64 d'AbiWord.
; L'arbre dist/windows-x86_64 doit déjà contenir bin\, lib\ et share\.
;
;   makensis -DVERSION=3.1.90 \
;            -DDIST=/chemin/dist/windows-x86_64 \
;            -DOUTFILE=/chemin/dist/AbiWord-3.1.90-windows-x64-setup.exe \
;            -DLICENSE=/chemin/COPYING \
;            -DICON=/chemin/src/wp/ap/win/abiword_app_32.ico \
;            packaging/windows/abiword.nsi

Unicode true
ManifestDPIAware true

!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "x64.nsh"
!include "FileFunc.nsh"
!include "StrFunc.nsh"

!ifndef VERSION
  !define VERSION "3.1.90"
!endif
!ifndef DIST
  !error "Definir DIST, le dossier windows-x86_64"
!endif
!ifndef OUTFILE
  !error "Definir OUTFILE"
!endif
!ifndef LICENSE
  !error "Definir LICENSE"
!endif
!ifndef ICON
  !error "Definir ICON"
!endif

${UnStrStr}

Name "AbiWord ${VERSION}"
OutFile "${OUTFILE}"
InstallDir "$PROGRAMFILES64\AbiWord"
InstallDirRegKey HKLM "Software\AbiWord" "InstallDir"
RequestExecutionLevel admin
SetCompressor /SOLID lzma
ShowInstDetails show
ShowUninstDetails show
BrandingText "AbiWord ${VERSION}"

!define MUI_ICON "${ICON}"
!define MUI_UNICON "${ICON}"
!define MUI_ABORTWARNING
!define MUI_LANGDLL_REGISTRY_ROOT HKLM
!define MUI_LANGDLL_REGISTRY_KEY "Software\AbiWord"
!define MUI_LANGDLL_REGISTRY_VALUENAME "Installer Language"
!define MUI_FINISHPAGE_RUN "$INSTDIR\bin\abiword.exe"
!define MUI_FINISHPAGE_RUN_TEXT "$(FINISH_RUN)"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${LICENSE}"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "French"
!insertmacro MUI_LANGUAGE "English"

LangString DESC_MAIN ${LANG_FRENCH} "Programme, greffons OpenDocument et OpenXML, et dictionnaires."
LangString DESC_MAIN ${LANG_ENGLISH} "Program, OpenDocument and OpenXML plugins, and dictionaries."
LangString SEC_DESKTOP ${LANG_FRENCH} "Raccourci sur le bureau"
LangString SEC_DESKTOP ${LANG_ENGLISH} "Desktop shortcut"
LangString DESC_DESKTOP ${LANG_FRENCH} "Ajoute un raccourci AbiWord sur le bureau."
LangString DESC_DESKTOP ${LANG_ENGLISH} "Add an AbiWord shortcut on the desktop."
LangString SEC_ASSOC ${LANG_FRENCH} "Documents .abw et .zabw"
LangString SEC_ASSOC ${LANG_ENGLISH} ".abw and .zabw documents"
LangString DESC_ASSOC ${LANG_FRENCH} "Ouvre les fichiers .abw et .zabw avec AbiWord."
LangString DESC_ASSOC ${LANG_ENGLISH} "Open .abw and .zabw files with AbiWord."
LangString UNINST_LINK ${LANG_FRENCH} "Désinstaller AbiWord"
LangString UNINST_LINK ${LANG_ENGLISH} "Uninstall AbiWord"
LangString FINISH_RUN ${LANG_FRENCH} "Lancer AbiWord"
LangString FINISH_RUN ${LANG_ENGLISH} "Run AbiWord"
LangString NEED_X64 ${LANG_FRENCH} "Cet installateur nécessite Windows 64 bits."
LangString NEED_X64 ${LANG_ENGLISH} "This installer requires 64-bit Windows."

!insertmacro MUI_RESERVEFILE_LANGDLL

Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_OK|MB_ICONSTOP "$(NEED_X64)"
    Abort
  ${EndIf}
  SetRegView 64
  !insertmacro MUI_LANGDLL_DISPLAY
FunctionEnd

Function un.onInit
  SetRegView 64
  !insertmacro MUI_UNGETLANGUAGE
FunctionEnd

Section "AbiWord" SecMain
  SectionIn RO

  SetOutPath "$INSTDIR"
  File "${LICENSE}"
  File /r "${DIST}/*.*"

  WriteUninstaller "$INSTDIR\uninstall.exe"

  CreateDirectory "$SMPROGRAMS\AbiWord"
  CreateShortcut "$SMPROGRAMS\AbiWord\AbiWord.lnk" "$INSTDIR\bin\abiword.exe"
  CreateShortcut "$SMPROGRAMS\AbiWord\$(UNINST_LINK).lnk" "$INSTDIR\uninstall.exe"

  WriteRegStr HKLM "Software\AbiWord" "InstallDir" "$INSTDIR"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\AbiWord" "DisplayName" "AbiWord ${VERSION}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\AbiWord" "DisplayVersion" "${VERSION}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\AbiWord" "Publisher" "AbiWord"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\AbiWord" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\AbiWord" "DisplayIcon" "$INSTDIR\bin\abiword.exe"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\AbiWord" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\AbiWord" "NoModify" 1
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\AbiWord" "NoRepair" 1

  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\AbiWord" "EstimatedSize" "$0"
SectionEnd

Section "$(SEC_DESKTOP)" SecDesktop
  CreateShortcut "$DESKTOP\AbiWord.lnk" "$INSTDIR\bin\abiword.exe"
SectionEnd

Section "$(SEC_ASSOC)" SecAssoc
  Call AssociateAbiWord
SectionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SecMain} $(DESC_MAIN)
  !insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop} $(DESC_DESKTOP)
  !insertmacro MUI_DESCRIPTION_TEXT ${SecAssoc} $(DESC_ASSOC)
!insertmacro MUI_FUNCTION_DESCRIPTION_END

Function AssociateOne
  ; $R0 suffix, $R1 content type
  WriteRegStr HKLM "Software\Classes\$R0" "" "AbiSuite.AbiWord"
  WriteRegStr HKLM "Software\Classes\$R0" "Content Type" "$R1"
FunctionEnd

Function AssociateAbiWord
  StrCpy $R0 ".abw"
  StrCpy $R1 "application/abiword"
  Call AssociateOne
  StrCpy $R0 ".zabw"
  StrCpy $R1 "application/abiword-compressed"
  Call AssociateOne

  WriteRegStr HKLM "Software\Classes\AbiSuite.AbiWord" "" "AbiWord Document"
  WriteRegStr HKLM "Software\Classes\AbiSuite.AbiWord\DefaultIcon" "" '"$INSTDIR\bin\abiword.exe",2'
  WriteRegStr HKLM "Software\Classes\AbiSuite.AbiWord\shell\open\command" "" '"$INSTDIR\bin\abiword.exe" "%1"'
  WriteRegStr HKLM "Software\Classes\AbiSuite.AbiWord\shell\open\ddeexec" "" '[Open("%1")]'
  WriteRegStr HKLM "Software\Classes\AbiSuite.AbiWord\shell\open\ddeexec\application" "" "AbiWord"
  WriteRegStr HKLM "Software\Classes\AbiSuite.AbiWord\shell\open\ddeexec\topic" "" "System"

  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'
FunctionEnd

Section "Uninstall"
  Delete "$DESKTOP\AbiWord.lnk"
  Delete "$SMPROGRAMS\AbiWord\AbiWord.lnk"
  Delete "$SMPROGRAMS\AbiWord\Désinstaller AbiWord.lnk"
  Delete "$SMPROGRAMS\AbiWord\Uninstall AbiWord.lnk"
  RMDir "$SMPROGRAMS\AbiWord"

  RMDir /r "$INSTDIR\bin"
  RMDir /r "$INSTDIR\lib"
  RMDir /r "$INSTDIR\share"
  Delete "$INSTDIR\COPYING"
  Delete "$INSTDIR\uninstall.exe"
  RMDir "$INSTDIR"

  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\AbiWord"
  DeleteRegKey HKLM "Software\AbiWord"

  ReadRegStr $0 HKLM "Software\Classes\.abw" ""
  ${If} $0 == "AbiSuite.AbiWord"
    DeleteRegKey HKLM "Software\Classes\.abw"
  ${EndIf}
  ReadRegStr $0 HKLM "Software\Classes\.zabw" ""
  ${If} $0 == "AbiSuite.AbiWord"
    DeleteRegKey HKLM "Software\Classes\.zabw"
  ${EndIf}

  ReadRegStr $0 HKLM "Software\Classes\AbiSuite.AbiWord\shell\open\command" ""
  ${UnStrStr} $1 $0 "$INSTDIR"
  ${If} $1 != ""
    DeleteRegKey HKLM "Software\Classes\AbiSuite.AbiWord"
  ${EndIf}

  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'
SectionEnd
