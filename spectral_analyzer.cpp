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
}

std::size_t spectral_analyzer::next_power_of_two(std::size_t v) {
    std::size_t p = 1;
    while (p < v) p <<= 1;
    return p;
}

void spectral_analyzer::feed(const float* interleaved, std::size_t frame_count) {
    if (!interleaved || frame_count == 0 || m_channels == 0) return;
    m_total_frames += frame_count;

    for (std::size_t f = 0; f < frame_count; ++f) {
        double mono = 0.0;
        const float* frame = interleaved + f * m_channels;
        for (std::uint32_t c = 0; c < m_channels; ++c) mono += frame[c];
        mono /= static_cast<double>(m_channels);
        m_mono_buffer.push_back(static_cast<float>(mono));

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
    const double low_n = std::min(1.0, (low / total) * 3.0);
    const double mid_n = std::min(1.0, (mid / total) * 3.0);
    const double high_n = std::min(1.0, (high / total) * 3.0);
    const double rms = count > 0 ? std::sqrt(sumSquares / static_cast<double>(count)) : 0.0;

    waveform_point p;
    p.peak = static_cast<std::uint16_t>(std::lround(std::min(1.0f, peak) * 65535.0f));
    p.rms = static_cast<std::uint16_t>(std::lround(std::min(1.0, rms) * 65535.0));
    p.bass = compress_energy(low_n);
    p.mids = compress_energy(mid_n);
    p.treble = compress_energy(high_n);
    m_points.push_back(p);
}

waveform_data spectral_analyzer::finish() {
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

    if (!m_points.empty()) {
        std::vector<std::uint16_t> rmsValues;
        rmsValues.reserve(m_points.size());
        for (const auto& p : m_points) rmsValues.push_back(p.rms);
        const std::size_t index = (rmsValues.size() - 1) * 95 / 100;
        std::nth_element(rmsValues.begin(), rmsValues.begin() + index, rmsValues.end());
        out.rms_reference = std::max<std::uint16_t>(1, rmsValues[index]);
    }

    out.points = std::move(m_points);
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
