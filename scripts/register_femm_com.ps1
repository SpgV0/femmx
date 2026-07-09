#!/usr/bin/env pwsh
<#
.SYNOPSIS
  Registers femmx.exe's ActiveFEMM COM automation class (femm.ActiveFEMM) in
  the current user's registry hive.

.DESCRIPTION
  femmx.exe is supposed to self-register this COM class on every normal
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
  produced, using femmx.exe's own hardcoded CLSID
  (0A35D5BD-DCA9-4C39-9512-1D89A1A37047, see femm/ActiveFEMM.cpp's
  IMPLEMENT_OLECREATE2 call). This is late-bound IDispatch access (what
  pyfemm/win32com.client.Dispatch use), so the missing type library doesn't
  matter for that purpose. The ProgID itself (femm.ActiveFEMM) is unchanged
  by the femm.exe -> femmx.exe rename -- only the LocalServer32 path moves.

  Entries are written under HKEY_CURRENT_USER\Software\Classes, so this
  needs no administrator rights and only affects the current user profile
  -- safe to run in CI or locally, and trivially reversible (delete the
  same keys, or just don't run it again).

.PARAMETER FemmExePath
  Path to the built femmx.exe. Defaults to <repo root>/bin/femmx.exe.

.EXAMPLE
  ./scripts/register_femm_com.ps1
.EXAMPLE
  ./scripts/register_femm_com.ps1 -FemmExePath C:\path\to\femmx.exe

.NOTES
  Before overwriting anything, this snapshots whatever LocalServer32 this
  CLSID already pointed at (if any) to
  $env:TEMP\femmx_com_registration_snapshot.json -- but only if that
  snapshot file doesn't already exist, so re-running this repeatedly in
  one session (e.g. switching between a CPU and a CUDA build while
  testing) doesn't lose track of the true pre-session original. Run
  scripts/unregister_femm_com.ps1 afterwards (e.g. once a regression test
  session is done) to restore that original registration, or remove it
  entirely if there wasn't one -- so running the test suite doesn't
  permanently repoint femm.ActiveFEMM on the machine it runs on.
#>
param(
  [string]$FemmExePath = (Join-Path (Split-Path -Parent $PSScriptRoot) "bin\femmx.exe")
)

$ErrorActionPreference = "Stop"

if (-Not (Test-Path $FemmExePath)) {
  throw "femmx.exe not found at '$FemmExePath'. Build it first (see build.ps1)."
}
$FemmExePath = (Resolve-Path $FemmExePath).Path

$clsid = "{0A35D5BD-DCA9-4C39-9512-1D89A1A37047}"
$progid = "femm.ActiveFEMM"

$snapshotPath = Join-Path $env:TEMP "femmx_com_registration_snapshot.json"
if (-not (Test-Path $snapshotPath)) {
  $localServerKey = "HKCU:\Software\Classes\CLSID\$clsid\LocalServer32"
  if (Test-Path $localServerKey) {
    $priorPath = (Get-Item $localServerKey).GetValue("")
    @{ HadPriorRegistration = $true; PriorLocalServer32 = $priorPath } | ConvertTo-Json | Set-Content $snapshotPath
  }
  else {
    @{ HadPriorRegistration = $false; PriorLocalServer32 = $null } | ConvertTo-Json | Set-Content $snapshotPath
  }
}

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
