from pathlib import Path
p = Path('spectral_ui.cpp')
text = p.read_text(encoding='utf-8')
old = '            g_showTimeMarkers.set(!g_showTimeMarkers.get());\n'
new = '            g_showTimeMarkers = !g_showTimeMarkers.get();\n'
if text.count(old) != 1:
    raise SystemExit(f'expected one cfg_bool set call, found {text.count(old)}')
p.write_text(text.replace(old, new, 1), encoding='utf-8')
print('cfg_bool compatibility fix applied')
