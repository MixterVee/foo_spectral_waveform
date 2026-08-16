from pathlib import Path
p = Path('spectral_ui.cpp')
s = p.read_text(encoding='utf-8')
s = s.replace('        m_reverseVisualConfirmedTick = 0;\n', '')
p.write_text(s, encoding='utf-8')
print('removed stale reverse confirmed reset references')
