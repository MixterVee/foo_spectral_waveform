from pathlib import Path

p = Path('spectral_ui.cpp')
s = p.read_text(encoding='utf-8')

old = '''    void update_scrub_motion_gate() {
        if (!m_centerScrubbing || !m_dragging || !m_dragMoved ||
            !m_scrubAudibleActive) return;

        const ULONGLONG now = GetTickCount64();
        if (now - m_scrubLastMotionTick <= kScrubMotionTailMs) {
            // Keepalive only. The matching Stem Separator build detects an
            // unchanged target and extends audibility without rewinding the
            // preview cursor back to the same sample every timer tick.
            if (!set_transport_scrub(m_scrubTargetPosition)) {
                m_scrubAudibleActive = false;
            }
            return;
        }

        // No real mouse movement recently: return to the stationary platter
        // state immediately instead of waiting for an internal audio timeout.
        set_transport_hold(m_scrubTargetPosition);
        m_scrubAudibleActive = false;
    }
'''
new = '''    void update_scrub_motion_gate() {
        if (!m_centerScrubbing || !m_dragging || !m_dragMoved ||
            !m_scrubAudibleActive) return;

        const ULONGLONG now = GetTickCount64();
        if (now - m_scrubLastMotionTick <= kScrubMotionTailMs) {
            // Actual WM_MOUSEMOVE events already send SCRUB targets. Do not send
            // unchanged keepalives here: the matching Stem build reserves one
            // unchanged target as the explicit soft-idle signal below.
            return;
        }

        // The hand stopped while the platter is still physically grabbed. Keep
        // transport in SCRUB and send one unchanged target so Stem can silence its
        // renderer without seeking/flush. A hard HOLD seek here used to discard
        // short scratch audio that was still queued in foobar's output pipeline.
        if (!set_transport_scrub(m_scrubTargetPosition)) {
            // Compatibility fallback for a missing transport service.
            set_transport_hold(m_scrubTargetPosition);
        }
        m_scrubAudibleActive = false;
    }
'''
if old not in s:
    raise SystemExit('update_scrub_motion_gate anchor not found')
s = s.replace(old, new, 1)

p.write_text(s, encoding='utf-8')
print('patched Spectral soft scrub idle gate')
