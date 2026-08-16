from pathlib import Path

p = Path('spectral_ui.cpp')
s = p.read_text(encoding='utf-8')

old = '''        auto transport = find_transport_service();\n        if (!transport.is_empty()) {\n            const double seconds = positionFrac * length;\n            bool ready = true;\n            const bool wasPaused = pc->is_paused();\n            bool pauseOwned = false;'''
new = '''        auto transport = find_transport_service();\n        if (!transport.is_empty()) {\n            const double seconds = positionFrac * length;\n\n            // Arm the visual/seek guard before playback_seek(). The seek callback\n            // can be delivered during the handoff; without this, centered follow\n            // briefly recenters from the normal playback clock and creates a\n            // large-looking hop when zoomed in.\n            m_transportReleaseTarget = positionFrac;\n            m_transportReleasePending = true;\n\n            bool ready = true;\n            const bool wasPaused = pc->is_paused();\n            bool pauseOwned = false;'''
if old not in s:
    raise SystemExit('release prearm block not found')
s = s.replace(old, new)

old = '''            m_transportReleaseTarget = positionFrac;\n            m_transportReleasePending = !ready;\n\n            if (!ready) {'''
new = '''            // Keep m_transportReleasePending armed even when the target is already\n            // ready. The next UI timer tick clears it after all seek callbacks from\n            // this handoff have had a chance to arrive.\n            if (!ready) {'''
if old not in s:
    raise SystemExit('release pending assignment block not found')
s = s.replace(old, new)

old = '''        if ((m_touchHoldLatched && !reverse_active()) || m_releaseGlideActive || !m_followPlayhead ||\n            m_viewSpan >= 0.9995 || m_dragging || m_wnd == nullptr) return false;'''
new = '''        if ((m_touchHoldLatched && !reverse_active()) || m_releaseGlideActive ||\n            m_transportReleasePending || !m_followPlayhead ||\n            m_viewSpan >= 0.9995 || m_dragging || m_wnd == nullptr) return false;'''
if old not in s:
    raise SystemExit('update_follow_view guard not found')
s = s.replace(old, new)

old = '''        if (m_releaseGlideActive) {\n            const double positionFrac = release_glide_position();\n            if (positionFrac < 0.0 || positionFrac < m_viewStart || positionFrac > view_end()) return -1;\n            const double visibleFrac = (positionFrac - m_viewStart) / m_viewSpan;\n            return static_cast<int>(std::lround(visibleFrac * std::max(0, width - 1)));\n        }\n\n        auto pc = playback_control::get();'''
new = '''        if (m_releaseGlideActive) {\n            const double positionFrac = release_glide_position();\n            if (positionFrac < 0.0 || positionFrac < m_viewStart || positionFrac > view_end()) return -1;\n            const double visibleFrac = (positionFrac - m_viewStart) / m_viewSpan;\n            return static_cast<int>(std::lround(visibleFrac * std::max(0, width - 1)));\n        }\n\n        if (m_transportReleasePending) {\n            const double positionFrac = std::clamp(m_transportReleaseTarget, 0.0, 1.0);\n            if (positionFrac < m_viewStart || positionFrac > view_end()) return -1;\n            const double visibleFrac = (positionFrac - m_viewStart) / m_viewSpan;\n            return static_cast<int>(std::lround(visibleFrac * std::max(0, width - 1)));\n        }\n\n        auto pc = playback_control::get();'''
if old not in s:
    raise SystemExit('current_playhead release guard insertion point not found')
s = s.replace(old, new)

p.write_text(s, encoding='utf-8')
print('patched release visual guard')
