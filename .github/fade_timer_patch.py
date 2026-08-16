from pathlib import Path

path = Path('spectral_ui.cpp')
s = path.read_text(encoding='utf-8')
old = '''        case WM_TIMER:\n            if (!update_follow_view()) invalidate_playhead();\n            return 0;\n'''
new = '''        case WM_TIMER: {\n            const bool viewChanged = update_follow_view();\n            if (spectral_waveform::live_output_capture::animation_active()) {\n                // Progressive stem blocks dissolve into place for a few frames.\n                // Rebuild the bitmap only while that short visual transition runs.\n                invalidate_all();\n            } else if (!viewChanged) {\n                invalidate_playhead();\n            }\n            return 0;\n        }\n'''
if old not in s:
    raise SystemExit('WM_TIMER patch anchor not found')
s = s.replace(old, new, 1)
path.write_text(s, encoding='utf-8')
