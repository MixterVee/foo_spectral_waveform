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

    // Favor the strongest regions enough to keep colours vivid, while retaining
    // neighboring-band mixtures for smooth musical transitions.
    float total = 0.0001f;
    for (auto& v : band) {
        v = std::pow(clamp01(v), 1.20f);
        total += v;
    }
    for (auto& v : band) v /= total;

    // Serato-inspired frequency anchors from warm lows through cool highs and
    // violet/pink air. Adjacent bands interpolate naturally by RGB mixing.
    static constexpr std::array<std::array<float, 3>, 12> kColor{{
        {{1.00f, 0.04f, 0.02f}}, // sub: red
        {{1.00f, 0.12f, 0.02f}}, // deep bass: red-orange
        {{1.00f, 0.28f, 0.02f}}, // bass: orange
        {{1.00f, 0.48f, 0.02f}}, // upper bass: amber
        {{1.00f, 0.76f, 0.03f}}, // low mid: gold
        {{0.92f, 1.00f, 0.04f}}, // mids: yellow-green
        {{0.35f, 1.00f, 0.08f}}, // upper mids: green
        {{0.04f, 0.95f, 0.50f}}, // vocal body: green-teal
        {{0.03f, 0.82f, 1.00f}}, // presence: cyan
        {{0.08f, 0.42f, 1.00f}}, // attack: blue
        {{0.42f, 0.14f, 1.00f}}, // highs: violet
        {{1.00f, 0.10f, 0.64f}}  // air: pink/magenta
    }};

    float r = 0.0f, g = 0.0f, b = 0.0f;
    for (std::size_t i = 0; i < band.size(); ++i) {
        r += band[i] * kColor[i][0];
        g += band[i] * kColor[i][1];
        b += band[i] * kColor[i][2];
    }

    // Remove some common white component so broadband passages retain clear
    // spectral colour instead of washing toward pale yellow.
    const float common = std::min({r, g, b});
    constexpr float kCommonRemoval = 0.30f;
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
