#include "spectral_palette.h"
#include <algorithm>
#include <cmath>

namespace spectral_waveform {

static float clamp01(float v) {
    return std::max(0.0f, std::min(1.0f, v));
}

static float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

static float hue_for_position(float p) {
    struct stop { float p; float h; };
    static constexpr stop stops[] = {
        {0.00f,   4.0f},  // deep bass: red
        {0.18f,  28.0f},  // bass / low-mid: orange
        {0.36f,  56.0f},  // low-mid: yellow
        {0.52f, 112.0f},  // mids / vocals: green
        {0.68f, 178.0f},  // upper mids: cyan
        {0.82f, 224.0f},  // highs: blue
        {1.00f, 296.0f},  // extreme highs / air: purple-pink
    };

    p = clamp01(p);
    for (std::size_t i = 1; i < std::size(stops); ++i) {
        if (p <= stops[i].p) {
            const float span = stops[i].p - stops[i - 1].p;
            const float t = span > 0.0f ? (p - stops[i - 1].p) / span : 0.0f;
            return lerp(stops[i - 1].h, stops[i].h, t);
        }
    }
    return stops[std::size(stops) - 1].h;
}

static rgb_color hsv_to_rgb(float hue_deg, float saturation, float value) {
    const float h = std::fmod(std::max(0.0f, hue_deg), 360.0f) / 60.0f;
    const float s = clamp01(saturation);
    const float v = clamp01(value);

    const int sector = static_cast<int>(std::floor(h)) % 6;
    const float f = h - std::floor(h);
    const float p = v * (1.0f - s);
    const float q = v * (1.0f - s * f);
    const float t = v * (1.0f - s * (1.0f - f));

    float r = 0.0f, g = 0.0f, b = 0.0f;
    switch (sector) {
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    default: r = v; g = p; b = q; break;
    }

    return {
        static_cast<std::uint8_t>(std::lround(clamp01(r) * 255.0f)),
        static_cast<std::uint8_t>(std::lround(clamp01(g) * 255.0f)),
        static_cast<std::uint8_t>(std::lround(clamp01(b) * 255.0f))
    };
}

rgb_color color_for_point(const waveform_point& p) {
    const float low_raw = p.bass / 255.0f;
    const float mid_raw = p.mids / 255.0f;
    const float high_raw = p.treble / 255.0f;
    const float peak = p.peak / 65535.0f;

    // V6: use the stored three-band spectrum as a continuous spectral-position
    // signal instead of a direct RGB mix. This avoids replacing one dominant
    // colour (orange) with another (red), and creates a real DJ-style colour
    // sweep across the spectrum.
    //
    // The analyzer's low band is naturally strong in mastered music while the
    // high band occupies less total energy. Compensate perceptually before
    // calculating the colour position so mids and transients remain visible.
    const float low = 0.72f * std::pow(clamp01(low_raw), 1.04f);
    const float mid = 1.18f * std::pow(clamp01(mid_raw), 0.90f);
    const float high = 2.05f * std::pow(clamp01(high_raw), 0.74f);

    const float total = std::max(0.0001f, low + mid + high);
    const float low_share = low / total;
    const float mid_share = mid / total;
    const float high_share = high / total;

    // Weighted spectral centroid in our three-band space.
    const float spectral_position =
        clamp01(0.50f * mid_share + high_share);

    const float hue = hue_for_position(spectral_position);

    // Keep colours vivid, but let broadband passages soften slightly instead
    // of forcing every point to maximum saturation.
    const float dominance = std::max({low_share, mid_share, high_share});
    const float saturation = 0.76f + 0.22f * clamp01((dominance - 0.34f) / 0.66f);

    // Loudness still changes brightness only; waveform geometry is untouched.
    const float brightness =
        0.27f + 0.73f * std::pow(clamp01(peak), 0.42f);

    return hsv_to_rgb(hue, saturation, brightness);
}

} // namespace spectral_waveform
