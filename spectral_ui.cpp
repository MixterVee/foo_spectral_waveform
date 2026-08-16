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
#include <string>

#include "spectral_analyzer.h"
#include "spectral_palette.h"
#include "waveform_cache.h"
#include "live_output_capture.h"
#include "stem_waveform_analysis.h"

namespace {

static const GUID guid_spectral_waveform_ui =
{ 0x8a3fe0d1, 0x62dc, 0x4bf2, { 0x9a, 0x72, 0x56, 0x37, 0x42, 0x2c, 0xb1, 0x91 } };

static const GUID guid_show_time_markers =
{ 0x6d9a14c3, 0x2f87, 0x4ca5, { 0xa7, 0xe1, 0x31, 0x7b, 0x93, 0x04, 0xd6, 0x52 } };
static cfg_bool g_showTimeMarkers(guid_show_time_markers, true);

static const wchar_t* kWindowClassName = L"foo_spectral_waveform_ui_v03";
static constexpr UINT kMsgAnalysisReady = WM_APP + 0x351;
static constexpr double kMinViewSpan = 0.02;
static constexpr double kKeyboardPanFraction = 0.10;
static constexpr int kLiveRefreshBehind = 160;
static constexpr int kLiveRefreshAhead = 40;
// Centered grab scrubbing is intentionally coalesced. Short drags make only
// the final seek on release; longer drags audition the latest position at a
// relaxed rate so the decoder/DSP chain does not chatter.
static constexpr ULONGLONG kScrubSeekIntervalMs = 250;
static constexpr ULONGLONG kGrabClickThresholdMs = 350;
static constexpr ULONGLONG kReleaseGlideDurationMs = 260;
static constexpr UINT kMenuStemBase = 1000;

enum stem_menu_command : unsigned {
    kStemOriginal = 0,
    kStemVocals,
    kStemInstrumental,
    kStemSaveVocalsWav,
    kStemSaveInstrumentalWav,
    kStemSaveVocalsMp3,
    kStemSaveInstrumentalMp3,
    kStemPrecache,
    kStemCommandCount
};

static const GUID kStemCommandGuids[kStemCommandCount] = {
    {0xa92a1001,0xd1f0,0x4ae1,{0xa0,0x11,0x31,0x10,0x42,0x00,0x00,0x01}},
    {0xa92a1002,0xd1f0,0x4ae1,{0xa0,0x11,0x31,0x10,0x42,0x00,0x00,0x02}},
    {0xa92a1003,0xd1f0,0x4ae1,{0xa0,0x11,0x31,0x10,0x42,0x00,0x00,0x03}},
    {0xa92a1004,0xd1f0,0x4ae1,{0xa0,0x11,0x31,0x10,0x42,0x00,0x00,0x04}},
    {0xa92a1005,0xd1f0,0x4ae1,{0xa0,0x11,0x31,0x10,0x42,0x00,0x00,0x05}},
    {0xa92a1006,0xd1f0,0x4ae1,{0xa0,0x11,0x31,0x10,0x42,0x00,0x00,0x06}},
    {0xa92a1007,0xd1f0,0x4ae1,{0xa0,0x11,0x31,0x10,0x42,0x00,0x00,0x07}},
    {0xa92a1008,0xd1f0,0x4ae1,{0xa0,0x11,0x31,0x10,0x42,0x00,0x00,0x08}}
};

std::wstring utf8_menu_text(const char* text) {
    if (text == nullptr || *text == 0) return {};
    const int count = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
    if (count <= 1) return {};
    std::wstring out(static_cast<size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text, -1, out.data(), count);
    out.resize(static_cast<size_t>(count - 1));
    return out;
}

enum : UINT {
    kMenuZoomIn = 1,
    kMenuZoomOut,
    kMenuFitTrack,
    kMenuCenterPlayhead,
    kMenuFollowPlayhead,
    kMenuFollowCentered,
    kMenuFollowPaged,
    kMenuShowTimeMarkers,
    kMenuReanalyze,
};

enum class follow_mode {
    centered,
    paged,
};

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
        m_wnd = CreateWindowExW(0, kWindowClassName, L"",
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
            0, 0, 0, 0, parent, nullptr, core_api::get_my_instance(), this);
        if (m_wnd == nullptr) throw exception_win32(GetLastError());
        SetTimer(m_wnd, 1, 16, nullptr);

        metadb_handle_ptr nowPlaying;
        if (playback_control::get()->get_now_playing(nowPlaying)) start_analysis(nowPlaying);
    }

    ~spectral_waveform_instance() {
        stop_analysis();
        release_back_buffer();
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
        if (what == ui_element_notify_colors_changed && m_wnd != nullptr) invalidate_all();
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
        auto* self = reinterpret_cast<spectral_waveform_instance*>(GetWindowLongPtrW(wnd, GWLP_USERDATA));
        if (msg == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            self = static_cast<spectral_waveform_instance*>(cs->lpCreateParams);
            SetWindowLongPtrW(wnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (self != nullptr) return self->handle_message(wnd, msg, wp, lp);
        return DefWindowProcW(wnd, msg, wp, lp);
    }

    LRESULT handle_message(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
        switch (msg) {
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: paint(); return 0;
        case WM_TIMER: {
            bool viewChanged = update_release_glide();
            if (!viewChanged) viewChanged = update_follow_view();
            if (spectral_waveform::live_output_capture::animation_active()) {
                // Progressive stem blocks dissolve into place for a few frames.
                // Rebuild the bitmap only while that short visual transition runs.
                invalidate_all();
            } else if (!viewChanged) {
                invalidate_playhead();
            }
            return 0;
        }
        case WM_SIZE:
            invalidate_all();
            return 0;
        case WM_MOUSEWHEEL:
            zoom_from_wheel(GET_WHEEL_DELTA_WPARAM(wp), GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            return 0;
        case WM_CONTEXTMENU:
            show_context_menu(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            return 0;
        case WM_GETDLGCODE:
            return DLGC_WANTARROWS | DLGC_WANTCHARS;
        case WM_KEYDOWN:
            switch (wp) {
            case VK_SPACE:
                if (m_viewSpan < 0.9995) {
                    m_followPlayhead = true;
                    recenter_on_playhead();
                }
                return 0;
            case VK_UP:
                zoom_from_keyboard(true);
                return 0;
            case VK_DOWN:
                zoom_from_keyboard(false);
                return 0;
            case VK_LEFT:
                pan_from_keyboard(-1);
                return 0;
            case VK_RIGHT:
                pan_from_keyboard(1);
                return 0;
            }
            break;
        case WM_LBUTTONDOWN:
            SetFocus(wnd);
            begin_drag(GET_X_LPARAM(lp));
            return 0;
        case WM_MOUSEMOVE:
            if (m_dragging && (wp & MK_LBUTTON)) update_drag(GET_X_LPARAM(lp));
            return 0;
        case WM_LBUTTONUP:
            end_drag(GET_X_LPARAM(lp));
            return 0;
        case WM_CAPTURECHANGED:
            m_dragging = false;
            m_dragMoved = false;
            m_centerScrubbing = false;
            return 0;
        case WM_LBUTTONDBLCLK:
            seek_from_x(GET_X_LPARAM(lp));
            return 0;
        case kMsgAnalysisReady:
            invalidate_all();
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

    void reset_view() {
        m_releaseGlideActive = false;
        m_viewStart = 0.0;
        m_viewSpan = 1.0;
        m_followPlayhead = false;
    }

    double view_end() const { return std::min(1.0, m_viewStart + m_viewSpan); }

    void clamp_view() {
        m_viewSpan = std::clamp(m_viewSpan, kMinViewSpan, 1.0);
        m_viewStart = std::clamp(m_viewStart, 0.0, 1.0 - m_viewSpan);
        if (m_viewSpan > 0.9995) reset_view();
    }

    bool playback_fraction(double& out) const {
        auto pc = playback_control::get();
        const double length = pc->playback_get_length_ex();
        if (length <= 0.0) return false;
        out = std::clamp(pc->playback_get_position() / length, 0.0, 1.0);
        return true;
    }

    void center_on_playhead_once() {
        if (m_wnd == nullptr || m_viewSpan >= 0.9995) return;
        double positionFrac = 0.0;
        if (!playback_fraction(positionFrac)) return;
        m_viewStart = positionFrac - m_viewSpan * 0.5;
        clamp_view();
        invalidate_frame();
    }

    void recenter_on_playhead() {
        m_releaseGlideActive = false;
        center_on_playhead_once();
    }

    void begin_release_glide(double targetPosition) {
        if (!m_followPlayhead || m_followMode != follow_mode::centered ||
            m_viewSpan >= 0.9995 || m_wnd == nullptr) {
            m_releaseGlideActive = false;
            return;
        }

        m_releaseGlideActive = true;
        m_releaseGlideStartTick = GetTickCount64();
        m_releaseGlideStartView = m_viewStart;
        m_releaseGlideTargetPosition = std::clamp(targetPosition, 0.0, 1.0);
        invalidate_frame();
    }

    double release_glide_position() const {
        if (!m_releaseGlideActive) return -1.0;
        auto pc = playback_control::get();
        const double length = pc->playback_get_length_ex();
        if (length <= 0.0) return m_releaseGlideTargetPosition;

        const ULONGLONG elapsedMs = GetTickCount64() - m_releaseGlideStartTick;
        const double advance = pc->is_playing()
            ? (static_cast<double>(elapsedMs) / 1000.0) / length
            : 0.0;
        return std::clamp(m_releaseGlideTargetPosition + advance, 0.0, 1.0);
    }

    bool update_release_glide() {
        if (!m_releaseGlideActive || m_dragging || m_wnd == nullptr ||
            m_viewSpan >= 0.9995 || !m_followPlayhead ||
            m_followMode != follow_mode::centered) {
            m_releaseGlideActive = false;
            return false;
        }

        const ULONGLONG now = GetTickCount64();
        const ULONGLONG elapsedMs = now - m_releaseGlideStartTick;
        const double t = std::clamp(
            static_cast<double>(elapsedMs) / static_cast<double>(kReleaseGlideDurationMs),
            0.0, 1.0);
        // Smoothstep-like cubic ease-out: fast enough to feel responsive, with a
        // soft landing as normal centered follow resumes.
        const double eased = 1.0 - std::pow(1.0 - t, 3.0);
        const double virtualPosition = release_glide_position();
        if (virtualPosition < 0.0) {
            m_releaseGlideActive = false;
            return false;
        }

        double targetView = virtualPosition - m_viewSpan * 0.5;
        targetView = std::clamp(targetView, 0.0, 1.0 - m_viewSpan);
        const double oldStart = m_viewStart;
        m_viewStart = m_releaseGlideStartView +
            (targetView - m_releaseGlideStartView) * eased;
        m_viewStart = std::clamp(m_viewStart, 0.0, 1.0 - m_viewSpan);

        if (t >= 1.0) m_releaseGlideActive = false;
        if (std::abs(m_viewStart - oldStart) > 1.0e-10 || t < 1.0) {
            invalidate_frame();
            return true;
        }
        return false;
    }

    bool update_follow_view() {
        if (m_releaseGlideActive || !m_followPlayhead || m_viewSpan >= 0.9995 ||
            m_dragging || m_wnd == nullptr) return false;
        if (!playback_control::get()->is_playing()) return false;
        double positionFrac = 0.0;
        if (!playback_fraction(positionFrac)) return false;

        constexpr double epsilon = 1.0e-9;
        const double oldStart = m_viewStart;

        if (m_followMode == follow_mode::centered) {
            m_viewStart = positionFrac - m_viewSpan * 0.5;
            clamp_view();
        } else {
            const double pageEnd = view_end();
            if (positionFrac >= m_viewStart - epsilon && positionFrac <= pageEnd + epsilon) return false;

            if (positionFrac > pageEnd) {
                do {
                    m_viewStart += m_viewSpan;
                } while (positionFrac > m_viewStart + m_viewSpan + epsilon &&
                    m_viewStart < 1.0 - m_viewSpan);
            } else {
                const double pageIndex = std::floor(positionFrac / m_viewSpan);
                m_viewStart = pageIndex * m_viewSpan;
            }
            clamp_view();
        }

        if (std::abs(m_viewStart - oldStart) < epsilon) return false;
        invalidate_frame();
        return true;
    }

    int current_playhead_x(int width) const {
        if (width <= 0) return -1;

        // While the user grabs a centered-follow waveform, the play position is
        // the stationary reference and the waveform moves underneath it. Do not
        // let intermediate playback_seek callbacks make the blue line jump.
        if (m_centerScrubbing) {
            const double anchor = std::clamp(m_scrubAnchorX, 0.0, 1.0);
            return static_cast<int>(std::lround(anchor * std::max(0, width - 1)));
        }

        if (m_releaseGlideActive) {
            const double positionFrac = release_glide_position();
            if (positionFrac < 0.0 || positionFrac < m_viewStart || positionFrac > view_end()) return -1;
            const double visibleFrac = (positionFrac - m_viewStart) / m_viewSpan;
            return static_cast<int>(std::lround(visibleFrac * std::max(0, width - 1)));
        }

        auto pc = playback_control::get();
        if (!pc->is_playing()) return -1;
        double positionFrac = 0.0;
        if (!playback_fraction(positionFrac)) return -1;
        if (positionFrac < m_viewStart || positionFrac > view_end()) return -1;
        const double visibleFrac = (positionFrac - m_viewStart) / m_viewSpan;
        return static_cast<int>(visibleFrac * std::max(0, width - 1));
    }

    void invalidate_playhead() {
        if (m_wnd == nullptr) return;
        RECT rc{};
        GetClientRect(m_wnd, &rc);
        const int newX = current_playhead_x(rc.right - rc.left);

        auto invalidate_strip = [this, &rc](int x) {
            if (x < 0) return;
            RECT strip{x - 3, rc.top, x + 4, rc.bottom};
            InvalidateRect(m_wnd, &strip, FALSE);
        };
        auto invalidate_live_band = [this, &rc](int x) {
            if (x < 0) return;
            RECT band{x - kLiveRefreshBehind, rc.top, x + kLiveRefreshAhead, rc.bottom};
            InvalidateRect(m_wnd, &band, FALSE);
        };

        if (newX == m_lastPlayX) {
            invalidate_live_band(newX);
            return;
        }

        invalidate_strip(m_lastPlayX);
        invalidate_live_band(newX);
        m_lastPlayX = newX;
    }

    void zoom_from_wheel(short delta, int screenX, int screenY) {
        if (delta == 0 || m_wnd == nullptr) return;
        POINT pt{screenX, screenY};
        ScreenToClient(m_wnd, &pt);
        RECT rc{};
        GetClientRect(m_wnd, &rc);
        const int width = rc.right - rc.left;
        if (width <= 1) return;

        const double cursorX = std::clamp(static_cast<double>(pt.x) / static_cast<double>(width - 1), 0.0, 1.0);
        const double anchor = m_viewStart + cursorX * m_viewSpan;
        const double steps = static_cast<double>(delta) / WHEEL_DELTA;
        const double factor = std::pow(0.80, steps);
        const double newSpan = std::clamp(m_viewSpan * factor, kMinViewSpan, 1.0);

        if (newSpan >= 0.9995) {
            reset_view();
        } else {
            m_viewSpan = newSpan;
            m_viewStart = anchor - cursorX * m_viewSpan;
            clamp_view();
            m_followPlayhead = true;
            SetFocus(m_wnd);
            recenter_on_playhead();
            return;
        }
        invalidate_all();
    }

    void zoom_from_keyboard(bool zoomIn) {
        if (m_wnd == nullptr) return;

        double anchor = m_viewStart + m_viewSpan * 0.5;
        double positionFrac = 0.0;
        if (playback_fraction(positionFrac) &&
            positionFrac >= m_viewStart && positionFrac <= view_end()) {
            anchor = positionFrac;
        }

        const double anchorX = std::clamp((anchor - m_viewStart) / m_viewSpan, 0.0, 1.0);
        const double factor = zoomIn ? 0.80 : 1.25;
        const double newSpan = std::clamp(m_viewSpan * factor, kMinViewSpan, 1.0);

        if (newSpan >= 0.9995) {
            reset_view();
        } else {
            const bool keepFollow = m_followPlayhead;
            m_viewSpan = newSpan;
            m_viewStart = anchor - anchorX * m_viewSpan;
            clamp_view();
            m_followPlayhead = keepFollow;
        }
        invalidate_all();
    }

    void pan_from_keyboard(int direction) {
        if (direction == 0 || m_viewSpan >= 0.9995) return;
        m_followPlayhead = false;
        m_viewStart += static_cast<double>(direction) * m_viewSpan * kKeyboardPanFraction;
        clamp_view();
        invalidate_frame();
    }

    bool resolve_stem_command(unsigned command, service_ptr_t<contextmenu_item>& item, t_uint32& index) const {
        if (command >= kStemCommandCount) return false;
        return menu_item_resolver::g_resolve_context_command(kStemCommandGuids[command], item, index);
    }

    std::wstring stem_command_name(unsigned command, const wchar_t* fallback) const {
        service_ptr_t<contextmenu_item> item;
        t_uint32 index = 0;
        if (!resolve_stem_command(command, item, index) || m_currentTrack.is_empty()) return fallback;

        metadb_handle_list data;
        data.add_item(m_currentTrack);
        pfc::string8 text;
        unsigned flags = 0;
        if (!item->item_get_display_data_root(
                text, flags, index, data, contextmenu_item::caller_now_playing)) return fallback;

        const std::wstring converted = utf8_menu_text(text.c_str());
        return converted.empty() ? std::wstring(fallback) : converted;
    }

    void execute_stem_command(unsigned command) {
        if (m_currentTrack.is_empty()) return;

        service_ptr_t<contextmenu_item> item;
        t_uint32 index = 0;
        if (!resolve_stem_command(command, item, index)) return;

        metadb_handle_list data;
        data.add_item(m_currentTrack);
        item->item_execute_simple(index, pfc::guid_null, data, contextmenu_item::caller_now_playing);

        // The normal separator context menu is observed on the next playback-time
        // callback. A menu embedded in the waveform should update immediately,
        // including while playback is paused.
        spectral_waveform::live_output_capture::refresh_mode();
        invalidate_all();
    }

    void show_context_menu(int screenX, int screenY) {
        if (m_wnd == nullptr) return;
        SetFocus(m_wnd);

        POINT pt{screenX, screenY};
        if (screenX == -1 && screenY == -1) {
            RECT rc{};
            GetClientRect(m_wnd, &rc);
            pt.x = (rc.left + rc.right) / 2;
            pt.y = (rc.top + rc.bottom) / 2;
            ClientToScreen(m_wnd, &pt);
        }

        HMENU menu = CreatePopupMenu();
        if (menu == nullptr) return;

        AppendMenuW(menu, MF_STRING | (m_viewSpan <= kMinViewSpan + 1.0e-9 ? MF_GRAYED : 0), kMenuZoomIn, L"Zoom In\tUp Arrow");
        AppendMenuW(menu, MF_STRING | (m_viewSpan >= 0.9995 ? MF_GRAYED : 0), kMenuZoomOut, L"Zoom Out\tDown Arrow");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING | (m_viewSpan >= 0.9995 ? MF_GRAYED : 0), kMenuFitTrack, L"Fit Entire Track");
        AppendMenuW(menu, MF_STRING | (m_viewSpan >= 0.9995 ? MF_GRAYED : 0), kMenuCenterPlayhead, L"Center on Playhead\tSpace");
        AppendMenuW(menu, MF_STRING | (m_viewSpan >= 0.9995 ? MF_GRAYED : 0) |
            (m_followPlayhead ? MF_CHECKED : 0), kMenuFollowPlayhead, L"Follow Playhead");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING | (m_followMode == follow_mode::centered ? MF_CHECKED : 0),
            kMenuFollowCentered, L"Follow Mode: Centered Scroll");
        AppendMenuW(menu, MF_STRING | (m_followMode == follow_mode::paged ? MF_CHECKED : 0),
            kMenuFollowPaged, L"Follow Mode: Page at Edge");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING | (g_showTimeMarkers.get() ? MF_CHECKED : 0),
            kMenuShowTimeMarkers, L"Show Time Markers");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

        // Mirror Stem Separator's registered context commands rather than
        // duplicating its DSP/export implementation in this component.
        HMENU stemMenu = CreatePopupMenu();
        if (stemMenu != nullptr) {
            service_ptr_t<contextmenu_item> probe;
            t_uint32 probeIndex = 0;
            const bool stemAvailable = resolve_stem_command(kStemOriginal, probe, probeIndex);
            const bool stemEnabled = stemAvailable && !m_currentTrack.is_empty();
            const int stemMode = stemAvailable ? spectral_waveform::current_stem_mode() : -1;

            auto addStem = [&](unsigned commandIndex, const wchar_t* fallback, bool checked = false) {
                const std::wstring label = stem_command_name(commandIndex, fallback);
                UINT flags = MF_STRING | (stemEnabled ? 0 : MF_GRAYED);
                if (checked) flags |= MF_CHECKED;
                AppendMenuW(stemMenu, flags, kMenuStemBase + commandIndex, label.c_str());
            };

            addStem(kStemOriginal, L"Original", stemMode == 0);
            addStem(kStemVocals, L"Vocals", stemMode == 1);
            addStem(kStemInstrumental, L"Instrumental", stemMode == 2);
            AppendMenuW(stemMenu, MF_SEPARATOR, 0, nullptr);
            addStem(kStemSaveVocalsWav, L"Save Vocals as WAV...");
            addStem(kStemSaveInstrumentalWav, L"Save Instrumental as WAV...");
            addStem(kStemSaveVocalsMp3, L"Save Vocals as MP3...");
            addStem(kStemSaveInstrumentalMp3, L"Save Instrumental as MP3...");
            AppendMenuW(stemMenu, MF_SEPARATOR, 0, nullptr);
            addStem(kStemPrecache, L"Pre-cache at track start");

            AppendMenuW(menu, MF_POPUP | (stemAvailable ? 0 : MF_GRAYED),
                reinterpret_cast<UINT_PTR>(stemMenu), L"Stem Separator");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        }

        AppendMenuW(menu, MF_STRING | (m_currentTrack.is_empty() ? MF_GRAYED : 0),
            kMenuReanalyze, L"Re-analyze Current Track");

        const UINT command = TrackPopupMenu(menu,
            TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
            pt.x, pt.y, 0, m_wnd, nullptr);
        DestroyMenu(menu);

        if (command >= kMenuStemBase && command < kMenuStemBase + kStemCommandCount) {
            execute_stem_command(command - kMenuStemBase);
            return;
        }

        switch (command) {
        case kMenuZoomIn:
            zoom_from_keyboard(true);
            break;
        case kMenuZoomOut:
            zoom_from_keyboard(false);
            break;
        case kMenuFitTrack:
            reset_view();
            invalidate_all();
            break;
        case kMenuCenterPlayhead:
            center_on_playhead_once();
            break;
        case kMenuFollowPlayhead:
            m_followPlayhead = !m_followPlayhead;
            if (m_followPlayhead) center_on_playhead_once();
            else invalidate_frame();
            break;
        case kMenuFollowCentered:
            m_followMode = follow_mode::centered;
            if (m_followPlayhead) center_on_playhead_once();
            else invalidate_frame();
            break;
        case kMenuFollowPaged:
            m_followMode = follow_mode::paged;
            if (m_followPlayhead) invalidate_frame();
            break;
        case kMenuShowTimeMarkers:
            g_showTimeMarkers = !g_showTimeMarkers.get();
            invalidate_frame();
            break;
        case kMenuReanalyze:
            reanalyze_current_track();
            break;
        }
    }

    void reanalyze_current_track() {
        const auto track = m_currentTrack;
        if (track.is_empty()) return;

        // Stop both generations before deleting files so stale background work
        // cannot recreate an old cache after the recovery command runs.
        stop_analysis();
        spectral_waveform::live_output_capture::reset();

        abort_callback_impl aborter;
        spectral_waveform::remove_waveform_caches(track, aborter);

        // Keep the user's zoom/pan while rebuilding the Original waveform now.
        start_analysis(track, false);
    }

    void begin_drag(int x) {
        if (m_wnd == nullptr) return;
        m_releaseGlideActive = false;
        m_dragging = true;
        m_dragMoved = false;
        m_dragStartX = x;
        m_dragStartView = m_viewStart;
        m_dragStartTick = GetTickCount64();
        // Do not seek immediately on the first few pixels of a grab. If the
        // gesture is short, release performs the only seek and feels much cleaner.
        m_scrubLastSeekTick = m_dragStartTick;
        m_centerScrubbing = false;

        double positionFrac = 0.0;
        auto pc = playback_control::get();
        if (m_followPlayhead &&
            m_followMode == follow_mode::centered &&
            m_viewSpan < 0.9995 &&
            pc->is_playing() &&
            pc->playback_can_seek() &&
            playback_fraction(positionFrac)) {

            m_centerScrubbing = true;
            m_scrubStartPosition = positionFrac;
            m_scrubTargetPosition = positionFrac;
            m_scrubAnchorX = std::clamp(
                (positionFrac - m_viewStart) / m_viewSpan, 0.0, 1.0);
        }

        SetCapture(m_wnd);
    }

    void update_drag(int x) {
        if (!m_dragging || m_wnd == nullptr) return;
        RECT rc{};
        GetClientRect(m_wnd, &rc);
        const int width = rc.right - rc.left;
        if (width <= 1) return;

        const int dx = x - m_dragStartX;
        if (std::abs(dx) >= 3) {
            m_dragMoved = true;
            if (!m_centerScrubbing) m_followPlayhead = false;
        }
        if (!m_dragMoved || m_viewSpan >= 0.9995) return;

        if (m_centerScrubbing) {
            // Grab the waveform itself: dragging right moves earlier audio under
            // the play position; dragging left moves later audio underneath it.
            const double deltaFrac =
                (static_cast<double>(dx) / static_cast<double>(width - 1)) * m_viewSpan;
            m_scrubTargetPosition = std::clamp(
                m_scrubStartPosition - deltaFrac, 0.0, 1.0);

            m_viewStart = m_scrubTargetPosition - m_scrubAnchorX * m_viewSpan;
            clamp_view();

            // Seek often enough to audition the new location while dragging, but
            // not on every mouse event so the decoder/DSP chain stays responsive.
            const ULONGLONG now = GetTickCount64();
            if (m_scrubLastSeekTick == 0 ||
                now - m_scrubLastSeekTick >= kScrubSeekIntervalMs) {
                auto pc = playback_control::get();
                const double length = pc->playback_get_length_ex();
                if (pc->is_playing() && pc->playback_can_seek() && length > 0.0) {
                    pc->playback_seek(m_scrubTargetPosition * length);
                    m_scrubLastSeekTick = now;
                }
            }
            invalidate_all();
            return;
        }

        m_viewStart = m_dragStartView -
            (static_cast<double>(dx) / static_cast<double>(width - 1)) * m_viewSpan;
        clamp_view();
        invalidate_all();
    }

    void end_drag(int x) {
        if (!m_dragging) return;

        const bool wasMoved = m_dragMoved;
        const bool centeredScrub = m_centerScrubbing;
        const ULONGLONG heldMs = GetTickCount64() - m_dragStartTick;

        m_dragging = false;
        m_dragMoved = false;
        m_centerScrubbing = false;
        if (GetCapture() == m_wnd) ReleaseCapture();

        if (centeredScrub) {
            if (wasMoved) {
                auto pc = playback_control::get();
                const double length = pc->playback_get_length_ex();
                if (pc->is_playing() && pc->playback_can_seek() && length > 0.0) {
                    pc->playback_seek(m_scrubTargetPosition * length);
                }
                // Do not immediately query playback_get_position() here. Right
                // after a seek it can briefly report the preceding audition
                // position, which caused the visible release snap. Use the exact
                // scrub target for a short visual handoff, then resume follow.
                if (m_followPlayhead) begin_release_glide(m_scrubTargetPosition);
            } else if (heldMs < kGrabClickThresholdMs) {
                // Preserve the existing quick click-to-seek action. A longer hold
                // with no movement simply freezes centered scrolling temporarily.
                seek_from_x(x);
            } else if (m_followPlayhead) {
                recenter_on_playhead();
            }
            return;
        }

        if (!wasMoved) seek_from_x(x);
    }

    std::shared_ptr<const spectral_waveform::waveform_data> waveform_snapshot() const {
        std::lock_guard<std::mutex> lock(m_waveformMutex);
        return m_waveform;
    }

    spectral_waveform::waveform_point aggregate_point(
        const spectral_waveform::waveform_data& data, int x, int width) const {
        spectral_waveform::waveform_point out{};
        const size_t count = data.points.size();
        if (count == 0 || width <= 0) return out;

        const double leftFrac = m_viewStart + (static_cast<double>(x) / width) * m_viewSpan;
        const double rightFrac = m_viewStart + (static_cast<double>(x + 1) / width) * m_viewSpan;

        if (data.duration_seconds > 0.0) {
            const double startSeconds = leftFrac * data.duration_seconds;
            const double endSeconds = rightFrac * data.duration_seconds;
            if (spectral_waveform::live_output_capture::aggregate(startSeconds, endSeconds, out)) {
                return out;
            }
        }

        size_t begin = static_cast<size_t>(std::floor(leftFrac * count));
        size_t end = static_cast<size_t>(std::ceil(rightFrac * count));
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
        const double db = 20.0 * std::log10(std::max(rms, 1.0e-8));
        const double normalized = std::clamp((db + 42.0) / 42.0, 0.0, 1.0);
        const double loudnessShape = std::pow(normalized, 2.60);
        const double transient = std::sqrt(std::clamp(peak, 0.0, 1.0));
        return std::clamp(0.94 * loudnessShape + 0.06 * transient, 0.0, 0.92);
    }

    void draw_status_text(HDC dc, const RECT& rc, const wchar_t* text) const {
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, query_color(ui_color_text, COLOR_WINDOWTEXT));
        RECT textRc = rc;
        DrawTextW(dc, text, -1, &textRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    void draw_view_overlay(HDC dc, int width, int height) const {
        if (dc == nullptr || width < 80 || height < 24) return;

        const double zoom = 1.0 / std::max(m_viewSpan, 1.0e-9);
        const int tenths = std::max(10, static_cast<int>(std::lround(zoom * 10.0)));
        wchar_t label[64]{};
        if ((tenths % 10) == 0) {
            if (m_followPlayhead) wsprintfW(label, L"%dx  Follow", tenths / 10);
            else wsprintfW(label, L"%dx", tenths / 10);
        } else {
            if (m_followPlayhead) wsprintfW(label, L"%d.%dx  Follow", tenths / 10, tenths % 10);
            else wsprintfW(label, L"%d.%dx", tenths / 10, tenths % 10);
        }

        SIZE textSize{};
        if (!GetTextExtentPoint32W(dc, label, lstrlenW(label), &textSize)) return;

        const int padX = 6;
        const int padY = 3;
        RECT box{
            std::max<LONG>(4L, static_cast<LONG>(width) - textSize.cx - static_cast<LONG>(padX * 2 + 6)),
            6L,
            static_cast<LONG>(width - 6),
            std::min<LONG>(static_cast<LONG>(height - 4), 6L + textSize.cy + static_cast<LONG>(padY * 2))
        };
        if (box.right <= box.left || box.bottom <= box.top) return;

        HBRUSH background = CreateSolidBrush(query_color(ui_color_background, COLOR_WINDOW));
        FillRect(dc, &box, background);
        DeleteObject(background);

        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, query_color(ui_color_text, COLOR_WINDOWTEXT));
        RECT textRc = box;
        textRc.left += padX;
        textRc.right -= padX;
        textRc.top += padY;
        textRc.bottom -= padY;
        DrawTextW(dc, label, -1, &textRc, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    static COLORREF blend_color(COLORREF foreground, COLORREF background, unsigned foregroundPercent) {
        foregroundPercent = std::min(100u, foregroundPercent);
        const unsigned backgroundPercent = 100u - foregroundPercent;
        const BYTE r = static_cast<BYTE>((GetRValue(foreground) * foregroundPercent +
            GetRValue(background) * backgroundPercent) / 100u);
        const BYTE g = static_cast<BYTE>((GetGValue(foreground) * foregroundPercent +
            GetGValue(background) * backgroundPercent) / 100u);
        const BYTE b = static_cast<BYTE>((GetBValue(foreground) * foregroundPercent +
            GetBValue(background) * backgroundPercent) / 100u);
        return RGB(r, g, b);
    }

    static void format_time_label(double seconds, double step, wchar_t* out, size_t outCount) {
        if (out == nullptr || outCount == 0) return;
        seconds = std::max(0.0, seconds);

        if (step < 1.0) {
            const int tenths = static_cast<int>(std::lround(seconds * 10.0));
            const int totalSeconds = tenths / 10;
            const int tenth = tenths % 10;
            const int hours = totalSeconds / 3600;
            const int minutes = (totalSeconds / 60) % 60;
            const int secs = totalSeconds % 60;
            if (hours > 0) swprintf_s(out, outCount, L"%d:%02d:%02d.%d", hours, minutes, secs, tenth);
            else swprintf_s(out, outCount, L"%d:%02d.%d", minutes, secs, tenth);
        } else {
            const int totalSeconds = static_cast<int>(std::lround(seconds));
            const int hours = totalSeconds / 3600;
            const int minutes = (totalSeconds / 60) % 60;
            const int secs = totalSeconds % 60;
            if (hours > 0) swprintf_s(out, outCount, L"%d:%02d:%02d", hours, minutes, secs);
            else swprintf_s(out, outCount, L"%d:%02d", minutes, secs);
        }
    }

    void draw_time_ruler(HDC dc, int width, int height, double durationSeconds) const {
        if (dc == nullptr || width < 120 || height < 42 || durationSeconds <= 0.0) return;

        const double visibleSeconds = durationSeconds * m_viewSpan;
        if (visibleSeconds <= 0.0) return;
        const double pixelsPerSecond = static_cast<double>(std::max(1, width - 1)) / visibleSeconds;

        // Keep labels deliberately sparse. The chosen interval adapts continuously
        // as zoom changes while staying on familiar musical/editor time divisions.
        static constexpr double intervals[] = {
            0.1, 0.2, 0.5, 1.0, 2.0, 5.0, 10.0, 15.0, 30.0,
            60.0, 120.0, 300.0, 600.0, 900.0, 1800.0, 3600.0
        };
        constexpr double kMinLabelPixels = 96.0;
        double step = intervals[std::size(intervals) - 1];
        for (double candidate : intervals) {
            if (candidate * pixelsPerSecond >= kMinLabelPixels) {
                step = candidate;
                break;
            }
        }

        const double viewStartSeconds = m_viewStart * durationSeconds;
        const double viewEndSeconds = view_end() * durationSeconds;
        const double epsilon = step * 1.0e-8;
        double first = std::ceil((viewStartSeconds - epsilon) / step) * step;

        const COLORREF foreground = query_color(ui_color_text, COLOR_WINDOWTEXT);
        const COLORREF background = query_color(ui_color_background, COLOR_WINDOW);
        const COLORREF majorColor = blend_color(foreground, background, 62);
        const COLORREF minorColor = blend_color(foreground, background, 38);

        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, majorColor);
        HGDIOBJ oldPen = SelectObject(dc, GetStockObject(DC_PEN));

        // Half-step ticks provide orientation without adding more text.
        const double halfStep = step * 0.5;
        if (halfStep * pixelsPerSecond >= 28.0) {
            double minor = std::ceil((viewStartSeconds - epsilon) / halfStep) * halfStep;
            for (; minor <= viewEndSeconds + epsilon; minor += halfStep) {
                const double majorIndex = minor / step;
                if (std::abs(majorIndex - std::round(majorIndex)) < 1.0e-6) continue;
                const double frac = (minor / durationSeconds - m_viewStart) / m_viewSpan;
                const int x = static_cast<int>(std::lround(frac * std::max(0, width - 1)));
                if (x < 0 || x >= width) continue;
                SetDCPenColor(dc, minorColor);
                MoveToEx(dc, x, height - 3, nullptr);
                LineTo(dc, x, height);
            }
        }

        for (double t = first; t <= viewEndSeconds + epsilon; t += step) {
            const double frac = (t / durationSeconds - m_viewStart) / m_viewSpan;
            const int x = static_cast<int>(std::lround(frac * std::max(0, width - 1)));
            if (x < 0 || x >= width) continue;

            SetDCPenColor(dc, majorColor);
            MoveToEx(dc, x, height - 6, nullptr);
            LineTo(dc, x, height);

            wchar_t label[32]{};
            format_time_label(t, step, label, std::size(label));

            constexpr int halfLabel = 38;
            RECT textRc{};
            if (x < halfLabel) {
                textRc = {2, height - 20, 2 + halfLabel * 2, height - 6};
            } else if (x > width - halfLabel) {
                textRc = {width - 2 - halfLabel * 2, height - 20, width - 2, height - 6};
            } else {
                textRc = {x - halfLabel, height - 20, x + halfLabel, height - 6};
            }
            DrawTextW(dc, label, -1, &textRc,
                DT_CENTER | DT_BOTTOM | DT_SINGLELINE | DT_NOPREFIX | DT_NOCLIP);
        }

        SelectObject(dc, oldPen);
    }

    void release_back_buffer() {
        if (m_waveDC != nullptr) {
            if (m_waveOldBitmap != nullptr) SelectObject(m_waveDC, m_waveOldBitmap);
            m_waveOldBitmap = nullptr;
            if (m_waveBitmap != nullptr) DeleteObject(m_waveBitmap);
            m_waveBitmap = nullptr;
            DeleteDC(m_waveDC);
            m_waveDC = nullptr;
        }
        m_bufferWidth = 0;
        m_bufferHeight = 0;
        m_bufferValid = false;
    }

    bool ensure_back_buffer(HDC referenceDC, int width, int height) {
        if (width <= 0 || height <= 0) return false;
        if (m_waveDC != nullptr && m_bufferWidth == width && m_bufferHeight == height) return true;

        release_back_buffer();
        m_waveDC = CreateCompatibleDC(referenceDC);
        if (m_waveDC == nullptr) return false;
        m_waveBitmap = CreateCompatibleBitmap(referenceDC, width, height);
        if (m_waveBitmap == nullptr) {
            release_back_buffer();
            return false;
        }
        m_waveOldBitmap = SelectObject(m_waveDC, m_waveBitmap);
        m_bufferWidth = width;
        m_bufferHeight = height;
        m_bufferValid = false;
        return true;
    }

    void clear_columns(HDC dc, int x0, int x1, int height) const {
        x0 = std::max(0, x0);
        x1 = std::min(m_bufferWidth, x1);
        if (x1 <= x0) return;
        RECT r{x0, 0, x1, height};
        HBRUSH brush = CreateSolidBrush(query_color(ui_color_background, COLOR_WINDOW));
        FillRect(dc, &r, brush);
        DeleteObject(brush);
    }

    void render_columns(HDC dc, const spectral_waveform::waveform_data& waveform,
        int x0, int x1, int width, int height) {
        x0 = std::max(0, x0);
        x1 = std::min(width, x1);
        if (x1 <= x0) return;

        clear_columns(dc, x0, x1, height);
        const int mid = height / 2;
        const int usable = std::max(2, height - 8);
        HGDIOBJ oldPen = SelectObject(dc, GetStockObject(DC_PEN));
        for (int x = x0; x < x1; ++x) {
            const auto point = aggregate_point(waveform, x, width);
            const double amp = display_amplitude(point);
            const int half = static_cast<int>(std::lround(amp * usable * 0.5));
            if (half <= 0) continue;
            const auto color = spectral_waveform::color_for_point(point);
            SetDCPenColor(dc, RGB(color.r, color.g, color.b));
            MoveToEx(dc, x, mid - half, nullptr);
            LineTo(dc, x, mid + half + 1);
        }
        SelectObject(dc, oldPen);
    }

    void rebuild_waveform_buffer(const spectral_waveform::waveform_data& waveform,
        int width, int height) {
        render_columns(m_waveDC, waveform, 0, width, width, height);
        m_bufferViewStart = m_viewStart;
        m_bufferViewSpan = m_viewSpan;
        m_bufferValid = true;
    }

    bool scroll_waveform_buffer(const spectral_waveform::waveform_data& waveform,
        int width, int height) {
        if (!m_bufferValid || std::abs(m_bufferViewSpan - m_viewSpan) > 1.0e-12) return false;

        const double exactShift = ((m_viewStart - m_bufferViewStart) / m_viewSpan) * width;
        const int shift = static_cast<int>(std::lround(exactShift));
        if (shift == 0) return true;
        if (std::abs(shift) >= width) return false;

        if (shift > 0) {
            BitBlt(m_waveDC, 0, 0, width - shift, height, m_waveDC, shift, 0, SRCCOPY);
            render_columns(m_waveDC, waveform, width - shift, width, width, height);
        } else {
            const int amount = -shift;
            BitBlt(m_waveDC, amount, 0, width - amount, height, m_waveDC, 0, 0, SRCCOPY);
            render_columns(m_waveDC, waveform, 0, amount, width, height);
        }

        m_bufferViewStart += (static_cast<double>(shift) / width) * m_viewSpan;
        return true;
    }

    void paint() {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(m_wnd, &ps);
        if (dc == nullptr) return;

        RECT rc{};
        GetClientRect(m_wnd, &rc);
        const int width = std::max(0L, rc.right - rc.left);
        const int height = std::max(0L, rc.bottom - rc.top);
        const auto waveform = waveform_snapshot();

        if (width > 0 && height > 0 && ensure_back_buffer(dc, width, height)) {
            if (waveform && !waveform->points.empty()) {
                if (!scroll_waveform_buffer(*waveform, width, height)) {
                    rebuild_waveform_buffer(*waveform, width, height);
                }

                const int liveX = current_playhead_x(width);
                if (liveX >= 0) {
                    render_columns(m_waveDC, *waveform,
                        std::max(0, liveX - kLiveRefreshBehind),
                        std::min(width, liveX + kLiveRefreshAhead),
                        width, height);
                }
            } else {
                clear_columns(m_waveDC, 0, width, height);
                if (m_analyzing.load()) draw_status_text(m_waveDC, rc, L"Analyzing waveform...");
                m_bufferViewStart = m_viewStart;
                m_bufferViewSpan = m_viewSpan;
                m_bufferValid = true;
            }

            const int paintWidth = std::max(0L, ps.rcPaint.right - ps.rcPaint.left);
            const int paintHeight = std::max(0L, ps.rcPaint.bottom - ps.rcPaint.top);
            if (paintWidth > 0 && paintHeight > 0) {
                BitBlt(dc, ps.rcPaint.left, ps.rcPaint.top, paintWidth, paintHeight,
                    m_waveDC, ps.rcPaint.left, ps.rcPaint.top, SRCCOPY);
            }
        } else {
            HBRUSH bg = CreateSolidBrush(query_color(ui_color_background, COLOR_WINDOW));
            FillRect(dc, &ps.rcPaint, bg);
            DeleteObject(bg);
        }

        if (width > 0 && height > 0) {
            if (g_showTimeMarkers.get() && waveform && waveform->duration_seconds > 0.0) {
                draw_time_ruler(dc, width, height, waveform->duration_seconds);
            }
            const int playX = current_playhead_x(width);
            if (playX >= 0) {
                const COLORREF accent = query_color(ui_color_highlight, COLOR_HIGHLIGHT);
                HPEN pen = CreatePen(PS_SOLID, 2, accent);
                HGDIOBJ old = SelectObject(dc, pen);
                MoveToEx(dc, playX, 0, nullptr);
                LineTo(dc, playX, height);
                SelectObject(dc, old);
                DeleteObject(pen);
                m_lastPlayX = playX;
            }
            draw_view_overlay(dc, width, height);
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

        const double visible = std::clamp(static_cast<double>(x) / static_cast<double>(width - 1), 0.0, 1.0);
        const double trackFrac = std::clamp(m_viewStart + visible * m_viewSpan, 0.0, 1.0);
        pc->playback_seek(trackFrac * length);
        if (m_followPlayhead) recenter_on_playhead();
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

    void start_analysis(metadb_handle_ptr track, bool resetView = true) {
        stop_analysis();
        m_currentTrack = track;
        clear_waveform();
        if (resetView) reset_view();
        m_bufferValid = false;
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
                spectral_waveform::waveform_data cached;
                if (spectral_waveform::load_waveform_cache(track, cached, *aborter)) {
                    auto result = std::make_shared<spectral_waveform::waveform_data>(std::move(cached));
                    {
                        std::lock_guard<std::mutex> lock(m_waveformMutex);
                        m_waveform = std::move(result);
                    }
                    m_analyzing.store(false);
                    if (!aborter->is_aborting() && targetWnd != nullptr)
                        PostMessageW(targetWnd, kMsgAnalysisReady, 0, 0);
                    return;
                }

                const t_uint32 decodeFlags = input_flag_simpledecode;
                service_ptr_t<input_decoder> decoder;
                input_entry::g_open_for_decoding(decoder, nullptr, track->get_path(), *aborter);
                decoder->initialize(track->get_subsong_index(), decodeFlags, *aborter);

                audio_chunk_impl_temporary chunk;
                std::unique_ptr<spectral_waveform::spectral_analyzer> analyzer;
                std::vector<float> pcm;
                unsigned sampleRate = 0, channels = 0;

                while (decoder->run(chunk, *aborter)) {
                    aborter->check();
                    if (chunk.is_empty()) continue;
                    if (!analyzer) {
                        sampleRate = chunk.get_sample_rate();
                        channels = chunk.get_channels();
                        if (sampleRate == 0 || channels == 0) continue;
                        analyzer = std::make_unique<spectral_waveform::spectral_analyzer>(sampleRate, channels);
                    }
                    if (chunk.get_sample_rate() != sampleRate || chunk.get_channels() != channels)
                        throw exception_unexpected_audio_format_change();

                    const size_t used = chunk.get_used_size();
                    pcm.resize(used);
                    const audio_sample* src = chunk.get_data();
                    for (size_t i = 0; i < used; ++i) pcm[i] = static_cast<float>(src[i]);
                    analyzer->feed(pcm.data(), chunk.get_sample_count());
                }

                aborter->check();
                if (analyzer) {
                    auto data = analyzer->finish();
                    spectral_waveform::save_waveform_cache(track, data, *aborter);
                    auto result = std::make_shared<spectral_waveform::waveform_data>(std::move(data));
                    std::lock_guard<std::mutex> lock(m_waveformMutex);
                    m_waveform = std::move(result);
                }
            } catch (exception_aborted const&) {
            } catch (std::exception const& e) {
                pfc::string_formatter msg;
                msg << "foo_spectral_waveform: analysis failed: " << e.what();
                console::print(msg);
            }
            m_analyzing.store(false);
            if (!aborter->is_aborting() && targetWnd != nullptr) PostMessageW(targetWnd, kMsgAnalysisReady, 0, 0);
        });
        invalidate_all();
    }

    void on_playback_new_track(metadb_handle_ptr track) override { start_analysis(track); }
    void on_playback_stop(play_control::t_stop_reason) override {
        stop_analysis();
        invalidate_all(false);
    }
    void on_playback_seek(double) override {
        // Continuous seeks generated by a centered grab must not fight the user's
        // drag by recentering the view on every seek callback.
        if (m_centerScrubbing || m_releaseGlideActive) {
            invalidate_playhead();
            return;
        }
        if (m_followPlayhead) recenter_on_playhead();
        else invalidate_playhead();
    }
    void on_playback_pause(bool) override { invalidate_playhead(); }
    void on_playback_time(double) override {
        if (!m_followPlayhead) invalidate_playhead();
    }

    void invalidate_frame() {
        if (m_wnd != nullptr) {
            m_lastPlayX = -1;
            InvalidateRect(m_wnd, nullptr, FALSE);
        }
    }

    void invalidate_all(bool discardBuffer = true) {
        if (discardBuffer) m_bufferValid = false;
        invalidate_frame();
    }

    HWND m_wnd = nullptr;
    int m_lastPlayX = -1;
    ui_element_instance_callback::ptr m_callback;

    double m_viewStart = 0.0;
    double m_viewSpan = 1.0;
    bool m_followPlayhead = false;
    follow_mode m_followMode = follow_mode::centered;
    bool m_dragging = false;
    bool m_dragMoved = false;
    bool m_centerScrubbing = false;
    int m_dragStartX = 0;
    double m_dragStartView = 0.0;
    ULONGLONG m_dragStartTick = 0;
    ULONGLONG m_scrubLastSeekTick = 0;
    double m_scrubStartPosition = 0.0;
    double m_scrubTargetPosition = 0.0;
    double m_scrubAnchorX = 0.5;
    bool m_releaseGlideActive = false;
    ULONGLONG m_releaseGlideStartTick = 0;
    double m_releaseGlideStartView = 0.0;
    double m_releaseGlideTargetPosition = 0.0;

    HDC m_waveDC = nullptr;
    HBITMAP m_waveBitmap = nullptr;
    HGDIOBJ m_waveOldBitmap = nullptr;
    int m_bufferWidth = 0;
    int m_bufferHeight = 0;
    bool m_bufferValid = false;
    double m_bufferViewStart = 0.0;
    double m_bufferViewSpan = 1.0;

    mutable std::mutex m_waveformMutex;
    std::shared_ptr<const spectral_waveform::waveform_data> m_waveform;
    metadb_handle_ptr m_currentTrack;
    std::thread m_worker;
    std::shared_ptr<abort_callback_impl> m_abort;
    std::atomic_bool m_analyzing{false};
};

class spectral_waveform_element : public ui_element {
public:
    GUID get_guid() override { return guid_spectral_waveform_ui; }
    GUID get_subclass() override { return ui_element_subclass_playback_visualisation; }
    void get_name(pfc::string_base& out) override { out = "Spectral Waveform"; }
    ui_element_instance::ptr instantiate(HWND parent, ui_element_config::ptr cfg,
        ui_element_instance_callback::ptr callback) override {
        return new service_impl_t<spectral_waveform_instance>(parent, cfg, callback);
    }
    ui_element_config::ptr get_default_configuration() override {
        return ui_element_config::g_create_empty(guid_spectral_waveform_ui);
    }
    ui_element_children_enumerator::ptr enumerate_children(ui_element_config::ptr) override { return nullptr; }
    bool get_description(pfc::string_base& out) override {
        out = "Frequency-colored waveform with mouse/keyboard zoom and pan, context controls, zoom/follow status, centered or paged follow modes, persistent analysis cache and live post-DSP stem-aware waveform updates.";
        return true;
    }
};

static service_factory_single_t<spectral_waveform_element> g_spectral_waveform_element_factory;

} // namespace
