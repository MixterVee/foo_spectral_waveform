from pathlib import Path

p = Path('spectral_ui.cpp')
s = p.read_text(encoding='utf-8')

old = '''        m_reverseVisualActive = true;\n        m_reverseVisualPosition = positionFrac;\n        m_reverseVisualLastTick = GetTickCount64();\n        m_releaseGlideActive = false;\n\n        if (pc->is_playing() && !pc->is_paused() && pc->playback_can_seek()) {\n            pc->playback_seek(seconds);\n        }\n\n        invalidate_frame();'''

new = '''        m_reverseVisualActive = true;\n        m_reverseVisualPosition = positionFrac;\n        m_reverseVisualLastTick = 0;\n        m_releaseGlideActive = false;\n\n        // Hard reverse handoff: prevent any already-queued forward audio from\n        // reaching the output while the seek flush is being processed. Reverse\n        // is armed first, then playback is paused only for the flush itself.\n        if (pc->is_playing() && pc->playback_can_seek()) {\n            const bool wasPaused = pc->is_paused();\n            if (!wasPaused) pc->pause(true);\n            pc->playback_seek(seconds);\n            if (!wasPaused) pc->pause(false);\n        }\n\n        // Start the visual reverse clock only after the output handoff is done,\n        // so the display cannot run ahead during the pause/flush/unpause cycle.\n        m_reverseVisualLastTick = GetTickCount64();\n        invalidate_frame();'''

if old not in s:
    raise SystemExit('reverse flush block not found')
s = s.replace(old, new)
p.write_text(s, encoding='utf-8')
print('patched hard reverse flush')
