#pragma once
#include <cstdint>
#include <vector>

namespace spectral_waveform {

struct waveform_point {
    std::uint16_t peak = 0;
    std::uint16_t rms = 0;
    std::uint8_t bass = 0;
    std::uint8_t mids = 0;
    std::uint8_t treble = 0;
};

struct waveform_data {
    double duration_seconds = 0.0;
    std::uint32_t sample_rate = 0;
    std::uint32_t channels = 0;
    std::uint16_t rms_reference = 0;
    std::vector<waveform_point> points;
};

} // namespace spectral_waveform
