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
    //
    // V4 keeps the useful V2/V3 frequency separation but backs off the warm
    // dominance another small step. The goal is a natural DJ-style gradient,
    // not large blocks of a nearly primary color.
    constexpr float kBandContrast = 1.22f;
    const float low = std::pow(clamp01(low_raw), kBandContrast);
    const float mid = std::pow(clamp01(mid_raw), kBandContrast);
    const float high = std::pow(clamp01(high_raw), kBandContrast);

    // Bass -> red/orange, mids/vocals -> yellow/green, treble/transients ->
    // cyan/blue. Compared with V3, bass contributes less pure red and more
    // green, while upper-mid/high energy receives slightly more cyan/blue.
    float r = 0.92f * low + 0.43f * mid + 0.04f * high;
    float g = 0.34f * low + 1.00f * mid + 0.40f * high;
    float b = 0.04f * low + 0.27f * mid + 1.06f * high;

    // Keep almost all of the common component. The normalized V2 analyzer
    // already supplies strong band contrast, so only a tiny saturation lift is
    // needed to stop broadband material from becoming washed out.
    const float common = std::min({r, g, b});
    constexpr float kCommonRemoval = 0.04f;
    r = std::max(0.0f, r - common * kCommonRemoval);
    g = std::max(0.0f, g - common * kCommonRemoval);
    b = std::max(0.0f, b - common * kCommonRemoval);

    const float maxc = std::max({r, g, b, 0.0001f});
    r /= maxc;
    g /= maxc;
    b /= maxc;

    // Keep amplitude encoded in brightness. Shape/height is unchanged; this
    // affects only the color drawn inside the existing waveform geometry.
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
