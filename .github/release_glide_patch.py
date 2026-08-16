from pathlib import Path

path = Path('spectral_ui.cpp')
s = path.read_text(encoding='utf-8')

replacements = [
(
'''static constexpr ULONGLONG kScrubSeekIntervalMs = 250;\nstatic constexpr ULONGLONG kGrabClickThresholdMs = 350;\nstatic constexpr UINT kMenuStemBase = 1000;\n''',
'''static constexpr ULONGLONG kScrubSeekIntervalMs = 250;\nstatic constexpr ULONGLONG kGrabClickThresholdMs = 350;\nstatic constexpr ULONGLONG kReleaseGlideDurationMs = 260;\nstatic constexpr UINT kMenuStemBase = 1000;\n'''
),
(
'''        case WM_TIMER: {\n            const bool viewChanged = update_follow_view();\n            if (spectral_waveform::live_output_capture::animation_active()) {\n''',
'''        case WM_TIMER: {\n            bool viewChanged = update_release_glide();\n            if (!viewChanged) viewChanged = update_follow_view();\n            if (spectral_waveform::live_output_capture::animation_active()) {\n'''
),
(
'''        case WM_CAPTURECHANGED:\n            m_dragging = false;\n            m_dragMoved = false;\n            m_centerScrubbing = false;\n            return 0;\n''',
'''        case WM_CAPTURECHANGED:\n            m_dragging = false;\n            m_dragMoved = false;\n            m_centerScrubbing = false;\n            return 0;\n'''
),
(
'''    void reset_view() {\n        m_viewStart = 0.0;\n        m_viewSpan = 1.0;\n        m_followPlayhead = false;\n    }\n''',
'''    void reset_view() {\n        m_releaseGlideActive = false;\n        m_viewStart = 0.0;\n        m_viewSpan = 1.0;\n        m_followPlayhead = false;\n    }\n'''
),
(
'''    void recenter_on_playhead() {\n        center_on_playhead_once();\n    }\n\n    bool update_follow_view() {\n        if (!m_followPlayhead || m_viewSpan >= 0.9995 || m_dragging || m_wnd == nullptr) return false;\n''',
'''    void recenter_on_playhead() {\n        m_releaseGlideActive = false;\n        center_on_playhead_once();\n    }\n\n    void begin_release_glide(double targetPosition) {\n        if (!m_followPlayhead || m_followMode != follow_mode::centered ||\n            m_viewSpan >= 0.9995 || m_wnd == nullptr) {\n            m_releaseGlideActive = false;\n            return;\n        }\n\n        m_releaseGlideActive = true;\n        m_releaseGlideStartTick = GetTickCount64();\n        m_releaseGlideStartView = m_viewStart;\n        m_releaseGlideTargetPosition = std::clamp(targetPosition, 0.0, 1.0);\n        invalidate_frame();\n    }\n\n    double release_glide_position() const {\n        if (!m_releaseGlideActive) return -1.0;\n        auto pc = playback_control::get();\n        const double length = pc->playback_get_length_ex();\n        if (length <= 0.0) return m_releaseGlideTargetPosition;\n\n        const ULONGLONG elapsedMs = GetTickCount64() - m_releaseGlideStartTick;\n        const double advance = pc->is_playing()\n            ? (static_cast<double>(elapsedMs) / 1000.0) / length\n            : 0.0;\n        return std::clamp(m_releaseGlideTargetPosition + advance, 0.0, 1.0);\n    }\n\n    bool update_release_glide() {\n        if (!m_releaseGlideActive || m_dragging || m_wnd == nullptr ||\n            m_viewSpan >= 0.9995 || !m_followPlayhead ||\n            m_followMode != follow_mode::centered) {\n            m_releaseGlideActive = false;\n            return false;\n        }\n\n        const ULONGLONG now = GetTickCount64();\n        const ULONGLONG elapsedMs = now - m_releaseGlideStartTick;\n        const double t = std::clamp(\n            static_cast<double>(elapsedMs) / static_cast<double>(kReleaseGlideDurationMs),\n            0.0, 1.0);\n        // Smoothstep-like cubic ease-out: fast enough to feel responsive, with a\n        // soft landing as normal centered follow resumes.\n        const double eased = 1.0 - std::pow(1.0 - t, 3.0);\n        const double virtualPosition = release_glide_position();\n        if (virtualPosition < 0.0) {\n            m_releaseGlideActive = false;\n            return false;\n        }\n\n        double targetView = virtualPosition - m_viewSpan * 0.5;\n        targetView = std::clamp(targetView, 0.0, 1.0 - m_viewSpan);\n        const double oldStart = m_viewStart;\n        m_viewStart = m_releaseGlideStartView +\n            (targetView - m_releaseGlideStartView) * eased;\n        m_viewStart = std::clamp(m_viewStart, 0.0, 1.0 - m_viewSpan);\n\n        if (t >= 1.0) m_releaseGlideActive = false;\n        if (std::abs(m_viewStart - oldStart) > 1.0e-10 || t < 1.0) {\n            invalidate_frame();\n            return true;\n        }\n        return false;\n    }\n\n    bool update_follow_view() {\n        if (m_releaseGlideActive || !m_followPlayhead || m_viewSpan >= 0.9995 ||\n            m_dragging || m_wnd == nullptr) return false;\n'''
),
(
'''        if (m_centerScrubbing) {\n            const double anchor = std::clamp(m_scrubAnchorX, 0.0, 1.0);\n            return static_cast<int>(std::lround(anchor * std::max(0, width - 1)));\n        }\n\n        auto pc = playback_control::get();\n''',
'''        if (m_centerScrubbing) {\n            const double anchor = std::clamp(m_scrubAnchorX, 0.0, 1.0);\n            return static_cast<int>(std::lround(anchor * std::max(0, width - 1)));\n        }\n\n        if (m_releaseGlideActive) {\n            const double positionFrac = release_glide_position();\n            if (positionFrac < 0.0 || positionFrac < m_viewStart || positionFrac > view_end()) return -1;\n            const double visibleFrac = (positionFrac - m_viewStart) / m_viewSpan;\n            return static_cast<int>(std::lround(visibleFrac * std::max(0, width - 1)));\n        }\n\n        auto pc = playback_control::get();\n'''
),
(
'''    void begin_drag(int x) {\n        if (m_wnd == nullptr) return;\n        m_dragging = true;\n''',
'''    void begin_drag(int x) {\n        if (m_wnd == nullptr) return;\n        m_releaseGlideActive = false;\n        m_dragging = true;\n'''
),
(
'''                if (pc->is_playing() && pc->playback_can_seek() && length > 0.0) {\n                    pc->playback_seek(m_scrubTargetPosition * length);\n                }\n                if (m_followPlayhead) recenter_on_playhead();\n''',
'''                if (pc->is_playing() && pc->playback_can_seek() && length > 0.0) {\n                    pc->playback_seek(m_scrubTargetPosition * length);\n                }\n                // Do not immediately query playback_get_position() here. Right\n                // after a seek it can briefly report the preceding audition\n                // position, which caused the visible release snap. Use the exact\n                // scrub target for a short visual handoff, then resume follow.\n                if (m_followPlayhead) begin_release_glide(m_scrubTargetPosition);\n'''
),
(
'''        if (m_centerScrubbing) {\n            invalidate_playhead();\n            return;\n        }\n        if (m_followPlayhead) recenter_on_playhead();\n''',
'''        if (m_centerScrubbing || m_releaseGlideActive) {\n            invalidate_playhead();\n            return;\n        }\n        if (m_followPlayhead) recenter_on_playhead();\n'''
),
(
'''    double m_scrubStartPosition = 0.0;\n    double m_scrubTargetPosition = 0.0;\n    double m_scrubAnchorX = 0.5;\n\n    HDC m_waveDC = nullptr;\n''',
'''    double m_scrubStartPosition = 0.0;\n    double m_scrubTargetPosition = 0.0;\n    double m_scrubAnchorX = 0.5;\n    bool m_releaseGlideActive = false;\n    ULONGLONG m_releaseGlideStartTick = 0;\n    double m_releaseGlideStartView = 0.0;\n    double m_releaseGlideTargetPosition = 0.0;\n\n    HDC m_waveDC = nullptr;\n'''
),
]

for old, new in replacements:
    if old not in s:
        raise SystemExit('Patch anchor not found:\n' + old[:220])
    s = s.replace(old, new, 1)

path.write_text(s, encoding='utf-8')
