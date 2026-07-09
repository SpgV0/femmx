@echo off
rem Builds FEMMX (femmx.exe) with the CUDA-accelerated solver enabled and
rem the NSIS installer (bin\FEMMX_installer.exe), locally.
rem Real logic lives in build_femmx.ps1, which wraps build.ps1.
rem
rem Usage:
rem   build_cuda.bat
rem   build_cuda.bat "C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.6"
rem
rem Optional: set FEMM_CUDA_CCBIN to an alternate MSVC toolset bin dir if
rem your installed Visual Studio is newer than the CUDA Toolkit supports
rem (see fkn/CMakeLists.txt and .claude-memory/gpu_speedup_investigation.md).

setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build_femmx.ps1" -Cuda -CudaRoot "%~1"
set "EXITCODE=%ERRORLEVEL%"
endlocal & exit /b %EXITCODE%
