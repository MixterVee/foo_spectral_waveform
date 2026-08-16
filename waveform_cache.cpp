#include "waveform_cache.h"
#include "stem_transport_service.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <type_traits>
#include <vector>

namespace spectral_waveform {
namespace {

static constexpr std::uint32_t kCacheVersion = 1;
static constexpr std::uint32_t kStemCacheRevision = 2;
static constexpr std::uint32_t kTransportCacheVersion = 1;
static constexpr std::uint32_t kTransportCacheRevision = 1;
static constexpr std::array<char, 8> kMagic{{'F','S','W','A','V','E','0','1'}};
static constexpr std::array<char, 8> kTransportManifestMagic{{'F','S','P','C','M','I','0','1'}};
static constexpr std::array<char, 8> kTransportBlockMagic{{'F','S','P','C','M','B','0','1'}};
static constexpr std::uint64_t kMaxPointCount = 20'000'000ULL;
static constexpr std::uint64_t kMaxTransportBlocks = 10000ULL;
static constexpr std::uint64_t kMaxTransportSamples = 100000000ULL;

struct cache_header {
    char magic[8];
    std::uint32_t version;
    std::uint32_t point_size;
    std::uint32_t envelope_point_size;
    std::uint32_t reserved;
    std::uint64_t source_size;
    std::uint64_t source_timestamp;
    double duration_seconds;
    std::uint32_t sample_rate;
    std::uint32_t channels;
    std::uint16_t rms_reference;
    std::uint16_t reserved2;
    std::uint64_t point_count;
    std::uint64_t envelope_count;
};

struct transport_manifest_header {
    char magic[8];
    std::uint32_t version;
    std::uint32_t entry_size;
    std::uint64_t source_size;
    std::uint64_t source_timestamp;
    std::uint64_t block_count;
};

struct transport_manifest_entry {
    std::int64_t start_us;
};

struct transport_block_header {
    char magic[8];
    std::uint32_t version;
    std::uint32_t sample_rate;
    std::uint32_t channels;
    std::uint32_t bytes_per_sample;
    std::uint64_t source_size;
    std::uint64_t source_timestamp;
    std::int64_t start_us;
    std::uint64_t frames;
    float vocals_scale;
    float instrumental_scale;
};

static_assert(std::is_trivially_copyable_v<waveform_point>);
static_assert(std::is_trivially_copyable_v<waveform_envelope_point>);
static_assert(std::is_trivially_copyable_v<cache_header>);
static_assert(std::is_trivially_copyable_v<transport_manifest_header>);
static_assert(std::is_trivially_copyable_v<transport_manifest_entry>);
static_assert(std::is_trivially_copyable_v<transport_block_header>);

std::uint64_t fnv1a64(const void* ptr, std::size_t bytes, std::uint64_t hash = 14695981039346656037ULL) {
    const auto* p = static_cast<const unsigned char*>(ptr);
    for (std::size_t i = 0; i < bytes; ++i) {
        hash ^= p[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::uint64_t track_hash(metadb_handle_ptr track) {
    std::uint64_t hash = 14695981039346656037ULL;
    const char* path = track->get_path();
    hash = fnv1a64(path, std::strlen(path), hash);
    const auto subsong = static_cast<std::uint32_t>(track->get_subsong_index());
    hash = fnv1a64(&subsong, sizeof(subsong), hash);
    hash = fnv1a64(&kCacheVersion, sizeof(kCacheVersion), hash);
    return hash;
}

std::uint64_t cache_hash_for_track(metadb_handle_ptr track, int mode) {
    auto hash = track_hash(track);
    if (mode == 1 || mode == 2) {
        // Stem waveform revision is independent of the Original waveform cache.
        // Bump this whenever stem-generation semantics change so old stem-only
        // graphics are not silently reused while Original remains cached.
        hash = fnv1a64(&kStemCacheRevision, sizeof(kStemCacheRevision), hash);
    }
    return hash;
}

pfc::string8 cache_path_for_track(metadb_handle_ptr track, int mode) {
    char name[112]{};
    const auto hash = cache_hash_for_track(track, mode);

    if (mode == 1) {
        std::snprintf(name, sizeof(name), "foo_spectral_waveform-cache-vocals-%016llx.bin",
            static_cast<unsigned long long>(hash));
    } else if (mode == 2) {
        std::snprintf(name, sizeof(name), "foo_spectral_waveform-cache-instrumental-%016llx.bin",
            static_cast<unsigned long long>(hash));
    } else {
        // Preserve the original cache filename so existing source-waveform caches remain valid.
        std::snprintf(name, sizeof(name), "foo_spectral_waveform-cache-%016llx.bin",
            static_cast<unsigned long long>(hash));
    }

    return core_api::pathInProfile(name);
}

foobar2000_io::t_filestats source_stats(metadb_handle_ptr track, abort_callback& aborter) {
    foobar2000_io::t_filestats stats{};
    bool writeable = false;
    try {
        filesystem::g_get_stats(track->get_path(), stats, writeable, aborter);
    } catch (exception_io const&) {
        // Some virtual/remote inputs do not expose stable stats. The cache key still
        // includes path + subsong, so caching remains useful for those sources.
        stats = foobar2000_io::filestats_invalid;
    }
    return stats;
}

bool stats_match(const cache_header& header, const foobar2000_io::t_filestats& current) {
    if (current.m_size != foobar2000_io::filesize_invalid &&
        header.source_size != static_cast<std::uint64_t>(foobar2000_io::filesize_invalid) &&
        header.source_size != static_cast<std::uint64_t>(current.m_size)) return false;

    if (current.m_timestamp != foobar2000_io::filetimestamp_invalid &&
        header.source_timestamp != static_cast<std::uint64_t>(foobar2000_io::filetimestamp_invalid) &&
        header.source_timestamp != static_cast<std::uint64_t>(current.m_timestamp)) return false;

    return true;
}

bool valid_header(const cache_header& h) {
    if (std::memcmp(h.magic, kMagic.data(), kMagic.size()) != 0) return false;
    if (h.version != kCacheVersion) return false;
    if (h.point_size != sizeof(waveform_point)) return false;
    if (h.envelope_point_size != sizeof(waveform_envelope_point)) return false;
    if (h.point_count > kMaxPointCount || h.envelope_count > kMaxPointCount) return false;
    if (h.sample_rate > 768000 || h.channels > 256) return false;
    return true;
}

std::uint64_t transport_hash_for_track(metadb_handle_ptr track) {
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

bool load_cache_variant(
    metadb_handle_ptr track,
    int mode,
    waveform_data& out,
    abort_callback& aborter) {

    if (track.is_empty()) return false;

    try {
        const auto path = cache_path_for_track(track, mode);
        if (!filesystem::g_exists(path, aborter)) return false;

        service_ptr_t<file> f;
        filesystem::g_open_read(f, path, aborter);

        cache_header header{};
        f->read_object(&header, sizeof(header), aborter);
        if (!valid_header(header)) return false;
        if (!stats_match(header, source_stats(track, aborter))) return false;

        waveform_data data;
        data.duration_seconds = header.duration_seconds;
        data.sample_rate = header.sample_rate;
        data.channels = header.channels;
        data.rms_reference = header.rms_reference;
        data.points.resize(static_cast<std::size_t>(header.point_count));
        data.envelope.resize(static_cast<std::size_t>(header.envelope_count));

        if (!data.points.empty()) {
            f->read_object(data.points.data(), data.points.size() * sizeof(waveform_point), aborter);
        }
        if (!data.envelope.empty()) {
            f->read_object(data.envelope.data(), data.envelope.size() * sizeof(waveform_envelope_point), aborter);
        }

        out = std::move(data);
        return true;
    } catch (exception_aborted const&) {
        throw;
    } catch (exception_io const&) {
        return false;
    } catch (...) {
        return false;
    }
}

void save_cache_variant(
    metadb_handle_ptr track,
    int mode,
    const waveform_data& data,
    abort_callback& aborter) {

    if (track.is_empty() || data.points.empty()) return;

    try {
        const auto stats = source_stats(track, aborter);
        cache_header header{};
        std::memcpy(header.magic, kMagic.data(), kMagic.size());
        header.version = kCacheVersion;
        header.point_size = sizeof(waveform_point);
        header.envelope_point_size = sizeof(waveform_envelope_point);
        header.source_size = static_cast<std::uint64_t>(stats.m_size);
        header.source_timestamp = static_cast<std::uint64_t>(stats.m_timestamp);
        header.duration_seconds = data.duration_seconds;
        header.sample_rate = data.sample_rate;
        header.channels = data.channels;
        header.rms_reference = data.rms_reference;
        header.point_count = static_cast<std::uint64_t>(data.points.size());
        header.envelope_count = static_cast<std::uint64_t>(data.envelope.size());

        const auto path = cache_path_for_track(track, mode);
        service_ptr_t<file> f;
        filesystem::g_open_write_new(f, path, aborter);
        f->write_object(&header, sizeof(header), aborter);
        f->write_object(data.points.data(), data.points.size() * sizeof(waveform_point), aborter);
        if (!data.envelope.empty()) {
            f->write_object(data.envelope.data(), data.envelope.size() * sizeof(waveform_envelope_point), aborter);
        }
    } catch (exception_aborted const&) {
        throw;
    } catch (exception_io const&) {
        // Cache writes are opportunistic; playback/waveform display must not fail
        // just because the profile folder is temporarily unwritable.
    }
}

void remove_cache_variant(
    metadb_handle_ptr track,
    int mode,
    abort_callback& aborter) {

    if (track.is_empty()) return;

    try {
        const auto path = cache_path_for_track(track, mode);
        if (filesystem::g_exists(path, aborter)) {
            filesystem::g_remove(path, aborter);
        }
    } catch (exception_aborted const&) {
        throw;
    } catch (exception_io const&) {
        // Manual recovery is best-effort. A missing/unwritable cache must not
        // interfere with playback or with the new analysis.
    }
}

} // namespace

bool load_waveform_cache(metadb_handle_ptr track, waveform_data& out, abort_callback& aborter) {
    return load_cache_variant(track, 0, out, aborter);
}

void save_waveform_cache(metadb_handle_ptr track, const waveform_data& data, abort_callback& aborter) {
    save_cache_variant(track, 0, data, aborter);
}

bool load_stem_waveform_cache(
    metadb_handle_ptr track,
    int mode,
    waveform_data& out,
    abort_callback& aborter) {

    if (mode != 1 && mode != 2) return false;
    return load_cache_variant(track, mode, out, aborter);
}

void save_stem_waveform_cache(
    metadb_handle_ptr track,
    int mode,
    const waveform_data& data,
    abort_callback& aborter) {

    if (mode != 1 && mode != 2) return;
    save_cache_variant(track, mode, data, aborter);
}

void save_transport_pcm_block(
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
}

} // namespace spectral_waveform
