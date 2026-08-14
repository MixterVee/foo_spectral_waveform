#include "spectral_palette.h"
#include <algorithm>
#include <cmath>

namespace spectral_waveform {

static float clamp01(float v) {
    return std::max(0.0f, std::min(1.0f, v));
}

rgb_color color_for_point(const waveform_point& p) {
    const float low = p.bass / 255.0f;
    const float mid = p.mids / 255.0f;
    const float high = p.treble / 255.0f;
    const float peak = p.peak / 65535.0f;

    float r = low + 0.35f * mid;
    float g = 0.85f * mid + 0.20f * high;
    float b = high + 0.10f * mid;

    const float maxc = std::max({r, g, b, 0.0001f});
    r /= maxc;
    g /= maxc;
    b /= maxc;

    const float brightness = 0.25f + 0.75f * std::sqrt(clamp01(peak));
    r = clamp01(r * brightness);
    g = clamp01(g * brightness);
    b = clamp01(b * brightness);

    return {
        static_cast<std::uint8_t>(std::lround(r * 255.0f)),
        static_cast<std::uint8_t>(std::lround(g * 255.0f)),
        static_cast<std::uint8_t>(std::lround(b * 255.0f))
    };
}

} // namespace spectral_waveform
