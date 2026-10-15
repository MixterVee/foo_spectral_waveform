# foo_spectral_waveform

Native foobar2000 v2 spectral waveform component for Windows, with universal Win32/x64 packaging.

## Current stable: 0.6.1

### Waveform display

- Serato-inspired 12-band frequency coloring
- zoom and fit-entire-track views
- centered and paged follow modes
- playback cursor and optional time markers
- persistent waveform analysis cache
- re-analyze current track command
- automatic waveform keyboard focus when a new track starts

### Keyboard / transport controls

- Enter routes to foobar2000's native Play or pause command
- Space toggles Follow while zoomed
- Left / Right moves the playhead while paused or stopped
- Ctrl + Left / Right provides finer zoom-scaled playhead movement
- Up / Down zoom controls
- stopped position, zoom, pan and follow state are preserved for the same track
- pause/resume keeps the visible cursor solid without transport correction

### Stem awareness

When Stem Separator is installed, the waveform follows the processed stem result so Original, Vocals, Instrumental and Blend views reflect the selected mode.

The waveform right-click menu mirrors Stem Separator controls, including exports, pre-cache, persistent-cache settings and backend selection.

### Drag and hold behavior

Audible scratching is intentionally muted/held while the waveform is grabbed. The final seek is committed on release.

## Requirements

- Windows 10/11
- foobar2000 v2, 32-bit or 64-bit
- Visual Studio 2022 for source builds
- foobar2000 SDK compatible with the repository workflow

## Build

The supported build is `.github/workflows/build-native.yml`. It builds both Win32 and x64 and packages one universal `.fb2k-component`.
