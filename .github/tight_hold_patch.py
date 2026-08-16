from pathlib import Path

p = Path('spectral_ui.cpp')
s = p.read_text(encoding='utf-8')

old = 'static constexpr ULONGLONG kScrubMotionTailMs = 220;'
new = 'static constexpr ULONGLONG kScrubMotionTailMs = 40;'
assert s.count(old) == 1, s.count(old)
s = s.replace(old, new, 1)

old = '''    bool set_transport_hold(double positionFrac) {\n        auto pc = playback_control::get();\n        const double length = pc->playback_get_length_ex();\n        auto transport = find_transport_service();\n        if (length <= 0.0 || transport.is_empty()) return false;\n        try {\n            transport->set_hold(std::clamp(positionFrac, 0.0, 1.0) * length);\n            return true;\n        } catch (...) {\n            return false;\n        }\n    }'''

new = '''    bool set_transport_hold(double positionFrac) {\n        auto pc = playback_control::get();\n        const double length = pc->playback_get_length_ex();\n        auto transport = find_transport_service();\n        if (length <= 0.0 || transport.is_empty()) return false;\n        try {\n            const double seconds =\n                std::clamp(positionFrac, 0.0, 1.0) * length;\n\n            // Arm HOLD before flushing foobar's playback pipeline. Any freshly\n            // decoded audio after the seek is therefore silence immediately.\n            // This avoids waiting for already-rendered scrub/playback samples to\n            // drain through the output buffer after H or after the hand stops.\n            transport->set_hold(seconds);\n\n            if (pc->is_playing() &&\n                !pc->is_paused() &&\n                pc->playback_can_seek()) {\n                pc->playback_seek(seconds);\n            }\n\n            return true;\n        } catch (...) {\n            return false;\n        }\n    }'''

assert s.count(old) == 1, s.count(old)
s = s.replace(old, new, 1)

p.write_text(s, encoding='utf-8')
