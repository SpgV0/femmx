@echo off
setlocal
rem Builds manual\manual.pdf from the LaTeX sources in this directory.
rem Mirrors the latex -^> dvips -^> ps2pdf pipeline in manual\CMakeLists.txt
rem (which only runs as part of a full CMake configure/build) -- useful
rem for rebuilding just the manual without setting up the whole project.
rem
rem Requires a TeX distribution (MiKTeX or TeX Live) providing latex.exe
rem and dvips.exe, plus Ghostscript for the ps2pdf step (or a ps2pdf.exe,
rem e.g. the one TeX Live bundles).

cd /d "%~dp0"

where latex >nul 2>nul
if errorlevel 1 (
  echo ERROR: latex.exe not found on PATH.
  echo Install MiKTeX ^(https://miktex.org^) or TeX Live ^(https://tug.org/texlive^) and make sure its bin directory is on PATH.
  exit /b 1
)

where dvips >nul 2>nul
if errorlevel 1 (
  echo ERROR: dvips.exe not found on PATH. It ships with MiKTeX/TeX Live.
  exit /b 1
)

set "PS2PDF_CMD="
where ps2pdf >nul 2>nul
if not errorlevel 1 set "PS2PDF_CMD=ps2pdf"
if not defined PS2PDF_CMD (
  where gswin64c >nul 2>nul
  if not errorlevel 1 set "PS2PDF_CMD=gswin64c"
)
if not defined PS2PDF_CMD (
  where gswin32c >nul 2>nul
  if not errorlevel 1 set "PS2PDF_CMD=gswin32c"
)
if not defined PS2PDF_CMD (
  echo ERROR: no ps2pdf or Ghostscript ^(gswin64c/gswin32c^) found on PATH.
  echo Install Ghostscript: https://ghostscript.com/releases/gsdnld.html
  exit /b 1
)

echo Generating manual.tex / cflow.tex / heatflow.tex from their .tex.in sources...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0generate_tex.ps1"
if errorlevel 1 (
  echo ERROR: failed to generate .tex files from .tex.in sources.
  exit /b 1
)

echo Running latex ^(pass 1 of 2^)...
latex -interaction=nonstopmode manual.tex
echo Running latex ^(pass 2 of 2, resolves table of contents / cross-references^)...
latex -interaction=nonstopmode manual.tex
if not exist manual.dvi (
  echo ERROR: latex did not produce manual.dvi -- see manual.log for details.
  exit /b 1
)

echo Running dvips...
dvips manual.dvi -o manual.ps
if errorlevel 1 (
  echo ERROR: dvips failed.
  exit /b 1
)

echo Converting to PDF with %PS2PDF_CMD%...
if "%PS2PDF_CMD%"=="ps2pdf" (
  ps2pdf manual.ps manual.pdf
) else (
  %PS2PDF_CMD% -sDEVICE=pdfwrite -dNOPAUSE -dBATCH -dSAFER -sOutputFile=manual.pdf manual.ps
)

if exist manual.pdf (
  echo Done: %~dp0manual.pdf
) else (
  echo ERROR: manual.pdf was not created.
  exit /b 1
)

endlocal
