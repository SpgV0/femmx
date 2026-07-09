#!/usr/bin/env pwsh
<#
.SYNOPSIS
  Undoes register_femm_com.ps1: restores whatever femm.ActiveFEMM COM
  registration existed before it ran, or removes the registration
  entirely if there wasn't one.

.DESCRIPTION
  register_femm_com.ps1 snapshots the prior LocalServer32 value (if any)
  to $env:TEMP\femmx_com_registration_snapshot.json the first time it
  runs in a session, without overwriting that snapshot on later
  re-registrations (e.g. switching between a CPU and a CUDA build while
  testing) -- so it always remembers the true pre-session original. This
  script reads that snapshot, restores or removes the registration
  accordingly, then deletes the snapshot file.

  Meant to run once, after a test session that used
  register_femm_com.ps1 (see test/conftest.py, which relies on
  femm.ActiveFEMM being registered), so running the regression suite
  doesn't leave a permanent side effect on the machine it runs on.

  Safe to run even if register_femm_com.ps1 was never called this
  session (no-op) or was called multiple times (idempotent once the
  snapshot is consumed -- running this twice in a row is a no-op the
  second time).

.EXAMPLE
  ./scripts/unregister_femm_com.ps1
#>

$ErrorActionPreference = "Stop"

$clsid = "{0A35D5BD-DCA9-4C39-9512-1D89A1A37047}"
$progid = "femm.ActiveFEMM"
$snapshotPath = Join-Path $env:TEMP "femmx_com_registration_snapshot.json"

if (-not (Test-Path $snapshotPath)) {
  Write-Host "No femm.ActiveFEMM registration snapshot found -- nothing to restore (register_femm_com.ps1 probably wasn't run this session)." -ForegroundColor Yellow
  exit 0
}

$snapshot = Get-Content $snapshotPath -Raw | ConvertFrom-Json

if ($snapshot.HadPriorRegistration) {
  New-Item -Path "HKCU:\Software\Classes\CLSID\$clsid\LocalServer32" -Force | Out-Null
  Set-Item -Path "HKCU:\Software\Classes\CLSID\$clsid\LocalServer32" -Value $snapshot.PriorLocalServer32
  Write-Host "Restored femm.ActiveFEMM -> $($snapshot.PriorLocalServer32)" -ForegroundColor Green
}
else {
  Remove-Item -Path "HKCU:\Software\Classes\$progid" -Recurse -Force -ErrorAction SilentlyContinue
  Remove-Item -Path "HKCU:\Software\Classes\CLSID\$clsid" -Recurse -Force -ErrorAction SilentlyContinue
  Write-Host "Unregistered femm.ActiveFEMM (no prior registration existed before this session)." -ForegroundColor Green
}

Remove-Item $snapshotPath -Force
