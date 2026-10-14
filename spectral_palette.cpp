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
        1.00f, 1.02f, 1.04f, 1.03f,
        0.98f, 0.94f, 0.94f, 0.97f,
        1.03f, 1.08f, 1.12f, 1.14f
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
        {{1.00f, 0.48f, 0.02f}}, // upper bass
        {{1.00f, 0.68f, 0.03f}}, // low mid: warmer amber
        {{1.00f, 0.92f, 0.04f}}, // mids: yellow, less green
        {{0.58f, 1.00f, 0.08f}}, // upper mids: yellow-green
        {{0.12f, 0.92f, 0.50f}}, // vocal body: green-teal
        {{0.05f, 0.76f, 1.00f}}, // presence: cyan
        {{0.10f, 0.40f, 1.00f}}, // attack: blue
        {{0.46f, 0.16f, 1.00f}}, // highs: violet
        {{0.90f, 0.10f, 0.72f}}  // air: pink/magenta
    }};

    float r = 0.0f, g = 0.0f, b = 0.0f;
    for (std::size_t i = 0; i < band.size(); ++i) {
        r += band[i] * kColor[i][0];
        g += band[i] * kColor[i][1];
        b += band[i] * kColor[i][2];
    }

    // Test9 still leaned too yellow/green. Apply a small split balance rather
    // than increasing saturation: a little more warm red and cool blue, with a
    // slight green reduction, keeps the image cohesive while restoring the
    // Serato-like orange and blue/purple presence.
    r *= 1.05f;
    g *= 0.94f;
    b *= 1.04f;

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
