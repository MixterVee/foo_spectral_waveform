from pathlib import Path

p = Path('spectral_ui.cpp')
text = p.read_text(encoding='utf-8')

def once(old, new, label):
    global text
    n = text.count(old)
    if n != 1:
        raise SystemExit(f'{label}: expected 1 match, found {n}')
    text = text.replace(old, new, 1)

once(
'''static const GUID guid_spectral_waveform_ui =
{ 0x8a3fe0d1, 0x62dc, 0x4bf2, { 0x9a, 0x72, 0x56, 0x37, 0x42, 0x2c, 0xb1, 0x91 } };
''',
'''static const GUID guid_spectral_waveform_ui =
{ 0x8a3fe0d1, 0x62dc, 0x4bf2, { 0x9a, 0x72, 0x56, 0x37, 0x42, 0x2c, 0xb1, 0x91 } };

static const GUID guid_show_time_markers =
{ 0x6d9a14c3, 0x2f87, 0x4ca5, { 0xa7, 0xe1, 0x31, 0x7b, 0x93, 0x04, 0xd6, 0x52 } };
static cfg_bool g_showTimeMarkers(guid_show_time_markers, true);
''',
'config guid')

once(
'''    kMenuFollowPaged,
    kMenuReanalyze,
''',
'''    kMenuFollowPaged,
    kMenuShowTimeMarkers,
    kMenuReanalyze,
''',
'menu enum')

once(
'''        AppendMenuW(menu, MF_STRING | (m_followMode == follow_mode::paged ? MF_CHECKED : 0),
            kMenuFollowPaged, L"Follow Mode: Page at Edge");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING | (m_currentTrack.is_empty() ? MF_GRAYED : 0),
''',
'''        AppendMenuW(menu, MF_STRING | (m_followMode == follow_mode::paged ? MF_CHECKED : 0),
            kMenuFollowPaged, L"Follow Mode: Page at Edge");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING | (g_showTimeMarkers.get() ? MF_CHECKED : 0),
            kMenuShowTimeMarkers, L"Show Time Markers");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING | (m_currentTrack.is_empty() ? MF_GRAYED : 0),
''',
'menu item')

once(
'''        case kMenuFollowPaged:
            m_followMode = follow_mode::paged;
            if (m_followPlayhead) invalidate_frame();
            break;
        case kMenuReanalyze:
''',
'''        case kMenuFollowPaged:
            m_followMode = follow_mode::paged;
            if (m_followPlayhead) invalidate_frame();
            break;
        case kMenuShowTimeMarkers:
            g_showTimeMarkers.set(!g_showTimeMarkers.get());
            invalidate_frame();
            break;
        case kMenuReanalyze:
''',
'menu switch')

once(
'''            if (waveform && waveform->duration_seconds > 0.0) {
                draw_time_ruler(dc, width, height, waveform->duration_seconds);
            }
''',
'''            if (g_showTimeMarkers.get() && waveform && waveform->duration_seconds > 0.0) {
                draw_time_ruler(dc, width, height, waveform->duration_seconds);
            }
''',
'paint toggle')

p.write_text(text, encoding='utf-8')
print('Persistent Show Time Markers toggle applied.')
