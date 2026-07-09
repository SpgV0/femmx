; Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-09:
; rebranded from femm/FEMM to femmx/FEMMX (project rebrand: femm_plus ->
; femmx): PROJECT_NAME, the packaged femm.exe -> femmx.exe, and the
; start-menu shortcut.
; Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-09:
; OutFile now writes into bin\ instead of the repo root, so the built
; installer lands next to the executables it packages. Invoked
; automatically from CMake (see the root CMakeLists.txt's "installer"
; target) whenever makensis is found, with the working directory set to
; the repo root so the relative "bin\..." paths below resolve.
Unicode True
!include MUI2.nsh
!include LogicLib.nsh
!define PROJECT_NAME "FEMMX"
!define PROJECT_REG_UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PROJECT_NAME}"
!define PROJECT_UNINSTALL_EXE "uninstall.exe"

LicenseData "license.txt"
Page license
Page directory
Page instFiles
UninstPage uninstConfirm
UninstPage instfiles

# define name of installer
OutFile "bin\${PROJECT_NAME}_installer.exe"

# define installation directory
InstallDir "$APPDATA\${PROJECT_NAME}"

# We do not need any admin privilege
RequestExecutionLevel user

# start default section
Section
    ClearErrors
    EnumRegKey $0 HKCU "${PROJECT_REG_UNINSTALL_KEY}" "QuietUninstallString"
    IfErrors ContinueInstall KeyExist
    KeyExist:
        ReadRegStr $R0 HKCU "${PROJECT_REG_UNINSTALL_KEY}" "QuietUninstallString"
        ExecWait "$R0"

    ContinueInstall:
    # set the installation directory as the destination for the following actions
    SetOutPath "$INSTDIR"
    File "bin\belasolv.exe"
    File "bin\condlib.dat"
    File "bin\csolv.exe"
    File "bin\femmx.exe"
    File "bin\femmplot.exe"
    File "bin\fkn.exe"
    File "bin\heatlib.dat"
    File "bin\hsolv.exe"
    File "bin\init.lua"
    File "bin\matlib.dat"
    File "bin\statlib.dat"
    File "bin\triangle.exe"

    # create the uninstaller and a link to it in the start menu
    WriteUninstaller "$INSTDIR\${PROJECT_UNINSTALL_EXE}"

    SetOutPath "$STARTMENU\Programs\${PROJECT_NAME}"
    CreateShortcut "$SMPROGRAMS\${PROJECT_NAME}\FEMMX.lnk" "$INSTDIR\femmx.exe"
    CreateShortcut "$SMPROGRAMS\${PROJECT_NAME}\Uninstall.lnk" "$INSTDIR\${PROJECT_UNINSTALL_EXE}"

    WriteRegStr HKCU "${PROJECT_REG_UNINSTALL_KEY}" "UninstallString" '"$INSTDIR\${PROJECT_UNINSTALL_EXE}" _?=$INSTDIR'
    WriteRegStr HKCU "${PROJECT_REG_UNINSTALL_KEY}" "QuietUninstallString" '"$INSTDIR\${PROJECT_UNINSTALL_EXE}" /S _?=$INSTDIR'
SectionEnd

# uninstaller section start
Section "uninstall"
    # delete the EXEs and the DLLs
    Delete "$INSTDIR\belasolv.exe"
    Delete "$INSTDIR\condlib.dat"
    Delete "$INSTDIR\csolv.exe"
    Delete "$INSTDIR\femmx.exe"
    Delete "$INSTDIR\femmplot.exe"
    Delete "$INSTDIR\fkn.exe"
    Delete "$INSTDIR\heatlib.dat"
    Delete "$INSTDIR\hsolv.exe"
    Delete "$INSTDIR\init.lua"
    Delete "$INSTDIR\license.txt"
    Delete "$INSTDIR\matlib.dat"
    Delete "$INSTDIR\statlib.dat"
    Delete "$INSTDIR\triangle.exe"

    # delete the uninstaller
    Delete "$INSTDIR\${PROJECT_UNINSTALL_EXE}"

    # remove the links from the start menu
    Delete "$SMPROGRAMS\${PROJECT_NAME}\FEMMX.lnk"
    Delete "$SMPROGRAMS\${PROJECT_NAME}\Uninstall.lnk"
    RMDir "$STARTMENU\Programs\${PROJECT_NAME}"

    # remove the installdir
    RMDir "$INSTDIR"

    DeleteRegKey HKCU "${PROJECT_REG_UNINSTALL_KEY}"
SectionEnd
