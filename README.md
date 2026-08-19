# foo_spectral_waveform

Native foobar2000 v2 x64 spectral waveform component with stem-aware visualization and controls.

## Current stable: 0.4.0

### Waveform display

- Serato-inspired frequency coloring
- bass / mids / treble spectral analysis
- zoom and fit-entire-track views
- centered and page follow modes
- playback cursor and optional time markers
- persistent waveform analysis cache
- re-analyze current track command

### Stem awareness

When Stem Separator is installed, the waveform follows the post-DSP result so Original, Vocals, and Instrumental views reflect what is being heard.

The waveform right-click menu mirrors Stem Separator commands rather than maintaining a second copy of stem state. This includes:

- Original / Vocals / Instrumental
- WAV and MP3 stem export
- pre-cache at track start
- Cache Settings
  - Persistent Cache On/Off
  - Current Cache size
  - Clear Stem Cache
  - 2 / 5 / 10 / 20 / 50 / 100 GB maximum-size presets
- processing backend benchmark / selection

### Drag and hold behavior

Audible scratching was intentionally removed. While the waveform is grabbed or dragged, transport audio is muted/held; the final seek is committed on release. This is the stable intended behavior and avoids the looping/stuck-audio problems from the earlier scratch experiments.

## Requirements

- Windows 10/11 x64
- foobar2000 v2 x64
- Visual Studio 2022 for source builds
- foobar2000 SDK compatible with the repository workflow

## Build

The supported build is `.github/workflows/build-native.yml`, which builds the foobar2000 SDK dependencies, the component, and packages `foo_spectral_waveform.fb2k-component`.
