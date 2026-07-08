#!/usr/bin/env pwsh
<#
.SYNOPSIS
  Registers femm.exe's ActiveFEMM COM automation class (femm.ActiveFEMM) in
  the current user's registry hive.

.DESCRIPTION
  femm.exe is supposed to self-register this COM class on every normal
  launch (CFemmApp::InitInstance calls COleObjectFactory::UpdateRegistryAll)
  and supports the standard MFC "/regserver" switch to do so explicitly.
  In practice, neither path currently registers anything -- this CMake-based
  build never runs the MIDL/ODL compilation step that the original Visual
  Studio project used to produce femm's type library
  (femm/femm.odl -> femm.tlb), and something about that gap appears to
  prevent COleObjectFactory::UpdateRegistryAll from writing any registry
  entries at all, even though writing them doesn't structurally require the
  type library.

  Until that's root-caused and fixed upstream in the CMake build, this
  script performs the equivalent registration by hand: it writes the
  ProgID/CLSID/LocalServer32 entries a working self-registration would have
  produced, using femm.exe's own hardcoded CLSID
  (0A35D5BD-DCA9-4C39-9512-1D89A1A37047, see femm/ActiveFEMM.cpp's
  IMPLEMENT_OLECREATE2 call). This is late-bound IDispatch access (what
  pyfemm/win32com.client.Dispatch use), so the missing type library doesn't
  matter for that purpose.

  Entries are written under HKEY_CURRENT_USER\Software\Classes, so this
  needs no administrator rights and only affects the current user profile
  -- safe to run in CI or locally, and trivially reversible (delete the
  same keys, or just don't run it again).

.PARAMETER FemmExePath
  Path to the built femm.exe. Defaults to <repo root>/bin/femm.exe.

.EXAMPLE
  ./scripts/register_femm_com.ps1
.EXAMPLE
  ./scripts/register_femm_com.ps1 -FemmExePath C:\path\to\femm.exe
#>
param(
  [string]$FemmExePath = (Join-Path (Split-Path -Parent $PSScriptRoot) "bin\femm.exe")
)

$ErrorActionPreference = "Stop"

if (-Not (Test-Path $FemmExePath)) {
  throw "femm.exe not found at '$FemmExePath'. Build it first (see build.ps1)."
}
$FemmExePath = (Resolve-Path $FemmExePath).Path

$clsid = "{0A35D5BD-DCA9-4C39-9512-1D89A1A37047}"
$progid = "femm.ActiveFEMM"

New-Item -Path "HKCU:\Software\Classes\$progid" -Force | Out-Null
Set-Item -Path "HKCU:\Software\Classes\$progid" -Value "Femm.ActiveFEMM Object"
New-Item -Path "HKCU:\Software\Classes\$progid\CLSID" -Force | Out-Null
Set-Item -Path "HKCU:\Software\Classes\$progid\CLSID" -Value $clsid

New-Item -Path "HKCU:\Software\Classes\CLSID\$clsid" -Force | Out-Null
Set-Item -Path "HKCU:\Software\Classes\CLSID\$clsid" -Value "Femm.ActiveFEMM Object"
New-Item -Path "HKCU:\Software\Classes\CLSID\$clsid\LocalServer32" -Force | Out-Null
Set-Item -Path "HKCU:\Software\Classes\CLSID\$clsid\LocalServer32" -Value "`"$FemmExePath`""
New-Item -Path "HKCU:\Software\Classes\CLSID\$clsid\ProgID" -Force | Out-Null
Set-Item -Path "HKCU:\Software\Classes\CLSID\$clsid\ProgID" -Value $progid

Write-Host "Registered $progid -> $FemmExePath" -ForegroundColor Green
