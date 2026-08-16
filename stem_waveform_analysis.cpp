#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <foobar2000/SDK/foobar2000.h>

#include "spectral_analyzer.h"
#include "stem_waveform_analysis.h"
#include "stem_waveform_provider.h"
#include "stem_transport_service.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#undef FOOGUIDDECL
#define FOOGUIDDECL
FOOGUIDDECL const GUID stem_waveform_provider::class_guid =
{ 0x8b940972, 0xc051, 0x4c0c, { 0x88, 0x43, 0x95, 0x7b, 0x56, 0x8b, 0x1f, 0xa1 } };

namespace spectral_waveform {
namespace {

constexpr double kStemBlockSeconds = 5.0;
constexpr double kStemContextSeconds = 3.0;
constexpr double kPriorityAheadSeconds = 20.0;

stem_waveform_provider::ptr find_provider() {
    stem_waveform_provider::ptr provider;
    auto e = stem_waveform_provider::enumerate();
    if (!e.first(provider)) provider.release();
    return provider;
}

stem_transport_service::ptr find_transport_service() {
    stem_transport_service::ptr service;
    auto e = stem_transport_service::enumerate();
    if (!e.first(service)) service.release();
    return service;
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

bool analyze_stems_progressive(
    metadb_handle_ptr track,
    const waveform_data& original,
    double priority_seconds,
    abort_callback& aborter,
    const std::function<void(const waveform_data&, const waveform_data&)>& on_update,
    waveform_data& vocals_out,
    waveform_data& instrumental_out) {

    if (track.is_empty() || original.points.empty()) return false;

    auto provider = find_provider();
    if (provider.is_empty()) {
        console::print("foo_spectral_waveform: Stem Waveform Provider was not found.");
        return false;
    }
    // Optional companion service. New Stem Separator builds expose this so the
    // exact PCM that creates each progressive waveform block can also become
    // immediately available to audible jog/reverse transport.
    auto transport = find_transport_service();

    // Seeking remains enabled so uncached stem waveforms can begin around the
    // current playhead. Seekable inputs are analyzed with context on both sides
    // of every displayed block so Spleeter's artificial window edges are trimmed
    // away before they ever reach the waveform.
    const t_uint32 decodeFlags = input_flag_no_looping;
    service_ptr_t<input_decoder> decoder;
    input_entry::g_open_for_decoding(decoder, nullptr, track->get_path(), aborter);
    decoder->initialize(track->get_subsong_index(), decodeFlags, aborter);

    waveform_data workingVocals = original;
    waveform_data workingInstrumental = original;

    // Stem caches contain stem-derived spectral points only. Keeping the source
    // envelope would make a future envelope renderer incorrectly show Original.
    workingVocals.envelope.clear();
    workingInstrumental.envelope.clear();

    unsigned sampleRate = 0;
    unsigned channels = 0;

    auto verify_format = [&](const audio_chunk& chunk) -> bool {
        if (sampleRate == 0) {
            sampleRate = chunk.get_sample_rate();
            channels = chunk.get_channels();
            if (sampleRate == 0 || channels == 0) return false;
        }
        if (chunk.get_sample_rate() != sampleRate || chunk.get_channels() != channels)
            throw exception_unexpected_audio_format_change();
        return true;
    };

    auto separate_and_merge = [&](
        const float* pcm,
        size_t frames,
        size_t trimStartFrames,
        size_t targetFrames,
        double targetStartSeconds) -> bool {

        if (pcm == nullptr || frames == 0 || targetFrames == 0 ||
            sampleRate == 0 || channels == 0) return true;
        if (trimStartFrames >= frames) return false;
        targetFrames = std::min(targetFrames, frames - trimStartFrames);
        if (targetFrames == 0) return true;

        aborter.check();

        const size_t samples = frames * channels;
        std::vector<float> vocals(samples);
        std::vector<float> instrumental(samples);

        if (!provider->process_both(
                pcm,
                static_cast<t_size>(frames),
                channels,
                sampleRate,
                vocals.data(),
                instrumental.data(),
                static_cast<t_size>(samples),
                aborter)) {
            pfc::string_formatter msg;
            msg << "foo_spectral_waveform: stem provider failed for "
                << sampleRate << " Hz / " << channels << " channels.";
            console::print(msg);
            return false;
        }

        const size_t trimSample = trimStartFrames * channels;

        if (!transport.is_empty()) {
            try {
                transport->publish_cache_block(
                    track->get_path(),
                    targetStartSeconds,
                    pcm + trimSample,
                    vocals.data() + trimSample,
                    instrumental.data() + trimSample,
                    static_cast<t_size>(targetFrames),
                    channels,
                    sampleRate);
            } catch (...) {
                // Transport sharing is an optimization. Never let an older or
                // unavailable Stem Separator interfere with waveform analysis.
            }
        }

        spectral_analyzer vocalsAnalyzer(sampleRate, channels);
        vocalsAnalyzer.feed(vocals.data() + trimSample, targetFrames);
        waveform_data vocalsBlock = vocalsAnalyzer.finish();

        spectral_analyzer instrumentalAnalyzer(sampleRate, channels);
        instrumentalAnalyzer.feed(instrumental.data() + trimSample, targetFrames);
        waveform_data instrumentalBlock = instrumentalAnalyzer.finish();

        const double targetEndSeconds = targetStartSeconds +
            static_cast<double>(targetFrames) / static_cast<double>(sampleRate);
        merge_block(workingVocals, vocalsBlock, targetStartSeconds, targetEndSeconds);
        merge_block(workingInstrumental, instrumentalBlock, targetStartSeconds, targetEndSeconds);

        if (on_update) on_update(workingVocals, workingInstrumental);
        return true;
    };

    const double duration = original.duration_seconds;
    const bool canPrioritize = duration > 0.0 && decoder->can_seek();

    // Contextual block path for normal seekable files. Each visible 5-second
    // target is separated from up to 3 seconds of real audio before and after it.
    // Only the center target is analyzed for display; model-edge samples are discarded.
    auto process_context_block = [&](double targetStart, double targetEnd) -> bool {
        constexpr double epsilon = 1.0e-9;
        if (targetEnd <= targetStart + epsilon) return true;

        const double contextStart = std::max(0.0, targetStart - kStemContextSeconds);
        const double contextEnd = std::min(duration, targetEnd + kStemContextSeconds);
        if (contextEnd <= contextStart + epsilon) return true;

        aborter.check();
        decoder->seek(contextStart, aborter);

        audio_chunk_impl_temporary chunk;
        std::vector<float> pcm;
        std::uint64_t acceptedFrames = 0;
        std::uint64_t requestedFrames = 0;
        bool targetKnown = false;

        while (!targetKnown || acceptedFrames < requestedFrames) {
            aborter.check();
            if (!decoder->run(chunk, aborter)) break;
            if (chunk.is_empty()) continue;
            if (!verify_format(chunk)) return false;

            if (!targetKnown) {
                requestedFrames = static_cast<std::uint64_t>(std::max<double>(
                    1.0,
                    std::llround((contextEnd - contextStart) * static_cast<double>(sampleRate))));
                targetKnown = true;
            }

            const size_t chunkFrames = chunk.get_sample_count();
            const std::uint64_t remaining = requestedFrames - acceptedFrames;
            const size_t takeFrames = static_cast<size_t>(std::min<std::uint64_t>(
                static_cast<std::uint64_t>(chunkFrames), remaining));
            if (takeFrames == 0) break;

            const size_t takeSamples = takeFrames * channels;
            const size_t oldSize = pcm.size();
            pcm.resize(oldSize + takeSamples);
            const audio_sample* src = chunk.get_data();
            for (size_t i = 0; i < takeSamples; ++i) {
                pcm[oldSize + i] = static_cast<float>(src[i]);
            }
            acceptedFrames += takeFrames;
        }

        if (!targetKnown || acceptedFrames == 0 || channels == 0) return false;

        const size_t decodedFrames = pcm.size() / channels;
        size_t trimStartFrames = static_cast<size_t>(std::max<double>(
            0.0,
            std::llround((targetStart - contextStart) * static_cast<double>(sampleRate))));
        trimStartFrames = std::min(trimStartFrames, decodedFrames);

        size_t targetFrames = static_cast<size_t>(std::max<double>(
            1.0,
            std::llround((targetEnd - targetStart) * static_cast<double>(sampleRate))));
        if (trimStartFrames >= decodedFrames) return false;
        targetFrames = std::min(targetFrames, decodedFrames - trimStartFrames);

        return separate_and_merge(
            pcm.data(), decodedFrames, trimStartFrames, targetFrames, targetStart);
    };

    auto process_context_range = [&](double rangeStart, double rangeEnd) -> bool {
        constexpr double epsilon = 1.0e-9;
        if (rangeEnd <= rangeStart + epsilon) return true;

        double blockStart = rangeStart;
        while (blockStart < rangeEnd - epsilon) {
            aborter.check();
            const double blockEnd = std::min(rangeEnd, blockStart + kStemBlockSeconds);
            if (!process_context_block(blockStart, blockEnd)) return false;
            blockStart = blockEnd;
        }
        return true;
    };

    // Sequential fallback for non-seekable inputs. These cannot obtain audio
    // from both sides of a target block, so retain the previous streaming behavior.
    auto process_sequential = [&](double rangeEnd) -> bool {
        audio_chunk_impl_temporary chunk;
        std::vector<float> pending;
        double pendingStartSeconds = 0.0;

        for (;;) {
            aborter.check();
            if (!decoder->run(chunk, aborter)) break;
            if (chunk.is_empty()) continue;
            if (!verify_format(chunk)) return false;

            const size_t chunkFrames = chunk.get_sample_count();
            const audio_sample* src = chunk.get_data();
            const size_t takeSamples = chunkFrames * channels;
            const size_t oldSize = pending.size();
            pending.resize(oldSize + takeSamples);
            for (size_t i = 0; i < takeSamples; ++i) {
                pending[oldSize + i] = static_cast<float>(src[i]);
            }

            const size_t blockFrames = std::max<size_t>(
                1,
                static_cast<size_t>(std::lround(sampleRate * kStemBlockSeconds)));

            while (pending.size() / channels >= blockFrames) {
                if (!separate_and_merge(
                        pending.data(), blockFrames, 0, blockFrames, pendingStartSeconds)) return false;
                pending.erase(
                    pending.begin(),
                    pending.begin() + static_cast<std::ptrdiff_t>(blockFrames * channels));
                pendingStartSeconds += static_cast<double>(blockFrames) / sampleRate;
                if (pendingStartSeconds >= rangeEnd) return true;
            }
        }

        const size_t remainFrames = channels > 0 ? pending.size() / channels : 0;
        if (remainFrames > 0) {
            if (!separate_and_merge(
                    pending.data(), remainFrames, 0, remainFrames, pendingStartSeconds)) return false;
        }
        return true;
    };

    if (!canPrioritize) {
        const double fallbackEnd = duration > 0.0 ? duration : 24.0 * 60.0 * 60.0;
        if (!process_sequential(fallbackEnd)) return false;
    } else {
        const double safePriority = std::clamp(
            priority_seconds,
            0.0,
            std::max(0.0, duration - 1.0e-9));

        // Start with the 5-second block containing the playhead and continue
        // roughly 20 seconds ahead, but each block now has hidden context.
        const double focusStart = std::floor(safePriority / kStemBlockSeconds) * kStemBlockSeconds;
        const double focusEnd = std::min(duration, focusStart + kPriorityAheadSeconds);
        const double behindStart = std::max(0.0, focusStart - kStemBlockSeconds);

        if (!process_context_range(focusStart, focusEnd)) return false;

        if (behindStart < focusStart) {
            if (!process_context_range(behindStart, focusStart)) return false;
        }

        if (behindStart > 0.0) {
            if (!process_context_range(0.0, behindStart)) return false;
        }
        if (focusEnd < duration) {
            if (!process_context_range(focusEnd, duration)) return false;
        }
    }

    vocals_out = std::move(workingVocals);
    instrumental_out = std::move(workingInstrumental);
    return true;
}

} // namespace spectral_waveform
