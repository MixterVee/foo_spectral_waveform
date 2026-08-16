from pathlib import Path

p = Path('spectral_ui.cpp')
s = p.read_text(encoding='utf-8')

old_constants = '''// Reverse audio position arrives in DSP-sized steps. Interpolate the display at
// the UI timer rate, but never run far ahead of confirmed transport progress.
// Keep enough extrapolation headroom to bridge coarse DSP callbacks without
// freezing the reverse waveform between blocks. Visual catch-up is speed-limited
// below, so a late authoritative update cannot create a visible jump.
static constexpr double kReverseVisualLeadSeconds = 0.300;
static constexpr double kReverseVisualMaxSpeed = 1.75;
'''
new_constants = '''// Reverse display runs from the UI wall clock at exactly 1x. The DSP transport
// remains responsible for audio, while release seeks to the visible platter
// position so coarse audio callbacks can never make the waveform jump.
'''
if old_constants not in s:
    raise SystemExit('reverse constants block not found')
s = s.replace(old_constants, new_constants)

start = s.index('    void update_reverse_visual_clock() {')
end = s.index('\n    void begin_reverse_transport(bool latched) {', start)
new_clock = '''    void update_reverse_visual_clock() {
        if (!reverse_active() || !m_reverseVisualActive) {
            m_reverseVisualActive = false;
            return;
        }

        auto pc = playback_control::get();
        const double length = pc->playback_get_length_ex();
        auto transport = find_transport_service();
        if (length <= 0.0 || transport.is_empty()) {
            m_reverseVisualActive = false;
            return;
        }

        try {
            if (transport->get_state() != stem_transport_reverse) {
                m_reverseVisualActive = false;
                return;
            }
        } catch (...) {
            m_reverseVisualActive = false;
            return;
        }

        const ULONGLONG now = GetTickCount64();
        if (m_reverseVisualLastTick == 0) {
            m_reverseVisualLastTick = now;
            return;
        }

        // Pure timer-rate 1x reverse. Do not chase DSP block positions here;
        // doing so was the source of the visible staircase/catch-up jumps.
        const double dt = std::min(
            0.050, static_cast<double>(now - m_reverseVisualLastTick) / 1000.0);
        m_reverseVisualLastTick = now;
        if (dt <= 0.0) return;

        m_reverseVisualPosition = std::clamp(
            m_reverseVisualPosition - dt / length, 0.0, 1.0);
    }
'''
s = s[:start] + new_clock + s[end:]

s = s.replace(
'''        m_reverseVisualActive = true;
        m_reverseVisualPosition = positionFrac;
        m_reverseVisualConfirmedPosition = positionFrac;
        m_reverseVisualLastTick = GetTickCount64();
        m_reverseVisualConfirmedTick = m_reverseVisualLastTick;
        m_releaseGlideActive = false;''',
'''        m_reverseVisualActive = true;
        m_reverseVisualPosition = positionFrac;
        m_reverseVisualLastTick = GetTickCount64();
        m_releaseGlideActive = false;''')

old_end = '''    void end_reverse_transport() {
        if (!reverse_active()) return;
        double positionFrac = 0.0;
        if (!authoritative_transport_fraction(positionFrac) &&
            !playback_fraction(positionFrac)) {
            positionFrac = m_touchHoldPosition;
        }

        const bool returnToHold = m_touchHoldLatched || m_reverseReturnToHold;
        m_reverseKeyHeld = false;
        m_reverseLatched = false;
        m_reverseReturnToHold = false;
        m_reverseVisualActive = false;

        if (returnToHold) {
            m_touchHoldPosition = positionFrac;
            m_touchHoldAnchorX = 0.5;
            set_transport_hold(positionFrac);
        } else {
            if (m_followPlayhead) begin_release_glide(positionFrac);
            release_transport_to(positionFrac);
        }
        invalidate_frame();
    }'''
new_end = '''    void end_reverse_transport() {
        if (!reverse_active()) return;

        // The visible platter is the release authority. Audio reverse and the UI
        // both run at 1x, but the DSP reports position only at block boundaries.
        // Releasing from that coarse position caused the large final catch-up
        // seen in the recording. Seek to exactly what the user sees instead.
        double positionFrac = 0.0;
        if (m_reverseVisualActive) {
            positionFrac = std::clamp(m_reverseVisualPosition, 0.0, 1.0);
        } else if (!authoritative_transport_fraction(positionFrac) &&
                   !playback_fraction(positionFrac)) {
            positionFrac = m_touchHoldPosition;
        }

        const bool returnToHold = m_touchHoldLatched || m_reverseReturnToHold;
        m_reverseKeyHeld = false;
        m_reverseLatched = false;
        m_reverseReturnToHold = false;
        m_reverseVisualActive = false;
        m_releaseGlideActive = false;

        if (returnToHold) {
            m_touchHoldPosition = positionFrac;
            m_touchHoldAnchorX = 0.5;
            set_transport_hold(positionFrac);
        } else {
            release_transport_to(positionFrac);
        }
        invalidate_frame();
    }'''
if old_end not in s:
    raise SystemExit('end reverse block not found')
s = s.replace(old_end, new_end)

s = s.replace('''    ULONGLONG m_reverseVisualLastTick = 0;
    ULONGLONG m_reverseVisualConfirmedTick = 0;
    double m_reverseVisualPosition = 0.0;
    double m_reverseVisualConfirmedPosition = 0.0;
''', '''    ULONGLONG m_reverseVisualLastTick = 0;
    double m_reverseVisualPosition = 0.0;
''')

p.write_text(s, encoding='utf-8')
print('patched spectral_ui.cpp')
