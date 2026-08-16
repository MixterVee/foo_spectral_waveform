from pathlib import Path

p = Path('stem_waveform_analysis.cpp')
s = p.read_text(encoding='utf-8')

def rep(old, new):
    global s
    if old not in s:
        raise SystemExit('anchor not found:\n' + old[:300])
    s = s.replace(old, new, 1)

rep('''#include "stem_waveform_analysis.h"\n#include "stem_waveform_provider.h"\n''', '''#include "stem_waveform_analysis.h"\n#include "stem_waveform_provider.h"\n#include "stem_transport_service.h"\n''')

rep('''stem_waveform_provider::ptr find_provider() {\n    stem_waveform_provider::ptr provider;\n    auto e = stem_waveform_provider::enumerate();\n    if (!e.first(provider)) provider.release();\n    return provider;\n}\n\nvoid merge_block(\n''', '''stem_waveform_provider::ptr find_provider() {\n    stem_waveform_provider::ptr provider;\n    auto e = stem_waveform_provider::enumerate();\n    if (!e.first(provider)) provider.release();\n    return provider;\n}\n\nstem_transport_service::ptr find_transport_service() {\n    stem_transport_service::ptr service;\n    auto e = stem_transport_service::enumerate();\n    if (!e.first(service)) service.release();\n    return service;\n}\n\nvoid merge_block(\n''')

rep('''    auto provider = find_provider();\n    if (provider.is_empty()) {\n        console::print("foo_spectral_waveform: Stem Waveform Provider was not found.");\n        return false;\n    }\n\n    // Seeking remains enabled so uncached stem waveforms can begin around the\n''', '''    auto provider = find_provider();\n    if (provider.is_empty()) {\n        console::print("foo_spectral_waveform: Stem Waveform Provider was not found.");\n        return false;\n    }\n    // Optional companion service. New Stem Separator builds expose this so the\n    // exact PCM that creates each progressive waveform block can also become\n    // immediately available to audible jog/reverse transport.\n    auto transport = find_transport_service();\n\n    // Seeking remains enabled so uncached stem waveforms can begin around the\n''')

rep('''        const size_t trimSample = trimStartFrames * channels;\n\n        spectral_analyzer vocalsAnalyzer(sampleRate, channels);\n''', '''        const size_t trimSample = trimStartFrames * channels;\n\n        if (!transport.is_empty()) {\n            try {\n                transport->publish_cache_block(\n                    track->get_path(),\n                    targetStartSeconds,\n                    pcm + trimSample,\n                    vocals.data() + trimSample,\n                    instrumental.data() + trimSample,\n                    static_cast<t_size>(targetFrames),\n                    channels,\n                    sampleRate);\n            } catch (...) {\n                // Transport sharing is an optimization. Never let an older or\n                // unavailable Stem Separator interfere with waveform analysis.\n            }\n        }\n\n        spectral_analyzer vocalsAnalyzer(sampleRate, channels);\n''')

p.write_text(s, encoding='utf-8')
print('Spectral transport PCM publishing patch applied')
