#include "spectral_palette.h"
#include <algorithm>
#include <cmath>

namespace spectral_waveform {

static float clamp01(float v) {
    return std::max(0.0f, std::min(1.0f, v));
}

rgb_color color_for_point(const waveform_point& p) {
    const float low_raw = p.bass / 255.0f;
    const float mid_raw = p.mids / 255.0f;
    const float high_raw = p.treble / 255.0f;
    const float peak = p.peak / 65535.0f;

    // V5: keep the stored bass/mid/treble analysis untouched and make the
    // draw-time palette more DJ-like. The previous matrix let bass spill too
    // heavily into green and mids spill too heavily into red, so normal
    // full-range music collapsed toward orange. Stronger band separation gives
    // the three normalized shares enough room to form secondary colours:
    // low+mid -> yellow, mid+high -> cyan, low+high -> magenta/pink.
    constexpr float kBandContrast = 1.42f;
    const float low = std::pow(clamp01(low_raw), kBandContrast);
    const float mid = std::pow(clamp01(mid_raw), kBandContrast);
    const float high = std::pow(clamp01(high_raw), 1.32f);

    // Frequency-to-colour mapping:
    //   bass            -> red / orange
    //   mids / vocals   -> yellow / green
    //   treble          -> cyan / blue
    // Mixed spectral content naturally produces the wider Serato-style range
    // of yellows, greens, cyans, blues, purples and pinks.
    float r = 1.06f * low + 0.18f * mid + 0.03f * high;
    float g = 0.12f * low + 1.02f * mid + 0.22f * high;
    float b = 0.03f * low + 0.10f * mid + 1.16f * high;

    // Pull out part of the shared white component. This increases colour
    // separation without changing the waveform geometry or cached analysis.
    const float common = std::min({r, g, b});
    constexpr float kCommonRemoval = 0.24f;
    r = std::max(0.0f, r - common * kCommonRemoval);
    g = std::max(0.0f, g - common * kCommonRemoval);
    b = std::max(0.0f, b - common * kCommonRemoval);

    const float maxc = std::max({r, g, b, 0.0001f});
    r /= maxc;
    g /= maxc;
    b /= maxc;

    // Amplitude still controls brightness, but raise the floor slightly so
    // quieter spectral detail keeps its colour instead of disappearing into
    // dark orange/brown tones.
    const float brightness =
        0.26f + 0.74f * std::pow(clamp01(peak), 0.42f);

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
