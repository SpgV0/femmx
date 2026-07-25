# Added by Claude (Anthropic), noreply@anthropic.com, 2026-07-09.
#
# Shared helper behind build_plain.bat / build_cuda.bat. Not meant to be
# run directly -- use one of those instead. Both run with zero required
# arguments; everything below is auto-detected, with diagnostics printed
# for anything missing or ambiguous.
#
# Wraps build.ps1 with the flags needed for a local dev build that
# produces both femmx.exe and the NSIS installer (bin\FEMMX_v<version>_
# installer.exe, built automatically by the root CMakeLists.txt's install
# step whenever makensis is found -- see script.nsi's PROJECT_VERSION).
# With -Cuda, also enables the
# CUDA-accelerated solver (-DENABLE_CUDA_SOLVER=ON, see fkn/CMakeLists.txt).
#
# IMPORTANT: the call to build.ps1 below is deliberately NOT wrapped in
# try/catch. build.ps1 opens with a Stop-Transcript guarded by
# $ErrorActionPreference = "SilentlyContinue", meant to harmlessly no-op
# when nothing is transcribing yet. That guard only works when nothing
# further up the call stack has a try/catch around the invocation -- with
# one, PowerShell turns that normally-suppressed error into a real thrown
# exception and the build fails immediately, before printing anything
# (verified by direct repro). If you add error handling here, do it
# without try/catch around the "&" call (e.g. check $LASTEXITCODE).

param(
  [switch]$Cuda,
  [string]$CudaRoot = ""
)

Push-Location $PSScriptRoot

# build.ps1/CMake/script.nsi always build into the fixed bin\ dir (CI relies
# on that path, so it's left alone). To keep a plain and a CUDA build from
# overwriting each other, everything bin\ ends up holding after a successful
# build is moved into bin\plain\ or bin\cuda\ at the end of this script.
$variant = if ($Cuda) { "cuda" } else { "plain" }
$variantDir = Join-Path "$PSScriptRoot\bin" $variant

Write-Host "=== FEMMX local build ===" -ForegroundColor Cyan

# --- NSIS (installer) diagnostics -------------------------------------
$makensis = Get-Command makensis -ErrorAction SilentlyContinue
if (-not $makensis -and (Test-Path "$env:ProgramFiles\NSIS\makensis.exe")) {
  $makensis = "$env:ProgramFiles\NSIS\makensis.exe"
}
if (-not $makensis -and (Test-Path "${env:ProgramFiles(x86)}\NSIS\makensis.exe")) {
  $makensis = "${env:ProgramFiles(x86)}\NSIS\makensis.exe"
}
if ($makensis) {
  Write-Host "[ok] NSIS found -- installer will be built" -ForegroundColor Green
}
else {
  Write-Host "[!!] NSIS not found -- the installer will NOT be built." -ForegroundColor Yellow
  Write-Host "     Install it, e.g.: winget install NSIS.NSIS" -ForegroundColor Yellow
}

# --- CMake / Git diagnostics (build.ps1 also checks these, but fail fast) ---
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
  Write-Host "[!!] cmake not found on PATH -- install it: winget install Kitware.CMake" -ForegroundColor Red
  Pop-Location
  exit 1
}
if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
  Write-Host "[!!] git not found on PATH -- install it: winget install Git.Git" -ForegroundColor Red
  Pop-Location
  exit 1
}

$extra = ""

if ($Cuda) {
  # --- CUDA Toolkit root ---
  if (-not $CudaRoot) { $CudaRoot = $env:CUDA_PATH }
  if (-not $CudaRoot) {
    $candidates = Get-ChildItem "$env:ProgramFiles\NVIDIA GPU Computing Toolkit\CUDA" -Directory -Filter "v*" -ErrorAction SilentlyContinue
    if ($candidates) {
      # Prefer the newest 12.x install: CUDA 13.x dropped compute capability
      # sm_60, one of this project's default FEMM_CUDA_ARCHS entries (see
      # fkn/CMakeLists.txt and .claude-memory/gpu_speedup_investigation.md),
      # so a 13.x-only build would fail with the default arch list.
      $preferred = $candidates | Where-Object { $_.Name -match '^v12\.' } | Sort-Object Name -Descending | Select-Object -First 1
      if ($preferred) {
        $CudaRoot = $preferred.FullName
      }
      else {
        $newest = $candidates | Sort-Object Name -Descending | Select-Object -First 1
        $CudaRoot = $newest.FullName
        Write-Host "[!!] Only CUDA $($newest.Name) found -- 13.x+ dropped sm_60 support (a default FEMM_CUDA_ARCHS entry)." -ForegroundColor Yellow
        Write-Host "     The build may fail. Install CUDA 12.x for a known-working combo, or edit FEMM_CUDA_ARCHS in fkn/CMakeLists.txt." -ForegroundColor Yellow
      }
      if ($candidates.Count -gt 1) {
        Write-Host "[i] Multiple CUDA Toolkit versions found, using $CudaRoot" -ForegroundColor Cyan
        Write-Host "    (pass a path as the first argument to override)" -ForegroundColor Cyan
      }
    }
  }
  if (-not $CudaRoot) {
    Write-Host "[!!] No CUDA Toolkit installation found." -ForegroundColor Red
    Write-Host "     Install it: winget install Nvidia.CUDA, or pass its path as an argument:" -ForegroundColor Red
    Write-Host '     build_cuda.bat "C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.6"' -ForegroundColor Red
    Pop-Location
    exit 1
  }
  Write-Host "[ok] Using CUDA Toolkit: $CudaRoot" -ForegroundColor Green

  $extra = "-DENABLE_CUDA_SOLVER=ON `"-DFEMM_CUDA_ROOT=$CudaRoot`""

  # --- nvcc host compiler (-ccbin) ---
  # CUDA's tested MSVC support window tends to lag behind the newest
  # installed Visual Studio; nvcc's cudafe++ can hard-crash parsing
  # headers from a too-new MSVC. If a VS2022 toolset is present, prefer
  # it for nvcc's host compiler regardless of which VS is "latest" --
  # this is the combo previously validated to work (see
  # .claude-memory/gpu_speedup_investigation.md's toolchain-gotchas
  # section). $env:FEMM_CUDA_CCBIN, if set, always wins.
  if ($env:FEMM_CUDA_CCBIN) {
    Write-Host "[ok] Using FEMM_CUDA_CCBIN from environment: $($env:FEMM_CUDA_CCBIN)" -ForegroundColor Green
    $extra += " `"-DFEMM_CUDA_CCBIN=$($env:FEMM_CUDA_CCBIN)`""
  }
  else {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $ccbin = $null
    if (Test-Path $vswhere) {
      $vs2022Path = & $vswhere -products * -version "[17.0,18.0)" -latest -property installationPath 2>$null
      if ($vs2022Path) {
        $msvcRoot = Join-Path $vs2022Path "VC\Tools\MSVC"
        $toolset = Get-ChildItem $msvcRoot -Directory -ErrorAction SilentlyContinue | Sort-Object Name -Descending | Select-Object -First 1
        if ($toolset) {
          $candidateCcbin = Join-Path $toolset.FullName "bin\Hostx64\x64"
          if (Test-Path (Join-Path $candidateCcbin "cl.exe")) {
            $ccbin = $candidateCcbin
          }
        }
      }
    }
    if ($ccbin) {
      Write-Host "[ok] Found VS2022 toolset for nvcc -ccbin: $ccbin" -ForegroundColor Green
      $extra += " `"-DFEMM_CUDA_CCBIN=$ccbin`""
    }
    else {
      Write-Host "[i] No VS2022 toolset found -- nvcc will use whatever MSVC build.ps1 selects." -ForegroundColor Cyan
      Write-Host "    If nvcc crashes compiling spars_cuda.cu, install VS2022 Build Tools" -ForegroundColor Cyan
      Write-Host "    (winget install Microsoft.VisualStudio.2022.BuildTools, VCTools workload)" -ForegroundColor Cyan
      Write-Host "    and/or set FEMM_CUDA_CCBIN to its VC\Tools\MSVC\<ver>\bin\Hostx64\x64." -ForegroundColor Cyan
    }
  }
}

Write-Host ""
Write-Host "Starting build..." -ForegroundColor Cyan
Write-Host ""

# build.ps1 signals failure by throwing (MyThrow, since -DisableInteractive
# is passed) rather than via an exit code -- it never sets one on success.
# So: reaching the line after this call at all means it succeeded: on
# failure the throw propagates uncaught (deliberately not caught here,
# see the note at the top of this file) and PowerShell exits the process
# with a non-zero code on its own.
& "$PSScriptRoot\build.ps1" -DoNotUpdateTOOL -DisableInteractive -DisableLaTeX -ForceTriangle32bit -AdditionalBuildSetup $extra

# Move this build's output (everything build.ps1 just placed directly in
# bin\, not the plain\/cuda\ subfolders themselves) into bin\<variant>\, so
# a plain build and a CUDA build don't clobber each other. Except: a
# handful of data files (condlib.dat etc.) are static, git-tracked repo
# assets that live in bin\ from the start -- not something CMake
# (re)generates -- and script.nsi expects to find them there on the next
# build too, so those get copied into the variant folder rather than
# moved out of bin\.
$staticDataFiles = @("condlib.dat", "heatlib.dat", "init.lua", "license.txt", "matlib.dat", "statlib.dat")
Remove-Item -Recurse -Force $variantDir -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $variantDir | Out-Null
Get-ChildItem -Path "$PSScriptRoot\bin" -File | ForEach-Object {
  if ($staticDataFiles -contains $_.Name) {
    Copy-Item $_.FullName -Destination $variantDir
  } else {
    Move-Item $_.FullName -Destination $variantDir -Force
  }
}
# Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-18:
# also move the Qt plugin subdirectories qt_generate_deploy_app_script()
# places directly in bin\ during `--target install` (see
# femmqt/CMakeLists.txt) into the variant folder. Without this,
# femmqt.exe itself gets moved (it's a top-level file, caught by the loop
# above) but its required Qt platform plugin does not, and it fails to
# start with "no Qt platform plugin could be initialized". Named
# allowlist (matching Qt's own deploy-tool plugin category names) rather
# than "any directory found in bin\", deliberately: an unrelated stray
# bin\cuda\ directory was found sitting there from an earlier, unrelated
# build during development of this step, and a blanket move would have
# silently relocated it into bin\plain\cuda\ -- exactly the kind of
# leftover-garbage-on-disk mistake to avoid, not compound.
$qtPluginDirs = @("generic", "iconengines", "imageformats", "networkinformation",
  "platforms", "styles", "tls", "multimedia", "sqldrivers", "printsupport")
Get-ChildItem -Path "$PSScriptRoot\bin" -Directory -ErrorAction SilentlyContinue |
  Where-Object { $qtPluginDirs -contains $_.Name } |
  ForEach-Object {
    Move-Item $_.FullName -Destination $variantDir -Force
  }

Pop-Location

Write-Host ""
Write-Host "Build complete." -ForegroundColor Green
Write-Host "  femmx.exe:            bin\$variant\femmx.exe"
$installer = Get-ChildItem "bin\$variant\FEMMX_v*_installer.exe" -ErrorAction SilentlyContinue | Select-Object -First 1
if ($installer) {
    Write-Host "  Installer:            bin\$variant\$($installer.Name)"
} else {
    Write-Host "  Installer:            not built (makensis not found, or partial build)"
}
exit 0
