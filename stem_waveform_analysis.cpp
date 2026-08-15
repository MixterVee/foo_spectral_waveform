#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <foobar2000/SDK/foobar2000.h>

#include "spectral_analyzer.h"
#include "stem_waveform_analysis.h"
#include "stem_waveform_provider.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#undef FOOGUIDDECL
#define FOOGUIDDECL
FOOGUIDDECL const GUID stem_waveform_provider::class_guid =
{ 0xd8ae9a42, 0x5f1d, 0x4cba, { 0xa9, 0xb7, 0x3c, 0x2e, 0x61, 0xf4, 0xd8, 0x12 } };

namespace spectral_waveform {
namespace {

constexpr double kStemBlockSeconds = 5.0;

stem_waveform_provider::ptr find_provider() {
    stem_waveform_provider::ptr provider;
    auto e = stem_waveform_provider::enumerate();
    if (!e.first(provider)) provider.release();
    return provider;
}

void merge_block(
    waveform_data& working,
    const waveform_data& block,
    double start_seconds,
    double end_seconds) {

    if (working.points.empty() || block.points.empty() || working.duration_seconds <= 0.0) return;

    const size_t total = working.points.size();
    size_t begin = static_cast<size_t>(std::floor(
        std::clamp(start_seconds / working.duration_seconds, 0.0, 1.0) * total));
    size_t end = static_cast<size_t>(std::ceil(
        std::clamp(end_seconds / working.duration_seconds, 0.0, 1.0) * total));

    begin = std::min(begin, total - 1);
    end = std::max(begin + 1, std::min(end, total));

    const size_t span = end - begin;
    for (size_t i = 0; i < span; ++i) {
        const double frac = span > 1 ? static_cast<double>(i) / static_cast<double>(span - 1) : 0.0;
        size_t src = static_cast<size_t>(std::lround(frac * static_cast<double>(block.points.size() - 1)));
        src = std::min(src, block.points.size() - 1);
        working.points[begin + i] = block.points[src];
    }
}

} // namespace

int current_stem_mode() {
    auto provider = find_provider();
    if (provider.is_empty()) return -1;
    try {
        return provider->get_mode();
    } catch (...) {
        return -1;
    }
}

bool analyze_stem_progressive(
    metadb_handle_ptr track,
    int mode,
    const waveform_data& original,
    abort_callback& aborter,
    const std::function<void(const waveform_data&)>& on_update) {

    if (track.is_empty() || mode <= 0 || mode > 2 || original.points.empty()) return false;

    auto provider = find_provider();
    if (provider.is_empty()) return false;

    const t_uint32 decodeFlags = input_flag_simpledecode;
    service_ptr_t<input_decoder> decoder;
    input_entry::g_open_for_decoding(decoder, nullptr, track->get_path(), aborter);
    decoder->initialize(track->get_subsong_index(), decodeFlags, aborter);

    audio_chunk_impl_temporary chunk;
    std::vector<float> pending;
    waveform_data working = original;

    unsigned sampleRate = 0;
    unsigned channels = 0;
    std::uint64_t processedFrames = 0;

    auto flush_block = [&](size_t frames) -> bool {
        if (frames == 0 || sampleRate == 0 || channels == 0) return true;
        aborter.check();

        const size_t samples = frames * channels;
        if (pending.size() < samples) return true;

        pfc::array_t<float> vocals;
        pfc::array_t<float> instrumental;
        if (!provider->process_both(
                pending.data(),
                static_cast<t_size>(frames),
                channels,
                sampleRate,
                vocals,
                instrumental,
                aborter)) {
            return false;
        }

        const pfc::array_t<float>& selected = mode == 1 ? vocals : instrumental;
        if (selected.get_size() != samples) return false;

        spectral_analyzer analyzer(sampleRate, channels);
        analyzer.feed(selected.get_ptr(), frames);
        waveform_data block = analyzer.finish();

        const double startSeconds = static_cast<double>(processedFrames) / sampleRate;
        const double endSeconds = startSeconds + static_cast<double>(frames) / sampleRate;
        merge_block(working, block, startSeconds, endSeconds);
        processedFrames += frames;

        pending.erase(pending.begin(), pending.begin() + static_cast<std::ptrdiff_t>(samples));

        if (on_update) on_update(working);
        return true;
    };

    while (decoder->run(chunk, aborter)) {
        aborter.check();
        if (chunk.is_empty()) continue;

        if (sampleRate == 0) {
            sampleRate = chunk.get_sample_rate();
            channels = chunk.get_channels();
            if (sampleRate == 0 || channels == 0) return false;
        }

        if (chunk.get_sample_rate() != sampleRate || chunk.get_channels() != channels)
            throw exception_unexpected_audio_format_change();

        const size_t used = chunk.get_used_size();
        const audio_sample* src = chunk.get_data();
        const size_t oldSize = pending.size();
        pending.resize(oldSize + used);
        for (size_t i = 0; i < used; ++i) pending[oldSize + i] = static_cast<float>(src[i]);

        const size_t blockFrames = std::max<size_t>(1, static_cast<size_t>(std::lround(sampleRate * kStemBlockSeconds)));
        while (pending.size() / channels >= blockFrames) {
            if (!flush_block(blockFrames)) return false;
        }
    }

    const size_t remainFrames = channels > 0 ? pending.size() / channels : 0;
    if (remainFrames > 0) {
        if (!flush_block(remainFrames)) return false;
    }

    return true;
}

} // namespace spectral_waveform
