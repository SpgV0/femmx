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
; Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-17:
; stepped to v1.2.0 (dark theme extended to the whole application, manual
; branding/version update + versioned-PDF CI artifact, load monitor
; window widened to 1000s, and a redraw-corruption fix for pan/zoom
; during large-mesh drawing -- see CHANGELOG.md).
; Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-19:
; stepped to v2.0.0 -- femmqt.exe (Qt GUI) becomes the sole Start Menu
; shortcut, see the FEMMX.lnk comment below (see CHANGELOG.md).
; Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-19:
; packaged the new femmqt.exe (Qt GUI, magnetics-only Phase 1) and its Qt6
; runtime (DLLs + plugin subfolders, deployed into flat bin\ by
; femmqt/CMakeLists.txt's windeployqt install(CODE ...) step -- see that
; file for why it's flat bin\, not bin\plain\: this whole installer build
; runs as part of the same `cmake --build . --target install` invocation
; that populates bin\ in the first place, before build_femmx.ps1's later
; bin\ -> bin\plain\/bin\cuda\ move ever happens). Made it the only Start
; Menu shortcut target (FEMMX.lnk); the classic MFC GUI stays fully
; installed to bin\ (still load-bearing -- see below) but no longer gets
; its own Start Menu entry, per user request: a user who followed the
; earlier "FEMMX (Classic)" shortcut out of habit landed in the MFC app
; and hit a real bug there (FemmviewView.cpp's OnSwitchToQtGui was never
; wired into its message map, so its own "Switch to Qt GUI" menu item
; silently did nothing). femm.ActiveFEMM's COM automation registration
; deliberately still points at femmx.exe (below, unchanged) -- femmqt.exe
; has no COM automation support yet, so pointing COM at it would break
; existing pyfemm/Octave/Mathematica/Scilab scripts. All the new File
; lines are /nonfatal, matching this file's existing CUDA DLL precedent,
; so a build with SKIP_femmqt set still packages cleanly.
Unicode True
!include MUI2.nsh
!include LogicLib.nsh
!define PROJECT_NAME "FEMMX"
; Single source of truth for the installer's own display/file version --
; keep in sync with femm/femm.rc's VERSIONINFO block and the git tag
; created for each release (see CHANGELOG.md).
!define PROJECT_VERSION "2.0.0"
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

; Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-20:
; switched from the legacy `Page license`/`Page directory`/`Page instFiles`
; commands to actual Modern UI 2 page macros -- MUI2.nsh was already
; !include'd above but its macros were never used, which is exactly why
; every build log shows a page of "Variable mui.Header.Text not
; referenced or never set" warnings: the header's variables were declared
; but nothing ever consumed them. Real, user-visible effect: this gets
; the modern left-banner welcome/finish screens and progress-page styling
; instead of the bare classic page flow. Also set the installer/
; uninstaller window icon to FEMMX's real app icon (previously the
; generic NSIS icon), and added a "View README" checkbox on the finish
; page pointing at the copy of README.md this installer now bundles into
; $INSTDIR (see the File "README.md" line below) -- both per user
; request.
!define MUI_ICON "femm\res\idr_mainframe.ico"
!define MUI_UNICON "femm\res\idr_mainframe.ico"
!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_SHOWREADME "$INSTDIR\README.md"
!define MUI_FINISHPAGE_SHOWREADME_TEXT "View README"
; Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-20:
; without this, checking the box launches README.md via its OS file
; association -- confirmed directly on a real machine (`assoc .md`
; returns "File association not found for extension .md") that .md has
; NO default handler on a stock Windows install, so the box would
; silently do nothing (or prompt an unhelpful "how do you want to open
; this file" chooser) instead of showing the readme. Force it through
; Notepad instead, which is always present and needs no association.
!define MUI_FINISHPAGE_SHOWREADME_FUNCTION ShowReadme

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "license.txt"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

# Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-20:
# NSIS's `Name` directive (not previously set at all) drives the wizard
# window's own title ("Welcome to <Name> Setup", the taskbar entry, etc.)
# -- without it the installer window literally read "Name Setup",
# confirmed directly by actually running the built installer and looking
# at it, not just reading the script.
Name "${PROJECT_NAME}"

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
    # top-level docs -- README.md is opened from the finish page's "View
    # README" checkbox (see MUI_FINISHPAGE_SHOWREADME above), so it needs
    # to actually be installed, not just referenced from the repo checkout.
    SetOutPath "$INSTDIR"
    File "README.md"

    # executables + runtime data files, mirroring FEMM 4.2's C:\femm42\bin
    SetOutPath "$INSTDIR\bin"
    File "bin\belasolv.exe"
    File "bin\condlib.dat"
    File "bin\csolv.exe"
    File "bin\femmx.exe"
    File /nonfatal "bin\femmqt.exe"
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

    # femmqt.exe's Qt6 runtime, deployed into flat bin\ by
    # femmqt/CMakeLists.txt's windeployqt install(CODE ...) step. Wildcarded
    # (not individually named) so this doesn't need to track exactly which
    # DLLs/plugins windeployqt decides a given Qt version needs -- same
    # reasoning as the CUDA DLLs above. /nonfatal throughout: a build with
    # SKIP_femmqt set (or a Qt-less machine) has none of this in bin\, and
    # the installer should still package everything else cleanly.
    #
    # Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-20:
    # this used to individually name each Qt6*.dll, which silently fell out
    # of sync with reality: Qt6PrintSupport.dll (needed once femmqt gained
    # Print/Print Preview support) was never added to this list, so every
    # installed copy shipped without it -- femmqt.exe would launch, load 41
    # DLLs, then hang indefinitely (0% CPU, zero windows, confirmed via
    # dumpbin/module-list diffing against a working dev build) rather than
    # failing loudly, since the missing dependency wasn't in the platform
    # plugin's own direct import table. Switched to an actual wildcard,
    # matching what the comment above already claimed and what every
    # plugin-subfolder File line below already does -- the next new Qt
    # module femmqt links against won't need this list touched by hand.
    File /nonfatal "bin\Qt6*.dll"
    File /nonfatal "bin\opengl32sw.dll"
    File /nonfatal "bin\D3Dcompiler_47.dll"
    File /nonfatal "bin\dxcompiler.dll"
    File /nonfatal "bin\dxil.dll"

    SetOutPath "$INSTDIR\bin\generic"
    File /nonfatal "bin\generic\*.dll"
    SetOutPath "$INSTDIR\bin\iconengines"
    File /nonfatal "bin\iconengines\*.dll"
    SetOutPath "$INSTDIR\bin\imageformats"
    File /nonfatal "bin\imageformats\*.dll"
    SetOutPath "$INSTDIR\bin\networkinformation"
    File /nonfatal "bin\networkinformation\*.dll"
    SetOutPath "$INSTDIR\bin\platforms"
    File /nonfatal "bin\platforms\*.dll"
    SetOutPath "$INSTDIR\bin\styles"
    File /nonfatal "bin\styles\*.dll"
    SetOutPath "$INSTDIR\bin\tls"
    File /nonfatal "bin\tls\*.dll"
    SetOutPath "$INSTDIR\bin"

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
    ; Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-21:
    ; reverted -- FEMMX.lnk now launches the classic MFC GUI again, per
    ; user request ("QT has too many bugs at the moment, use the old GUI
    ; as the default option"). femmqt.exe stays fully installed (still
    ; needed: its own "Switch to Classic GUI" menu item, and vice versa
    ; via femmx.exe's "Switch to Qt GUI") and gets its own secondary
    ; shortcut below rather than none at all, so it's still one click
    ; away for anyone who wants it despite not being the default.
    CreateShortcut "$SMPROGRAMS\${PROJECT_NAME}\FEMMX.lnk" "$INSTDIR\bin\femmx.exe"
    CreateShortcut "$SMPROGRAMS\${PROJECT_NAME}\FEMMX (Qt).lnk" "$INSTDIR\bin\femmqt.exe"
    CreateShortcut "$SMPROGRAMS\${PROJECT_NAME}\Uninstall.lnk" "$INSTDIR\${PROJECT_UNINSTALL_EXE}"

    WriteRegStr HKCU "${PROJECT_REG_UNINSTALL_KEY}" "UninstallString" '"$INSTDIR\${PROJECT_UNINSTALL_EXE}" _?=$INSTDIR'
    WriteRegStr HKCU "${PROJECT_REG_UNINSTALL_KEY}" "QuietUninstallString" '"$INSTDIR\${PROJECT_UNINSTALL_EXE}" /S _?=$INSTDIR'
    WriteRegStr HKCU "${PROJECT_REG_UNINSTALL_KEY}" "DisplayVersion" "${PROJECT_VERSION}"
SectionEnd

; Called from the finish page's "View README" checkbox (see
; MUI_FINISHPAGE_SHOWREADME_FUNCTION above) instead of MUI2's default
; ExecShell-by-association behavior.
Function ShowReadme
    Exec '"$WINDIR\notepad.exe" "$INSTDIR\README.md"'
FunctionEnd

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

    # delete top-level docs
    Delete "$INSTDIR\README.md"

    # delete the uninstaller
    Delete "$INSTDIR\${PROJECT_UNINSTALL_EXE}"

    # remove the links from the start menu
    Delete "$SMPROGRAMS\${PROJECT_NAME}\FEMMX.lnk"
    Delete "$SMPROGRAMS\${PROJECT_NAME}\FEMMX (Classic).lnk"
    Delete "$SMPROGRAMS\${PROJECT_NAME}\FEMMX (Qt).lnk"
    Delete "$SMPROGRAMS\${PROJECT_NAME}\Uninstall.lnk"
    ; the FEMMX (Classic).lnk Delete above is kept even though no shipped
    ; version of the installer has created that exact filename since --
    ; harmlessly no-ops on a fresh install, but still cleans it up for
    ; anyone upgrading from the specific older install that did create it.
    RMDir "$STARTMENU\Programs\${PROJECT_NAME}"

    # remove the installdir
    RMDir "$INSTDIR"

    DeleteRegKey HKCU "${PROJECT_REG_UNINSTALL_KEY}"
SectionEnd
