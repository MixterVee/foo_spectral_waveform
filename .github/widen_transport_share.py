from pathlib import Path

p = Path('stem_waveform_analysis.cpp')
s = p.read_text(encoding='utf-8')

old = '''constexpr double kStemBlockSeconds = 5.0;\nconstexpr double kStemContextSeconds = 3.0;\nconstexpr double kPriorityAheadSeconds = 20.0;'''
new = '''constexpr double kStemBlockSeconds = 5.0;\nconstexpr double kStemContextSeconds = 3.0;\n// Share an extra 1.5 seconds on each side of the visible 5-second target.\n// That leaves at least 1.5 seconds between transport PCM and the artificial\n// Spleeter context edge while giving adjacent transport tiles a 3-second overlap.\nconstexpr double kTransportShareContextSeconds = 1.5;\nconstexpr double kPriorityAheadSeconds = 20.0;'''
assert old in s
s = s.replace(old, new, 1)

old = '''        size_t trimStartFrames,\n        size_t targetFrames,\n        double targetStartSeconds) -> bool {'''
new = '''        size_t trimStartFrames,\n        size_t targetFrames,\n        double targetStartSeconds,\n        size_t transportStartFrames,\n        size_t transportFrames,\n        double transportStartSeconds) -> bool {'''
assert old in s
s = s.replace(old, new, 1)

old = '''        const size_t trimSample = trimStartFrames * channels;\n\n        if (!transport.is_empty()) {\n            try {\n                transport->publish_cache_block(\n                    track->get_path(),\n                    targetStartSeconds,\n                    pcm + trimSample,\n                    vocals.data() + trimSample,\n                    instrumental.data() + trimSample,\n                    static_cast<t_size>(targetFrames),\n                    channels,\n                    sampleRate);\n            } catch (...) {\n                // Transport sharing is an optimization. Never let an older or\n                // unavailable Stem Separator interfere with waveform analysis.\n            }\n        }'''
new = '''        const size_t trimSample = trimStartFrames * channels;\n\n        // The display still uses only the artifact-free center target, but the\n        // transport cache can safely reuse more of the already separated context.\n        // Adjacent 5-second targets therefore publish overlapping PCM without any\n        // additional Spleeter inference.\n        transportStartFrames = std::min(transportStartFrames, frames);\n        if (transportStartFrames < frames) {\n            transportFrames = std::min(transportFrames, frames - transportStartFrames);\n        } else {\n            transportFrames = 0;\n        }\n\n        if (!transport.is_empty() && transportFrames > 0) {\n            try {\n                const size_t transportSample = transportStartFrames * channels;\n                transport->publish_cache_block(\n                    track->get_path(),\n                    transportStartSeconds,\n                    pcm + transportSample,\n                    vocals.data() + transportSample,\n                    instrumental.data() + transportSample,\n                    static_cast<t_size>(transportFrames),\n                    channels,\n                    sampleRate);\n            } catch (...) {\n                // Transport sharing is an optimization. Never let an older or\n                // unavailable Stem Separator interfere with waveform analysis.\n            }\n        }'''
assert old in s
s = s.replace(old, new, 1)

old = '''        return separate_and_merge(\n            pcm.data(), decodedFrames, trimStartFrames, targetFrames, targetStart);'''
new = '''        const double transportStart = std::max(\n            contextStart, targetStart - kTransportShareContextSeconds);\n        const double transportEnd = std::min(\n            contextEnd, targetEnd + kTransportShareContextSeconds);\n\n        size_t transportStartFrames = static_cast<size_t>(std::max<double>(\n            0.0,\n            std::llround((transportStart - contextStart) * static_cast<double>(sampleRate))));\n        transportStartFrames = std::min(transportStartFrames, decodedFrames);\n\n        size_t transportFrames = 0;\n        if (transportStartFrames < decodedFrames && transportEnd > transportStart) {\n            transportFrames = static_cast<size_t>(std::max<double>(\n                1.0,\n                std::llround((transportEnd - transportStart) * static_cast<double>(sampleRate))));\n            transportFrames = std::min(transportFrames, decodedFrames - transportStartFrames);\n        }\n\n        const double actualTransportStart = contextStart +\n            static_cast<double>(transportStartFrames) / static_cast<double>(sampleRate);\n\n        return separate_and_merge(\n            pcm.data(), decodedFrames, trimStartFrames, targetFrames, targetStart,\n            transportStartFrames, transportFrames, actualTransportStart);'''
assert old in s
s = s.replace(old, new, 1)

old = '''                if (!separate_and_merge(\n                        pending.data(), blockFrames, 0, blockFrames, pendingStartSeconds)) return false;'''
new = '''                if (!separate_and_merge(\n                        pending.data(), blockFrames, 0, blockFrames, pendingStartSeconds,\n                        0, blockFrames, pendingStartSeconds)) return false;'''
assert old in s
s = s.replace(old, new, 1)

old = '''            if (!separate_and_merge(\n                    pending.data(), remainFrames, 0, remainFrames, pendingStartSeconds)) return false;'''
new = '''            if (!separate_and_merge(\n                    pending.data(), remainFrames, 0, remainFrames, pendingStartSeconds,\n                    0, remainFrames, pendingStartSeconds)) return false;'''
assert old in s
s = s.replace(old, new, 1)

p.write_text(s, encoding='utf-8')
