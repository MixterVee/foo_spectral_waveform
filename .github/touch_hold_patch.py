from pathlib import Path

path = Path('spectral_ui.cpp')
s = path.read_text(encoding='utf-8')

repls = [
(
'''    kMenuFollowCentered,\n    kMenuFollowPaged,\n    kMenuShowTimeMarkers,\n''',
'''    kMenuFollowCentered,\n    kMenuFollowPaged,\n    kMenuHoldPlayback,\n    kMenuShowTimeMarkers,\n'''
),
(
'''            case VK_SPACE:\n                if (m_viewSpan < 0.9995) {\n                    m_followPlayhead = true;\n                    recenter_on_playhead();\n                }\n                return 0;\n            case VK_UP:\n''',
'''            case VK_SPACE:\n                if (m_viewSpan < 0.9995) {\n                    m_followPlayhead = true;\n                    recenter_on_playhead();\n                }\n                return 0;\n            case 'H':\n                toggle_touch_hold();\n                return 0;\n            case VK_UP:\n'''
),
(
'''        case WM_CAPTURECHANGED:\n            m_dragging = false;\n            m_dragMoved = false;\n            m_centerScrubbing = false;\n            return 0;\n''',
'''        case WM_CAPTURECHANGED: {\n            const bool lostCenteredGrab = m_centerScrubbing;\n            m_dragging = false;\n            m_dragMoved = false;\n            m_centerScrubbing = false;\n            // If capture is lost unexpectedly (Alt-Tab, another popup, etc.),\n            // never leave a momentary platter touch holding playback paused.\n            if (lostCenteredGrab && !m_touchHoldLatched) {\n                const double target = m_scrubTargetPosition;\n                if (m_followPlayhead) begin_release_glide(target);\n                resume_touch_pause();\n            }\n            return 0;\n        }\n'''
),
(
'''    void reset_view() {\n        m_releaseGlideActive = false;\n        m_viewStart = 0.0;\n''',
'''    void reset_view() {\n        if (m_touchHoldLatched && !m_dragging) release_touch_hold();\n        m_releaseGlideActive = false;\n        m_viewStart = 0.0;\n'''
),
(
'''    bool playback_fraction(double& out) const {\n        auto pc = playback_control::get();\n        const double length = pc->playback_get_length_ex();\n        if (length <= 0.0) return false;\n        out = std::clamp(pc->playback_get_position() / length, 0.0, 1.0);\n        return true;\n    }\n\n    void center_on_playhead_once() {\n''',
'''    bool playback_fraction(double& out) const {\n        auto pc = playback_control::get();\n        const double length = pc->playback_get_length_ex();\n        if (length <= 0.0) return false;\n        out = std::clamp(pc->playback_get_position() / length, 0.0, 1.0);\n        return true;\n    }\n\n    bool touch_hold_can_start() const {\n        auto pc = playback_control::get();\n        return m_followPlayhead &&\n            m_followMode == follow_mode::centered &&\n            m_viewSpan < 0.9995 &&\n            pc->is_playing() &&\n            pc->playback_can_seek();\n    }\n\n    void pause_for_touch() {\n        auto pc = playback_control::get();\n        if (!pc->is_playing()) return;\n        if (!pc->is_paused()) {\n            // Remember ownership so we only unpause playback that *we* paused.\n            m_touchPauseOwned = true;\n            pc->pause(true);\n        }\n    }\n\n    void resume_touch_pause() {\n        auto pc = playback_control::get();\n        if (m_touchPauseOwned && pc->is_playing() && pc->is_paused()) {\n            pc->pause(false);\n        }\n        m_touchPauseOwned = false;\n    }\n\n    void release_touch_hold() {\n        if (!m_touchHoldLatched) return;\n        const double target = m_touchHoldPosition;\n        m_touchHoldLatched = false;\n        if (m_followPlayhead && m_followMode == follow_mode::centered) {\n            begin_release_glide(target);\n        }\n        resume_touch_pause();\n        invalidate_frame();\n    }\n\n    void toggle_touch_hold() {\n        // While the mouse is physically down, H only arms/disarms the latch.\n        // The platter remains paused until mouse-up either way.\n        if (m_centerScrubbing) {\n            m_touchHoldLatched = !m_touchHoldLatched;\n            if (m_touchHoldLatched) {\n                m_touchHoldPosition = m_scrubTargetPosition;\n                m_touchHoldAnchorX = m_scrubAnchorX;\n                pause_for_touch();\n            }\n            invalidate_frame();\n            return;\n        }\n\n        if (m_touchHoldLatched) {\n            release_touch_hold();\n            return;\n        }\n\n        if (!touch_hold_can_start()) return;\n        double positionFrac = 0.0;\n        if (!playback_fraction(positionFrac)) return;\n\n        m_releaseGlideActive = false;\n        m_touchHoldLatched = true;\n        m_touchHoldPosition = positionFrac;\n        m_touchHoldAnchorX = std::clamp(\n            (positionFrac - m_viewStart) / m_viewSpan, 0.0, 1.0);\n        pause_for_touch();\n        invalidate_frame();\n    }\n\n    void center_on_playhead_once() {\n'''
),
(
'''        const double advance = pc->is_playing()\n            ? (static_cast<double>(elapsedMs) / 1000.0) / length\n            : 0.0;\n''',
'''        const double advance = (pc->is_playing() && !pc->is_paused())\n            ? (static_cast<double>(elapsedMs) / 1000.0) / length\n            : 0.0;\n'''
),
(
'''    bool update_follow_view() {\n        if (m_releaseGlideActive || !m_followPlayhead || m_viewSpan >= 0.9995 ||\n            m_dragging || m_wnd == nullptr) return false;\n        if (!playback_control::get()->is_playing()) return false;\n''',
'''    bool update_follow_view() {\n        if (m_touchHoldLatched || m_releaseGlideActive || !m_followPlayhead ||\n            m_viewSpan >= 0.9995 || m_dragging || m_wnd == nullptr) return false;\n        auto pc = playback_control::get();\n        if (!pc->is_playing() || pc->is_paused()) return false;\n'''
),
(
'''        if (m_centerScrubbing) {\n            const double anchor = std::clamp(m_scrubAnchorX, 0.0, 1.0);\n            return static_cast<int>(std::lround(anchor * std::max(0, width - 1)));\n        }\n\n        if (m_releaseGlideActive) {\n''',
'''        if (m_centerScrubbing) {\n            const double anchor = std::clamp(m_scrubAnchorX, 0.0, 1.0);\n            return static_cast<int>(std::lround(anchor * std::max(0, width - 1)));\n        }\n\n        if (m_touchHoldLatched) {\n            const double anchor = std::clamp(m_touchHoldAnchorX, 0.0, 1.0);\n            return static_cast<int>(std::lround(anchor * std::max(0, width - 1)));\n        }\n\n        if (m_releaseGlideActive) {\n'''
),
(
'''        AppendMenuW(menu, MF_STRING | (m_followMode == follow_mode::paged ? MF_CHECKED : 0),\n            kMenuFollowPaged, L"Follow Mode: Page at Edge");\n        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);\n        AppendMenuW(menu, MF_STRING | (g_showTimeMarkers.get() ? MF_CHECKED : 0),\n''',
'''        AppendMenuW(menu, MF_STRING | (m_followMode == follow_mode::paged ? MF_CHECKED : 0),\n            kMenuFollowPaged, L"Follow Mode: Page at Edge");\n        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);\n        {\n            const bool canHold = m_touchHoldLatched || touch_hold_can_start();\n            AppendMenuW(menu, MF_STRING | (canHold ? 0 : MF_GRAYED) |\n                (m_touchHoldLatched ? MF_CHECKED : 0),\n                kMenuHoldPlayback,\n                m_touchHoldLatched ? L"Release Playback\\tH" : L"Hold Playback\\tH");\n        }\n        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);\n        AppendMenuW(menu, MF_STRING | (g_showTimeMarkers.get() ? MF_CHECKED : 0),\n'''
),
(
'''        case kMenuFollowPaged:\n            m_followMode = follow_mode::paged;\n            if (m_followPlayhead) invalidate_frame();\n            break;\n        case kMenuShowTimeMarkers:\n''',
'''        case kMenuFollowPaged:\n            if (m_touchHoldLatched) release_touch_hold();\n            m_followMode = follow_mode::paged;\n            if (m_followPlayhead) invalidate_frame();\n            break;\n        case kMenuHoldPlayback:\n            toggle_touch_hold();\n            break;\n        case kMenuShowTimeMarkers:\n'''
),
(
'''    void begin_drag(int x) {\n        if (m_wnd == nullptr) return;\n        m_releaseGlideActive = false;\n''',
'''    void begin_drag(int x) {\n        if (m_wnd == nullptr) return;\n        m_releaseGlideActive = false;\n'''
),
(
'''            m_centerScrubbing = true;\n            m_scrubStartPosition = positionFrac;\n            m_scrubTargetPosition = positionFrac;\n            m_scrubAnchorX = std::clamp(\n                (positionFrac - m_viewStart) / m_viewSpan, 0.0, 1.0);\n        }\n\n        SetCapture(m_wnd);\n''',
'''            m_centerScrubbing = true;\n            m_scrubStartPosition = positionFrac;\n            m_scrubTargetPosition = positionFrac;\n            m_scrubAnchorX = std::clamp(\n                (positionFrac - m_viewStart) / m_viewSpan, 0.0, 1.0);\n            m_touchHoldPosition = positionFrac;\n            m_touchHoldAnchorX = m_scrubAnchorX;\n\n            // DJ-platter semantics: touching the centered waveform stops the\n            // music immediately. Seeking while held changes the queued position;\n            // playback only resumes when the touch is released (unless latched).\n            pause_for_touch();\n        }\n\n        SetCapture(m_wnd);\n'''
),
(
'''            m_scrubTargetPosition = std::clamp(\n                m_scrubStartPosition - deltaFrac, 0.0, 1.0);\n\n            m_viewStart = m_scrubTargetPosition - m_scrubAnchorX * m_viewSpan;\n''',
'''            m_scrubTargetPosition = std::clamp(\n                m_scrubStartPosition - deltaFrac, 0.0, 1.0);\n            if (m_touchHoldLatched) {\n                m_touchHoldPosition = m_scrubTargetPosition;\n                m_touchHoldAnchorX = m_scrubAnchorX;\n            }\n\n            m_viewStart = m_scrubTargetPosition - m_scrubAnchorX * m_viewSpan;\n'''
),
(
'''    void end_drag(int x) {\n        if (!m_dragging) return;\n\n        const bool wasMoved = m_dragMoved;\n        const bool centeredScrub = m_centerScrubbing;\n        const ULONGLONG heldMs = GetTickCount64() - m_dragStartTick;\n\n        m_dragging = false;\n        m_dragMoved = false;\n        m_centerScrubbing = false;\n        if (GetCapture() == m_wnd) ReleaseCapture();\n\n        if (centeredScrub) {\n            if (wasMoved) {\n                auto pc = playback_control::get();\n                const double length = pc->playback_get_length_ex();\n                if (pc->is_playing() && pc->playback_can_seek() && length > 0.0) {\n                    pc->playback_seek(m_scrubTargetPosition * length);\n                }\n                // Do not immediately query playback_get_position() here. Right\n                // after a seek it can briefly report the preceding audition\n                // position, which caused the visible release snap. Use the exact\n                // scrub target for a short visual handoff, then resume follow.\n                if (m_followPlayhead) begin_release_glide(m_scrubTargetPosition);\n            } else if (heldMs < kGrabClickThresholdMs) {\n                // Preserve the existing quick click-to-seek action. A longer hold\n                // with no movement simply freezes centered scrolling temporarily.\n                seek_from_x(x);\n            } else if (m_followPlayhead) {\n                recenter_on_playhead();\n            }\n            return;\n        }\n\n        if (!wasMoved) seek_from_x(x);\n    }\n''',
'''    void end_drag(int x) {\n        if (!m_dragging) return;\n\n        const bool wasMoved = m_dragMoved;\n        const bool centeredScrub = m_centerScrubbing;\n        const ULONGLONG heldMs = GetTickCount64() - m_dragStartTick;\n\n        m_dragging = false;\n        m_dragMoved = false;\n        m_centerScrubbing = false;\n        if (GetCapture() == m_wnd) ReleaseCapture();\n\n        if (centeredScrub) {\n            auto pc = playback_control::get();\n            const double length = pc->playback_get_length_ex();\n            double releaseTarget = m_scrubTargetPosition;\n\n            if (!wasMoved && heldMs < kGrabClickThresholdMs) {\n                // Keep quick click-to-seek, but perform it while the platter is\n                // still paused so no transient audio leaks before mouse-up.\n                RECT rc{};\n                GetClientRect(m_wnd, &rc);\n                const int width = rc.right - rc.left;\n                if (width > 1) {\n                    const double clickX = std::clamp(\n                        static_cast<double>(x) / static_cast<double>(width - 1), 0.0, 1.0);\n                    releaseTarget = std::clamp(\n                        m_viewStart + clickX * m_viewSpan, 0.0, 1.0);\n                }\n            }\n\n            if ((wasMoved || heldMs < kGrabClickThresholdMs) &&\n                pc->is_playing() && pc->playback_can_seek() && length > 0.0) {\n                pc->playback_seek(releaseTarget * length);\n            }\n\n            m_touchHoldPosition = releaseTarget;\n            m_touchHoldAnchorX = m_scrubAnchorX;\n\n            if (m_touchHoldLatched) {\n                // H was armed: mouse-up drops capture, but the virtual platter\n                // stays stopped until H (or the context command) releases it.\n                invalidate_frame();\n            } else {\n                // Start the visual handoff before unpausing so a synchronous seek\n                // callback cannot pull the view to an older reported position.\n                if (m_followPlayhead) begin_release_glide(releaseTarget);\n                resume_touch_pause();\n            }\n            return;\n        }\n\n        if (!wasMoved) seek_from_x(x);\n    }\n'''
),
(
'''        if ((tenths % 10) == 0) {\n            if (m_followPlayhead) wsprintfW(label, L"%dx  Follow", tenths / 10);\n            else wsprintfW(label, L"%dx", tenths / 10);\n        } else {\n            if (m_followPlayhead) wsprintfW(label, L"%d.%dx  Follow", tenths / 10, tenths % 10);\n            else wsprintfW(label, L"%d.%dx", tenths / 10, tenths % 10);\n        }\n\n        SIZE textSize{};\n''',
'''        if ((tenths % 10) == 0) {\n            if (m_followPlayhead) wsprintfW(label, L"%dx  Follow", tenths / 10);\n            else wsprintfW(label, L"%dx", tenths / 10);\n        } else {\n            if (m_followPlayhead) wsprintfW(label, L"%d.%dx  Follow", tenths / 10, tenths % 10);\n            else wsprintfW(label, L"%d.%dx", tenths / 10, tenths % 10);\n        }\n        if (m_touchHoldLatched) wcscat_s(label, L"  HOLD");\n\n        SIZE textSize{};\n'''
),
(
'''    void on_playback_new_track(metadb_handle_ptr track) override { start_analysis(track); }\n    void on_playback_stop(play_control::t_stop_reason) override {\n        stop_analysis();\n        invalidate_all(false);\n    }\n    void on_playback_seek(double) override {\n''',
'''    void on_playback_new_track(metadb_handle_ptr track) override {\n        m_touchHoldLatched = false;\n        m_touchPauseOwned = false;\n        m_centerScrubbing = false;\n        start_analysis(track);\n    }\n    void on_playback_stop(play_control::t_stop_reason) override {\n        m_touchHoldLatched = false;\n        m_touchPauseOwned = false;\n        m_centerScrubbing = false;\n        stop_analysis();\n        invalidate_all(false);\n    }\n    void on_playback_seek(double) override {\n'''
),
(
'''        if (m_centerScrubbing || m_releaseGlideActive) {\n            invalidate_playhead();\n            return;\n        }\n''',
'''        if (m_centerScrubbing || m_touchHoldLatched || m_releaseGlideActive) {\n            invalidate_playhead();\n            return;\n        }\n'''
),
(
'''    void on_playback_pause(bool) override { invalidate_playhead(); }\n''',
'''    void on_playback_pause(bool paused) override {\n        // If playback is resumed elsewhere while a latched hold is idle, treat\n        // that external resume as an explicit release and keep the UI truthful.\n        if (!paused && m_touchHoldLatched && !m_centerScrubbing) {\n            m_touchHoldLatched = false;\n            m_touchPauseOwned = false;\n        }\n        invalidate_frame();\n    }\n'''
),
(
'''    double m_scrubStartPosition = 0.0;\n    double m_scrubTargetPosition = 0.0;\n    double m_scrubAnchorX = 0.5;\n    bool m_releaseGlideActive = false;\n''',
'''    double m_scrubStartPosition = 0.0;\n    double m_scrubTargetPosition = 0.0;\n    double m_scrubAnchorX = 0.5;\n    bool m_touchHoldLatched = false;\n    bool m_touchPauseOwned = false;\n    double m_touchHoldPosition = 0.0;\n    double m_touchHoldAnchorX = 0.5;\n    bool m_releaseGlideActive = false;\n'''
),
]

for old, new in repls:
    if old not in s:
        raise SystemExit('Patch anchor not found:\n' + old[:300])
    s = s.replace(old, new, 1)

path.write_text(s, encoding='utf-8')
