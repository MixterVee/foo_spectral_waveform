from pathlib import Path


def replace_once(path, old, new, label):
    p = Path(path)
    text = p.read_text(encoding='utf-8')
    if old not in text:
        raise RuntimeError(f'{label}: expected text not found in {path}')
    p.write_text(text.replace(old, new, 1), encoding='utf-8')

replace_once(
    'spectral_ui.cpp',
    '''    kStemBlendInstrumental75,\n    kStemBlendInstrumental100,\n    kStemCommandCount''',
    '''    kStemBlendInstrumental75,\n    kStemBlendInstrumental100,\n    kStemGainMatch,\n    kStemCommandCount''',
    'gain-match command enum',
)

replace_once(
    'spectral_ui.cpp',
    '''    {0xa92a1043,0xd1f0,0x4ae1,{0xa0,0x11,0x31,0x10,0x42,0x00,0x00,0x43}},\n    {0xa92a1044,0xd1f0,0x4ae1,{0xa0,0x11,0x31,0x10,0x42,0x00,0x00,0x44}}\n};''',
    '''    {0xa92a1043,0xd1f0,0x4ae1,{0xa0,0x11,0x31,0x10,0x42,0x00,0x00,0x43}},\n    {0xa92a1044,0xd1f0,0x4ae1,{0xa0,0x11,0x31,0x10,0x42,0x00,0x00,0x44}},\n    {0xa92a1050,0xd1f0,0x4ae1,{0xa0,0x11,0x31,0x10,0x42,0x00,0x00,0x50}}\n};''',
    'gain-match command guid',
)

replace_once(
    'spectral_ui.cpp',
    '''            }\n\n            AppendMenuW(stemMenu, MF_SEPARATOR, 0, nullptr);\n            addStem(kStemSaveVocalsWav, L"Save Vocals as WAV...");''',
    '''            }\n\n            service_ptr_t<contextmenu_item> gainMatchProbe;\n            t_uint32 gainMatchProbeIndex = 0;\n            const bool gainMatchAvailable = resolve_stem_command(\n                kStemGainMatch, gainMatchProbe, gainMatchProbeIndex);\n            const std::wstring gainMatchLabel = stem_command_name(\n                kStemGainMatch, L"Automatic Gain Matching");\n            AppendMenuW(stemMenu,\n                MF_STRING | (gainMatchAvailable ? 0 : MF_GRAYED),\n                kMenuStemBase + kStemGainMatch,\n                gainMatchLabel.c_str());\n\n            AppendMenuW(stemMenu, MF_SEPARATOR, 0, nullptr);\n            addStem(kStemSaveVocalsWav, L"Save Vocals as WAV...");''',
    'gain-match menu mirror',
)

replace_once(
    'foo_spectral_waveform.cpp',
    '"0.5.0 Stable Stem Blend Menu",',
    '"0.6.0-test1 Automatic Gain Matching Menu",',
    'waveform test version',
)
replace_once(
    'foo_spectral_waveform.cpp',
    'console::print("foo_spectral_waveform: v0.5.0 loaded");',
    'console::print("foo_spectral_waveform: v0.6.0-test1 loaded");',
    'waveform console version',
)

print('Automatic Gain Matching waveform menu patch applied successfully')
