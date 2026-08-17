$ErrorActionPreference = 'Stop'

$path = Join-Path $PSScriptRoot '..\spectral_ui.cpp'
$source = [System.IO.File]::ReadAllText($path)
$source = $source -replace "`r`n", "`n"

# Centered mouse grabbing used to feed every mouse position into Stem Separator's
# SCRUB transport. That path is intentionally disabled: while the waveform is
# physically grabbed, the transport remains in HOLD so audio stays silent. The
# UI still follows the hand and release commits the final target normally.
$pattern = '(?s)            if \(set_transport_scrub\(m_scrubTargetPosition\)\) \{.*?            invalidate_all\(\);\n            return;'
$matches = [regex]::Matches($source, $pattern)
if ($matches.Count -ne 1) {
    throw "Mute-grab patch: expected exactly one centered SCRUB block, found $($matches.Count)."
}

$replacement = @'
            // Scratch audition is intentionally disabled. Keep Stem Separator in
            // the HOLD state armed at grab-start so the output remains silent for
            // the entire mouse gesture. The waveform itself still tracks the hand;
            // end_drag() commits m_scrubTargetPosition once on release.
            m_scrubAudibleActive = false;
            invalidate_all();
            return;
'@

$source = [regex]::Replace($source, $pattern, $replacement, 1)

[System.IO.File]::WriteAllText(
    $path,
    $source,
    [System.Text.UTF8Encoding]::new($false))

Write-Host 'Applied mute-on-grab waveform patch.'
