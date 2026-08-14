#pragma once
#include "waveform_data.h"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace spectral_waveform {

class spectral_analyzer {
public:
    spectral_analyzer(std::uint32_t sample_rate, std::uint32_t channels);

    void feed(const float* interleaved, std::size_t frame_count);
    waveform_data finish();

private:
    void analyze_window(const float* mono, std::size_t count);
    void fft(std::vector<float>& real, std::vector<float>& imag) const;
    static std::size_t next_power_of_two(std::size_t v);
    static std::uint8_t compress_energy(double x);

    std::uint32_t m_sample_rate = 0;
    std::uint32_t m_channels = 0;
    std::size_t m_window_size = 0;
    std::size_t m_hop_size = 0;

    std::vector<float> m_mono_buffer;
    std::vector<waveform_point> m_points;
    std::uint64_t m_total_frames = 0;
};

} // namespace spectral_waveform
