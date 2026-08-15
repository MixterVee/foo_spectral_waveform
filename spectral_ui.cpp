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

static const wchar_t* kWindowClassName = L"foo_spectral_waveform_ui_v03";
static constexpr UINT kMsgAnalysisReady = WM_APP + 0x351;
static constexpr double kMinViewSpan = 0.02;
static constexpr double kKeyboardPanFraction = 0.10;

enum : UINT {
    kMenuZoomIn = 1,
    kMenuZoomOut,
    kMenuFitTrack,
    kMenuCenterPlayhead,
    kMenuFollowPlayhead,
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
        case WM_TIMER:
            if (!update_follow_view()) invalidate_playhead();
            return 0;
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
        center_on_playhead_once();
    }

    bool update_follow_view() {
        if (!m_followPlayhead || m_viewSpan >= 0.9995 || m_dragging || m_wnd == nullptr) return false;
        if (!playback_control::get()->is_playing()) return false;
        double positionFrac = 0.0;
        if (!playback_fraction(positionFrac)) return false;

        constexpr double epsilon = 1.0e-9;
        const double pageEnd = view_end();
        if (positionFrac >= m_viewStart - epsilon && positionFrac <= pageEnd + epsilon) return false;

        const double oldStart = m_viewStart;
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
        if (std::abs(m_viewStart - oldStart) < epsilon) return false;
        invalidate_frame();
        return true;
    }

    int current_playhead_x(int width) const {
        if (width <= 0) return -1;
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
        if (newX == m_lastPlayX) return;
        auto invalidate_strip = [this, &rc](int x) {
            if (x < 0) return;
            RECT strip{x - 3, rc.top, x + 4, rc.bottom};
            InvalidateRect(m_wnd, &strip, FALSE);
        };
        invalidate_strip(m_lastPlayX);
        invalidate_strip(newX);
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

        const UINT command = TrackPopupMenu(menu,
            TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
            pt.x, pt.y, 0, m_wnd, nullptr);
        DestroyMenu(menu);

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
        }
    }

    void begin_drag(int x) {
        if (m_wnd == nullptr) return;
        m_dragging = true;
        m_dragMoved = false;
        m_dragStartX = x;
        m_dragStartView = m_viewStart;
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
            m_followPlayhead = false;
        }
        if (!m_dragMoved || m_viewSpan >= 0.9995) return;

        m_viewStart = m_dragStartView - (static_cast<double>(dx) / static_cast<double>(width - 1)) * m_viewSpan;
        clamp_view();
        invalidate_all();
    }

    void end_drag(int x) {
        if (!m_dragging) return;
        const bool wasMoved = m_dragMoved;
        m_dragging = false;
        m_dragMoved = false;
        if (GetCapture() == m_wnd) ReleaseCapture();
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

    void start_analysis(metadb_handle_ptr track) {
        stop_analysis();
        clear_waveform();
        reset_view();
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
                    auto result = std::make_shared<spectral_waveform::waveform_data>(analyzer->finish());
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
    bool m_dragging = false;
    bool m_dragMoved = false;
    int m_dragStartX = 0;
    double m_dragStartView = 0.0;

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
        out = "Frequency-colored waveform with mouse/keyboard zoom and pan, context controls, zoom/follow status, Space-to-follow and paged waveform following.";
        return true;
    }
};

static service_factory_single_t<spectral_waveform_element> g_spectral_waveform_element_factory;

} // namespace
