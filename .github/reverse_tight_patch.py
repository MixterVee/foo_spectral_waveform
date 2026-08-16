from pathlib import Path

p = Path('spectral_ui.cpp')
s = p.read_text(encoding='utf-8')

s = s.replace(
'''static constexpr double kReverseVisualLeadSeconds = 0.140;\nstatic constexpr double kReverseVisualCatchupSeconds = 0.060;\nstatic constexpr double kReverseVisualMaxLagSeconds = 0.180;''',
'''// Keep enough extrapolation headroom to bridge coarse DSP callbacks without\n// freezing the reverse waveform between blocks. Visual catch-up is speed-limited\n// below, so a late authoritative update cannot create a visible jump.\nstatic constexpr double kReverseVisualLeadSeconds = 0.300;\nstatic constexpr double kReverseVisualMaxSpeed = 1.75;''')

old_visual = '''        // Advance at 1x every UI frame. If a newly confirmed DSP position is a\n        // little farther back, ease toward it instead of snapping the waveform.\n        double visual = std::clamp(\n            m_reverseVisualPosition - dt / length, 0.0, 1.0);\n        if (target < visual && dt > 0.0) {\n            const double alpha = 1.0 - std::exp(-dt / kReverseVisualCatchupSeconds);\n            visual += (target - visual) * alpha;\n        }\n\n        // Never display audio earlier than the bounded extrapolated target. Once\n        // confirmed progress is stale for 140 ms, this clamp freezes the waveform.\n        visual = std::max(visual, target);\n\n        // Conversely, do not let a coarse authoritative jump leave the graphic\n        // hundreds of milliseconds behind the audio while it catches up.\n        visual = std::min(\n            visual, target + kReverseVisualMaxLagSeconds / length);\n\n        // Reverse motion is monotonic. Small transport jitter must not make the\n        // waveform briefly move forward.\n        visual = std::min(visual, m_reverseVisualPosition);\n        m_reverseVisualPosition = std::clamp(visual, 0.0, 1.0);'''

new_visual = '''        // Drive the display from a timer-rate target, but never hard-clamp to a\n        // newly confirmed DSP block. Hard clamps were the remaining visible\n        // staircase. If authority is behind the extrapolated display, simply\n        // wait for it to catch up; reverse motion must never jump forward.\n        double visual = m_reverseVisualPosition;\n        if (target < visual && dt > 0.0) {\n            const double nominal_step = dt / length;\n            const double gap = visual - target;\n            const double max_step = nominal_step * kReverseVisualMaxSpeed;\n            visual -= (std::min)(gap, max_step);\n        }\n\n        visual = (std::min)(visual, m_reverseVisualPosition);\n        m_reverseVisualPosition = std::clamp(visual, 0.0, 1.0);'''

if old_visual not in s:
    raise SystemExit('visual block not found')
s = s.replace(old_visual, new_visual)

old_begin = '''        try {\n            transport->set_reverse(positionFrac * length);\n        } catch (...) {\n            return;\n        }\n\n        m_reverseReturnToHold = m_touchHoldLatched;\n        m_reverseLatched = latched;\n        m_reverseKeyHeld = !latched;\n        m_reverseVisualActive = true;\n        m_reverseVisualPosition = positionFrac;\n        m_reverseVisualConfirmedPosition = positionFrac;\n        m_reverseVisualLastTick = GetTickCount64();\n        m_reverseVisualConfirmedTick = m_reverseVisualLastTick;\n        m_releaseGlideActive = false;\n        invalidate_frame();'''

new_begin = '''        const double seconds = positionFrac * length;\n        try {\n            transport->set_reverse(seconds);\n        } catch (...) {\n            return;\n        }\n\n        // Mark reverse active before flushing playback so the seek callback cannot\n        // recenter the normal forward playhead. With REVERSE already armed, the\n        // next decoded output is reverse/silence rather than queued forward audio.\n        m_reverseReturnToHold = m_touchHoldLatched;\n        m_reverseLatched = latched;\n        m_reverseKeyHeld = !latched;\n        m_reverseVisualActive = true;\n        m_reverseVisualPosition = positionFrac;\n        m_reverseVisualConfirmedPosition = positionFrac;\n        m_reverseVisualLastTick = GetTickCount64();\n        m_reverseVisualConfirmedTick = m_reverseVisualLastTick;\n        m_releaseGlideActive = false;\n\n        if (pc->is_playing() && !pc->is_paused() && pc->playback_can_seek()) {\n            pc->playback_seek(seconds);\n        }\n\n        invalidate_frame();'''

if old_begin not in s:
    raise SystemExit('begin reverse block not found')
s = s.replace(old_begin, new_begin)

p.write_text(s, encoding='utf-8')
print('patched spectral_ui.cpp')
