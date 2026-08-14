# foo_spectral_waveform

Experimental foobar2000 v2 spectral waveform component.

## v0.1.0-alpha scope

This first milestone intentionally contains only:

- a valid foobar2000 component entry point
- a pure C++ PCM analysis engine
- cached-point data structures
- bass / mids / treble spectral extraction
- Serato-inspired RGB color mapping
- a foobar console message confirming that the component loaded

It does **not** yet expose a Default UI panel. That is the next milestone after
we verify this build loads cleanly in foobar2000.

## Spectral model

Initial bands:

- Bass: 20–250 Hz
- Mids: 250 Hz–4 kHz
- Treble: 4–20 kHz

Each analysis point stores:

```cpp
uint16_t peak;
uint8_t bass;
uint8_t mids;
uint8_t treble;
```

This keeps the cache compact while leaving enough information to draw a
frequency-colored waveform.

## Planned next milestone

v0.2:

1. Decode the current playing track in a worker thread.
2. Feed PCM into `spectral_analyzer`.
3. Add a Default UI element.
4. Draw the waveform with bass=warm/red, mids=green/yellow,
   treble=blue/cyan.
5. Draw playback progress and support click-to-seek.
