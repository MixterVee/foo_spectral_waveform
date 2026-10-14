#pragma once
#include <cstdint>
#include <vector>

namespace spectral_waveform {

struct waveform_point {
    std::uint16_t peak = 0;
    std::uint16_t rms = 0;

    // Twelve spectral colour regions, low to high:
    // 20-45, 45-70, 70-110, 110-180, 180-300, 300-500,
    // 500-850, 850-1400, 1.4-2.5k, 2.5-4.5k, 4.5-8k, 8-20k Hz.
    std::uint8_t sub = 0;
    std::uint8_t deep_bass = 0;
    std::uint8_t bass = 0;
    std::uint8_t upper_bass = 0;
    std::uint8_t low_mid = 0;
    std::uint8_t mids = 0;
    std::uint8_t upper_mid = 0;
    std::uint8_t vocal_body = 0;
    std::uint8_t presence = 0;
    std::uint8_t attack = 0;
    std::uint8_t highs = 0;
    std::uint8_t air = 0;
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
