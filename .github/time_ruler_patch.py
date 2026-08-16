from pathlib import Path

p = Path('spectral_ui.cpp')
text = p.read_text(encoding='utf-8')

anchor = '''    void release_back_buffer() {
'''
insert = r'''    static COLORREF blend_color(COLORREF foreground, COLORREF background, unsigned foregroundPercent) {
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

'''
if text.count(anchor) != 1:
    raise SystemExit(f'release buffer anchor count={text.count(anchor)}')
text = text.replace(anchor, insert + anchor, 1)

paint_anchor = '''        if (width > 0 && height > 0) {
            const int playX = current_playhead_x(width);
'''
paint_replacement = '''        if (width > 0 && height > 0) {
            if (waveform && waveform->duration_seconds > 0.0) {
                draw_time_ruler(dc, width, height, waveform->duration_seconds);
            }
            const int playX = current_playhead_x(width);
'''
if text.count(paint_anchor) != 1:
    raise SystemExit(f'paint anchor count={text.count(paint_anchor)}')
text = text.replace(paint_anchor, paint_replacement, 1)

p.write_text(text, encoding='utf-8')
print('Adaptive time ruler patch applied.')
