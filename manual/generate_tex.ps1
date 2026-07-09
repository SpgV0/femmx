# Substitutes @CMAKE_CURRENT_SOURCE_DIR@ for this directory's own path in
# the .tex.in sources, mirroring the configure_file() calls in
# CMakeLists.txt -- run by build_manual.bat before invoking latex, so the
# manual can be rebuilt without going through a full CMake configure.
#
# CMake's CMAKE_CURRENT_SOURCE_DIR uses forward slashes even on Windows
# (precisely so it's safe to embed in files like this one) -- LaTeX
# treats backslash as its command-escape character, so substituting
# $PSScriptRoot's native "C:\Users\..." form here would make every
# \includegraphics{...} path get parsed as a string of undefined control
# sequences instead of a filename. Match CMake's behavior explicitly.
$dir = $PSScriptRoot -replace '\\', '/'

foreach ($name in @('manual', 'cflow', 'heatflow')) {
    $inPath = Join-Path $dir "$name.tex.in"
    $outPath = Join-Path $dir "$name.tex"
    $content = Get-Content -LiteralPath $inPath -Raw
    $content = $content.Replace('@CMAKE_CURRENT_SOURCE_DIR@', $dir)
    Set-Content -LiteralPath $outPath -Value $content -NoNewline
}
