#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <foobar2000/SDK/foobar2000.h>
#include <windows.h>
#include <windowsx.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "spectral_analyzer.h"
#include "spectral_palette.h"

namespace {

static const GUID guid_spectral_waveform_ui =
{ 0x8a3fe0d1, 0x62dc, 0x4bf2, { 0x9a, 0x72, 0x56, 0x37, 0x42, 0x2c, 0xb1, 0x91 } };

static const wchar_t* kWindowClassName = L"foo_spectral_waveform_ui_v02";
static constexpr UINT kMsgAnalysisReady = WM_APP + 0x351;

class spectral_waveform_instance : public ui_element_instance, private play_callback_impl_base {
public:
    spectral_waveform_instance(HWND parent, ui_element_config::ptr, ui_element_instance_callback::ptr callback)
        : play_callback_impl_base(
            play_callback::flag_on_playback_new_track |
            play_callback::flag_on_playback_stop |
            play_callback::flag_on_playback_seek |
            play_callback::flag_on_playback_pause |
            play_callback::flag_on_playback_time),
          m_callback(callback) {
        ensure_window_class();
        m_wnd = CreateWindowExW(
            0, kWindowClassName, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
            0, 0, 0, 0, parent, nullptr, core_api::get_my_instance(), this);
        if (m_wnd == nullptr) throw exception_win32(GetLastError());
        SetTimer(m_wnd, 1, 50, nullptr);

        metadb_handle_ptr nowPlaying;
        if (playback_control::get()->get_now_playing(nowPlaying)) {
            start_analysis(nowPlaying);
        }
    }

    ~spectral_waveform_instance() {
        stop_analysis();
        if (m_wnd != nullptr) {
            KillTimer(m_wnd, 1);
            DestroyWindow(m_wnd);
            m_wnd = nullptr;
        }
    }

    HWND get_wnd() override { return m_wnd; }

    void set_configuration(ui_element_config::ptr) override {}

    ui_element_config::ptr get_configuration() override {
        return ui_element_config::g_create_empty(guid_spectral_waveform_ui);
    }

    GUID get_guid() override { return guid_spectral_waveform_ui; }
    GUID get_subclass() override { return ui_element_subclass_playback_visualisation; }

    ui_element_min_max_info get_min_max_info() override {
        ui_element_min_max_info info;
        info.m_min_width = 80;
        info.m_min_height = 32;
        return info;
    }

    void notify(const GUID& what, t_size, const void*, t_size) override {
        if (what == ui_element_notify_colors_changed && m_wnd != nullptr) {
            m_lastPlayX = -1;
            InvalidateRect(m_wnd, nullptr, FALSE);
        }
    }

private:
    static void ensure_window_class() {
        static bool registered = false;
        if (registered) return;

        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
        wc.lpfnWndProc = &spectral_waveform_instance::wnd_proc;
        wc.hInstance = core_api::get_my_instance();
        wc.hCursor = LoadCursor(nullptr, IDC_HAND);
        wc.hbrBackground = nullptr;
        wc.lpszClassName = kWindowClassName;

        if (RegisterClassExW(&wc) == 0) {
            const DWORD err = GetLastError();
            if (err != ERROR_CLASS_ALREADY_EXISTS) throw exception_win32(err);
        }
        registered = true;
    }

    static LRESULT CALLBACK wnd_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
        spectral_waveform_instance* self = reinterpret_cast<spectral_waveform_instance*>(
            GetWindowLongPtrW(wnd, GWLP_USERDATA));

        if (msg == WM_NCCREATE) {
            auto cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            self = static_cast<spectral_waveform_instance*>(cs->lpCreateParams);
            SetWindowLongPtrW(wnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }

        if (self != nullptr) return self->handle_message(wnd, msg, wp, lp);
        return DefWindowProcW(wnd, msg, wp, lp);
    }

    LRESULT handle_message(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
        switch (msg) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            paint();
            return 0;
        case WM_TIMER:
            invalidate_playhead();
            return 0;
        case WM_SIZE:
            m_lastPlayX = -1;
            InvalidateRect(wnd, nullptr, FALSE);
            return 0;
        case WM_LBUTTONDOWN:
            seek_from_x(GET_X_LPARAM(lp));
            return 0;
        case WM_LBUTTONDBLCLK:
            seek_from_x(GET_X_LPARAM(lp));
            return 0;
        case kMsgAnalysisReady:
            m_lastPlayX = -1;
            InvalidateRect(wnd, nullptr, FALSE);
            return 0;
        case WM_NCDESTROY:
            SetWindowLongPtrW(wnd, GWLP_USERDATA, 0);
            if (m_wnd == wnd) m_wnd = nullptr;
            break;
        }
        return DefWindowProcW(wnd, msg, wp, lp);
    }

    COLORREF query_color(const GUID& id, int fallback) const {
        t_ui_color out = 0;
        if (m_callback.is_valid() && m_callback->query_color(id, out)) return static_cast<COLORREF>(out);
        return GetSysColor(fallback);
    }

    int current_playhead_x(int width) const {
        if (width <= 0) return -1;

        auto pc = playback_control::get();
        if (!pc->is_playing()) return -1;

        const double length = pc->playback_get_length_ex();
        if (length <= 0.0) return -1;

        const double pos = pc->playback_get_position();
        const double frac = std::clamp(pos / length, 0.0, 1.0);
        return static_cast<int>(frac * std::max(0, width - 1));
    }

    void invalidate_playhead() {
        if (m_wnd == nullptr) return;

        RECT rc{};
        GetClientRect(m_wnd, &rc);
        const int width = rc.right - rc.left;
        const int newX = current_playhead_x(width);

        if (newX == m_lastPlayX) return;

        auto invalidate_strip = [this, &rc](int x) {
            if (x < 0) return;
            RECT strip{ x - 3, rc.top, x + 4, rc.bottom };
            InvalidateRect(m_wnd, &strip, FALSE);
        };

        invalidate_strip(m_lastPlayX);
        invalidate_strip(newX);
        m_lastPlayX = newX;
    }

    std::shared_ptr<const spectral_waveform::waveform_data> waveform_snapshot() const {
        std::lock_guard<std::mutex> lock(m_waveformMutex);
        return m_waveform;
    }

    static spectral_waveform::waveform_point aggregate_point(
        const spectral_waveform::waveform_data& data, int x, int width) {
        spectral_waveform::waveform_point out{};
        const size_t count = data.points.size();
        if (count == 0 || width <= 0) return out;

        size_t begin = (static_cast<size_t>(x) * count) / static_cast<size_t>(width);
        size_t end = (static_cast<size_t>(x + 1) * count) / static_cast<size_t>(width);
        begin = std::min(begin, count - 1);
        end = std::max(begin + 1, std::min(end, count));

        uint64_t rms = 0, bass = 0, mids = 0, treble = 0;
        uint16_t peak = 0;
        for (size_t i = begin; i < end; ++i) {
            const auto& p = data.points[i];
            peak = std::max(peak, p.peak);
            rms += p.rms;
            bass += p.bass;
            mids += p.mids;
            treble += p.treble;
        }

        const uint64_t n = static_cast<uint64_t>(end - begin);
        out.peak = peak;
        out.rms = static_cast<uint16_t>(rms / n);
        out.bass = static_cast<uint8_t>(bass / n);
        out.mids = static_cast<uint8_t>(mids / n);
        out.treble = static_cast<uint8_t>(treble / n);
        return out;
    }

    static double display_amplitude(const spectral_waveform::waveform_point& point) {
        const double rms = point.rms / 65535.0;
        const double peak = point.peak / 65535.0;
        if (rms <= 1.0e-8 && peak <= 1.0e-8) return 0.0;

        // Absolute RMS scale instead of per-track normalization. A typical modern
        // master around -10 dBFS RMS lands near 65-70% height instead of the top.
        const double db = 20.0 * std::log10(std::max(rms, 1.0e-8));
        const double normalized = std::clamp((db + 48.0) / 45.0, 0.0, 1.0);
        const double loudnessShape = std::pow(normalized, 2.15);
        const double transient = std::sqrt(std::clamp(peak, 0.0, 1.0));
        return std::clamp(0.95 * loudnessShape + 0.05 * transient, 0.0, 1.0);
    }

    void draw_status_text(HDC dc, const RECT& rc, const wchar_t* text) const {
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, query_color(ui_color_text, COLOR_WINDOWTEXT));
        RECT textRc = rc;
        DrawTextW(dc, text, -1, &textRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    void paint() {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(m_wnd, &ps);
        if (dc == nullptr) return;

        RECT rc{};
        GetClientRect(m_wnd, &rc);
        const int width = std::max(0L, rc.right - rc.left);
        const int height = std::max(0L, rc.bottom - rc.top);

        const COLORREF bg = query_color(ui_color_background, COLOR_WINDOW);
        HBRUSH bgBrush = CreateSolidBrush(bg);
        FillRect(dc, &ps.rcPaint, bgBrush);
        DeleteObject(bgBrush);

        const auto waveform = waveform_snapshot();

        if (width > 0 && height > 0) {
            if (waveform && !waveform->points.empty()) {
                const int mid = height / 2;
                const int usable = std::max(2, height - 8);
                const int xStart = std::max(0L, ps.rcPaint.left);
                const int xEnd = std::min(width, static_cast<int>(ps.rcPaint.right));

                HGDIOBJ oldPen = SelectObject(dc, GetStockObject(DC_PEN));
                for (int x = xStart; x < xEnd; ++x) {
                    const auto point = aggregate_point(*waveform, x, width);
                    const double amp = display_amplitude(point);
                    const int half = static_cast<int>(std::lround(amp * usable * 0.5));
                    if (half <= 0) continue;

                    const auto color = spectral_waveform::color_for_point(point);
                    SetDCPenColor(dc, RGB(color.r, color.g, color.b));
                    MoveToEx(dc, x, mid - half, nullptr);
                    LineTo(dc, x, mid + half + 1);
                }
                SelectObject(dc, oldPen);
            } else if (m_analyzing.load()) {
                draw_status_text(dc, rc, L"Analyzing waveform...");
            }

            const int playX = current_playhead_x(width);
            if (playX >= 0 && playX >= ps.rcPaint.left - 2 && playX <= ps.rcPaint.right + 2) {
                const COLORREF accent = query_color(ui_color_highlight, COLOR_HIGHLIGHT);
                HPEN pen = CreatePen(PS_SOLID, 2, accent);
                HGDIOBJ old = SelectObject(dc, pen);
                MoveToEx(dc, playX, 0, nullptr);
                LineTo(dc, playX, height);
                SelectObject(dc, old);
                DeleteObject(pen);
                m_lastPlayX = playX;
            }
        }

        EndPaint(m_wnd, &ps);
    }

    void seek_from_x(int x) {
        if (m_wnd == nullptr) return;
        RECT rc{};
        GetClientRect(m_wnd, &rc);
        const int width = rc.right - rc.left;
        if (width <= 1) return;

        auto pc = playback_control::get();
        if (!pc->is_playing() || !pc->playback_can_seek()) return;
        const double length = pc->playback_get_length_ex();
        if (length <= 0.0) return;

        const double frac = std::clamp(static_cast<double>(x) / static_cast<double>(width - 1), 0.0, 1.0);
        pc->playback_seek(frac * length);
    }

    void stop_analysis() {
        auto aborter = m_abort;
        if (aborter) aborter->abort();
        if (m_worker.joinable()) m_worker.join();
        m_abort.reset();
        m_analyzing.store(false);
    }

    void clear_waveform() {
        std::lock_guard<std::mutex> lock(m_waveformMutex);
        m_waveform.reset();
    }

    void start_analysis(metadb_handle_ptr track) {
        stop_analysis();
        clear_waveform();
        if (track.is_empty()) {
            invalidate_all();
            return;
        }

        m_analyzing.store(true);
        auto aborter = std::make_shared<abort_callback_impl>();
        m_abort = aborter;
        HWND targetWnd = m_wnd;

        m_worker = std::thread([this, track, aborter, targetWnd]() {
            try {
                const t_uint32 decodeFlags = input_flag_simpledecode;
                service_ptr_t<input_decoder> decoder;
                input_entry::g_open_for_decoding(
                    decoder, nullptr, track->get_path(), *aborter);
                decoder->initialize(track->get_subsong_index(), decodeFlags, *aborter);

                audio_chunk_impl_temporary chunk;
                std::unique_ptr<spectral_waveform::spectral_analyzer> analyzer;
                std::vector<float> pcm;
                unsigned sampleRate = 0;
                unsigned channels = 0;

                while (decoder->run(chunk, *aborter)) {
                    aborter->check();
                    if (chunk.is_empty()) continue;

                    if (!analyzer) {
                        sampleRate = chunk.get_sample_rate();
                        channels = chunk.get_channels();
                        if (sampleRate == 0 || channels == 0) continue;
                        analyzer = std::make_unique<spectral_waveform::spectral_analyzer>(sampleRate, channels);
                    }

                    if (chunk.get_sample_rate() != sampleRate || chunk.get_channels() != channels) {
                        throw exception_unexpected_audio_format_change();
                    }

                    const size_t used = chunk.get_used_size();
                    pcm.resize(used);
                    const audio_sample* src = chunk.get_data();
                    for (size_t i = 0; i < used; ++i) {
                        pcm[i] = static_cast<float>(src[i]);
                    }
                    analyzer->feed(pcm.data(), chunk.get_sample_count());
                }

                aborter->check();
                if (analyzer) {
                    auto result = std::make_shared<spectral_waveform::waveform_data>(analyzer->finish());
                    {
                        std::lock_guard<std::mutex> lock(m_waveformMutex);
                        m_waveform = std::move(result);
                    }
                }
            } catch (exception_aborted const&) {
            } catch (std::exception const& e) {
                pfc::string_formatter msg;
                msg << "foo_spectral_waveform: analysis failed: " << e.what();
                console::print(msg);
            }

            m_analyzing.store(false);
            if (!aborter->is_aborting() && targetWnd != nullptr) {
                PostMessageW(targetWnd, kMsgAnalysisReady, 0, 0);
            }
        });

        invalidate_all();
    }

    void on_playback_new_track(metadb_handle_ptr track) override {
        start_analysis(track);
    }

    void on_playback_stop(play_control::t_stop_reason) override {
        stop_analysis();
        invalidate_all();
    }

    void on_playback_seek(double) override { invalidate_playhead(); }
    void on_playback_pause(bool) override { invalidate_playhead(); }
    void on_playback_time(double) override { invalidate_playhead(); }

    void invalidate_all() {
        if (m_wnd != nullptr) {
            m_lastPlayX = -1;
            InvalidateRect(m_wnd, nullptr, FALSE);
        }
    }

    HWND m_wnd = nullptr;
    int m_lastPlayX = -1;
    ui_element_instance_callback::ptr m_callback;

    mutable std::mutex m_waveformMutex;
    std::shared_ptr<const spectral_waveform::waveform_data> m_waveform;
    std::thread m_worker;
    std::shared_ptr<abort_callback_impl> m_abort;
    std::atomic_bool m_analyzing{false};
};

class spectral_waveform_element : public ui_element {
public:
    GUID get_guid() override { return guid_spectral_waveform_ui; }
    GUID get_subclass() override { return ui_element_subclass_playback_visualisation; }

    void get_name(pfc::string_base& out) override {
        out = "Spectral Waveform";
    }

    ui_element_instance::ptr instantiate(
        HWND parent,
        ui_element_config::ptr cfg,
        ui_element_instance_callback::ptr callback) override {
        return new service_impl_t<spectral_waveform_instance>(parent, cfg, callback);
    }

    ui_element_config::ptr get_default_configuration() override {
        return ui_element_config::g_create_empty(guid_spectral_waveform_ui);
    }

    ui_element_children_enumerator::ptr enumerate_children(ui_element_config::ptr) override {
        return nullptr;
    }

    bool get_description(pfc::string_base& out) override {
        out = "Frequency-colored waveform seekbar using decoded spectral analysis.";
        return true;
    }
};

static service_factory_single_t<spectral_waveform_element> g_spectral_waveform_element_factory;

} // namespace
