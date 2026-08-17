#include "spectral_analyzer.h"
#include <algorithm>
#include <cmath>

namespace spectral_waveform {

static constexpr double kPi = 3.1415926535897932384626433832795;

spectral_analyzer::spectral_analyzer(std::uint32_t sample_rate, std::uint32_t channels)
    : m_sample_rate(sample_rate), m_channels(channels) {
    const std::size_t desired = std::max<std::size_t>(512, sample_rate / 22);
    m_window_size = next_power_of_two(desired);
    m_window_size = std::min<std::size_t>(m_window_size, 4096);
    m_hop_size = m_window_size / 2;
    m_mono_buffer.reserve(m_window_size * 2);

    // About 5 ms per envelope point. This is intentionally independent of the
    // FFT hop so zoomed waveform detail can be much finer without extra FFTs.
    m_envelope_frames = std::max<std::size_t>(64, sample_rate / 200);
    m_envelope.reserve(4096);
}

std::size_t spectral_analyzer::next_power_of_two(std::size_t v) {
    std::size_t p = 1;
    while (p < v) p <<= 1;
    return p;
}

void spectral_analyzer::feed_envelope_sample(float mono) {
    m_envelope_peak = std::max(m_envelope_peak, std::abs(mono));
    m_envelope_sum_squares += static_cast<double>(mono) * static_cast<double>(mono);
    ++m_envelope_count;
    if (m_envelope_count >= m_envelope_frames) flush_envelope();
}

void spectral_analyzer::flush_envelope() {
    if (m_envelope_count == 0) return;

    const double rms = std::sqrt(m_envelope_sum_squares / static_cast<double>(m_envelope_count));
    waveform_envelope_point p;
    p.peak = static_cast<std::uint16_t>(std::lround(
        std::min(1.0f, m_envelope_peak) * 65535.0f));
    p.rms = static_cast<std::uint16_t>(std::lround(
        std::min(1.0, rms) * 65535.0));
    m_envelope.push_back(p);

    m_envelope_count = 0;
    m_envelope_peak = 0.0f;
    m_envelope_sum_squares = 0.0;
}

void spectral_analyzer::feed(const float* interleaved, std::size_t frame_count) {
    if (!interleaved || frame_count == 0 || m_channels == 0) return;
    m_total_frames += frame_count;

    for (std::size_t f = 0; f < frame_count; ++f) {
        double mono = 0.0;
        const float* frame = interleaved + f * m_channels;
        for (std::uint32_t c = 0; c < m_channels; ++c) mono += frame[c];
        mono /= static_cast<double>(m_channels);
        const float monoSample = static_cast<float>(mono);

        feed_envelope_sample(monoSample);
        m_mono_buffer.push_back(monoSample);

        while (m_mono_buffer.size() >= m_window_size) {
            analyze_window(m_mono_buffer.data(), m_window_size);
            m_mono_buffer.erase(m_mono_buffer.begin(),
                m_mono_buffer.begin() + static_cast<std::ptrdiff_t>(m_hop_size));
        }
    }
}

std::uint8_t spectral_analyzer::compress_energy(double x) {
    x = std::max(0.0, x);
    const double y = std::log1p(x * 40.0) / std::log1p(40.0);
    const double clamped = std::max(0.0, std::min(1.0, y));
    return static_cast<std::uint8_t>(std::lround(clamped * 255.0));
}

void spectral_analyzer::analyze_window(const float* mono, std::size_t count) {
    std::vector<float> real(m_window_size, 0.0f);
    std::vector<float> imag(m_window_size, 0.0f);
    float peak = 0.0f;
    double sumSquares = 0.0;

    for (std::size_t i = 0; i < count; ++i) {
        const float s = mono[i];
        peak = std::max(peak, std::abs(s));
        sumSquares += static_cast<double>(s) * static_cast<double>(s);

        const double w = 0.5 - 0.5 * std::cos((2.0 * kPi * static_cast<double>(i)) /
            static_cast<double>(std::max<std::size_t>(1, count - 1)));
        real[i] = static_cast<float>(s * w);
    }

    fft(real, imag);

    double low = 0.0, mid = 0.0, high = 0.0;
    for (std::size_t bin = 1; bin < m_window_size / 2; ++bin) {
        const double freq = (static_cast<double>(bin) * m_sample_rate) /
            static_cast<double>(m_window_size);
        const double re = real[bin];
        const double im = imag[bin];
        const double power = re * re + im * im;

        if (freq < 20.0) continue;
        if (freq < 250.0) low += power;
        else if (freq < 4000.0) mid += power;
        else if (freq <= 20000.0) high += power;
    }

    const double total = low + mid + high + 1.0e-20;

    // Preserve RELATIVE spectral dominance instead of independently boosting
    // every active band toward 255. The old *3 + clamp + log compression made
    // ordinary full-range music look as if bass, mids and treble were all nearly
    // maximal at once, leaving the palette too little information to distinguish
    // kicks, vocals and cymbals.
    //
    // Mild perceptual compensation keeps the broad mid band from owning almost
    // every frame and gives short high-frequency transients enough weight to be
    // visible. After compensation the three stored values are normalized shares;
    // together they describe one spectral mixture rather than three loudnesses.
    const double low_share = low / total;
    const double mid_share = mid / total;
    const double high_share = high / total;

    const double low_weight = 1.15 * std::pow(low_share, 0.85);
    const double mid_weight = 0.92 * std::pow(mid_share, 0.90);
    const double high_weight = 1.30 * std::pow(high_share, 0.78);
    const double weight_total = low_weight + mid_weight + high_weight + 1.0e-20;

    const auto encode_share = [weight_total](double value) -> std::uint8_t {
        const double normalized = std::clamp(value / weight_total, 0.0, 1.0);
        return static_cast<std::uint8_t>(std::lround(normalized * 255.0));
    };

    const double rms = count > 0 ? std::sqrt(sumSquares / static_cast<double>(count)) : 0.0;

    waveform_point p;
    p.peak = static_cast<std::uint16_t>(std::lround(std::min(1.0f, peak) * 65535.0f));
    p.rms = static_cast<std::uint16_t>(std::lround(std::min(1.0, rms) * 65535.0));
    p.bass = encode_share(low_weight);
    p.mids = encode_share(mid_weight);
    p.treble = encode_share(high_weight);
    m_points.push_back(p);
}

waveform_data spectral_analyzer::finish() {
    flush_envelope();

    if (!m_mono_buffer.empty()) {
        std::vector<float> final_window(m_window_size, 0.0f);
        std::copy(m_mono_buffer.begin(), m_mono_buffer.end(), final_window.begin());
        analyze_window(final_window.data(), m_window_size);
        m_mono_buffer.clear();
    }

    waveform_data out;
    out.sample_rate = m_sample_rate;
    out.channels = m_channels;
    out.duration_seconds = m_sample_rate ? static_cast<double>(m_total_frames) / m_sample_rate : 0.0;

    // Expand the drawable timeline to the fine envelope rate, carrying the nearest
    // FFT color into each point. This gives the UI roughly 5 ms shape resolution
    // without increasing the number of FFT calculations.
    if (!m_envelope.empty() && !m_points.empty()) {
        out.points.reserve(m_envelope.size());
        const std::size_t spectralCount = m_points.size();
        const std::size_t envelopeCount = m_envelope.size();
        for (std::size_t i = 0; i < envelopeCount; ++i) {
            const std::size_t spectralIndex = std::min(
                spectralCount - 1,
                (i * spectralCount) / envelopeCount);
            waveform_point p = m_points[spectralIndex];
            p.peak = m_envelope[i].peak;
            p.rms = m_envelope[i].rms;
            out.points.push_back(p);
        }
    } else {
        out.points = std::move(m_points);
    }

    out.envelope = std::move(m_envelope);
    return out;
}

void spectral_analyzer::fft(std::vector<float>& real, std::vector<float>& imag) const {
    const std::size_t n = real.size();
    std::size_t j = 0;
    for (std::size_t i = 1; i < n; ++i) {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            std::swap(real[i], real[j]);
            std::swap(imag[i], imag[j]);
        }
    }

    for (std::size_t len = 2; len <= n; len <<= 1) {
        const double angle = -2.0 * kPi / static_cast<double>(len);
        const float wlen_r = static_cast<float>(std::cos(angle));
        const float wlen_i = static_cast<float>(std::sin(angle));

        for (std::size_t i = 0; i < n; i += len) {
            float wr = 1.0f, wi = 0.0f;
            for (std::size_t k = 0; k < len / 2; ++k) {
                const std::size_t u = i + k;
                const std::size_t v = i + k + len / 2;
                const float vr = real[v] * wr - imag[v] * wi;
                const float vi = real[v] * wi + imag[v] * wr;
                const float ur = real[u];
                const float ui = imag[u];
                real[u] = ur + vr;
                imag[u] = ui + vi;
                real[v] = ur - vr;
                imag[v] = ui - vi;
                const float next_wr = wr * wlen_r - wi * wlen_i;
                wi = wr * wlen_i + wi * wlen_r;
                wr = next_wr;
            }
        }
    }
}

} // namespace spectral_waveform
