#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <foobar2000/SDK/foobar2000.h>
#include <windows.h>

#include "live_output_capture.h"
#include "stem_waveform_analysis.h"
#include "waveform_cache.h"

#include <algorithm>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>

namespace spectral_waveform::live_output_capture {
namespace {

static const wchar_t* kWaveformWindowClass = L"foo_spectral_waveform_ui_v03";

std::mutex g_previewMutex;
std::shared_ptr<const waveform_data> g_preview;
HWND g_mainWindow = nullptr;

void invalidate_waveform_windows() {
    HWND root = g_mainWindow;
    if (root == nullptr || !IsWindow(root)) return;

    EnumChildWindows(root,
        [](HWND wnd, LPARAM) -> BOOL {
            wchar_t className[128]{};
            if (GetClassNameW(wnd, className, static_cast<int>(std::size(className))) > 0 &&
                wcscmp(className, kWaveformWindowClass) == 0) {
                // The waveform's WM_SIZE handler discards its cached bitmap and
                // schedules a full repaint. The actual size is unchanged.
                PostMessageW(wnd, WM_SIZE, 0, 0);
            }
            return TRUE;
        }, 0);
}

void publish_preview(const waveform_data& data) {
    {
        std::lock_guard<std::mutex> lock(g_previewMutex);
        g_preview = std::make_shared<waveform_data>(data);
    }
    invalidate_waveform_windows();
}

void clear_preview(bool invalidate) {
    {
        std::lock_guard<std::mutex> lock(g_previewMutex);
        g_preview.reset();
    }
    if (invalidate) invalidate_waveform_windows();
}

std::shared_ptr<const waveform_data> preview_snapshot() {
    std::lock_guard<std::mutex> lock(g_previewMutex);
    return g_preview;
}

class preview_manager {
public:
    preview_manager() : m_thread([this]() { worker_main(); }) {}

    ~preview_manager() {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_stop = true;
            if (m_activeAbort) m_activeAbort->abort();
        }
        m_cv.notify_all();
        if (m_thread.joinable()) m_thread.join();
    }

    void request(metadb_handle_ptr track, int mode) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_activeAbort) m_activeAbort->abort();
            m_track = track;
            m_mode = mode;
            ++m_generation;
            m_hasRequest = true;
        }

        // Never leave an old stem painted while a new mode is being prepared.
        // Original mode therefore restores the untouched source immediately.
        clear_preview(true);
        m_cv.notify_one();
    }

    void cancel_keep_preview() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_activeAbort) m_activeAbort->abort();
        ++m_generation;
        m_hasRequest = false;
    }

private:
    bool is_current(std::uint64_t generation) {
        std::lock_guard<std::mutex> lock(m_mutex);
        return !m_stop && generation == m_generation;
    }

    void worker_main() {
        for (;;) {
            metadb_handle_ptr track;
            int mode = -1;
            std::uint64_t generation = 0;
            std::shared_ptr<abort_callback_impl> aborter;

            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_cv.wait(lock, [this]() { return m_stop || m_hasRequest; });
                if (m_stop) return;

                track = m_track;
                mode = m_mode;
                generation = m_generation;
                m_hasRequest = false;

                aborter = std::make_shared<abort_callback_impl>();
                m_activeAbort = aborter;
            }

            // Original or no provider: the regular disk-cached waveform stays
            // authoritative and no preview is published.
            if (mode > 0 && mode <= 2 && track.is_valid()) {
                try {
                    waveform_data original;
                    if (!load_waveform_cache(track, original, *aborter)) {
                        // The UI analysis normally creates this cache first. If
                        // it is not ready yet, wait for the next mode/time poll
                        // rather than duplicating the source analysis here.
                    } else {
                        analyze_stem_progressive(
                            track,
                            mode,
                            original,
                            *aborter,
                            [this, generation, aborter](const waveform_data& data) {
                                if (aborter->is_aborting() || !is_current(generation)) return;
                                publish_preview(data);
                            });
                    }
                } catch (exception_aborted const&) {
                } catch (std::exception const& e) {
                    pfc::string_formatter msg;
                    msg << "foo_spectral_waveform: stem preview failed: " << e.what();
                    console::print(msg);
                }
            }

            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (m_activeAbort == aborter) m_activeAbort.reset();
            }
        }
    }

    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::thread m_thread;
    bool m_stop = false;
    bool m_hasRequest = false;
    std::uint64_t m_generation = 0;
    metadb_handle_ptr m_track;
    int m_mode = -1;
    std::shared_ptr<abort_callback_impl> m_activeAbort;
};

std::unique_ptr<preview_manager> g_manager;

class playback_observer : private play_callback_impl_base {
public:
    playback_observer()
        : play_callback_impl_base(
            play_callback::flag_on_playback_new_track |
            play_callback::flag_on_playback_stop |
            play_callback::flag_on_playback_time) {}

    void prime(metadb_handle_ptr track) {
        m_track = track;
        m_lastMode = current_stem_mode();
        if (g_manager) g_manager->request(m_track, m_lastMode);
    }

private:
    void on_playback_new_track(metadb_handle_ptr track) override {
        m_track = track;
        m_lastMode = current_stem_mode();
        if (g_manager) g_manager->request(m_track, m_lastMode);
    }

    void on_playback_stop(play_control::t_stop_reason) override {
        if (g_manager) g_manager->cancel_keep_preview();
    }

    void on_playback_time(double) override {
        const int mode = current_stem_mode();
        if (mode == m_lastMode) return;

        m_lastMode = mode;
        if (g_manager) g_manager->request(m_track, mode);
    }

    metadb_handle_ptr m_track;
    int m_lastMode = -999;
};

std::unique_ptr<playback_observer> g_observer;

class capture_lifecycle : public initquit {
public:
    void on_init() override {
        g_mainWindow = core_api::get_main_window();
        g_manager = std::make_unique<preview_manager>();
        g_observer = std::make_unique<playback_observer>();

        metadb_handle_ptr nowPlaying;
        if (playback_control::get()->get_now_playing(nowPlaying)) {
            g_observer->prime(nowPlaying);
        }
    }

    void on_quit() override {
        g_observer.reset();
        g_manager.reset();
        clear_preview(false);
        g_mainWindow = nullptr;
    }
};

static service_factory_single_t<capture_lifecycle> g_capture_lifecycle_factory;

} // namespace

bool point_at(double seconds, waveform_point& out) {
    auto preview = preview_snapshot();
    if (!preview || preview->points.empty() || preview->duration_seconds <= 0.0) return false;

    const double frac = std::clamp(seconds / preview->duration_seconds, 0.0, 1.0);
    size_t index = static_cast<size_t>(std::floor(frac * preview->points.size()));
    if (index >= preview->points.size()) index = preview->points.size() - 1;
    out = preview->points[index];
    return true;
}

bool aggregate(double start_seconds, double end_seconds, waveform_point& out) {
    auto preview = preview_snapshot();
    if (!preview || preview->points.empty() || preview->duration_seconds <= 0.0) return false;

    if (end_seconds <= start_seconds) end_seconds = start_seconds + 0.005;

    const size_t count = preview->points.size();
    const double leftFrac = std::clamp(start_seconds / preview->duration_seconds, 0.0, 1.0);
    const double rightFrac = std::clamp(end_seconds / preview->duration_seconds, 0.0, 1.0);

    size_t begin = static_cast<size_t>(std::floor(leftFrac * count));
    size_t end = static_cast<size_t>(std::ceil(rightFrac * count));
    begin = std::min(begin, count - 1);
    end = std::max(begin + 1, std::min(end, count));

    std::uint64_t rms = 0;
    std::uint64_t bass = 0;
    std::uint64_t mids = 0;
    std::uint64_t treble = 0;
    std::uint16_t peak = 0;

    for (size_t i = begin; i < end; ++i) {
        const waveform_point& p = preview->points[i];
        peak = std::max(peak, p.peak);
        rms += p.rms;
        bass += p.bass;
        mids += p.mids;
        treble += p.treble;
    }

    const std::uint64_t n = static_cast<std::uint64_t>(end - begin);
    out.peak = peak;
    out.rms = static_cast<std::uint16_t>(rms / n);
    out.bass = static_cast<std::uint8_t>(bass / n);
    out.mids = static_cast<std::uint8_t>(mids / n);
    out.treble = static_cast<std::uint8_t>(treble / n);
    return true;
}

void reset() {
    clear_preview(true);
}

} // namespace spectral_waveform::live_output_capture
