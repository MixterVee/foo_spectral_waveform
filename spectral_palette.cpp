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

    // Test13: preserve Test12's upper-band visibility, but restore more of the
    // orange/yellow/green midrange seen in the Serato reference. This is still
    // renderer-only; the twelve-band analyzer and cached data remain unchanged.
    static constexpr std::array<float, 12> kDisplayGain{
        0.90f, 0.93f, 0.96f, 0.98f,
        1.00f, 0.97f, 0.98f, 1.01f,
        1.18f, 1.24f, 1.38f, 1.48f
    };

    float total = 0.0001f;
    for (std::size_t i = 0; i < band.size(); ++i) {
        band[i] = std::pow(clamp01(band[i] * kDisplayGain[i]), 1.34f);
        total += band[i];
    }
    for (auto& v : band) v /= total;

    // Warmer low anchors and slightly fuller mid anchors move some of Test12's
    // excess deep red back toward orange/yellow/green. The top four anchors are
    // intentionally kept strong so cyan/violet/pink detail is not lost.
    static constexpr std::array<std::array<float, 3>, 12> kColor{{
        {{1.00f, 0.08f, 0.02f}}, // sub: red
        {{1.00f, 0.22f, 0.02f}}, // deep bass: red-orange
        {{1.00f, 0.40f, 0.02f}}, // bass: orange-red
        {{1.00f, 0.58f, 0.03f}}, // upper bass: orange
        {{1.00f, 0.72f, 0.04f}}, // low mid: amber
        {{0.98f, 0.94f, 0.06f}}, // mids: yellow
        {{0.60f, 1.00f, 0.10f}}, // upper mids: yellow-green
        {{0.12f, 0.94f, 0.58f}}, // vocal body: green-teal
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

    // Back off the red-heavy Test12 balance and allow a little more green back
    // into the blend while keeping the stronger blue/violet/pink contribution.
    r *= 1.00f;
    g *= 0.97f;
    b *= 1.09f;

    // Slightly softer separation than Test12 gives mixed sections a more
    // Serato-like pastel/cohesive look without washing out spectral identity.
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
