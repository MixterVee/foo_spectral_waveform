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

    // The analyzer already stores normalized twelve-band shares. For display,
    // compensate the natural visual dominance of low bands and then keep only
    // the strongest few spectral regions for each time slice. Blending all
    // twelve bands together was washing the extra detail back into red/orange.
    static constexpr std::array<float, 12> kDisplayGain{
        0.72f, 0.78f, 0.84f, 0.90f,
        0.98f, 1.05f, 1.15f, 1.28f,
        1.42f, 1.62f, 1.88f, 2.15f
    };

    std::array<float, 12> score{};
    for (std::size_t i = 0; i < score.size(); ++i) {
        score[i] = std::pow(clamp01(band[i] * kDisplayGain[i]), 1.55f);
    }

    std::array<std::size_t, 12> order{};
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(),
        [&score](std::size_t a, std::size_t b) { return score[a] > score[b]; });

    // Frequency anchors: warm lows, green/cyan mids, blue/violet highs and
    // magenta air. The top-three blend gives each column a distinct spectral
    // identity instead of an all-band average.
    static constexpr std::array<std::array<float, 3>, 12> kColor{{
        {{1.00f, 0.03f, 0.01f}}, // sub
        {{1.00f, 0.10f, 0.01f}}, // deep bass
        {{1.00f, 0.24f, 0.01f}}, // bass
        {{1.00f, 0.43f, 0.01f}}, // upper bass
        {{1.00f, 0.70f, 0.02f}}, // low mid
        {{0.90f, 1.00f, 0.03f}}, // mids
        {{0.28f, 1.00f, 0.06f}}, // upper mid
        {{0.02f, 0.98f, 0.42f}}, // vocal body
        {{0.02f, 0.82f, 1.00f}}, // presence
        {{0.06f, 0.36f, 1.00f}}, // attack
        {{0.42f, 0.10f, 1.00f}}, // highs
        {{1.00f, 0.05f, 0.68f}}  // air
    }};

    const float top = std::max(score[order[0]], 0.0001f);
    std::array<float, 3> mixWeight{
        1.00f,
        0.58f * (score[order[1]] / top),
        0.34f * (score[order[2]] / top)
    };

    // If a presence/high/air band is meaningfully active but narrowly misses
    // the top three, let it tint the slice rather than being swallowed by a
    // stack of adjacent bass bands.
    std::size_t coolIndex = 8;
    for (std::size_t i = 9; i < score.size(); ++i) {
        if (score[i] > score[coolIndex]) coolIndex = i;
    }
    const float coolRatio = score[coolIndex] / top;
    const bool addCoolAccent =
        coolRatio >= 0.32f &&
        coolIndex != order[0] && coolIndex != order[1] && coolIndex != order[2];

    float weightSum = mixWeight[0] + mixWeight[1] + mixWeight[2];
    float r = 0.0f, g = 0.0f, b = 0.0f;

    for (std::size_t rank = 0; rank < 3; ++rank) {
        const auto idx = order[rank];
        const float w = mixWeight[rank];
        r += w * kColor[idx][0];
        g += w * kColor[idx][1];
        b += w * kColor[idx][2];
    }

    if (addCoolAccent) {
        const float w = 0.30f * coolRatio;
        r += w * kColor[coolIndex][0];
        g += w * kColor[coolIndex][1];
        b += w * kColor[coolIndex][2];
        weightSum += w;
    }

    r /= std::max(weightSum, 0.0001f);
    g /= std::max(weightSum, 0.0001f);
    b /= std::max(weightSum, 0.0001f);

    // Increase saturation after the selective blend.
    const float common = std::min({r, g, b});
    constexpr float kCommonRemoval = 0.48f;
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
