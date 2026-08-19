from pathlib import Path


def replace_once(path, old, new, label):
    p = Path(path)
    text = p.read_text(encoding='utf-8')
    if old not in text:
        raise RuntimeError(f'{label}: expected text not found in {path}')
    p.write_text(text.replace(old, new, 1), encoding='utf-8')


# Add the new Stem Separator command IDs. The waveform remains a pure UI mirror.
replace_once(
    'spectral_ui.cpp',
    '''    kStemOriginal = 0,\n    kStemVocals,\n    kStemInstrumental,\n    kStemSaveVocalsWav,''',
    '''    kStemOriginal = 0,\n    kStemVocals,\n    kStemInstrumental,\n    kStemBlend,\n    kStemSaveVocalsWav,''',
    'blend command enum',
)

replace_once(
    'spectral_ui.cpp',
    '''    kStemCache50GB,\n    kStemCache100GB,\n    kStemCommandCount''',
    '''    kStemCache50GB,\n    kStemCache100GB,\n    kStemBlendVocal0,\n    kStemBlendVocal25,\n    kStemBlendVocal50,\n    kStemBlendVocal75,\n    kStemBlendVocal100,\n    kStemBlendInstrumental0,\n    kStemBlendInstrumental25,\n    kStemBlendInstrumental50,\n    kStemBlendInstrumental75,\n    kStemBlendInstrumental100,\n    kStemCommandCount''',
    'blend level enums',
)

replace_once(
    'spectral_ui.cpp',
    '''    {0xa92a1003,0xd1f0,0x4ae1,{0xa0,0x11,0x31,0x10,0x42,0x00,0x00,0x03}},\n    {0xa92a1004,''',
    '''    {0xa92a1003,0xd1f0,0x4ae1,{0xa0,0x11,0x31,0x10,0x42,0x00,0x00,0x03}},\n    {0xa92a1009,0xd1f0,0x4ae1,{0xa0,0x11,0x31,0x10,0x42,0x00,0x00,0x09}},\n    {0xa92a1004,''',
    'blend command guid',
)

replace_once(
    'spectral_ui.cpp',
    '''    {0xa92a1025,0xd1f0,0x4ae1,{0xa0,0x11,0x31,0x10,0x42,0x00,0x00,0x25}},\n    {0xa92a1026,0xd1f0,0x4ae1,{0xa0,0x11,0x31,0x10,0x42,0x00,0x00,0x26}}\n};''',
    '''    {0xa92a1025,0xd1f0,0x4ae1,{0xa0,0x11,0x31,0x10,0x42,0x00,0x00,0x25}},\n    {0xa92a1026,0xd1f0,0x4ae1,{0xa0,0x11,0x31,0x10,0x42,0x00,0x00,0x26}},\n    {0xa92a1030,0xd1f0,0x4ae1,{0xa0,0x11,0x31,0x10,0x42,0x00,0x00,0x30}},\n    {0xa92a1031,0xd1f0,0x4ae1,{0xa0,0x11,0x31,0x10,0x42,0x00,0x00,0x31}},\n    {0xa92a1032,0xd1f0,0x4ae1,{0xa0,0x11,0x31,0x10,0x42,0x00,0x00,0x32}},\n    {0xa92a1033,0xd1f0,0x4ae1,{0xa0,0x11,0x31,0x10,0x42,0x00,0x00,0x33}},\n    {0xa92a1034,0xd1f0,0x4ae1,{0xa0,0x11,0x31,0x10,0x42,0x00,0x00,0x34}},\n    {0xa92a1040,0xd1f0,0x4ae1,{0xa0,0x11,0x31,0x10,0x42,0x00,0x00,0x40}},\n    {0xa92a1041,0xd1f0,0x4ae1,{0xa0,0x11,0x31,0x10,0x42,0x00,0x00,0x41}},\n    {0xa92a1042,0xd1f0,0x4ae1,{0xa0,0x11,0x31,0x10,0x42,0x00,0x00,0x42}},\n    {0xa92a1043,0xd1f0,0x4ae1,{0xa0,0x11,0x31,0x10,0x42,0x00,0x00,0x43}},\n    {0xa92a1044,0xd1f0,0x4ae1,{0xa0,0x11,0x31,0x10,0x42,0x00,0x00,0x44}}\n};''',
    'blend level guids',
)

# Add Blend to the normal mode list and mirror the two level submenus.
replace_once(
    'spectral_ui.cpp',
    '''            addStem(kStemOriginal, L"Original", stemMode == 0);\n            addStem(kStemVocals, L"Vocals", stemMode == 1);\n            addStem(kStemInstrumental, L"Instrumental", stemMode == 2);\n            AppendMenuW(stemMenu, MF_SEPARATOR, 0, nullptr);''',
    '''            addStem(kStemOriginal, L"Original", stemMode == 0);\n            addStem(kStemVocals, L"Vocals", stemMode == 1);\n            addStem(kStemInstrumental, L"Instrumental", stemMode == 2);\n            addStem(kStemBlend, L"Blend", stemMode == 3);\n\n            service_ptr_t<contextmenu_item> blendProbe;\n            t_uint32 blendProbeIndex = 0;\n            const bool blendAvailable = resolve_stem_command(\n                kStemBlendVocal0, blendProbe, blendProbeIndex);\n            HMENU blendMenu = CreatePopupMenu();\n            if (blendMenu != nullptr) {\n                auto addLevelMenu = [&](HMENU target, unsigned commandIndex, const wchar_t* fallback) {\n                    const std::wstring label = stem_command_name(commandIndex, fallback);\n                    AppendMenuW(target,\n                        MF_STRING | (blendAvailable ? 0 : MF_GRAYED),\n                        kMenuStemBase + commandIndex,\n                        label.c_str());\n                };\n\n                HMENU vocalMenu = CreatePopupMenu();\n                if (vocalMenu != nullptr) {\n                    addLevelMenu(vocalMenu, kStemBlendVocal0, L"0%");\n                    addLevelMenu(vocalMenu, kStemBlendVocal25, L"25%");\n                    addLevelMenu(vocalMenu, kStemBlendVocal50, L"50%");\n                    addLevelMenu(vocalMenu, kStemBlendVocal75, L"75%");\n                    addLevelMenu(vocalMenu, kStemBlendVocal100, L"100%");\n                    AppendMenuW(blendMenu,\n                        MF_POPUP | (blendAvailable ? 0 : MF_GRAYED),\n                        reinterpret_cast<UINT_PTR>(vocalMenu),\n                        L"Vocals");\n                }\n\n                HMENU instrumentalMenu = CreatePopupMenu();\n                if (instrumentalMenu != nullptr) {\n                    addLevelMenu(instrumentalMenu, kStemBlendInstrumental0, L"0%");\n                    addLevelMenu(instrumentalMenu, kStemBlendInstrumental25, L"25%");\n                    addLevelMenu(instrumentalMenu, kStemBlendInstrumental50, L"50%");\n                    addLevelMenu(instrumentalMenu, kStemBlendInstrumental75, L"75%");\n                    addLevelMenu(instrumentalMenu, kStemBlendInstrumental100, L"100%");\n                    AppendMenuW(blendMenu,\n                        MF_POPUP | (blendAvailable ? 0 : MF_GRAYED),\n                        reinterpret_cast<UINT_PTR>(instrumentalMenu),\n                        L"Instrumental");\n                }\n\n                AppendMenuW(stemMenu,\n                    MF_POPUP | (blendAvailable ? 0 : MF_GRAYED),\n                    reinterpret_cast<UINT_PTR>(blendMenu),\n                    L"Stem Blend");\n            }\n\n            AppendMenuW(stemMenu, MF_SEPARATOR, 0, nullptr);''',
    'blend menu mirror',
)

# Test identity only. Stable promotion will remove the -test1 suffix.
replace_once(
    'foo_spectral_waveform.cpp',
    '"0.4.0 Stable Stem-Aware Waveform",',
    '"0.5.0-test1 Stem Blend Menu",',
    'waveform test version',
)
replace_once(
    'foo_spectral_waveform.cpp',
    'console::print("foo_spectral_waveform: v0.4.0 loaded");',
    'console::print("foo_spectral_waveform: v0.5.0-test1 loaded");',
    'waveform console version',
)

print('Stem Blend waveform menu patch applied successfully')
