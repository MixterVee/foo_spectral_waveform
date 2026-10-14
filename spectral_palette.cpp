#include "spectral_palette.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace spectral_waveform {

static float clamp01(float v) {
    return std::max(0.0f, std::min(1.0f, v));
}

rgb_color color_for_point(const waveform_point& p) {
    const float peak = p.peak / 65535.0f;

    std::array<float, 12> band{
        p.sub / 255.0f,
        p.deep_bass / 255.0f,
        p.bass / 255.0f,
        p.upper_bass / 255.0f,
        p.low_mid / 255.0f,
        p.mids / 255.0f,
        p.upper_mid / 255.0f,
        p.vocal_body / 255.0f,
        p.presence / 255.0f,
        p.attack / 255.0f,
        p.highs / 255.0f,
        p.air / 255.0f
    };

    // Test9: the dB-normalized analyzer in test8 finally gives us useful
    // twelve-band data. Do not collapse it back to only a few dominant bands;
    // blend all regions, but emphasize stronger bands enough to retain clear
    // spectral identity. This produces broader, more cohesive colour regions
    // instead of the neon-striped look of test8.
    static constexpr std::array<float, 12> kDisplayGain{
        0.94f, 0.96f, 0.98f, 1.00f,
        1.00f, 1.00f, 1.01f, 1.02f,
        1.04f, 1.06f, 1.09f, 1.12f
    };

    float total = 0.0001f;
    for (std::size_t i = 0; i < band.size(); ++i) {
        band[i] = std::pow(clamp01(band[i] * kDisplayGain[i]), 1.34f);
        total += band[i];
    }
    for (auto& v : band) v /= total;

    // Warm-low to cool-high anchors. Compared with test8, the high-frequency
    // anchors are slightly less extreme and all twelve bands participate in the
    // blend, giving Serato-like patches rather than one-column hue jumps.
    static constexpr std::array<std::array<float, 3>, 12> kColor{{
        {{1.00f, 0.05f, 0.02f}}, // sub: red
        {{1.00f, 0.15f, 0.02f}}, // deep bass
        {{1.00f, 0.30f, 0.02f}}, // bass
        {{1.00f, 0.50f, 0.02f}}, // upper bass
        {{1.00f, 0.76f, 0.04f}}, // low mid
        {{0.92f, 1.00f, 0.06f}}, // mids
        {{0.38f, 1.00f, 0.10f}}, // upper mids
        {{0.08f, 0.96f, 0.48f}}, // vocal body
        {{0.06f, 0.80f, 1.00f}}, // presence
        {{0.12f, 0.46f, 1.00f}}, // attack
        {{0.44f, 0.18f, 1.00f}}, // highs
        {{0.88f, 0.12f, 0.70f}}  // air: softened pink/magenta
    }};

    float r = 0.0f, g = 0.0f, b = 0.0f;
    for (std::size_t i = 0; i < band.size(); ++i) {
        r += band[i] * kColor[i][0];
        g += band[i] * kColor[i][1];
        b += band[i] * kColor[i][2];
    }

    // Moderate saturation: stronger than a plain RGB average, but much softer
    // than test8 so neighboring time slices visually belong together.
    const float common = std::min({r, g, b});
    constexpr float kCommonRemoval = 0.24f;
    r = std::max(0.0f, r - common * kCommonRemoval);
    g = std::max(0.0f, g - common * kCommonRemoval);
    b = std::max(0.0f, b - common * kCommonRemoval);

    const float maxc = std::max({r, g, b, 0.0001f});
    r /= maxc;
    g /= maxc;
    b /= maxc;

    const float brightness =
        0.25f + 0.75f * std::pow(clamp01(peak), 0.42f);

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
