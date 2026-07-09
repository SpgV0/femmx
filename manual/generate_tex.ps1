# Substitutes @CMAKE_CURRENT_SOURCE_DIR@ for this directory's own path in
# the .tex.in sources, mirroring the configure_file() calls in
# CMakeLists.txt -- run by build_manual.bat before invoking latex, so the
# manual can be rebuilt without going through a full CMake configure.
$dir = $PSScriptRoot

foreach ($name in @('manual', 'cflow', 'heatflow')) {
    $inPath = Join-Path $dir "$name.tex.in"
    $outPath = Join-Path $dir "$name.tex"
    $content = Get-Content -LiteralPath $inPath -Raw
    $content = $content.Replace('@CMAKE_CURRENT_SOURCE_DIR@', $dir)
    Set-Content -LiteralPath $outPath -Value $content -NoNewline
}
