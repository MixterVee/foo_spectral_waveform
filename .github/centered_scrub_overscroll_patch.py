from pathlib import Path

p = Path('spectral_ui.cpp')
s = p.read_text(encoding='utf-8')

old = '''            m_viewStart = m_scrubTargetPosition - m_scrubAnchorX * m_viewSpan;\n            clamp_view();\n'''
new = '''            // While the platter is physically grabbed, keep the selected sample\n            // under the fixed playhead even near 0:00 / end-of-track. Normal view\n            // clamping creates an invisible wall roughly half a view-width from\n            // either boundary. Temporary overscroll is rendered as blank space.\n            m_viewStart = m_scrubTargetPosition - m_scrubAnchorX * m_viewSpan;\n            const double scrubMinStart = -m_scrubAnchorX * m_viewSpan;\n            const double scrubMaxStart = 1.0 - m_scrubAnchorX * m_viewSpan;\n            m_viewStart = std::clamp(m_viewStart, scrubMinStart, scrubMaxStart);\n'''
if old not in s:
    raise SystemExit('center scrub view anchor not found')
s = s.replace(old, new, 1)

old = '''        const double leftFrac = m_viewStart + (static_cast<double>(x) / width) * m_viewSpan;\n        const double rightFrac = m_viewStart + (static_cast<double>(x + 1) / width) * m_viewSpan;\n\n        if (data.duration_seconds > 0.0) {\n            const double startSeconds = leftFrac * data.duration_seconds;\n            const double endSeconds = rightFrac * data.duration_seconds;\n            if (spectral_waveform::live_output_capture::aggregate(startSeconds, endSeconds, out)) {\n                return out;\n            }\n        }\n\n        size_t begin = static_cast<size_t>(std::floor(leftFrac * count));\n        size_t end = static_cast<size_t>(std::ceil(rightFrac * count));\n'''
new = '''        const double leftFrac = m_viewStart + (static_cast<double>(x) / width) * m_viewSpan;\n        const double rightFrac = m_viewStart + (static_cast<double>(x + 1) / width) * m_viewSpan;\n\n        // Centered platter scrubbing may temporarily overscroll beyond the file\n        // boundaries so 0:00 / EOF can stay under the fixed playhead. Anything\n        // outside the track is true blank space, not a stretched copy of the first\n        // or last waveform point.\n        if (rightFrac <= 0.0 || leftFrac >= 1.0) return out;\n        const double clippedLeftFrac = std::clamp(leftFrac, 0.0, 1.0);\n        const double clippedRightFrac = std::clamp(rightFrac, 0.0, 1.0);\n        if (clippedRightFrac <= clippedLeftFrac) return out;\n\n        if (data.duration_seconds > 0.0) {\n            const double startSeconds = clippedLeftFrac * data.duration_seconds;\n            const double endSeconds = clippedRightFrac * data.duration_seconds;\n            if (spectral_waveform::live_output_capture::aggregate(startSeconds, endSeconds, out)) {\n                return out;\n            }\n        }\n\n        size_t begin = static_cast<size_t>(std::floor(clippedLeftFrac * count));\n        size_t end = static_cast<size_t>(std::ceil(clippedRightFrac * count));\n'''
if old not in s:
    raise SystemExit('aggregate clipping anchor not found')
s = s.replace(old, new, 1)

old = '''        const double viewStartSeconds = m_viewStart * durationSeconds;\n        const double viewEndSeconds = view_end() * durationSeconds;\n        const double epsilon = step * 1.0e-8;\n        double first = std::ceil((viewStartSeconds - epsilon) / step) * step;\n'''
new = '''        const double viewStartSeconds = std::max(0.0, m_viewStart * durationSeconds);\n        const double viewEndSeconds = std::min(\n            durationSeconds, (m_viewStart + m_viewSpan) * durationSeconds);\n        if (viewEndSeconds <= viewStartSeconds) return;\n        const double epsilon = step * 1.0e-8;\n        double first = std::max(\n            0.0, std::ceil((viewStartSeconds - epsilon) / step) * step);\n'''
if old not in s:
    raise SystemExit('time ruler overscroll anchor not found')
s = s.replace(old, new, 1)

old = '''        m_viewStart = m_releaseGlideStartView +\n            (targetView - m_releaseGlideStartView) * eased;\n        m_viewStart = std::clamp(m_viewStart, 0.0, 1.0 - m_viewSpan);\n'''
new = '''        m_viewStart = m_releaseGlideStartView +\n            (targetView - m_releaseGlideStartView) * eased;\n        // The glide may begin from a temporary centered-scrub overscroll. Do not\n        // snap that negative/after-EOF view back inside the file on the first\n        // release frame; the interpolation reaches targetView naturally.\n        m_viewStart = std::clamp(m_viewStart, -m_viewSpan, 1.0);\n'''
if old not in s:
    raise SystemExit('release glide clamp anchor not found')
s = s.replace(old, new, 1)

p.write_text(s, encoding='utf-8')
print('patched centered scrub overscroll')
