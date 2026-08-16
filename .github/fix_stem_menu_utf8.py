from pathlib import Path
p = Path('spectral_ui.cpp')
s = p.read_text(encoding='utf-8')
old = '''    std::wstring out(static_cast<size_t>(count - 1), L'\\0');\n    MultiByteToWideChar(CP_UTF8, 0, text, -1, out.data(), count);\n    return out;\n'''
new = '''    std::wstring out(static_cast<size_t>(count), L'\\0');\n    MultiByteToWideChar(CP_UTF8, 0, text, -1, out.data(), count);\n    out.resize(static_cast<size_t>(count - 1));\n    return out;\n'''
if old not in s:
    raise SystemExit('UTF-8 menu conversion anchor not found')
p.write_text(s.replace(old, new, 1), encoding='utf-8')
