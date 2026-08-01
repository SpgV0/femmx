@echo off
rem Builds manual_qt.pdf with a plain pdflatex pipeline (no dvips/
rem Ghostscript step needed -- see manual_qt.tex's header comment).
rem Run from this directory (manual_qt\).
setlocal

where pdflatex >nul 2>nul
if errorlevel 1 (
  echo pdflatex not found on PATH -- install MiKTeX or TeX Live.
  exit /b 1
)

pdflatex -interaction=nonstopmode -halt-on-error manual_qt.tex
if errorlevel 1 goto :fail

rem Second pass to resolve the table of contents / page references.
pdflatex -interaction=nonstopmode -halt-on-error manual_qt.tex
if errorlevel 1 goto :fail

echo.
echo Build succeeded: manual_qt.pdf
exit /b 0

:fail
echo.
echo Build FAILED -- see manual_qt.log
exit /b 1
