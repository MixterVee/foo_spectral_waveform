#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <foobar2000/SDK/foobar2000.h>
#include <windows.h>

#include "live_output_capture.h"
#include "stem_waveform_analysis.h"
#include "waveform_cache.h"

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>

namespace spectral_waveform::live_output_capture {
namespace {

static const wchar_t* kWaveformWindowClass = L"foo_spectral_waveform_ui_v03";
static constexpr ULONGLONG kPreviewFadeMs = 420;

std::mutex g_previewMutex;
std::shared_ptr<const waveform_data> g_vocalsPreview;
std::shared_ptr<const waveform_data> g_instrumentalPreview;
std::shared_ptr<const waveform_data> g_previousVocalsPreview;
std::shared_ptr<const waveform_data> g_previousInstrumentalPreview;
ULONGLONG g_previewFadeStart = 0;
int g_selectedMode = 0;
HWND g_mainWindow = nullptr;

void invalidate_waveform_windows() {
    HWND root = g_mainWindow;
    if (root == nullptr || !IsWindow(root)) return;

    EnumChildWindows(root,
        [](HWND wnd, LPARAM) -> BOOL {
            wchar_t className[128]{};
            if (GetClassNameW(wnd, className, _countof(className)) > 0 &&
                wcscmp(className, kWaveformWindowClass) == 0) {
                // The waveform's WM_SIZE handler discards its cached bitmap and
                // schedules a full repaint. The actual size is unchanged.
                PostMessageW(wnd, WM_SIZE, 0, 0);
            }
            return TRUE;
        }, 0);
}

void set_selected_mode(int mode, bool invalidate) {
    {
        std::lock_guard<std::mutex> lock(g_previewMutex);
        g_selectedMode = (mode == 1 || mode == 2) ? mode : 0;
    }
    if (invalidate) invalidate_waveform_windows();
}

void publish_previews(
    const waveform_data& vocals,
    const waveform_data& instrumental,
    bool animate) {

    {
        std::lock_guard<std::mutex> lock(g_previewMutex);

        if (animate && g_vocalsPreview && g_instrumentalPreview) {
            g_previousVocalsPreview = g_vocalsPreview;
            g_previousInstrumentalPreview = g_instrumentalPreview;
            g_previewFadeStart = GetTickCount64();
        } else {
            g_previousVocalsPreview.reset();
            g_previousInstrumentalPreview.reset();
            g_previewFadeStart = 0;
        }

        g_vocalsPreview = std::make_shared<waveform_data>(vocals);
        g_instrumentalPreview = std::make_shared<waveform_data>(instrumental);
    }
    invalidate_waveform_windows();
}

void clear_previews(bool invalidate) {
    {
        std::lock_guard<std::mutex> lock(g_previewMutex);
        g_vocalsPreview.reset();
        g_instrumentalPreview.reset();
        g_previousVocalsPreview.reset();
        g_previousInstrumentalPreview.reset();
        g_previewFadeStart = 0;
    }
    if (invalidate) invalidate_waveform_windows();
}

struct preview_blend_snapshot {
    std::shared_ptr<const waveform_data> previous;
    std::shared_ptr<const waveform_data> current;
    double mix = 1.0;
};

preview_blend_snapshot preview_snapshot() {
    std::lock_guard<std::mutex> lock(g_previewMutex);

    preview_blend_snapshot out;
    if (g_selectedMode == 1) {
        out.previous = g_previousVocalsPreview;
        out.current = g_vocalsPreview;
    } else if (g_selectedMode == 2) {
        out.previous = g_previousInstrumentalPreview;
        out.current = g_instrumentalPreview;
    } else {
        return out;
    }

    if (!out.current) return out;
    if (!out.previous || g_previewFadeStart == 0) {
        out.previous.reset();
        out.mix = 1.0;
        return out;
    }

    const ULONGLONG elapsed = GetTickCount64() - g_previewFadeStart;
    if (elapsed >= kPreviewFadeMs) {
        // The fade has finished. Drop the previous full-track snapshots so they
        // do not remain resident after the transition is no longer visible.
        g_previousVocalsPreview.reset();
        g_previousInstrumentalPreview.reset();
        g_previewFadeStart = 0;
        out.previous.reset();
        out.mix = 1.0;
        return out;
    }

    // Smoothstep avoids a mechanical-looking linear dissolve at either end.
    const double t = std::clamp(
        static_cast<double>(elapsed) / static_cast<double>(kPreviewFadeMs),
        0.0, 1.0);
    out.mix = t * t * (3.0 - 2.0 * t);
    return out;
}

bool same_track(metadb_handle_ptr a, metadb_handle_ptr b) {
    if (a.is_empty() || b.is_empty()) return a.is_empty() && b.is_empty();
    if (a->get_subsong_index() != b->get_subsong_index()) return false;
    return std::strcmp(a->get_path(), b->get_path()) == 0;
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

    void request(metadb_handle_ptr track, int mode, double prioritySeconds) {
        bool newTrack = false;
        bool queueAnalysis = false;

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            newTrack = !same_track(m_track, track);

            if (newTrack) {
                if (m_activeAbort) m_activeAbort->abort();
                ++m_generation;
                m_track = track;
                m_ready = false;
                m_hasRequest = false;
                // The worker being aborted belongs to the old generation. Mark
                // the new generation idle so its request can be queued now; the
                // single worker thread will pick it up after the old one exits.
                m_analysisActive = false;
            }

            m_mode = mode;
            m_prioritySeconds = std::max(0.0, prioritySeconds);

            // Mode changes on the same track do not cancel a dual-stem pass.
            // Both stem previews are being generated together, so the UI can
            // simply select whichever one is requested while work continues.
            if (mode > 0 && mode <= 2 && track.is_valid() &&
                !m_ready && !m_analysisActive && !m_hasRequest) {
                m_hasRequest = true;
                queueAnalysis = true;
            }
        }

        if (newTrack) clear_previews(false);
        set_selected_mode(mode, true);
        if (queueAnalysis) m_cv.notify_one();
    }

    void cancel_keep_preview() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_activeAbort) m_activeAbort->abort();
        ++m_generation;
        m_hasRequest = false;
        m_analysisActive = false;
        m_ready = false;
    }

    void invalidate_current() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_activeAbort) m_activeAbort->abort();
        ++m_generation;
        m_hasRequest = false;
        m_analysisActive = false;
        m_ready = false;
    }

private:
    bool is_current(std::uint64_t generation) {
        std::lock_guard<std::mutex> lock(m_mutex);
        return !m_stop && generation == m_generation;
    }

    void worker_main() {
        for (;;) {
            metadb_handle_ptr track;
            double prioritySeconds = 0.0;
            std::uint64_t generation = 0;
            std::shared_ptr<abort_callback_impl> aborter;

            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_cv.wait(lock, [this]() { return m_stop || m_hasRequest; });
                if (m_stop) return;

                track = m_track;
                prioritySeconds = m_prioritySeconds;
                generation = m_generation;
                m_hasRequest = false;
                m_analysisActive = true;

                aborter = std::make_shared<abort_callback_impl>();
                m_activeAbort = aborter;
            }

            bool completed = false;

            if (track.is_valid()) {
                try {
                    waveform_data cachedVocals;
                    waveform_data cachedInstrumental;

                    const bool haveVocals =
                        load_stem_waveform_cache(track, 1, cachedVocals, *aborter);
                    const bool haveInstrumental =
                        load_stem_waveform_cache(track, 2, cachedInstrumental, *aborter);

                    if (haveVocals && haveInstrumental && is_current(generation)) {
                        // Already-cached tracks stay visually instant. Restore the
                        // matching separated PCM in this same background worker so
                        // scrub/reverse is restart-ready without another Spleeter pass.
                        publish_previews(cachedVocals, cachedInstrumental, false);
                        rehydrate_transport_pcm_cache(track, prioritySeconds, *aborter);
                        completed = true;
                    } else {
                        waveform_data original;
                        bool haveOriginal = false;

                        // A new-track callback can arrive just before the regular
                        // waveform worker has finished writing its disk cache. Wait
                        // briefly in this background thread and retry instead of
                        // missing stem preview generation for the whole track.
                        for (unsigned attempt = 0; attempt < 40 && !aborter->is_aborting(); ++attempt) {
                            if (!is_current(generation)) break;
                            if (load_waveform_cache(track, original, *aborter)) {
                                haveOriginal = true;
                                break;
                            }
                            aborter->sleep(0.25);
                        }

                        if (haveOriginal && is_current(generation)) {
                            waveform_data vocalsFinal;
                            waveform_data instrumentalFinal;

                            // Seed the preview with Original. The first completed
                            // stem block can then fade from the exact graphic the
                            // user was already seeing instead of appearing abruptly.
                            publish_previews(original, original, false);

                            const bool analyzed = analyze_stems_progressive(
                                track,
                                original,
                                prioritySeconds,
                                *aborter,
                                [this, generation, aborter](
                                    const waveform_data& vocals,
                                    const waveform_data& instrumental) {
                                    if (aborter->is_aborting() || !is_current(generation)) return;
                                    publish_previews(vocals, instrumental, true);
                                },
                                vocalsFinal,
                                instrumentalFinal);

                            if (analyzed && is_current(generation) && !aborter->is_aborting()) {
                                save_stem_waveform_cache(track, 1, vocalsFinal, *aborter);
                                save_stem_waveform_cache(track, 2, instrumentalFinal, *aborter);
                                // The last progressive callback already supplied the
                                // final graphic. Republish without animation merely
                                // settles state and releases any previous snapshot.
                                publish_previews(vocalsFinal, instrumentalFinal, false);
                                completed = true;
                            }
                        }
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
                if (generation == m_generation) {
                    m_analysisActive = false;
                    if (completed) m_ready = true;
                }
                if (m_activeAbort == aborter) m_activeAbort.reset();
            }
        }
    }

    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::thread m_thread;
    bool m_stop = false;
    bool m_hasRequest = false;
    bool m_analysisActive = false;
    bool m_ready = false;
    std::uint64_t m_generation = 0;
    metadb_handle_ptr m_track;
    int m_mode = -1;
    double m_prioritySeconds = 0.0;
    std::shared_ptr<abort_callback_impl> m_activeAbort;
};

std::unique_ptr<preview_manager> g_manager;

class playback_observer : private play_callback_impl_base {
public:
    playback_observer()
        : play_callback_impl_base(
            play_callback::flag_on_playback_new_track |
            play_callback::flag_on_playback_stop |
            play_callback::flag_on_playback_seek |
            play_callback::flag_on_playback_time) {}

    void prime(metadb_handle_ptr track) {
        m_track = track;
        m_lastMode = current_stem_mode();
        const double position = std::max(0.0, playback_control::get()->playback_get_position());
        if (g_manager) g_manager->request(m_track, m_lastMode, position);
    }

    void refresh_now() {
        m_lastMode = current_stem_mode();
        const double position = std::max(0.0, playback_control::get()->playback_get_position());
        if (g_manager) g_manager->request(m_track, m_lastMode, position);
    }

    void force_mode_refresh() {
        m_lastMode = -999;
    }

private:
    void on_playback_new_track(metadb_handle_ptr track) override {
        m_track = track;
        m_lastMode = current_stem_mode();
        if (g_manager) g_manager->request(m_track, m_lastMode, 0.0);
    }

    void on_playback_stop(play_control::t_stop_reason) override {
        if (g_manager) g_manager->cancel_keep_preview();
    }

    void on_playback_seek(double time) override {
        m_lastMode = current_stem_mode();
        if (g_manager) g_manager->request(m_track, m_lastMode, time);
    }

    void on_playback_time(double time) override {
        const int mode = current_stem_mode();
        if (mode == m_lastMode) return;

        m_lastMode = mode;
        if (g_manager) g_manager->request(m_track, mode, time);
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
        clear_previews(false);
        set_selected_mode(0, false);
        g_mainWindow = nullptr;
    }
};

static service_factory_single_t<capture_lifecycle> g_capture_lifecycle_factory;

waveform_point blend_point(const waveform_point& a, const waveform_point& b, double mix) {
    mix = std::clamp(mix, 0.0, 1.0);
    const double inv = 1.0 - mix;

    waveform_point out{};
    out.peak = static_cast<std::uint16_t>(std::lround(a.peak * inv + b.peak * mix));
    out.rms = static_cast<std::uint16_t>(std::lround(a.rms * inv + b.rms * mix));
    out.bass = static_cast<std::uint8_t>(std::lround(a.bass * inv + b.bass * mix));
    out.mids = static_cast<std::uint8_t>(std::lround(a.mids * inv + b.mids * mix));
    out.treble = static_cast<std::uint8_t>(std::lround(a.treble * inv + b.treble * mix));
    return out;
}

bool point_from_preview(
    const std::shared_ptr<const waveform_data>& preview,
    double seconds,
    waveform_point& out) {

    if (!preview || preview->points.empty() || preview->duration_seconds <= 0.0) return false;

    const double frac = std::clamp(seconds / preview->duration_seconds, 0.0, 1.0);
    size_t index = static_cast<size_t>(std::floor(frac * preview->points.size()));
    if (index >= preview->points.size()) index = preview->points.size() - 1;
    out = preview->points[index];
    return true;
}

bool aggregate_from_preview(
    const std::shared_ptr<const waveform_data>& preview,
    double start_seconds,
    double end_seconds,
    waveform_point& out) {

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

} // namespace

bool point_at(double seconds, waveform_point& out) {
    const auto preview = preview_snapshot();
    waveform_point current{};
    if (!point_from_preview(preview.current, seconds, current)) return false;

    if (!preview.previous || preview.mix >= 1.0) {
        out = current;
        return true;
    }

    waveform_point previous{};
    if (!point_from_preview(preview.previous, seconds, previous)) {
        out = current;
        return true;
    }

    out = blend_point(previous, current, preview.mix);
    return true;
}

bool aggregate(double start_seconds, double end_seconds, waveform_point& out) {
    const auto preview = preview_snapshot();
    waveform_point current{};
    if (!aggregate_from_preview(preview.current, start_seconds, end_seconds, current)) return false;

    if (!preview.previous || preview.mix >= 1.0) {
        out = current;
        return true;
    }

    waveform_point previous{};
    if (!aggregate_from_preview(preview.previous, start_seconds, end_seconds, previous)) {
        out = current;
        return true;
    }

    out = blend_point(previous, current, preview.mix);
    return true;
}

bool animation_active() {
    std::lock_guard<std::mutex> lock(g_previewMutex);
    if (g_selectedMode != 1 && g_selectedMode != 2) return false;
    if (g_previewFadeStart == 0) return false;

    const bool havePrevious = g_selectedMode == 1
        ? static_cast<bool>(g_previousVocalsPreview)
        : static_cast<bool>(g_previousInstrumentalPreview);
    if (!havePrevious) return false;

    return GetTickCount64() - g_previewFadeStart < kPreviewFadeMs;
}

void refresh_mode() {
    if (g_observer) g_observer->refresh_now();
}

void reset() {
    if (g_manager) g_manager->invalidate_current();
    if (g_observer) g_observer->force_mode_refresh();
    clear_previews(true);
}

} // namespace spectral_waveform::live_output_capture
