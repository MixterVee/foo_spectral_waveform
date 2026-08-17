$ErrorActionPreference = 'Stop'

$path = Join-Path $PSScriptRoot '..\spectral_ui.cpp'
$source = [System.IO.File]::ReadAllText($path)
$source = $source -replace "`r`n", "`n"

# Scratch audition is no longer part of the mouse-grab design. Remove the
# diagnostic renderer and its only call site without touching HOLD, Reverse,
# stem switching, waveform drawing, or release behavior.
$functionPattern = '(?s)\n    void draw_transport_debug_overlay\(HDC dc, int width, int height\) const \{.*?\n    \}\n\n    static COLORREF blend_color'
$functionMatches = [regex]::Matches($source, $functionPattern)
if ($functionMatches.Count -ne 1) {
    throw "Scratch-debug cleanup: expected one overlay function, found $($functionMatches.Count)."
}
$source = [regex]::Replace(
    $source,
    $functionPattern,
    "`n    static COLORREF blend_color",
    1)

$callPattern = '            draw_view_overlay\(dc, width, height\);\n            draw_transport_debug_overlay\(dc, width, height\);'
$callMatches = [regex]::Matches($source, $callPattern)
if ($callMatches.Count -ne 1) {
    throw "Scratch-debug cleanup: expected one overlay call site, found $($callMatches.Count)."
}
$source = [regex]::Replace(
    $source,
    $callPattern,
    '            draw_view_overlay(dc, width, height);',
    1)

[System.IO.File]::WriteAllText(
    $path,
    $source,
    [System.Text.UTF8Encoding]::new($false))

Write-Host 'Removed scratch diagnostic overlay.'
