from pathlib import Path

p = Path('spectral_ui.cpp')
s = p.read_text(encoding='utf-8')

old = '''static constexpr ULONGLONG kScrubSeekIntervalMs = 250;\nstatic constexpr ULONGLONG kGrabClickThresholdMs = 350;'''
new = '''static constexpr ULONGLONG kScrubSeekIntervalMs = 250;\n// Keep audible jog alive briefly after the most recent real mouse movement.\n// The timer explicitly returns transport to HOLD after this quiet period, so\n// slow drags do not fall through a fixed audio timeout while the hand is moving.\nstatic constexpr ULONGLONG kScrubMotionTailMs = 220;\nstatic constexpr ULONGLONG kGrabClickThresholdMs = 350;'''
assert old in s
s = s.replace(old, new, 1)

old = '''        case WM_TIMER: {\n            update_transport_release_wait();'''
new = '''        case WM_TIMER: {\n            update_scrub_motion_gate();\n            update_transport_release_wait();'''
assert old in s
s = s.replace(old, new, 1)

old = '''            m_dragging = false;\n            m_dragMoved = false;\n            m_centerScrubbing = false;'''
new = '''            m_dragging = false;\n            m_dragMoved = false;\n            m_centerScrubbing = false;\n            m_scrubAudibleActive = false;'''
assert old in s
s = s.replace(old, new, 1)

needle = '''    void pause_for_touch_fallback() {'''
insert = '''    void update_scrub_motion_gate() {\n        if (!m_centerScrubbing || !m_dragging || !m_dragMoved ||\n            !m_scrubAudibleActive) return;\n\n        const ULONGLONG now = GetTickCount64();\n        if (now - m_scrubLastMotionTick <= kScrubMotionTailMs) {\n            // Keepalive only. The matching Stem Separator build detects an\n            // unchanged target and extends audibility without rewinding the\n            // preview cursor back to the same sample every timer tick.\n            if (!set_transport_scrub(m_scrubTargetPosition)) {\n                m_scrubAudibleActive = false;\n            }\n            return;\n        }\n\n        // No real mouse movement recently: return to the stationary platter\n        // state immediately instead of waiting for an internal audio timeout.\n        set_transport_hold(m_scrubTargetPosition);\n        m_scrubAudibleActive = false;\n    }\n\n'''
assert needle in s
s = s.replace(needle, insert + needle, 1)

old = '''        m_scrubLastSeekTick = m_dragStartTick;\n        m_centerScrubbing = false;'''
new = '''        m_scrubLastSeekTick = m_dragStartTick;\n        m_scrubLastMotionTick = m_dragStartTick;\n        m_scrubLastMotionX = x;\n        m_scrubAudibleActive = false;\n        m_centerScrubbing = false;'''
assert old in s
s = s.replace(old, new, 1)

old = '''        const int dx = x - m_dragStartX;\n        if (std::abs(dx) >= 3) {'''
new = '''        const ULONGLONG motionNow = GetTickCount64();\n        if (x != m_scrubLastMotionX) {\n            m_scrubLastMotionX = x;\n            m_scrubLastMotionTick = motionNow;\n        }\n\n        const int dx = x - m_dragStartX;\n        if (std::abs(dx) >= 3) {'''
assert old in s
s = s.replace(old, new, 1)

old = '''            if (!set_transport_scrub(m_scrubTargetPosition)) {\n                // Compatibility fallback for the pre-transport Stem Separator.\n                const ULONGLONG now = GetTickCount64();\n                if (m_scrubLastSeekTick == 0 ||\n                    now - m_scrubLastSeekTick >= kScrubSeekIntervalMs) {'''
new = '''            if (set_transport_scrub(m_scrubTargetPosition)) {\n                m_scrubAudibleActive = true;\n            } else {\n                m_scrubAudibleActive = false;\n                // Compatibility fallback for the pre-transport Stem Separator.\n                const ULONGLONG now = motionNow;\n                if (m_scrubLastSeekTick == 0 ||\n                    now - m_scrubLastSeekTick >= kScrubSeekIntervalMs) {'''
assert old in s
s = s.replace(old, new, 1)

old = '''        m_dragging = false;\n        m_dragMoved = false;\n        m_centerScrubbing = false;\n        if (GetCapture() == m_wnd) ReleaseCapture();'''
new = '''        m_dragging = false;\n        m_dragMoved = false;\n        m_centerScrubbing = false;\n        m_scrubAudibleActive = false;\n        if (GetCapture() == m_wnd) ReleaseCapture();'''
assert old in s
s = s.replace(old, new, 1)

old = '''        m_touchPauseOwned = false;\n        m_centerScrubbing = false;\n        m_reverseKeyHeld = false;'''
new = '''        m_touchPauseOwned = false;\n        m_centerScrubbing = false;\n        m_scrubAudibleActive = false;\n        m_reverseKeyHeld = false;'''
assert s.count(old) >= 2
s = s.replace(old, new, 2)

old = '''    ULONGLONG m_scrubLastSeekTick = 0;\n    double m_scrubStartPosition = 0.0;'''
new = '''    ULONGLONG m_scrubLastSeekTick = 0;\n    ULONGLONG m_scrubLastMotionTick = 0;\n    int m_scrubLastMotionX = 0;\n    bool m_scrubAudibleActive = false;\n    double m_scrubStartPosition = 0.0;'''
assert old in s
s = s.replace(old, new, 1)

p.write_text(s, encoding='utf-8')
