@echo off
rem Added by Claude (Anthropic), noreply@anthropic.com, 2026-07-10.
rem
rem Builds both FEMMX variants back to back: build_plain.bat (CPU-only,
rem bin\plain\) then build_cuda.bat (GPU-accelerated, bin\cuda\). Each of
rem those is already a complete, independent build -- this is just a
rem convenience wrapper so both end up freshly built in one run, e.g.
rem before cutting a release. Real logic lives in build_femmx.ps1.
rem
rem Usage:
rem   build_all.bat
rem   build_all.bat "C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.6"
rem
rem Optional: set FEMM_CUDA_CCBIN to an alternate MSVC toolset bin dir if
rem your installed Visual Studio is newer than the CUDA Toolkit supports
rem (see fkn/CMakeLists.txt and .claude-memory/gpu_speedup_investigation.md).

setlocal
echo === Building plain (CPU-only) variant ===
call "%~dp0build_plain.bat"
if not "%ERRORLEVEL%"=="0" (
  echo build_plain.bat failed with exit code %ERRORLEVEL% -- stopping before the CUDA build.
  endlocal & exit /b 1
)

echo.
echo === Building CUDA-accelerated variant ===
call "%~dp0build_cuda.bat" "%~1"
if not "%ERRORLEVEL%"=="0" (
  echo build_cuda.bat failed with exit code %ERRORLEVEL%.
  endlocal & exit /b 1
)

echo.
echo Both variants built successfully.
echo   bin\plain\femmx.exe
echo   bin\cuda\femmx.exe
endlocal & exit /b 0
