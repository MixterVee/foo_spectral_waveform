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

    // Six normalized spectral regions from low to high frequency.
    std::array<float, 6> band{
        p.sub_bass / 255.0f,
        p.bass / 255.0f,
        p.low_mid / 255.0f,
        p.mids / 255.0f,
        p.high_mid / 255.0f,
        p.treble / 255.0f
    };

    // Emphasize the strongest one or two regions so broadband music stays
    // vividly coloured instead of washing toward a common yellow/grey.
    float weight_total = 0.0001f;
    for (auto& v : band) {
        v = std::pow(clamp01(v), 1.24f);
        weight_total += v;
    }
    for (auto& v : band) v /= weight_total;

    // Serato-inspired frequency anchors:
    // sub -> red, bass -> orange, low-mid -> yellow,
    // mid -> green, presence -> cyan/blue, air -> violet/pink.
    static constexpr std::array<std::array<float, 3>, 6> kColor{{
        {{1.00f, 0.05f, 0.02f}},
        {{1.00f, 0.30f, 0.02f}},
        {{1.00f, 0.88f, 0.04f}},
        {{0.10f, 1.00f, 0.13f}},
        {{0.03f, 0.73f, 1.00f}},
        {{0.62f, 0.08f, 1.00f}}
    }};

    float r = 0.0f, g = 0.0f, b = 0.0f;
    for (std::size_t i = 0; i < band.size(); ++i) {
        r += band[i] * kColor[i][0];
        g += band[i] * kColor[i][1];
        b += band[i] * kColor[i][2];
    }

    // Remove part of the shared white component to keep adjacent frequency
    // mixtures saturated while still allowing smooth transitions.
    const float common = std::min({r, g, b});
    constexpr float kCommonRemoval = 0.34f;
    r = std::max(0.0f, r - common * kCommonRemoval);
    g = std::max(0.0f, g - common * kCommonRemoval);
    b = std::max(0.0f, b - common * kCommonRemoval);

    const float maxc = std::max({r, g, b, 0.0001f});
    r /= maxc;
    g /= maxc;
    b /= maxc;

    // Keep quieter details colourful while loudness still controls brightness.
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
