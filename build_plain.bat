@echo off
rem Added by Claude (Anthropic), noreply@anthropic.com, 2026-07-09.
rem
rem Builds FEMMX (femmx.exe) and the NSIS installer (bin\FEMMX_v<version>_
rem installer.exe, see script.nsi's PROJECT_VERSION) locally, CPU-only. For
rem CUDA GPU acceleration use build_cuda.bat instead.
rem Real logic lives in build_femmx.ps1, which wraps build.ps1.

setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build_femmx.ps1"
set "EXITCODE=%ERRORLEVEL%"
endlocal & exit /b %EXITCODE%
