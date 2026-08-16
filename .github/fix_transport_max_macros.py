from pathlib import Path
p = Path('waveform_cache.cpp')
s = p.read_text(encoding='utf-8')
s = s.replace('std::max(peak, std::abs(data[i]))', '(std::max)(peak, std::abs(data[i]))')
s = s.replace('std::max(1.0f, peak)', '(std::max)(1.0f, peak)')
s = s.replace('std::max(scale, 1.0e-20f)', '(std::max)(scale, 1.0e-20f)')
s = s.replace('priority_seconds = std::max(0.0, priority_seconds);', 'priority_seconds = (std::max)(0.0, priority_seconds);')
s = s.replace('std::numeric_limits<t_size>::max()', '(std::numeric_limits<t_size>::max)()')
p.write_text(s, encoding='utf-8')
