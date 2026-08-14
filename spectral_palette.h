#pragma once
#include "waveform_data.h"
#include <cstdint>

namespace spectral_waveform {

struct rgb_color {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
};

rgb_color color_for_point(const waveform_point& p);

} // namespace spectral_waveform
