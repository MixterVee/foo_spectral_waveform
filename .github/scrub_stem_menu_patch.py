from pathlib import Path

ui = Path('spectral_ui.cpp')
s = ui.read_text(encoding='utf-8')

repls = [
(
'''#include "waveform_cache.h"\n#include "live_output_capture.h"\n''',
'''#include "waveform_cache.h"\n#include "live_output_capture.h"\n#include "stem_waveform_analysis.h"\n'''
),
(
'''#include <thread>\n#include <vector>\n''',
'''#include <thread>\n#include <vector>\n#include <string>\n'''
),
(
'''static constexpr ULONGLONG kScrubSeekIntervalMs = 90;\nstatic constexpr ULONGLONG kGrabClickThresholdMs = 350;\n''',
'''// Centered grab scrubbing is intentionally coalesced. Short drags make only\n// the final seek on release; longer drags audition the latest position at a\n// relaxed rate so the decoder/DSP chain does not chatter.\nstatic constexpr ULONGLONG kScrubSeekIntervalMs = 250;\nstatic constexpr ULONGLONG kGrabClickThresholdMs = 350;\nstatic constexpr UINT kMenuStemBase = 1000;\n\nenum stem_menu_command : unsigned {\n    kStemOriginal = 0,\n    kStemVocals,\n    kStemInstrumental,\n    kStemSaveVocalsWav,\n    kStemSaveInstrumentalWav,\n    kStemSaveVocalsMp3,\n    kStemSaveInstrumentalMp3,\n    kStemPrecache,\n    kStemCommandCount\n};\n\nstatic const GUID kStemCommandGuids[kStemCommandCount] = {\n    {0xa92a1001,0xd1f0,0x4ae1,{0xa0,0x11,0x31,0x10,0x42,0x00,0x00,0x01}},\n    {0xa92a1002,0xd1f0,0x4ae1,{0xa0,0x11,0x31,0x10,0x42,0x00,0x00,0x02}},\n    {0xa92a1003,0xd1f0,0x4ae1,{0xa0,0x11,0x31,0x10,0x42,0x00,0x00,0x03}},\n    {0xa92a1004,0xd1f0,0x4ae1,{0xa0,0x11,0x31,0x10,0x42,0x00,0x00,0x04}},\n    {0xa92a1005,0xd1f0,0x4ae1,{0xa0,0x11,0x31,0x10,0x42,0x00,0x00,0x05}},\n    {0xa92a1006,0xd1f0,0x4ae1,{0xa0,0x11,0x31,0x10,0x42,0x00,0x00,0x06}},\n    {0xa92a1007,0xd1f0,0x4ae1,{0xa0,0x11,0x31,0x10,0x42,0x00,0x00,0x07}},\n    {0xa92a1008,0xd1f0,0x4ae1,{0xa0,0x11,0x31,0x10,0x42,0x00,0x00,0x08}}\n};\n\nstd::wstring utf8_menu_text(const char* text) {\n    if (text == nullptr || *text == 0) return {};\n    const int count = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);\n    if (count <= 1) return {};\n    std::wstring out(static_cast<size_t>(count - 1), L'\\0');\n    MultiByteToWideChar(CP_UTF8, 0, text, -1, out.data(), count);\n    return out;\n}\n'''
),
(
'''    int current_playhead_x(int width) const {\n        if (width <= 0) return -1;\n        auto pc = playback_control::get();\n''',
'''    int current_playhead_x(int width) const {\n        if (width <= 0) return -1;\n\n        // While the user grabs a centered-follow waveform, the play position is\n        // the stationary reference and the waveform moves underneath it. Do not\n        // let intermediate playback_seek callbacks make the blue line jump.\n        if (m_centerScrubbing) {\n            const double anchor = std::clamp(m_scrubAnchorX, 0.0, 1.0);\n            return static_cast<int>(std::lround(anchor * std::max(0, width - 1)));\n        }\n\n        auto pc = playback_control::get();\n'''
),
(
'''    void show_context_menu(int screenX, int screenY) {\n''',
'''    bool resolve_stem_command(unsigned command, service_ptr_t<contextmenu_item>& item, t_uint32& index) const {\n        if (command >= kStemCommandCount) return false;\n        return menu_item_resolver::g_resolve_context_command(kStemCommandGuids[command], item, index);\n    }\n\n    std::wstring stem_command_name(unsigned command, const wchar_t* fallback) const {\n        service_ptr_t<contextmenu_item> item;\n        t_uint32 index = 0;\n        if (!resolve_stem_command(command, item, index) || m_currentTrack.is_empty()) return fallback;\n\n        metadb_handle_list data;\n        data.add_item(m_currentTrack);\n        pfc::string8 text;\n        unsigned flags = 0;\n        if (!item->item_get_display_data_root(\n                text, flags, index, data, contextmenu_item::caller_now_playing)) return fallback;\n\n        const std::wstring converted = utf8_menu_text(text.c_str());\n        return converted.empty() ? std::wstring(fallback) : converted;\n    }\n\n    void execute_stem_command(unsigned command) {\n        if (m_currentTrack.is_empty()) return;\n\n        service_ptr_t<contextmenu_item> item;\n        t_uint32 index = 0;\n        if (!resolve_stem_command(command, item, index)) return;\n\n        metadb_handle_list data;\n        data.add_item(m_currentTrack);\n        item->item_execute_simple(index, pfc::guid_null, data, contextmenu_item::caller_now_playing);\n\n        // The normal separator context menu is observed on the next playback-time\n        // callback. A menu embedded in the waveform should update immediately,\n        // including while playback is paused.\n        spectral_waveform::live_output_capture::refresh_mode();\n        invalidate_all();\n    }\n\n    void show_context_menu(int screenX, int screenY) {\n'''
),
(
'''        AppendMenuW(menu, MF_STRING | (g_showTimeMarkers.get() ? MF_CHECKED : 0),\n            kMenuShowTimeMarkers, L"Show Time Markers");\n        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);\n        AppendMenuW(menu, MF_STRING | (m_currentTrack.is_empty() ? MF_GRAYED : 0),\n            kMenuReanalyze, L"Re-analyze Current Track");\n\n        const UINT command = TrackPopupMenu(menu,\n''',
'''        AppendMenuW(menu, MF_STRING | (g_showTimeMarkers.get() ? MF_CHECKED : 0),\n            kMenuShowTimeMarkers, L"Show Time Markers");\n        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);\n\n        // Mirror Stem Separator's registered context commands rather than\n        // duplicating its DSP/export implementation in this component.\n        HMENU stemMenu = CreatePopupMenu();\n        if (stemMenu != nullptr) {\n            service_ptr_t<contextmenu_item> probe;\n            t_uint32 probeIndex = 0;\n            const bool stemAvailable = resolve_stem_command(kStemOriginal, probe, probeIndex);\n            const bool stemEnabled = stemAvailable && !m_currentTrack.is_empty();\n            const int stemMode = stemAvailable ? spectral_waveform::current_stem_mode() : -1;\n\n            auto addStem = [&](unsigned commandIndex, const wchar_t* fallback, bool checked = false) {\n                const std::wstring label = stem_command_name(commandIndex, fallback);\n                UINT flags = MF_STRING | (stemEnabled ? 0 : MF_GRAYED);\n                if (checked) flags |= MF_CHECKED;\n                AppendMenuW(stemMenu, flags, kMenuStemBase + commandIndex, label.c_str());\n            };\n\n            addStem(kStemOriginal, L"Original", stemMode == 0);\n            addStem(kStemVocals, L"Vocals", stemMode == 1);\n            addStem(kStemInstrumental, L"Instrumental", stemMode == 2);\n            AppendMenuW(stemMenu, MF_SEPARATOR, 0, nullptr);\n            addStem(kStemSaveVocalsWav, L"Save Vocals as WAV...");\n            addStem(kStemSaveInstrumentalWav, L"Save Instrumental as WAV...");\n            addStem(kStemSaveVocalsMp3, L"Save Vocals as MP3...");\n            addStem(kStemSaveInstrumentalMp3, L"Save Instrumental as MP3...");\n            AppendMenuW(stemMenu, MF_SEPARATOR, 0, nullptr);\n            addStem(kStemPrecache, L"Pre-cache at track start");\n\n            AppendMenuW(menu, MF_POPUP | (stemAvailable ? 0 : MF_GRAYED),\n                reinterpret_cast<UINT_PTR>(stemMenu), L"Stem Separator");\n            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);\n        }\n\n        AppendMenuW(menu, MF_STRING | (m_currentTrack.is_empty() ? MF_GRAYED : 0),\n            kMenuReanalyze, L"Re-analyze Current Track");\n\n        const UINT command = TrackPopupMenu(menu,\n'''
),
(
'''        DestroyMenu(menu);\n\n        switch (command) {\n''',
'''        DestroyMenu(menu);\n\n        if (command >= kMenuStemBase && command < kMenuStemBase + kStemCommandCount) {\n            execute_stem_command(command - kMenuStemBase);\n            return;\n        }\n\n        switch (command) {\n'''
),
(
'''        m_dragStartTick = GetTickCount64();\n        m_scrubLastSeekTick = 0;\n        m_centerScrubbing = false;\n''',
'''        m_dragStartTick = GetTickCount64();\n        // Do not seek immediately on the first few pixels of a grab. If the\n        // gesture is short, release performs the only seek and feels much cleaner.\n        m_scrubLastSeekTick = m_dragStartTick;\n        m_centerScrubbing = false;\n'''
),
]

for old, new in repls:
    if old not in s:
        raise SystemExit('UI patch anchor not found:\n' + old[:220])
    s = s.replace(old, new, 1)
ui.write_text(s, encoding='utf-8')

hdr = Path('live_output_capture.h')
h = hdr.read_text(encoding='utf-8')
old = '''// Clears the active previews and invalidates the current stem-analysis\n// generation so stale workers cannot recreate caches during manual recovery.\nvoid reset();\n'''
new = '''// Re-read the current Stem Separator mode and publish/select its preview now.\n// Used by controls embedded in the waveform so paused playback updates instantly.\nvoid refresh_mode();\n\n// Clears the active previews and invalidates the current stem-analysis\n// generation so stale workers cannot recreate caches during manual recovery.\nvoid reset();\n'''
if old not in h:
    raise SystemExit('Header patch anchor not found')
hdr.write_text(h.replace(old, new, 1), encoding='utf-8')

cpp = Path('live_output_capture.cpp')
c = cpp.read_text(encoding='utf-8')
old = '''    void force_mode_refresh() {\n        m_lastMode = -999;\n    }\n'''
new = '''    void refresh_now() {\n        m_lastMode = current_stem_mode();\n        const double position = std::max(0.0, playback_control::get()->playback_get_position());\n        if (g_manager) g_manager->request(m_track, m_lastMode, position);\n    }\n\n    void force_mode_refresh() {\n        m_lastMode = -999;\n    }\n'''
if old not in c:
    raise SystemExit('Observer patch anchor not found')
c = c.replace(old, new, 1)
old = '''void reset() {\n    if (g_manager) g_manager->invalidate_current();\n'''
new = '''void refresh_mode() {\n    if (g_observer) g_observer->refresh_now();\n}\n\nvoid reset() {\n    if (g_manager) g_manager->invalidate_current();\n'''
if old not in c:
    raise SystemExit('Refresh function patch anchor not found')
c = c.replace(old, new, 1)
cpp.write_text(c, encoding='utf-8')
