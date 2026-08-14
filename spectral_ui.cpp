#include <foobar2000/SDK/foobar2000.h>
#include <windows.h>
#include <algorithm>
#include <cmath>

namespace {

static const GUID guid_spectral_waveform_ui =
{ 0x8a3fe0d1, 0x62dc, 0x4bf2, { 0x9a, 0x72, 0x56, 0x37, 0x42, 0x2c, 0xb1, 0x91 } };

static const wchar_t* kWindowClassName = L"foo_spectral_waveform_ui_v02";

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
    }

    ~spectral_waveform_instance() {
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
            InvalidateRect(wnd, nullptr, FALSE);
            return 0;
        case WM_LBUTTONDOWN:
            seek_from_x(GET_X_LPARAM(lp));
            return 0;
        case WM_LBUTTONDBLCLK:
            seek_from_x(GET_X_LPARAM(lp));
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
        FillRect(dc, &rc, bgBrush);
        DeleteObject(bgBrush);

        if (width > 0 && height > 0) {
            const int mid = height / 2;
            const int usable = std::max(2, height - 8);

            // v0.2 UI scaffold: deterministic multiband-looking bars.
            // Real decoded spectral points replace these in the next step.
            for (int x = 0; x < width; ++x) {
                const double t = static_cast<double>(x) / std::max(1, width - 1);
                const double a = 0.26 + 0.34 * std::abs(std::sin(t * 19.0))
                                     + 0.22 * std::abs(std::sin(t * 53.0 + 0.8));
                const int half = std::max(1, static_cast<int>(a * usable * 0.5));

                const double low = 0.5 + 0.5 * std::sin(t * 8.0 + 0.4);
                const double midBand = 0.5 + 0.5 * std::sin(t * 13.0 + 2.1);
                const double high = 0.5 + 0.5 * std::sin(t * 21.0 + 4.0);
                const double norm = std::max(0.001, std::max(low, std::max(midBand, high)));

                const int r = static_cast<int>(255.0 * std::min(1.0, (low + 0.25 * midBand) / norm));
                const int g = static_cast<int>(255.0 * std::min(1.0, (0.85 * midBand + 0.15 * high) / norm));
                const int b = static_cast<int>(255.0 * std::min(1.0, (high + 0.08 * midBand) / norm));

                HPEN pen = CreatePen(PS_SOLID, 1, RGB(r, g, b));
                HGDIOBJ old = SelectObject(dc, pen);
                MoveToEx(dc, x, mid - half, nullptr);
                LineTo(dc, x, mid + half + 1);
                SelectObject(dc, old);
                DeleteObject(pen);
            }

            auto pc = playback_control::get();
            if (pc->is_playing()) {
                const double length = pc->playback_get_length_ex();
                const double pos = pc->playback_get_position();
                if (length > 0.0) {
                    const double frac = std::clamp(pos / length, 0.0, 1.0);
                    const int playX = static_cast<int>(frac * std::max(0, width - 1));
                    const COLORREF accent = query_color(ui_color_highlight, COLOR_HIGHLIGHT);
                    HPEN pen = CreatePen(PS_SOLID, 2, accent);
                    HGDIOBJ old = SelectObject(dc, pen);
                    MoveToEx(dc, playX, 0, nullptr);
                    LineTo(dc, playX, height);
                    SelectObject(dc, old);
                    DeleteObject(pen);
                }
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

    void on_playback_new_track(metadb_handle_ptr) override { invalidate(); }
    void on_playback_stop(play_control::t_stop_reason) override { invalidate(); }
    void on_playback_seek(double) override { invalidate(); }
    void on_playback_pause(bool) override { invalidate(); }
    void on_playback_time(double) override { invalidate(); }

    void invalidate() {
        if (m_wnd != nullptr) InvalidateRect(m_wnd, nullptr, FALSE);
    }

    HWND m_wnd = nullptr;
    ui_element_instance_callback::ptr m_callback;
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
        out = "Frequency-colored waveform seekbar. v0.2 UI scaffold.";
        return true;
    }
};

static service_factory_single_t<spectral_waveform_element> g_spectral_waveform_element_factory;

} // namespace
