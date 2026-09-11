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
; Modified by Claude (Anthropic), noreply@anthropic.com, 2026-08-04:
; bumped to v2.1.2 -- fixes femmqt's Density Plot H (magnetic field
; intensity) being wrong by orders of magnitude for nonlinear (BH-curve)
; materials (see CHANGELOG.md).
; Single source of truth for the installer's own display/file version --
; keep in sync with femm/femm.rc's VERSIONINFO block and the git tag
; created for each release (see CHANGELOG.md).
; Modified by Claude (Anthropic), noreply@anthropic.com, 2026-09-09:
; bumped to v2.2.1 (see CHANGELOG.md; v2.2.0 was burned by a deleted
; immutable release and can never be re-published).
!define PROJECT_VERSION "2.2.1"
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
; Modified by Claude (Anthropic), noreply@anthropic.com: per direct user
; request for "a tickbox to start the software in the end". Points at the
; classic GUI, which is what the primary FEMMX.lnk Start Menu shortcut
; launches too (see CreateShortcut below) -- the Qt GUI stays reachable
; from its own shortcut and from View > Switch To.
;
; Launching directly rather than through a helper function is safe here
; only because this installer declares RequestExecutionLevel user (see
; below): it never elevates, so the app does not inherit administrator
; rights the way it would from a typical elevated installer.
!define MUI_FINISHPAGE_RUN "$INSTDIR\bin\femmx.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Run FEMMX"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "license.txt"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

; Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-25:
; detect an existing installation before showing any wizard pages, and ask
; the user whether to uninstall it first, rather than silently layering a
; fresh install on top of (or alongside) an old one. Runs in .onInit (before
; the Welcome page) so a "No" answer can bail out without making the user
; click through License/Directory first. Replaces the old silent
; auto-uninstall attempt that used to live at the top of the install
; Section: that block called EnumRegKey (which enumerates the Nth *subkey*
; by numeric index) with "QuietUninstallString" as the index argument --
; that's a value name, not a subkey index, so it never actually detected an
; existing install at all.
Function .onInit
    ClearErrors
    ReadRegStr $0 HKCU "${PROJECT_REG_UNINSTALL_KEY}" "QuietUninstallString"
    IfErrors done

    ; Modified by Claude (Anthropic), noreply@anthropic.com, 2026-09-11:
    ; NSIS runs .onInit for a /S install too, and a MessageBox there has
    ; nobody to answer it -- so `installer.exe /S` used to hang FOREVER on
    ; any machine that already had FEMMX installed, with the wizard window
    ; created but never shown. Measured while writing the installer smoke
    ; test (issue #21): the process sat at 0.1s of CPU with a "FEMMX Setup"
    ; main window and no visible top-level window, until killed. That made
    ; unattended upgrades impossible -- for CI, for a deployment script,
    ; for anyone at all -- and it failed by blocking rather than by saying
    ; anything, which is the same shape as the modal-dialog hangs in
    ; #10/#14 and in savebitmap.
    ;
    ; In silent mode the answer is not in doubt: the caller asked for an
    ; unattended install, so take the upgrade path without asking.
    IfSilent do_uninstall

    ReadRegStr $1 HKCU "${PROJECT_REG_UNINSTALL_KEY}" "DisplayVersion"
    MessageBox MB_YESNO|MB_ICONQUESTION \
        "${PROJECT_NAME} $1 is already installed.$\n$\nUninstall it and continue installing this version?" \
        IDYES do_uninstall
    Abort

    do_uninstall:
        ; QuietUninstallString already includes _?=$INSTDIR (see the
        ; WriteRegStr near the end of the install Section), which makes the
        ; uninstaller run in-process instead of copy-and-relaunch, so
        ; ExecWait genuinely blocks until removal is complete before this
        ; installer lays down fresh files.
        ExecWait '$0'
    done:
FunctionEnd

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
    # Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-21:
    # per user request -- neither GUI's Help > Help Topics could find this
    # since the installer never packaged it at all. Both femm/MainFrm.cpp
    # and femmqt/MainWindow.cpp's/SolutionView.cpp's manual-finding logic
    # look next to the exe (bin\manual.pdf) first, matching this. /nonfatal
    # since building it needs a LaTeX toolchain (manual/CMakeLists.txt) --
    # already known to be occasionally unavailable in CI (see
    # build_femmx.ps1's own choco/latex.exe PATH fix) -- so a build without
    # one still packages everything else cleanly, just without the manual.
    File /nonfatal "manual\manual.pdf"

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
    ;
    ; Modified by Claude (Anthropic), noreply@anthropic.com, 2026-09-11:
    ; SetRegView 64 added. makensis builds a 32-BIT installer, and on
    ; 64-bit Windows a 32-bit process writing under HKCU\Software\Classes\
    ; CLSID is silently redirected into HKCU\Software\Classes\Wow6432Node\
    ; CLSID. femmx.exe is 64-bit, so this was registering a 64-bit COM
    ; server in the one place no 64-bit client ever looks: after an
    ; install, 64-bit Python/pyfemm, Octave and Scilab all got "class not
    ; registered", and the only users for whom automation worked were
    ; those who had separately run scripts/register_femm_com.ps1.
    ;
    ; Measured directly (issue #21): a silent install put
    ; "C:\...\bin\femmx.exe" in ...Classes\Wow6432Node\CLSID\{...}\
    ; LocalServer32 while the 64-bit view kept whatever was there before.
    ; Note the half-registered state this produces, which is worse than a
    ; clean failure: the ProgID key (Software\Classes\femm.ActiveFEMM) is
    ; NOT subject to redirection, so it resolved to the CLSID perfectly
    ; well while the CLSID itself was invisible.
    SetRegView 64
    WriteRegStr HKCU "Software\Classes\${FEMM_COM_PROGID}" "" "Femm.ActiveFEMM Object"
    WriteRegStr HKCU "Software\Classes\${FEMM_COM_PROGID}\CLSID" "" "${FEMM_COM_CLSID}"
    WriteRegStr HKCU "Software\Classes\CLSID\${FEMM_COM_CLSID}" "" "Femm.ActiveFEMM Object"
    WriteRegStr HKCU "Software\Classes\CLSID\${FEMM_COM_CLSID}\LocalServer32" "" '"$INSTDIR\bin\femmx.exe"'
    WriteRegStr HKCU "Software\Classes\CLSID\${FEMM_COM_CLSID}\ProgID" "" "${FEMM_COM_PROGID}"
    SetRegView lastused

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
    ; SetRegView 64 for the same reason the install Section sets it: this
    ; uninstaller is a 32-bit binary, so without it the DeleteRegKey calls
    ; would go to Wow6432Node and leave the real registration behind
    ; (issue #21). The Wow6432Node keys are deleted too, so a machine that
    ; was "installed" by a pre-fix installer gets cleaned up rather than
    ; keeping a dangling 32-bit-view registration forever.
    SetRegView 64
    DeleteRegKey HKCU "Software\Classes\${FEMM_COM_PROGID}"
    DeleteRegKey HKCU "Software\Classes\CLSID\${FEMM_COM_CLSID}"
    DeleteRegKey HKCU "Software\Classes\Wow6432Node\CLSID\${FEMM_COM_CLSID}"
    DeleteRegKey HKCU "Software\Classes\Wow6432Node\${FEMM_COM_PROGID}"
    SetRegView lastused

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
