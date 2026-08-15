#include "waveform_cache.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <type_traits>

namespace spectral_waveform {
namespace {

static constexpr std::uint32_t kCacheVersion = 1;
static constexpr std::array<char, 8> kMagic{{'F','S','W','A','V','E','0','1'}};
static constexpr std::uint64_t kMaxPointCount = 20'000'000ULL;

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

static_assert(std::is_trivially_copyable_v<waveform_point>);
static_assert(std::is_trivially_copyable_v<waveform_envelope_point>);
static_assert(std::is_trivially_copyable_v<cache_header>);

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

pfc::string8 cache_path_for_track(metadb_handle_ptr track) {
    char name[96]{};
    const auto hash = track_hash(track);
    std::snprintf(name, sizeof(name), "foo_spectral_waveform-cache-%016llx.bin",
        static_cast<unsigned long long>(hash));
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

} // namespace

bool load_waveform_cache(metadb_handle_ptr track, waveform_data& out, abort_callback& aborter) {
    if (track.is_empty()) return false;

    try {
        const auto path = cache_path_for_track(track);
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

void save_waveform_cache(metadb_handle_ptr track, const waveform_data& data, abort_callback& aborter) {
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

        const auto path = cache_path_for_track(track);
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

} // namespace spectral_waveform
