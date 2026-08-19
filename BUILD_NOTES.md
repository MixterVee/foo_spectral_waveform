# Build notes

The supported release build is:

`.github/workflows/build-native.yml`

Target environment:

- Windows x64
- foobar2000 v2 x64
- Visual Studio 2022
- current repository foobar2000 SDK dependency

The project builds the PFC and foobar2000 SDK dependencies, component client, and `foo_spectral_waveform`, then packages the result as a `.fb2k-component` artifact.

The source tree includes the full UI, live post-DSP capture, stem processing status integration, Stem Separator transport/menu service interfaces, stem waveform analysis, and persistent waveform cache; the old alpha build notes are no longer applicable.
