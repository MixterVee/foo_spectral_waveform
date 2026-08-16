from pathlib import Path
p = Path('spectral_ui.cpp')
s = p.read_text(encoding='utf-8')

old = '''    kMenuHoldPlayback,\n    kMenuShowTimeMarkers,\n'''
new = '''    kMenuHoldPlayback,\n    kMenuReversePlayback,\n    kMenuShowTimeMarkers,\n'''
if old not in s:
    raise SystemExit('menu enum anchor not found')
s = s.replace(old, new, 1)

old = '''        {\n            const bool canHold = m_touchHoldLatched || touch_hold_can_start();\n            AppendMenuW(menu, MF_STRING | (canHold ? 0 : MF_GRAYED) |\n                (m_touchHoldLatched ? MF_CHECKED : 0),\n                kMenuHoldPlayback,\n                m_touchHoldLatched ? L"Release Playback\\tH" : L"Hold Playback\\tH");\n        }\n        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);\n        AppendMenuW(menu, MF_STRING | (g_showTimeMarkers.get() ? MF_CHECKED : 0),\n'''
new = '''        {\n            const bool canHold = m_touchHoldLatched || touch_hold_can_start();\n            AppendMenuW(menu, MF_STRING | (canHold ? 0 : MF_GRAYED) |\n                (m_touchHoldLatched ? MF_CHECKED : 0),\n                kMenuHoldPlayback,\n                m_touchHoldLatched ? L"Release Playback\\tH" : L"Hold Playback\\tH");\n        }\n        {\n            const bool canReverse = reverse_active() || touch_hold_can_start();\n            const bool reversing = reverse_active();\n            UINT flags = MF_STRING | (canReverse ? 0 : MF_GRAYED);\n            if (m_reverseLatched) flags |= MF_CHECKED;\n            AppendMenuW(menu, flags, kMenuReversePlayback,\n                reversing ? L"Release Reverse\\tR / Shift+R" : L"Reverse Playback\\tShift+R");\n        }\n        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);\n        AppendMenuW(menu, MF_STRING | (g_showTimeMarkers.get() ? MF_CHECKED : 0),\n'''
if old not in s:
    raise SystemExit('menu insertion anchor not found')
s = s.replace(old, new, 1)

old = '''        case kMenuHoldPlayback:\n            toggle_touch_hold();\n            break;\n        case kMenuShowTimeMarkers:\n'''
new = '''        case kMenuHoldPlayback:\n            toggle_touch_hold();\n            break;\n        case kMenuReversePlayback:\n            if (reverse_active()) end_reverse_transport();\n            else begin_reverse_transport(true);\n            break;\n        case kMenuShowTimeMarkers:\n'''
if old not in s:
    raise SystemExit('menu switch anchor not found')
s = s.replace(old, new, 1)

p.write_text(s, encoding='utf-8')
print('Reverse menu patched')
