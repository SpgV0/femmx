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
; Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-09:
; restructured the installed layout to match FEMM 4.2's C:\femm42
; convention: binaries now land in $INSTDIR\bin (previously flat in
; $INSTDIR), and the Mathematica/Octave/Scilab interfaces are now
; packaged too, at $INSTDIR\mathfemm, $INSTDIR\mfiles, $INSTDIR\scifemm
; -- matching what mathfemm.m and octavefemm/mfiles/openfemm.m already
; hardcode (c:\FEMMX\bin\femmx.exe). scifemm's scilink.dll is a new
; build output (scifemm/CMakeLists.txt now builds it as a proper DLL
; instead of an unused static lib) installed next to scifemm.sci, since
; that's where scifemm.sci looks for it at Scilab runtime.
; Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-17:
; added PROJECT_VERSION, folded into the installer's OutFile name (e.g.
; FEMMX_v1.1.0_installer.exe) and written as the uninstall registry key's
; DisplayVersion, so it shows up in Windows' "Apps & Features" list.
Unicode True
!include MUI2.nsh
!include LogicLib.nsh
!define PROJECT_NAME "FEMMX"
; Single source of truth for the installer's own display/file version --
; keep in sync with femm/femm.rc's VERSIONINFO block and the git tag
; created for each release (see CHANGELOG.md).
!define PROJECT_VERSION "1.1.0"
!define PROJECT_REG_UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PROJECT_NAME}"
!define PROJECT_UNINSTALL_EXE "uninstall.exe"

; femm.ActiveFEMM's COM automation CLSID -- hardcoded in femm/ActiveFEMM.cpp's
; IMPLEMENT_OLECREATE2 call, so it's fixed regardless of install location.
; See scripts/register_femm_com.ps1's docstring for why this needs to be
; written by hand at all: femmx.exe's own COM self-registration
; (COleObjectFactory::UpdateRegistryAll) doesn't currently write anything
; under this CMake build. Written here too so a normal end-user install
; (not just a dev build + that script) leaves pyfemm/Octave/Mathematica/
; Scilab automation working out of the box.
!define FEMM_COM_CLSID "{0A35D5BD-DCA9-4C39-9512-1D89A1A37047}"
!define FEMM_COM_PROGID "femm.ActiveFEMM"

LicenseData "license.txt"
Page license
Page directory
Page instFiles
UninstPage uninstConfirm
UninstPage instfiles

# define name of installer
OutFile "bin\${PROJECT_NAME}_v${PROJECT_VERSION}_installer.exe"

# define installation directory
# Fixed C:\FEMMX (not $APPDATA), matching the original FEMM 4.2 installer's
# fixed C:\femm42 default -- mathfemm.m and octavefemm/mfiles/openfemm.m
# hardcode this exact path (as C:\FEMMX\bin\femmx.exe) rather than probing
# the registry, so it needs to be predictable. Still user-overridable via
# the "Page directory" step below.
InstallDir "C:\${PROJECT_NAME}"

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
    # executables + runtime data files, mirroring FEMM 4.2's C:\femm42\bin
    SetOutPath "$INSTDIR\bin"
    File "bin\belasolv.exe"
    File "bin\condlib.dat"
    File "bin\csolv.exe"
    File "bin\femmx.exe"
    File "bin\femmplot.exe"
    File "bin\fkn.exe"
    File "bin\heatlib.dat"
    File "bin\hsolv.exe"
    File "bin\init.lua"
    File "bin\license.txt"
    File "bin\matlib.dat"
    File "bin\statlib.dat"
    File "bin\triangle.exe"

    # CUDA runtime DLLs, only present in bin\ for a -DENABLE_CUDA_SOLVER=ON
    # build (see fkn/CMakeLists.txt); /nonfatal so a CPU-only build, where
    # these don't exist, still packages fine.
    File /nonfatal "bin\cublas64_*.dll"
    File /nonfatal "bin\cublasLt64_*.dll"
    File /nonfatal "bin\cudart64_*.dll"
    File /nonfatal "bin\cusparse64_*.dll"
    File /nonfatal "bin\nvJitLink_*.dll"

    # Mathematica interface -- see mathfemm/usage.nb for setup instructions
    SetOutPath "$INSTDIR\mathfemm"
    File "mathfemm\mathfemm.m"
    File "mathfemm\usage.nb"

    # Octave interface (octavefemm/mfiles/ -> $INSTDIR\mfiles, matching
    # FEMM 4.2's naming -- not "octavefemm")
    SetOutPath "$INSTDIR\mfiles"
    File "octavefemm\mfiles\*.m"

    # Scilab interface. scilink.dll is /nonfatal since it's only built
    # when scifemm isn't SKIP'd (see scifemm/CMakeLists.txt's SKIP_scifemm).
    SetOutPath "$INSTDIR\scifemm"
    File "scifemm\scifemm.sci"
    File /nonfatal "scifemm\scilink.dll"

    # register the femm.ActiveFEMM COM automation class (see the
    # FEMM_COM_CLSID comment above) -- HKCU only, no admin rights needed
    WriteRegStr HKCU "Software\Classes\${FEMM_COM_PROGID}" "" "Femm.ActiveFEMM Object"
    WriteRegStr HKCU "Software\Classes\${FEMM_COM_PROGID}\CLSID" "" "${FEMM_COM_CLSID}"
    WriteRegStr HKCU "Software\Classes\CLSID\${FEMM_COM_CLSID}" "" "Femm.ActiveFEMM Object"
    WriteRegStr HKCU "Software\Classes\CLSID\${FEMM_COM_CLSID}\LocalServer32" "" '"$INSTDIR\bin\femmx.exe"'
    WriteRegStr HKCU "Software\Classes\CLSID\${FEMM_COM_CLSID}\ProgID" "" "${FEMM_COM_PROGID}"

    # create the uninstaller and a link to it in the start menu
    WriteUninstaller "$INSTDIR\${PROJECT_UNINSTALL_EXE}"

    SetOutPath "$STARTMENU\Programs\${PROJECT_NAME}"
    CreateShortcut "$SMPROGRAMS\${PROJECT_NAME}\FEMMX.lnk" "$INSTDIR\bin\femmx.exe"
    CreateShortcut "$SMPROGRAMS\${PROJECT_NAME}\Uninstall.lnk" "$INSTDIR\${PROJECT_UNINSTALL_EXE}"

    WriteRegStr HKCU "${PROJECT_REG_UNINSTALL_KEY}" "UninstallString" '"$INSTDIR\${PROJECT_UNINSTALL_EXE}" _?=$INSTDIR'
    WriteRegStr HKCU "${PROJECT_REG_UNINSTALL_KEY}" "QuietUninstallString" '"$INSTDIR\${PROJECT_UNINSTALL_EXE}" /S _?=$INSTDIR'
    WriteRegStr HKCU "${PROJECT_REG_UNINSTALL_KEY}" "DisplayVersion" "${PROJECT_VERSION}"
SectionEnd

# uninstaller section start
Section "uninstall"
    # unregister the femm.ActiveFEMM COM automation class
    DeleteRegKey HKCU "Software\Classes\${FEMM_COM_PROGID}"
    DeleteRegKey HKCU "Software\Classes\CLSID\${FEMM_COM_CLSID}"

    # delete the installed subfolders (bin, mathfemm, mfiles, scifemm)
    RMDir /r "$INSTDIR\bin"
    RMDir /r "$INSTDIR\mathfemm"
    RMDir /r "$INSTDIR\mfiles"
    RMDir /r "$INSTDIR\scifemm"

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
