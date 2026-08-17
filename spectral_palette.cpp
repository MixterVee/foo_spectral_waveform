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

    // The analyzer deliberately log-compresses each band so quiet spectral
    // detail survives in the cache. That is useful for analysis, but a direct
    // linear RGB mix makes most full-range music converge toward the same
    // pastel color. Re-expand the differences here so the dominant frequency
    // region is immediately obvious, like a DJ waveform.
    constexpr float kBandContrast = 1.75f;
    const float low = std::pow(clamp01(low_raw), kBandContrast);
    const float mid = std::pow(clamp01(mid_raw), kBandContrast);
    const float high = std::pow(clamp01(high_raw), kBandContrast);

    // Bass -> warm red/orange, mids/vocals -> green with a little warmth,
    // treble/transients -> blue/cyan. Small cross-band contributions keep
    // mixed material naturally yellow/cyan/magenta instead of hard primaries.
    float r = 1.10f * low + 0.30f * mid + 0.02f * high;
    float g = 0.10f * low + 1.00f * mid + 0.24f * high;
    float b = 0.01f * low + 0.15f * mid + 1.16f * high;

    // Remove part of the common RGB floor. This raises saturation only when
    // all three channels are present, preserving broadband white-ish peaks
    // while making genuinely dominant bands much easier to distinguish.
    const float common = std::min({r, g, b});
    constexpr float kCommonRemoval = 0.32f;
    r = std::max(0.0f, r - common * kCommonRemoval);
    g = std::max(0.0f, g - common * kCommonRemoval);
    b = std::max(0.0f, b - common * kCommonRemoval);

    const float maxc = std::max({r, g, b, 0.0001f});
    r /= maxc;
    g /= maxc;
    b /= maxc;

    // Keep amplitude encoded in brightness, but give quieter details a little
    // more visibility than before. Shape/height is unchanged; this affects
    // only the color drawn inside the existing waveform geometry.
    const float brightness =
        0.20f + 0.80f * std::pow(clamp01(peak), 0.45f);

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
