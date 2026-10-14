#pragma once
#include <cstdint>
#include <vector>

namespace spectral_waveform {

struct waveform_point {
    std::uint16_t peak = 0;
    std::uint16_t rms = 0;

    // Six-band spectral colour data. Existing names are retained for the
    // central three regions so the rest of the UI can evolve incrementally.
    // 20-90 Hz, 90-250 Hz, 250-700 Hz, 700-2000 Hz, 2-6 kHz, 6-20 kHz.
    std::uint8_t sub_bass = 0;
    std::uint8_t bass = 0;
    std::uint8_t low_mid = 0;
    std::uint8_t mids = 0;
    std::uint8_t high_mid = 0;
    std::uint8_t treble = 0;
};

// Lightweight high-resolution amplitude data used for the waveform shape.
// Spectral color remains in waveform_point at the lower FFT analysis rate.
struct waveform_envelope_point {
    std::uint16_t peak = 0;
    std::uint16_t rms = 0;
};

struct waveform_data {
    double duration_seconds = 0.0;
    std::uint32_t sample_rate = 0;
    std::uint32_t channels = 0;
    std::uint16_t rms_reference = 0;
    std::vector<waveform_point> points;
    std::vector<waveform_envelope_point> envelope;
};

} // namespace spectral_waveform
