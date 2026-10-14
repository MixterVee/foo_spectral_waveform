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

    // Test12: keep the warm Serato-style foundation from test11, but let the
    // upper spectral regions assert themselves more clearly. Low-band display
    // gains are eased slightly while presence/attack/highs/air are lifted.
    // The analyzer and cached twelve-band data are intentionally unchanged.
    static constexpr std::array<float, 12> kDisplayGain{
        0.92f, 0.94f, 0.95f, 0.95f,
        0.96f, 0.91f, 0.94f, 0.99f,
        1.18f, 1.24f, 1.38f, 1.48f
    };

    float total = 0.0001f;
    for (std::size_t i = 0; i < band.size(); ++i) {
        band[i] = std::pow(clamp01(band[i] * kDisplayGain[i]), 1.34f);
        total += band[i];
    }
    for (auto& v : band) v /= total;

    // Warm-low to cool-high anchors. Test12 trims some yellow bias in the
    // low-mid/mid transition and gives the top four bands cleaner cyan, blue,
    // violet and pink identities. The intent is colored Serato-like streaks,
    // not a rainbow reset.
    static constexpr std::array<std::array<float, 3>, 12> kColor{{
        {{1.00f, 0.05f, 0.02f}}, // sub: red
        {{1.00f, 0.15f, 0.02f}}, // deep bass
        {{1.00f, 0.30f, 0.02f}}, // bass
        {{1.00f, 0.48f, 0.02f}}, // upper bass
        {{1.00f, 0.61f, 0.03f}}, // low mid: amber, less yellow
        {{0.98f, 0.87f, 0.05f}}, // mids: warm yellow-green
        {{0.52f, 1.00f, 0.10f}}, // upper mids: green
        {{0.10f, 0.93f, 0.55f}}, // vocal body: green-teal
        {{0.04f, 0.78f, 1.00f}}, // presence: cyan
        {{0.10f, 0.36f, 1.00f}}, // attack: blue
        {{0.62f, 0.10f, 1.00f}}, // highs: violet
        {{1.00f, 0.06f, 0.88f}}  // air: pink/magenta
    }};

    float r = 0.0f, g = 0.0f, b = 0.0f;
    for (std::size_t i = 0; i < band.size(); ++i) {
        r += band[i] * kColor[i][0];
        g += band[i] * kColor[i][1];
        b += band[i] * kColor[i][2];
    }

    // Small renderer-only split balance: preserve the warm body, reduce the
    // tendency toward yellow/orange averaging, and make meaningful upper-band
    // energy survive as cyan/violet/pink in the final color.
    r *= 1.03f;
    g *= 0.92f;
    b *= 1.10f;

    // Slightly more separation than test11, still deliberately cohesive so
    // adjacent slices read as musical regions rather than neon stripes.
    const float common = std::min({r, g, b});
    constexpr float kCommonRemoval = 0.26f;
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
