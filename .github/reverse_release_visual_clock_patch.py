from pathlib import Path

p = Path('spectral_ui.cpp')
s = p.read_text(encoding='utf-8')

# Constants.
s = s.replace(
    'static constexpr ULONGLONG kReleaseGlideDurationMs = 260;\n',
    'static constexpr ULONGLONG kReleaseGlideDurationMs = 260;\n'
    '// After Reverse releases, bridge from the platter clock to foobar\'s normal\n'
    '// playback clock gradually. A direct clock switch is visible as a large hop\n'
    '// at high zoom because release_wait reports a fixed transport position.\n'
    'static constexpr double kReverseReleaseMaxCatchupRate = 0.45;\n'
    'static constexpr double kReverseReleaseSettleSeconds = 0.008;\n')

# Timer ordering: bridge owns the view before normal follow can run.
old = '''            update_reverse_visual_clock();\n            update_scrub_motion_gate();\n            update_transport_release_wait();\n            bool viewChanged = update_release_glide();\n            if (!viewChanged) viewChanged = update_follow_view();'''
new = '''            update_reverse_visual_clock();\n            update_scrub_motion_gate();\n            update_transport_release_wait();\n            bool viewChanged = update_reverse_release_visual_clock();\n            if (!viewChanged) viewChanged = update_release_glide();\n            if (!viewChanged) viewChanged = update_follow_view();'''
if old not in s: raise SystemExit('timer block not found')
s = s.replace(old, new)

# playback_fraction must stay on the release visual clock until it is reconciled.
old = '''        if (reverse_active() && m_reverseVisualActive) {\n            out = std::clamp(m_reverseVisualPosition, 0.0, 1.0);\n            return true;\n        }\n\n        if (authoritative_transport_fraction(out)) return true;'''
new = '''        if (reverse_active() && m_reverseVisualActive) {\n            out = std::clamp(m_reverseVisualPosition, 0.0, 1.0);\n            return true;\n        }\n\n        if (m_reverseReleaseVisualActive) {\n            out = std::clamp(m_reverseReleaseVisualPosition, 0.0, 1.0);\n            return true;\n        }\n\n        if (authoritative_transport_fraction(out)) return true;'''
if old not in s: raise SystemExit('playback_fraction insertion point not found')
s = s.replace(old, new)

# Insert the forward release bridge before begin_reverse_transport().
marker = '''    void begin_reverse_transport(bool latched) {\n'''
bridge = '''    void start_reverse_release_visual(double positionFrac) {\n        if (!m_followPlayhead || m_followMode != follow_mode::centered ||\n            m_viewSpan >= 0.9995 || m_wnd == nullptr) {\n            m_reverseReleaseVisualActive = false;\n            return;\n        }\n\n        m_reverseReleaseVisualActive = true;\n        m_reverseReleaseVisualPosition = std::clamp(positionFrac, 0.0, 1.0);\n        m_reverseReleaseVisualLastTick = GetTickCount64();\n        m_viewStart = m_reverseReleaseVisualPosition - m_viewSpan * 0.5;\n        m_viewStart = std::clamp(m_viewStart, 0.0, 1.0 - m_viewSpan);\n        invalidate_frame();\n    }\n\n    bool update_reverse_release_visual_clock() {\n        if (!m_reverseReleaseVisualActive) return false;\n        if (!m_followPlayhead || m_followMode != follow_mode::centered ||\n            m_viewSpan >= 0.9995 || m_wnd == nullptr) {\n            m_reverseReleaseVisualActive = false;\n            return false;\n        }\n\n        auto pc = playback_control::get();\n        const double length = pc->playback_get_length_ex();\n        if (!pc->is_playing() || length <= 0.0) {\n            m_reverseReleaseVisualActive = false;\n            return false;\n        }\n\n        const ULONGLONG now = GetTickCount64();\n        if (m_reverseReleaseVisualLastTick == 0) {\n            m_reverseReleaseVisualLastTick = now;\n            return true;\n        }\n\n        const double dt = std::min(\n            0.050, static_cast<double>(now - m_reverseReleaseVisualLastTick) / 1000.0);\n        m_reverseReleaseVisualLastTick = now;\n\n        if (!pc->is_paused() && dt > 0.0) {\n            double predicted = std::clamp(\n                m_reverseReleaseVisualPosition + dt / length, 0.0, 1.0);\n\n            // While Stem Separator still reports release_wait, advance at exactly\n            // 1x instead of displaying its fixed release position. Once transport\n            // becomes normal, gently phase-lock to foobar's playback clock.\n            bool normalReady = !m_transportReleasePending;\n            auto transport = find_transport_service();\n            if (!transport.is_empty()) {\n                try {\n                    normalReady = normalReady &&\n                        transport->get_state() == stem_transport_normal;\n                } catch (...) {\n                    normalReady = !m_transportReleasePending;\n                }\n            }\n\n            if (normalReady) {\n                const double normalPosition = std::clamp(\n                    pc->playback_get_position() / length, 0.0, 1.0);\n                const double error = normalPosition - predicted;\n                const double maxCorrection =\n                    (dt * kReverseReleaseMaxCatchupRate) / length;\n                const double correction = std::clamp(\n                    error * 0.25, -maxCorrection, maxCorrection);\n                predicted = std::clamp(predicted + correction, 0.0, 1.0);\n\n                if (std::abs(normalPosition - predicted) * length <=\n                    kReverseReleaseSettleSeconds) {\n                    predicted = normalPosition;\n                    m_reverseReleaseVisualActive = false;\n                }\n            }\n\n            m_reverseReleaseVisualPosition = predicted;\n        }\n\n        m_viewStart = m_reverseReleaseVisualPosition - m_viewSpan * 0.5;\n        m_viewStart = std::clamp(m_viewStart, 0.0, 1.0 - m_viewSpan);\n        invalidate_frame();\n        return true;\n    }\n\n'''
if marker not in s: raise SystemExit('begin_reverse marker not found')
s = s.replace(marker, bridge + marker, 1)

# New reverse cancels any old release bridge.
old = '''        m_reverseReturnToHold = m_touchHoldLatched;\n        m_reverseLatched = latched;'''
new = '''        m_reverseReleaseVisualActive = false;\n        m_reverseReleaseVisualLastTick = 0;\n        m_reverseReturnToHold = m_touchHoldLatched;\n        m_reverseLatched = latched;'''
if old not in s: raise SystemExit('begin reverse state block not found')
s = s.replace(old, new, 1)

# Reverse release: start the visual forward clock after the hard audio handoff is armed.
old = '''        if (returnToHold) {\n            m_touchHoldPosition = positionFrac;\n            m_touchHoldAnchorX = 0.5;\n            set_transport_hold(positionFrac);\n        } else {\n            release_transport_to(positionFrac);\n        }\n        invalidate_frame();'''
new = '''        if (returnToHold) {\n            m_reverseReleaseVisualActive = false;\n            m_reverseReleaseVisualLastTick = 0;\n            m_touchHoldPosition = positionFrac;\n            m_touchHoldAnchorX = 0.5;\n            set_transport_hold(positionFrac);\n        } else {\n            release_transport_to(positionFrac);\n            start_reverse_release_visual(positionFrac);\n        }\n        invalidate_frame();'''
if old not in s: raise SystemExit('end reverse release block not found')
s = s.replace(old, new, 1)

# Normal follow must not fight the bridge.
old = '''        if ((m_touchHoldLatched && !reverse_active()) || m_releaseGlideActive ||\n            m_transportReleasePending || !m_followPlayhead ||'''
new = '''        if ((m_touchHoldLatched && !reverse_active()) || m_releaseGlideActive ||\n            m_reverseReleaseVisualActive || m_transportReleasePending || !m_followPlayhead ||'''
if old not in s: raise SystemExit('follow guard not found')
s = s.replace(old, new, 1)

# Keep playhead stationary at the centered platter reference during the bridge.
old = '''        if (m_releaseGlideActive) {\n            const double positionFrac = release_glide_position();'''
new = '''        if (m_reverseReleaseVisualActive) {\n            return static_cast<int>(std::lround(0.5 * std::max(0, width - 1)));\n        }\n\n        if (m_releaseGlideActive) {\n            const double positionFrac = release_glide_position();'''
if old not in s: raise SystemExit('playhead bridge insertion point not found')
s = s.replace(old, new, 1)

# Seek callbacks during bridge cannot recenter.
old = '''        if (m_centerScrubbing || m_touchHoldLatched || reverse_active() ||\n            m_transportReleasePending || m_releaseGlideActive) {'''
new = '''        if (m_centerScrubbing || m_touchHoldLatched || reverse_active() ||\n            m_reverseReleaseVisualActive || m_transportReleasePending || m_releaseGlideActive) {'''
if old not in s: raise SystemExit('seek callback guard not found')
s = s.replace(old, new, 1)

# Reset bridge on new track and stop.
old = '''        m_reverseVisualActive = false;\n        m_reverseVisualLastTick = 0;\n        m_transportReleasePending = false;'''
new = '''        m_reverseVisualActive = false;\n        m_reverseVisualLastTick = 0;\n        m_reverseReleaseVisualActive = false;\n        m_reverseReleaseVisualLastTick = 0;\n        m_transportReleasePending = false;'''
if s.count(old) < 2: raise SystemExit('reset blocks not found twice')
s = s.replace(old, new, 2)

# Member fields.
old = '''    bool m_reverseVisualActive = false;\n    ULONGLONG m_reverseVisualLastTick = 0;\n    double m_reverseVisualPosition = 0.0;\n    bool m_transportReleasePending = false;'''
new = '''    bool m_reverseVisualActive = false;\n    ULONGLONG m_reverseVisualLastTick = 0;\n    double m_reverseVisualPosition = 0.0;\n    bool m_reverseReleaseVisualActive = false;\n    ULONGLONG m_reverseReleaseVisualLastTick = 0;\n    double m_reverseReleaseVisualPosition = 0.0;\n    bool m_transportReleasePending = false;'''
if old not in s: raise SystemExit('member insertion point not found')
s = s.replace(old, new, 1)

p.write_text(s, encoding='utf-8')
print('patched reverse release visual clock')
