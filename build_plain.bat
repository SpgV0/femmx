@echo off
rem Builds FEMMX (femmx.exe) and the NSIS installer (bin\FEMMX_installer.exe)
rem locally, CPU-only. For CUDA GPU acceleration use build_cuda.bat instead.
rem Real logic lives in build_femmx.ps1, which wraps build.ps1.

setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build_femmx.ps1"
set "EXITCODE=%ERRORLEVEL%"
endlocal & exit /b %EXITCODE%
