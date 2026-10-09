; 智录 GyanisScribe 的 Windows 安装器（NSIS 3）
;
; 由 scripts/WindowsPackage.bat 调用：它先把绿色包备好（windeployqt 的 Qt 运行时 + 文档），
; 再用 -DGyanisScribeVersion / -DGyanisScribeStage 两个 -D 传进来。版本号不在这里写死——
; 真源是 CMakeLists.txt 的 project(... VERSION)，两处各写一份迟早会对不上。
;
; 手工跑法（调试用）：
;   makensis -DGyanisScribeVersion=0.0.1 -DGyanisScribeStage=dist\stage packaging\GyanisScribe.nsi

Unicode true
!include "MUI2.nsh"

!ifndef GyanisScribeVersion
  !error "要用 -DGyanisScribeVersion=<版本>，取自 project(... VERSION)"
!endif
!ifndef GyanisScribeStage
  !define GyanisScribeStage "dist\stage"
!endif

Name "智录 GyanisScribe ${GyanisScribeVersion}"
OutFile "..\dist\GyanisScribe-${GyanisScribeVersion}-win64.exe"
InstallDir "$PROGRAMFILES64\GyanisScribe"
InstallDirRegKey HKLM "Software\Gyanis\GyanisScribe" "InstallDir"
RequestExecutionLevel admin
SetCompressor /SOLID lzma

!define MUI_ABORTWARNING
!ifndef GyanisScribeIco
  ; 安装包图标要的是 .ico 资源：可执行体里没有嵌 PE 图标时，拿它当图标源会被 makensis 判成
  ; 「invalid icon file」而中止。日后若加了 Resources/AppIcon.ico，用
  ;   makensis -DGyanisScribeIco=... 传进来即可，这里不猜一个不存在的路径。
!else
  !define MUI_ICON "${GyanisScribeIco}"
!endif

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "..\docs\ThirdPartyLicenses.md"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN "$INSTDIR\GyanisScribe.exe"
!define MUI_FINISHPAGE_RUN_TEXT "立即启动智录"
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "SimpChinese"

Section "智录主程序" SecMain
  SectionIn RO
  SetOutPath "$INSTDIR"
  File /r "${GyanisScribeStage}\*.*"

  WriteRegStr HKLM "Software\Gyanis\GyanisScribe" "InstallDir" "$INSTDIR"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\GyanisScribe" "DisplayName" "智录 GyanisScribe"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\GyanisScribe" "DisplayVersion" "${GyanisScribeVersion}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\GyanisScribe" "Publisher" "Gyanis"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\GyanisScribe" "UninstallString" "$INSTDIR\Uninstaller.exe"
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\GyanisScribe" "NoModify" 1
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\GyanisScribe" "NoRepair" 1

  CreateDirectory "$SMPROGRAMS\智录"
  CreateShortCut "$SMPROGRAMS\智录\智录.lnk" "$INSTDIR\GyanisScribe.exe"
  CreateShortCut "$SMPROGRAMS\智录\卸载智录.lnk" "$INSTDIR\Uninstaller.exe"
  CreateShortCut "$DESKTOP\智录.lnk" "$INSTDIR\GyanisScribe.exe"

  ; WriteUninstaller 只能写在安装段里（写在卸载段会被 makensis 直接拒：
  ; "WriteUninstaller only valid from install"）；文件名要与注册表 UninstallString 指的一致
  WriteUninstaller "$INSTDIR\Uninstaller.exe"
SectionEnd

Section "Uninstall"
  RMDir /r "$INSTDIR"
  Delete "$SMPROGRAMS\智录\智录.lnk"
  Delete "$SMPROGRAMS\智录\卸载智录.lnk"
  RMDir "$SMPROGRAMS\智录"
  Delete "$DESKTOP\智录.lnk"
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\GyanisScribe"
  DeleteRegKey HKLM "Software\Gyanis\GyanisScribe"
SectionEnd
