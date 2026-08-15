#include <foobar2000/SDK/foobar2000.h>

#include "live_output_capture.h"
#include "spectral_analyzer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace spectral_waveform::live_output_capture {
namespace {

constexpr double kBinSeconds = 0.005;

std::mutex g_mutex;
std::unordered_map<std::uint64_t, waveform_point> g_points;

std::uint64_t bin_for_time(double seconds) {
    if (seconds < 0.0) seconds = 0.0;
    return static_cast<std::uint64_t>(std::floor(seconds / kBinSeconds));
}

class capture_impl : public playback_stream_capture_callback_impl,
                     private play_callback_impl_base {
public:
    capture_impl()
        : playback_stream_capture_callback_impl(0.05),
          play_callback_impl_base(
              play_callback::flag_on_playback_new_track |
              play_callback::flag_on_playback_stop) {}

    void on_chunk(const audio_chunk& chunk) override {
        const unsigned sampleRate = chunk.get_sample_rate();
        const unsigned channels = chunk.get_channels();
        const t_size frames = chunk.get_sample_count();
        if (sampleRate == 0 || channels == 0 || frames == 0) return;

        const audio_sample* src = chunk.get_data();
        if (src == nullptr) return;

        std::vector<float> pcm;
        pcm.resize(static_cast<size_t>(frames) * channels);
        for (size_t i = 0; i < pcm.size(); ++i) {
            pcm[i] = static_cast<float>(src[i]);
        }

        spectral_analyzer analyzer(sampleRate, channels);
        analyzer.feed(pcm.data(), static_cast<size_t>(frames));
        waveform_data data = analyzer.finish();
        if (data.points.empty()) return;

        auto pc = playback_control::get();
        const double endSeconds = std::max(0.0, pc->playback_get_position());
        const double duration = static_cast<double>(frames) / static_cast<double>(sampleRate);
        const double startSeconds = std::max(0.0, endSeconds - duration);
        const double step = duration / static_cast<double>(data.points.size());

        std::lock_guard<std::mutex> lock(g_mutex);
        for (size_t i = 0; i < data.points.size(); ++i) {
            const double t = startSeconds + (static_cast<double>(i) + 0.5) * step;
            g_points[bin_for_time(t)] = data.points[i];
        }
    }

private:
    void on_playback_new_track(metadb_handle_ptr) override {
        reset();
    }

    void on_playback_stop(play_control::t_stop_reason) override {}
};

std::unique_ptr<capture_impl> g_capture;

class capture_lifecycle : public initquit {
public:
    void on_init() override {
        g_capture = std::make_unique<capture_impl>();
    }

    void on_quit() override {
        g_capture.reset();
        reset();
    }
};

static service_factory_single_t<capture_lifecycle> g_capture_lifecycle_factory;

} // namespace

bool point_at(double seconds, waveform_point& out) {
    std::lock_guard<std::mutex> lock(g_mutex);
    const auto it = g_points.find(bin_for_time(seconds));
    if (it == g_points.end()) return false;
    out = it->second;
    return true;
}

void reset() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_points.clear();
}

} // namespace spectral_waveform::live_output_capture
