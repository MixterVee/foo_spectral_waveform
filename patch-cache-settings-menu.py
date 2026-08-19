from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected 1 marker, found {count}")
    return text.replace(old, new, 1)


path = Path("spectral_ui.cpp")
text = path.read_text(encoding="utf-8-sig")

text = replace_once(
    text,
    '''enum stem_menu_command : unsigned {
    kStemOriginal = 0,
    kStemVocals,
    kStemInstrumental,
    kStemSaveVocalsWav,
    kStemSaveInstrumentalWav,
    kStemSaveVocalsMp3,
    kStemSaveInstrumentalMp3,
    kStemPrecache,
    kStemBenchmark,
    kStemCommandCount
};
''',
    '''enum stem_menu_command : unsigned {
    kStemOriginal = 0,
    kStemVocals,
    kStemInstrumental,
    kStemSaveVocalsWav,
    kStemSaveInstrumentalWav,
    kStemSaveVocalsMp3,
    kStemSaveInstrumentalMp3,
    kStemPrecache,
    kStemBenchmark,
    kStemCacheEnabled,
    kStemCacheStatus,
    kStemCacheClear,
    kStemCache2GB,
    kStemCache5GB,
    kStemCache10GB,
    kStemCache20GB,
    kStemCache50GB,
    kStemCache100GB,
    kStemCommandCount
};
''',
    "stem command enum",
)

text = replace_once(
    text,
    '''static const GUID kStemCommandGuids[kStemCommandCount] = {
    {0xa92a1001,0xd1f0,0x4ae1,{0xa0,0x11,0x31,0x10,0x42,0x00,0x00,0x01}},
    {0xa92a1002,0xd1f0,0x4ae1,{0xa0,0x11,0x31,0x10,0x42,0x00,0x00,0x02}},
    {0xa92a1003,0xd1f0,0x4ae1,{0xa0,0x11,0x31,0x10,0x42,0x00,0x00,0x03}},
    {0xa92a1004,0xd1f0,0x4ae1,{0xa0,0x11,0x31,0x10,0x42,0x00,0x00,0x04}},
    {0xa92a1005,0xd1f0,0x4ae1,{0xa0,0x11,0x31,0x10,0x42,0x00,0x00,0x05}},
    {0xa92a1006,0xd1f0,0x4ae1,{0xa0,0x11,0x31,0x10,0x42,0x00,0x00,0x06}},
    {0xa92a1007,0xd1f0,0x4ae1,{0xa0,0x11,0x31,0x10,0x42,0x00,0x00,0x07}},
    {0xa92a1008,0xd1f0,0x4ae1,{0xa0,0x11,0x31,0x10,0x42,0x00,0x00,0x08}},
    {0xa92a1010,0xd1f0,0x4ae1,{0xa0,0x11,0x31,0x10,0x42,0x00,0x00,0x10}}
};
''',
    '''static const GUID kStemCommandGuids[kStemCommandCount] = {
    {0xa92a1001,0xd1f0,0x4ae1,{0xa0,0x11,0x31,0x10,0x42,0x00,0x00,0x01}},
    {0xa92a1002,0xd1f0,0x4ae1,{0xa0,0x11,0x31,0x10,0x42,0x00,0x00,0x02}},
    {0xa92a1003,0xd1f0,0x4ae1,{0xa0,0x11,0x31,0x10,0x42,0x00,0x00,0x03}},
    {0xa92a1004,0xd1f0,0x4ae1,{0xa0,0x11,0x31,0x10,0x42,0x00,0x00,0x04}},
    {0xa92a1005,0xd1f0,0x4ae1,{0xa0,0x11,0x31,0x10,0x42,0x00,0x00,0x05}},
    {0xa92a1006,0xd1f0,0x4ae1,{0xa0,0x11,0x31,0x10,0x42,0x00,0x00,0x06}},
    {0xa92a1007,0xd1f0,0x4ae1,{0xa0,0x11,0x31,0x10,0x42,0x00,0x00,0x07}},
    {0xa92a1008,0xd1f0,0x4ae1,{0xa0,0x11,0x31,0x10,0x42,0x00,0x00,0x08}},
    {0xa92a1010,0xd1f0,0x4ae1,{0xa0,0x11,0x31,0x10,0x42,0x00,0x00,0x10}},
    {0xa92a1011,0xd1f0,0x4ae1,{0xa0,0x11,0x31,0x10,0x42,0x00,0x00,0x11}},
    {0xa92a1012,0xd1f0,0x4ae1,{0xa0,0x11,0x31,0x10,0x42,0x00,0x00,0x12}},
    {0xa92a1013,0xd1f0,0x4ae1,{0xa0,0x11,0x31,0x10,0x42,0x00,0x00,0x13}},
    {0xa92a1021,0xd1f0,0x4ae1,{0xa0,0x11,0x31,0x10,0x42,0x00,0x00,0x21}},
    {0xa92a1022,0xd1f0,0x4ae1,{0xa0,0x11,0x31,0x10,0x42,0x00,0x00,0x22}},
    {0xa92a1023,0xd1f0,0x4ae1,{0xa0,0x11,0x31,0x10,0x42,0x00,0x00,0x23}},
    {0xa92a1024,0xd1f0,0x4ae1,{0xa0,0x11,0x31,0x10,0x42,0x00,0x00,0x24}},
    {0xa92a1025,0xd1f0,0x4ae1,{0xa0,0x11,0x31,0x10,0x42,0x00,0x00,0x25}},
    {0xa92a1026,0xd1f0,0x4ae1,{0xa0,0x11,0x31,0x10,0x42,0x00,0x00,0x26}}
};
''',
    "stem command GUIDs",
)

text = replace_once(
    text,
    '''    std::wstring stem_command_name(unsigned command, const wchar_t* fallback) const {
        service_ptr_t<contextmenu_item> item;
        t_uint32 index = 0;
        if (!resolve_stem_command(command, item, index) || m_currentTrack.is_empty()) return fallback;

        metadb_handle_list data;
        data.add_item(m_currentTrack);
''',
    '''    std::wstring stem_command_name(unsigned command, const wchar_t* fallback) const {
        service_ptr_t<contextmenu_item> item;
        t_uint32 index = 0;
        if (!resolve_stem_command(command, item, index)) return fallback;

        metadb_handle_list data;
        if (!m_currentTrack.is_empty()) data.add_item(m_currentTrack);
''',
    "global cache command labels",
)

text = replace_once(
    text,
    '''    void execute_stem_command(unsigned command) {
        if (m_currentTrack.is_empty()) return;

        service_ptr_t<contextmenu_item> item;
        t_uint32 index = 0;
        if (!resolve_stem_command(command, item, index)) return;

        metadb_handle_list data;
        data.add_item(m_currentTrack);
''',
    '''    void execute_stem_command(unsigned command) {
        const bool cacheCommand = command >= kStemCacheEnabled;
        if (!cacheCommand && m_currentTrack.is_empty()) return;

        service_ptr_t<contextmenu_item> item;
        t_uint32 index = 0;
        if (!resolve_stem_command(command, item, index)) return;

        metadb_handle_list data;
        if (!m_currentTrack.is_empty()) data.add_item(m_currentTrack);
''',
    "global cache command execution",
)

text = replace_once(
    text,
    '''            addStem(kStemPrecache, L"Pre-cache at track start");
            AppendMenuW(stemMenu, MF_SEPARATOR, 0, nullptr);
            addStem(kStemBenchmark, L"Benchmark / Select Processing Backend...");
''',
    '''            addStem(kStemPrecache, L"Pre-cache at track start");
            AppendMenuW(stemMenu, MF_SEPARATOR, 0, nullptr);

            // Cache controls are global Stem Separator commands. Keep the waveform
            // as a thin menu mirror so both menus always read and change the same
            // persistent cache configuration and disk cache.
            service_ptr_t<contextmenu_item> cacheProbe;
            t_uint32 cacheProbeIndex = 0;
            const bool cacheAvailable = resolve_stem_command(
                kStemCacheEnabled, cacheProbe, cacheProbeIndex);
            HMENU cacheMenu = CreatePopupMenu();
            if (cacheMenu != nullptr) {
                auto addCache = [&](unsigned commandIndex, const wchar_t* fallback) {
                    const std::wstring label = stem_command_name(commandIndex, fallback);
                    AppendMenuW(cacheMenu,
                        MF_STRING | (cacheAvailable ? 0 : MF_GRAYED),
                        kMenuStemBase + commandIndex,
                        label.c_str());
                };

                addCache(kStemCacheEnabled, L"Persistent Cache");
                addCache(kStemCacheStatus, L"Current Cache");
                addCache(kStemCacheClear, L"Clear Stem Cache...");
                AppendMenuW(cacheMenu, MF_SEPARATOR, 0, nullptr);

                HMENU sizeMenu = CreatePopupMenu();
                if (sizeMenu != nullptr) {
                    auto addSize = [&](unsigned commandIndex, const wchar_t* fallback) {
                        const std::wstring label = stem_command_name(commandIndex, fallback);
                        AppendMenuW(sizeMenu,
                            MF_STRING | (cacheAvailable ? 0 : MF_GRAYED),
                            kMenuStemBase + commandIndex,
                            label.c_str());
                    };
                    addSize(kStemCache2GB, L"2 GB");
                    addSize(kStemCache5GB, L"5 GB");
                    addSize(kStemCache10GB, L"10 GB");
                    addSize(kStemCache20GB, L"20 GB");
                    addSize(kStemCache50GB, L"50 GB");
                    addSize(kStemCache100GB, L"100 GB");
                    AppendMenuW(cacheMenu,
                        MF_POPUP | (cacheAvailable ? 0 : MF_GRAYED),
                        reinterpret_cast<UINT_PTR>(sizeMenu),
                        L"Maximum Cache Size");
                }

                AppendMenuW(stemMenu,
                    MF_POPUP | (cacheAvailable ? 0 : MF_GRAYED),
                    reinterpret_cast<UINT_PTR>(cacheMenu),
                    L"Cache Settings");
            }

            AppendMenuW(stemMenu, MF_SEPARATOR, 0, nullptr);
            addStem(kStemBenchmark, L"Benchmark / Select Processing Backend...");
''',
    "cache settings submenu",
)

path.write_text(text, encoding="utf-8")
print("Applied waveform Stem Separator cache settings menu patch.")
