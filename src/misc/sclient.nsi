!include LogicLib.nsh
!include x64.nsh

; Program Details
Name "sclient"
OutFile "sclient_setup.exe"
Icon C:\code\sclient\src\res\desktop.ico
Caption "sclient"
VIProductVersion 1.0.0.0
VIAddVersionKey ProductName "sclient"
VIAddVersionKey Comments "a simple remote desktop client"
VIAddVersionKey CompanyName "Qi Zhou"
VIAddVersionKey LegalCopyright "Qi Zhou"
VIAddVersionKey FileDescription "a simple remote desktop client"
VIAddVersionKey FileVersion 1.0.0.0
VIAddVersionKey ProductVersion 1.0.0.0
VIAddVersionKey InternalName "sclient"
VIAddVersionKey LegalTrademarks "QQ is a Trademark of Tencent Ltd."
VIAddVersionKey OriginalFilename "sclient_setup.exe"
BrandingText " "

; details of the install: Show,Hide,NeverShow
ShowInstDetails Show
ShowUninstDetails NeverShow

; runtime privileges: user,admin
RequestExecutionLevel admin

Unicode True

; The default installation directory
InstallDir $PROGRAMFILES\sclient

; Registry key to check for directory (so if you install again, it will 
; overwrite the old one automatically)
InstallDirRegKey HKLM "Software\sclient" "Install_Dir"

Page components
Page directory
Page instfiles

UninstPage uninstConfirm
UninstPage instfiles

Section "sclient (required)"

  SectionIn RO
  
  ; Set output path to the installation directory.
  SetOutPath $INSTDIR
  
  ; Put file there
  File /r "C:\code\sclient\bin\*"

  ${DisableX64FSRedirection}
  IfFileExists "$SYSDIR\drivers\UsbDk.sys" endUsbdk beginUsbdk
    Goto endUsbdk
    beginUsbdk:
    MessageBox MB_OK "Press OK to install usbdk." /SD IDYES IDNO
    DetailPrint "Installing UsbDK"
    ${If} ${RunningX64}
      ExecWait '"msiexec" /i "$INSTDIR\UsbDk_1.0.21_x64.msi"'
    ${Else}
      ExecWait '"msiexec" /i "$INSTDIR\UsbDk_1.0.21_x86.msi"'
    ${EndIf} 
  endUsbdk:
  ${EnableX64FSRedirection}
  
  ; Write the installation path into the registry
  WriteRegStr HKLM SOFTWARE\sclient "Install_Dir" "$INSTDIR"
  
  ; Write the uninstall keys for Windows
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\sclient" "DisplayName" "sclient"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\sclient" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\sclient" "NoModify" 1
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\sclient" "NoRepair" 1
  WriteUninstaller "$INSTDIR\uninstall.exe"
  
SectionEnd

; Optional section (can be disabled by the user)
Section "Start Menu Shortcuts"

  CreateDirectory "$SMPROGRAMS\sclient"
  CreateShortcut "$SMPROGRAMS\sclient\Uninstall.lnk" "$INSTDIR\uninstall.exe" "" "$INSTDIR\uninstall.exe" 0
  CreateShortcut "$SMPROGRAMS\sclient\sclient.lnk" "$INSTDIR\sclient.exe" "" "$INSTDIR\sclient.exe" 0
  
SectionEnd

; Uninstaller

Section "Uninstall"
  
  ; Remove registry keys
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\sclient"
  DeleteRegKey HKLM SOFTWARE\sclient

  ; Remove files and uninstaller
  Delete "$INSTDIR\*"

  ; Remove shortcuts, if any
  Delete "$SMPROGRAMS\sclient\*.*"

  ; Remove directories used
  RMDir "$SMPROGRAMS\sclient"
  RMDir "$INSTDIR"

SectionEnd
