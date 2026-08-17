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
    // V3 deliberately uses a gentler contrast than V2. The analyzer now carries
    // real relative band dominance, so the palette no longer needs to force the
    // winning band nearly to a primary color.
    constexpr float kBandContrast = 1.30f;
    const float low = std::pow(clamp01(low_raw), kBandContrast);
    const float mid = std::pow(clamp01(mid_raw), kBandContrast);
    const float high = std::pow(clamp01(high_raw), kBandContrast);

    // Bass -> red/orange, mids/vocals -> yellow/green, treble/transients ->
    // cyan/blue. Stronger cross-band contributions make transitions smoother
    // and keep mixed musical material from collapsing into solid red.
    float r = 1.00f * low + 0.45f * mid + 0.04f * high;
    float g = 0.28f * low + 1.00f * mid + 0.35f * high;
    float b = 0.03f * low + 0.22f * mid + 1.00f * high;

    // Only a light common-floor reduction is needed with normalized spectral
    // shares. This retains saturation where one band is genuinely dominant but
    // allows broadband passages to blend into richer intermediate colors.
    const float common = std::min({r, g, b});
    constexpr float kCommonRemoval = 0.08f;
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
