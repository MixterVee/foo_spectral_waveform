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

    // V7: preserve the full three-way spectral mixture instead of collapsing
    // it to a one-dimensional hue. This lets low+high energy become magenta /
    // purple, mid+high become cyan / blue, and low+mid become orange / yellow.
    //
    // Mild perceptual equalisation prevents mastered music from spending most
    // of its time in the low+mid corner while still respecting the analyzer's
    // stored relative-band shares.
    float low = 0.88f * std::pow(clamp01(low_raw), 1.08f);
    float mid = 1.00f * std::pow(clamp01(mid_raw), 1.00f);
    float high = 1.62f * std::pow(clamp01(high_raw), 0.82f);

    const float total = std::max(0.0001f, low + mid + high);
    low /= total;
    mid /= total;
    high /= total;

    // Serato-inspired spectral primaries. These are deliberately not pure RGB:
    // bass carries a little amber, mids lean yellow-green, and highs lean
    // blue-violet. Their mixtures therefore span orange, yellow, green, cyan,
    // blue, purple, magenta and pink without forcing a single dominant hue.
    constexpr float lowR = 1.00f, lowG = 0.16f, lowB = 0.02f;
    constexpr float midR = 0.42f, midG = 1.00f, midB = 0.08f;
    constexpr float highR = 0.46f, highG = 0.08f, highB = 1.00f;

    float r = low * lowR + mid * midR + high * highR;
    float g = low * lowG + mid * midG + high * highG;
    float b = low * lowB + mid * midB + high * highB;

    // Pull out some shared white component so broadband passages retain a
    // readable colour rather than washing toward yellow-green.
    const float common = std::min({r, g, b});
    constexpr float kCommonRemoval = 0.22f;
    r = std::max(0.0f, r - common * kCommonRemoval);
    g = std::max(0.0f, g - common * kCommonRemoval);
    b = std::max(0.0f, b - common * kCommonRemoval);

    // Give low+high mixtures a small magenta lift. This preserves the
    // distinction that the one-dimensional centroid in test2 could not show.
    const float lowHigh = 2.0f * std::min(low, high);
    r += 0.22f * lowHigh;
    b += 0.26f * lowHigh;

    // Upper-band dominance should visibly reach violet/pink rather than stop at
    // cyan. Keep the lift subtle so cymbals/transients do not become uniformly
    // purple.
    const float highLead = std::max(0.0f, high - mid);
    r += 0.12f * highLead;
    b += 0.10f * highLead;

    const float maxc = std::max({r, g, b, 0.0001f});
    r /= maxc;
    g /= maxc;
    b /= maxc;

    // Loudness affects brightness only; waveform geometry and cached analysis
    // remain unchanged.
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
