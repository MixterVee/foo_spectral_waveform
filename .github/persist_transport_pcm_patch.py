from pathlib import Path

# waveform_cache.h
p = Path('waveform_cache.h')
s = p.read_text(encoding='utf-8')
old = '''// Removes Original, Vocals and Instrumental waveform caches for one track.\n// Missing/unwritable cache files are ignored; abort requests still propagate.\nvoid remove_waveform_caches(metadb_handle_ptr track, abort_callback& aborter);'''
new = '''// Persists the separated PCM used only for scrub/reverse transport. The on-disk\n// representation stores the two stems as scaled packed 24-bit samples so it is\n// much smaller than keeping three float streams while preserving ample fidelity.\nvoid save_transport_pcm_block(\n    metadb_handle_ptr track,\n    double start_seconds,\n    const float* vocals,\n    const float* instrumental,\n    t_size frames,\n    unsigned channels,\n    unsigned sample_rate,\n    abort_callback& aborter);\n\n// Re-publishes persistent transport blocks to the companion Stem Separator.\n// Blocks nearest priority_seconds are restored first.\nbool rehydrate_transport_pcm_cache(\n    metadb_handle_ptr track,\n    double priority_seconds,\n    abort_callback& aborter);\n\n// Removes Original, Vocals, Instrumental and persistent transport PCM caches\n// for one track. Missing/unwritable cache files are ignored.\nvoid remove_waveform_caches(metadb_handle_ptr track, abort_callback& aborter);'''
assert old in s
s = s.replace(old, new, 1)
p.write_text(s, encoding='utf-8')

# waveform_cache.cpp
p = Path('waveform_cache.cpp')
s = p.read_text(encoding='utf-8')
s = s.replace('#include "waveform_cache.h"\n', '#include "waveform_cache.h"\n#include "stem_transport_service.h"\n', 1)
s = s.replace('#include <cstdint>\n', '#include <cstdint>\n#include <cmath>\n', 1)
s = s.replace('#include <type_traits>\n', '#include <type_traits>\n#include <vector>\n', 1)

anchor = '''static constexpr std::uint32_t kStemCacheRevision = 2;\nstatic constexpr std::array<char, 8> kMagic{{'F','S','W','A','V','E','0','1'}};\nstatic constexpr std::uint64_t kMaxPointCount = 20'000'000ULL;'''
insert = '''static constexpr std::uint32_t kStemCacheRevision = 2;\nstatic constexpr std::uint32_t kTransportCacheVersion = 1;\nstatic constexpr std::uint32_t kTransportCacheRevision = 1;\nstatic constexpr std::array<char, 8> kMagic{{'F','S','W','A','V','E','0','1'}};\nstatic constexpr std::array<char, 8> kTransportManifestMagic{{'F','S','P','C','M','I','0','1'}};\nstatic constexpr std::array<char, 8> kTransportBlockMagic{{'F','S','P','C','M','B','0','1'}};\nstatic constexpr std::uint64_t kMaxPointCount = 20'000'000ULL;\nstatic constexpr std::uint64_t kMaxTransportBlocks = 10000ULL;\nstatic constexpr std::uint64_t kMaxTransportSamples = 100000000ULL;'''
assert anchor in s
s = s.replace(anchor, insert, 1)

anchor = '''struct cache_header {\n    char magic[8];\n    std::uint32_t version;\n    std::uint32_t point_size;\n    std::uint32_t envelope_point_size;\n    std::uint32_t reserved;\n    std::uint64_t source_size;\n    std::uint64_t source_timestamp;\n    double duration_seconds;\n    std::uint32_t sample_rate;\n    std::uint32_t channels;\n    std::uint16_t rms_reference;\n    std::uint16_t reserved2;\n    std::uint64_t point_count;\n    std::uint64_t envelope_count;\n};'''
insert = anchor + '''\n\nstruct transport_manifest_header {\n    char magic[8];\n    std::uint32_t version;\n    std::uint32_t entry_size;\n    std::uint64_t source_size;\n    std::uint64_t source_timestamp;\n    std::uint64_t block_count;\n};\n\nstruct transport_manifest_entry {\n    std::int64_t start_us;\n};\n\nstruct transport_block_header {\n    char magic[8];\n    std::uint32_t version;\n    std::uint32_t sample_rate;\n    std::uint32_t channels;\n    std::uint32_t bytes_per_sample;\n    std::uint64_t source_size;\n    std::uint64_t source_timestamp;\n    std::int64_t start_us;\n    std::uint64_t frames;\n    float vocals_scale;\n    float instrumental_scale;\n};'''
assert anchor in s
s = s.replace(anchor, insert, 1)

anchor = '''static_assert(std::is_trivially_copyable_v<waveform_point>);\nstatic_assert(std::is_trivially_copyable_v<waveform_envelope_point>);\nstatic_assert(std::is_trivially_copyable_v<cache_header>);'''
insert = anchor + '''\nstatic_assert(std::is_trivially_copyable_v<transport_manifest_header>);\nstatic_assert(std::is_trivially_copyable_v<transport_manifest_entry>);\nstatic_assert(std::is_trivially_copyable_v<transport_block_header>);'''
s = s.replace(anchor, insert, 1)

# Insert transport helpers immediately before load_cache_variant.
needle = '''bool load_cache_variant(\n    metadb_handle_ptr track,\n    int mode,\n    waveform_data& out,\n    abort_callback& aborter) {'''
helpers = r'''std::uint64_t transport_hash_for_track(metadb_handle_ptr track) {
    auto hash = cache_hash_for_track(track, 1);
    hash = fnv1a64(&kTransportCacheVersion, sizeof(kTransportCacheVersion), hash);
    hash = fnv1a64(&kTransportCacheRevision, sizeof(kTransportCacheRevision), hash);
    return hash;
}

pfc::string8 transport_manifest_path(metadb_handle_ptr track) {
    char name[112]{};
    std::snprintf(name, sizeof(name), "foo_spectral_waveform-transport-%016llx.idx",
        static_cast<unsigned long long>(transport_hash_for_track(track)));
    return core_api::pathInProfile(name);
}

pfc::string8 transport_block_path(metadb_handle_ptr track, std::int64_t start_us) {
    char name[144]{};
    std::snprintf(name, sizeof(name), "foo_spectral_waveform-transport-%016llx-%016llx.pcm",
        static_cast<unsigned long long>(transport_hash_for_track(track)),
        static_cast<unsigned long long>(start_us));
    return core_api::pathInProfile(name);
}

bool transport_stats_match(
    std::uint64_t source_size,
    std::uint64_t source_timestamp,
    const foobar2000_io::t_filestats& current) {

    if (current.m_size != foobar2000_io::filesize_invalid &&
        source_size != static_cast<std::uint64_t>(foobar2000_io::filesize_invalid) &&
        source_size != static_cast<std::uint64_t>(current.m_size)) return false;
    if (current.m_timestamp != foobar2000_io::filetimestamp_invalid &&
        source_timestamp != static_cast<std::uint64_t>(foobar2000_io::filetimestamp_invalid) &&
        source_timestamp != static_cast<std::uint64_t>(current.m_timestamp)) return false;
    return true;
}

bool load_transport_manifest_entries(
    metadb_handle_ptr track,
    std::vector<transport_manifest_entry>& entries,
    abort_callback& aborter) {

    entries.clear();
    if (track.is_empty()) return false;
    try {
        const auto path = transport_manifest_path(track);
        if (!filesystem::g_exists(path, aborter)) return false;

        service_ptr_t<file> f;
        filesystem::g_open_read(f, path, aborter);
        transport_manifest_header h{};
        f->read_object(&h, sizeof(h), aborter);
        if (std::memcmp(h.magic, kTransportManifestMagic.data(), kTransportManifestMagic.size()) != 0 ||
            h.version != kTransportCacheVersion ||
            h.entry_size != sizeof(transport_manifest_entry) ||
            h.block_count > kMaxTransportBlocks ||
            !transport_stats_match(h.source_size, h.source_timestamp, source_stats(track, aborter))) {
            return false;
        }

        entries.resize(static_cast<std::size_t>(h.block_count));
        if (!entries.empty()) {
            f->read_object(entries.data(), entries.size() * sizeof(transport_manifest_entry), aborter);
        }
        entries.erase(std::remove_if(entries.begin(), entries.end(),
            [](const transport_manifest_entry& e) { return e.start_us < 0; }), entries.end());
        std::sort(entries.begin(), entries.end(),
            [](const transport_manifest_entry& a, const transport_manifest_entry& b) {
                return a.start_us < b.start_us;
            });
        entries.erase(std::unique(entries.begin(), entries.end(),
            [](const transport_manifest_entry& a, const transport_manifest_entry& b) {
                return a.start_us == b.start_us;
            }), entries.end());
        return !entries.empty();
    } catch (exception_aborted const&) {
        throw;
    } catch (...) {
        entries.clear();
        return false;
    }
}

void save_transport_manifest_entries(
    metadb_handle_ptr track,
    const std::vector<transport_manifest_entry>& entries,
    abort_callback& aborter) {

    if (track.is_empty()) return;
    const auto stats = source_stats(track, aborter);
    transport_manifest_header h{};
    std::memcpy(h.magic, kTransportManifestMagic.data(), kTransportManifestMagic.size());
    h.version = kTransportCacheVersion;
    h.entry_size = sizeof(transport_manifest_entry);
    h.source_size = static_cast<std::uint64_t>(stats.m_size);
    h.source_timestamp = static_cast<std::uint64_t>(stats.m_timestamp);
    h.block_count = static_cast<std::uint64_t>(entries.size());

    service_ptr_t<file> f;
    filesystem::g_open_write_new(f, transport_manifest_path(track), aborter);
    f->write_object(&h, sizeof(h), aborter);
    if (!entries.empty()) {
        f->write_object(entries.data(), entries.size() * sizeof(transport_manifest_entry), aborter);
    }
}

float transport_pcm_scale(const float* data, std::size_t samples) {
    float peak = 0.0f;
    for (std::size_t i = 0; i < samples; ++i) peak = std::max(peak, std::abs(data[i]));
    return std::max(1.0f, peak);
}

void pack_transport_pcm24(
    const float* input,
    std::size_t samples,
    float scale,
    std::vector<std::uint8_t>& packed) {

    packed.resize(samples * 3);
    const double denom = static_cast<double>(std::max(scale, 1.0e-20f));
    for (std::size_t i = 0; i < samples; ++i) {
        const double normalized = std::clamp(static_cast<double>(input[i]) / denom, -1.0, 1.0);
        const std::int32_t q = static_cast<std::int32_t>(std::llround(normalized * 8388607.0));
        const std::uint32_t u = static_cast<std::uint32_t>(q) & 0x00ffffffu;
        packed[i * 3 + 0] = static_cast<std::uint8_t>(u & 0xffu);
        packed[i * 3 + 1] = static_cast<std::uint8_t>((u >> 8) & 0xffu);
        packed[i * 3 + 2] = static_cast<std::uint8_t>((u >> 16) & 0xffu);
    }
}

void unpack_transport_pcm24(
    const std::vector<std::uint8_t>& packed,
    float scale,
    std::vector<float>& output) {

    const std::size_t samples = packed.size() / 3;
    output.resize(samples);
    for (std::size_t i = 0; i < samples; ++i) {
        std::int32_t q = static_cast<std::int32_t>(
            static_cast<std::uint32_t>(packed[i * 3 + 0]) |
            (static_cast<std::uint32_t>(packed[i * 3 + 1]) << 8) |
            (static_cast<std::uint32_t>(packed[i * 3 + 2]) << 16));
        if ((q & 0x00800000) != 0) q |= static_cast<std::int32_t>(0xff000000u);
        output[i] = static_cast<float>(
            (static_cast<double>(q) / 8388607.0) * static_cast<double>(scale));
    }
}

bool load_transport_block(
    metadb_handle_ptr track,
    std::int64_t expected_start_us,
    double& start_seconds,
    std::vector<float>& vocals,
    std::vector<float>& instrumental,
    std::uint64_t& frames,
    unsigned& channels,
    unsigned& sample_rate,
    abort_callback& aborter) {

    vocals.clear();
    instrumental.clear();
    try {
        const auto path = transport_block_path(track, expected_start_us);
        if (!filesystem::g_exists(path, aborter)) return false;
        service_ptr_t<file> f;
        filesystem::g_open_read(f, path, aborter);

        transport_block_header h{};
        f->read_object(&h, sizeof(h), aborter);
        if (std::memcmp(h.magic, kTransportBlockMagic.data(), kTransportBlockMagic.size()) != 0 ||
            h.version != kTransportCacheVersion || h.bytes_per_sample != 3 ||
            h.start_us != expected_start_us || h.sample_rate == 0 || h.sample_rate > 768000 ||
            h.channels == 0 || h.channels > 32 || h.frames == 0 ||
            h.frames > kMaxTransportSamples / h.channels ||
            !transport_stats_match(h.source_size, h.source_timestamp, source_stats(track, aborter))) {
            return false;
        }

        const std::uint64_t sample_count64 = h.frames * h.channels;
        if (sample_count64 > kMaxTransportSamples) return false;
        const std::size_t sample_count = static_cast<std::size_t>(sample_count64);
        std::vector<std::uint8_t> packed(sample_count * 3);

        f->read_object(packed.data(), packed.size(), aborter);
        unpack_transport_pcm24(packed, h.vocals_scale, vocals);
        f->read_object(packed.data(), packed.size(), aborter);
        unpack_transport_pcm24(packed, h.instrumental_scale, instrumental);

        start_seconds = static_cast<double>(h.start_us) / 1000000.0;
        frames = h.frames;
        channels = h.channels;
        sample_rate = h.sample_rate;
        return true;
    } catch (exception_aborted const&) {
        throw;
    } catch (...) {
        vocals.clear();
        instrumental.clear();
        return false;
    }
}

'''
assert needle in s
s = s.replace(needle, helpers + needle, 1)

# Add public persistent transport functions before remove_waveform_caches.
needle = '''void remove_waveform_caches(metadb_handle_ptr track, abort_callback& aborter) {\n    remove_cache_variant(track, 0, aborter);\n    remove_cache_variant(track, 1, aborter);\n    remove_cache_variant(track, 2, aborter);\n}'''
public = r'''void save_transport_pcm_block(
    metadb_handle_ptr track,
    double start_seconds,
    const float* vocals,
    const float* instrumental,
    t_size frames,
    unsigned channels,
    unsigned sample_rate,
    abort_callback& aborter) {

    if (track.is_empty() || vocals == nullptr || instrumental == nullptr ||
        frames == 0 || channels == 0 || sample_rate == 0 || start_seconds < 0.0) return;

    try {
        const std::uint64_t frame_count = static_cast<std::uint64_t>(frames);
        if (frame_count > kMaxTransportSamples / channels) return;
        const std::uint64_t sample_count64 = frame_count * channels;
        if (sample_count64 == 0 || sample_count64 > kMaxTransportSamples) return;
        const std::size_t sample_count = static_cast<std::size_t>(sample_count64);
        const std::int64_t start_us = static_cast<std::int64_t>(
            std::llround(start_seconds * 1000000.0));
        if (start_us < 0) return;

        const auto stats = source_stats(track, aborter);
        transport_block_header h{};
        std::memcpy(h.magic, kTransportBlockMagic.data(), kTransportBlockMagic.size());
        h.version = kTransportCacheVersion;
        h.sample_rate = sample_rate;
        h.channels = channels;
        h.bytes_per_sample = 3;
        h.source_size = static_cast<std::uint64_t>(stats.m_size);
        h.source_timestamp = static_cast<std::uint64_t>(stats.m_timestamp);
        h.start_us = start_us;
        h.frames = frame_count;
        h.vocals_scale = transport_pcm_scale(vocals, sample_count);
        h.instrumental_scale = transport_pcm_scale(instrumental, sample_count);

        service_ptr_t<file> f;
        filesystem::g_open_write_new(f, transport_block_path(track, start_us), aborter);
        f->write_object(&h, sizeof(h), aborter);
        std::vector<std::uint8_t> packed;
        pack_transport_pcm24(vocals, sample_count, h.vocals_scale, packed);
        f->write_object(packed.data(), packed.size(), aborter);
        pack_transport_pcm24(instrumental, sample_count, h.instrumental_scale, packed);
        f->write_object(packed.data(), packed.size(), aborter);

        std::vector<transport_manifest_entry> entries;
        load_transport_manifest_entries(track, entries, aborter);
        const auto it = std::find_if(entries.begin(), entries.end(),
            [start_us](const transport_manifest_entry& e) { return e.start_us == start_us; });
        if (it == entries.end()) entries.push_back(transport_manifest_entry{start_us});
        std::sort(entries.begin(), entries.end(),
            [](const transport_manifest_entry& a, const transport_manifest_entry& b) {
                return a.start_us < b.start_us;
            });
        if (entries.size() <= kMaxTransportBlocks) {
            save_transport_manifest_entries(track, entries, aborter);
        }
    } catch (exception_aborted const&) {
        throw;
    } catch (...) {
        // Persistent transport PCM is opportunistic. Waveform generation and
        // playback must continue even if this cache cannot be written.
    }
}

bool rehydrate_transport_pcm_cache(
    metadb_handle_ptr track,
    double priority_seconds,
    abort_callback& aborter) {

    if (track.is_empty()) return false;
    stem_transport_service::ptr transport;
    auto e = stem_transport_service::enumerate();
    if (!e.first(transport) || transport.is_empty()) return false;

    std::vector<transport_manifest_entry> entries;
    if (!load_transport_manifest_entries(track, entries, aborter)) return false;
    priority_seconds = std::max(0.0, priority_seconds);
    std::stable_sort(entries.begin(), entries.end(),
        [priority_seconds](const transport_manifest_entry& a, const transport_manifest_entry& b) {
            const double da = std::abs(static_cast<double>(a.start_us) / 1000000.0 - priority_seconds);
            const double db = std::abs(static_cast<double>(b.start_us) / 1000000.0 - priority_seconds);
            return da < db;
        });

    std::size_t published = 0;
    for (const auto& entry : entries) {
        aborter.check();
        double start_seconds = 0.0;
        std::vector<float> vocals;
        std::vector<float> instrumental;
        std::uint64_t frames = 0;
        unsigned channels = 0;
        unsigned sample_rate = 0;
        if (!load_transport_block(track, entry.start_us, start_seconds,
                vocals, instrumental, frames, channels, sample_rate, aborter)) continue;
        if (frames > static_cast<std::uint64_t>(std::numeric_limits<t_size>::max())) continue;
        try {
            if (transport->publish_cache_block(
                    track->get_path(), start_seconds, nullptr,
                    vocals.data(), instrumental.data(),
                    static_cast<t_size>(frames), channels, sample_rate)) {
                ++published;
            }
        } catch (...) {
            // Companion component may be older or may have changed tracks.
        }
    }
    return published != 0;
}

void remove_transport_pcm_cache(metadb_handle_ptr track, abort_callback& aborter) {
    if (track.is_empty()) return;
    try {
        std::vector<transport_manifest_entry> entries;
        load_transport_manifest_entries(track, entries, aborter);
        for (const auto& entry : entries) {
            const auto path = transport_block_path(track, entry.start_us);
            if (filesystem::g_exists(path, aborter)) filesystem::g_remove(path, aborter);
        }
        const auto manifest = transport_manifest_path(track);
        if (filesystem::g_exists(manifest, aborter)) filesystem::g_remove(manifest, aborter);
    } catch (exception_aborted const&) {
        throw;
    } catch (...) {
        // Best-effort cache cleanup.
    }
}

void remove_waveform_caches(metadb_handle_ptr track, abort_callback& aborter) {
    remove_cache_variant(track, 0, aborter);
    remove_cache_variant(track, 1, aborter);
    remove_cache_variant(track, 2, aborter);
    remove_transport_pcm_cache(track, aborter);
}'''
assert needle in s
s = s.replace(needle, public, 1)
p.write_text(s, encoding='utf-8')

# stem_waveform_analysis.cpp
p = Path('stem_waveform_analysis.cpp')
s = p.read_text(encoding='utf-8')
s = s.replace('#include "stem_transport_service.h"\n', '#include "stem_transport_service.h"\n#include "waveform_cache.h"\n', 1)
old = '''constexpr double kTransportShareContextSeconds = 1.5;\nconstexpr double kPriorityAheadSeconds = 20.0;'''
new = '''constexpr double kTransportShareContextSeconds = 1.5;\n// Persistent transport PCM needs only enough overlap for seamless handoff and\n// the 350 ms transport readiness margin. Keep the much wider live sharing in RAM.\nconstexpr double kPersistentTransportOverlapSeconds = 0.25;\nconstexpr double kPriorityAheadSeconds = 20.0;'''
assert old in s
s = s.replace(old, new, 1)

old = '''        if (!transport.is_empty() && transportFrames > 0) {\n            try {\n                const size_t transportSample = transportStartFrames * channels;\n                transport->publish_cache_block(\n                    track->get_path(),\n                    transportStartSeconds,\n                    pcm + transportSample,\n                    vocals.data() + transportSample,\n                    instrumental.data() + transportSample,\n                    static_cast<t_size>(transportFrames),\n                    channels,\n                    sampleRate);\n            } catch (...) {\n                // Transport sharing is an optimization. Never let an older or\n                // unavailable Stem Separator interfere with waveform analysis.\n            }\n        }'''
new = '''        // Persist the artifact-free target plus 250 ms on either side. This is\n        // enough for seamless restart-time transport handoff without writing the\n        // full 3-second Spleeter context window to disk.\n        const size_t persistentPadFrames = static_cast<size_t>(std::llround(\n            kPersistentTransportOverlapSeconds * static_cast<double>(sampleRate)));\n        const size_t persistentStartFrames = trimStartFrames > persistentPadFrames\n            ? trimStartFrames - persistentPadFrames\n            : 0;\n        const size_t targetEndFrames = std::min(frames, trimStartFrames + targetFrames);\n        const size_t persistentEndFrames = std::min(\n            frames, targetEndFrames + persistentPadFrames);\n        if (persistentEndFrames > persistentStartFrames) {\n            const size_t persistentFrames = persistentEndFrames - persistentStartFrames;\n            const double persistentStartSeconds = targetStartSeconds -\n                static_cast<double>(trimStartFrames - persistentStartFrames) /\n                    static_cast<double>(sampleRate);\n            const size_t persistentSample = persistentStartFrames * channels;\n            save_transport_pcm_block(\n                track, persistentStartSeconds,\n                vocals.data() + persistentSample,\n                instrumental.data() + persistentSample,\n                static_cast<t_size>(persistentFrames), channels, sampleRate, aborter);\n        }\n\n        if (!transport.is_empty() && transportFrames > 0) {\n            try {\n                const size_t transportSample = transportStartFrames * channels;\n                // Original can be decoded cheaply on demand. Share only the two\n                // expensive separated stems so the companion cache uses less RAM.\n                transport->publish_cache_block(\n                    track->get_path(),\n                    transportStartSeconds,\n                    nullptr,\n                    vocals.data() + transportSample,\n                    instrumental.data() + transportSample,\n                    static_cast<t_size>(transportFrames),\n                    channels,\n                    sampleRate);\n            } catch (...) {\n                // Transport sharing is an optimization. Never let an older or\n                // unavailable Stem Separator interfere with waveform analysis.\n            }\n        }'''
assert old in s
s = s.replace(old, new, 1)
p.write_text(s, encoding='utf-8')

# live_output_capture.cpp - rehydrate transport PCM whenever both visual stem caches hit.
p = Path('live_output_capture.cpp')
s = p.read_text(encoding='utf-8')
old = '''                    if (haveVocals && haveInstrumental && is_current(generation)) {\n                        // Already-cached tracks stay instant. The visual dissolve is\n                        // reserved for newly completing progressive analysis blocks.\n                        publish_previews(cachedVocals, cachedInstrumental, false);\n                        completed = true;\n                    } else {'''
new = '''                    if (haveVocals && haveInstrumental && is_current(generation)) {\n                        // Already-cached tracks stay visually instant. Restore the\n                        // matching separated PCM in this same background worker so\n                        // scrub/reverse is restart-ready without another Spleeter pass.\n                        publish_previews(cachedVocals, cachedInstrumental, false);\n                        rehydrate_transport_pcm_cache(track, prioritySeconds, *aborter);\n                        completed = true;\n                    } else {'''
assert old in s
s = s.replace(old, new, 1)
p.write_text(s, encoding='utf-8')
