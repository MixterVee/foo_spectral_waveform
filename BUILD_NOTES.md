# Build notes

Target environment:

- foobar2000 v2.x, x64
- Visual Studio 2022
- foobar2000 SDK 2025-03-07
- C++17 or newer

Add these files to a normal foobar2000 component project:

- foo_spectral_waveform.cpp
- spectral_analyzer.cpp
- spectral_analyzer.h
- spectral_palette.cpp
- spectral_palette.h
- waveform_data.h

The resulting component filename must be:

`foo_spectral_waveform.dll`

On first load, foobar2000's console should contain:

`foo_spectral_waveform: v0.1.0-alpha loaded`
