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
{ 0x8b940972, 0xc051, 0x4c0c, { 0x88, 0x43, 0x95, 0x7b, 0x56, 0x8b, 0x1f, 0xa1 } };

namespace spectral_waveform {
namespace {

constexpr double kStemBlockSeconds = 5.0;
constexpr double kPriorityAheadSeconds = 20.0;

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

    // Seeking must remain enabled so an uncached stem waveform can begin around
    // the current playhead instead of decoding from 0:00 first.
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

    auto analyze_pcm_block = [&](
        const float* pcm,
        size_t frames,
        double startSeconds) -> bool {

        if (pcm == nullptr || frames == 0 || sampleRate == 0 || channels == 0) return true;
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

        spectral_analyzer vocalsAnalyzer(sampleRate, channels);
        vocalsAnalyzer.feed(vocals.data(), frames);
        waveform_data vocalsBlock = vocalsAnalyzer.finish();

        spectral_analyzer instrumentalAnalyzer(sampleRate, channels);
        instrumentalAnalyzer.feed(instrumental.data(), frames);
        waveform_data instrumentalBlock = instrumentalAnalyzer.finish();

        const double endSeconds = startSeconds + static_cast<double>(frames) / sampleRate;
        merge_block(workingVocals, vocalsBlock, startSeconds, endSeconds);
        merge_block(workingInstrumental, instrumentalBlock, startSeconds, endSeconds);

        if (on_update) on_update(workingVocals, workingInstrumental);
        return true;
    };

    auto process_range = [&](double rangeStart, double rangeEnd, bool seekFirst) -> bool {
        constexpr double epsilon = 1.0e-9;
        if (rangeEnd <= rangeStart + epsilon) return true;

        aborter.check();
        if (seekFirst) decoder->seek(rangeStart, aborter);

        audio_chunk_impl_temporary chunk;
        std::vector<float> pending;
        double pendingStartSeconds = rangeStart;
        std::uint64_t acceptedFrames = 0;
        std::uint64_t targetFrames = 0;
        bool targetKnown = false;

        for (;;) {
            aborter.check();
            if (targetKnown && acceptedFrames >= targetFrames) break;
            if (!decoder->run(chunk, aborter)) break;
            if (chunk.is_empty()) continue;

            if (sampleRate == 0) {
                sampleRate = chunk.get_sample_rate();
                channels = chunk.get_channels();
                if (sampleRate == 0 || channels == 0) return false;
            }

            if (chunk.get_sample_rate() != sampleRate || chunk.get_channels() != channels)
                throw exception_unexpected_audio_format_change();

            if (!targetKnown) {
                const double rangeSeconds = rangeEnd - rangeStart;
                targetFrames = static_cast<std::uint64_t>(std::max<double>(
                    1.0,
                    std::llround(rangeSeconds * static_cast<double>(sampleRate))));
                targetKnown = true;
            }

            const size_t chunkFrames = chunk.get_sample_count();
            const std::uint64_t remainingFrames = targetFrames - acceptedFrames;
            const size_t takeFrames = static_cast<size_t>(std::min<std::uint64_t>(
                static_cast<std::uint64_t>(chunkFrames), remainingFrames));
            if (takeFrames == 0) break;

            const audio_sample* src = chunk.get_data();
            const size_t takeSamples = takeFrames * channels;
            const size_t oldSize = pending.size();
            pending.resize(oldSize + takeSamples);
            for (size_t i = 0; i < takeSamples; ++i) {
                pending[oldSize + i] = static_cast<float>(src[i]);
            }
            acceptedFrames += takeFrames;

            const size_t blockFrames = std::max<size_t>(
                1,
                static_cast<size_t>(std::lround(sampleRate * kStemBlockSeconds)));

            while (pending.size() / channels >= blockFrames) {
                if (!analyze_pcm_block(pending.data(), blockFrames, pendingStartSeconds)) return false;
                pending.erase(
                    pending.begin(),
                    pending.begin() + static_cast<std::ptrdiff_t>(blockFrames * channels));
                pendingStartSeconds += static_cast<double>(blockFrames) / sampleRate;
            }
        }

        const size_t remainFrames = channels > 0 ? pending.size() / channels : 0;
        if (remainFrames > 0) {
            if (!analyze_pcm_block(pending.data(), remainFrames, pendingStartSeconds)) return false;
        }
        return true;
    };

    const double duration = original.duration_seconds;
    const bool canPrioritize = duration > 0.0 && decoder->can_seek();

    if (!canPrioritize) {
        // Non-seekable inputs retain the original sequential behavior.
        const double fallbackEnd = duration > 0.0 ? duration : 24.0 * 60.0 * 60.0;
        if (!process_range(0.0, fallbackEnd, false)) return false;
    } else {
        const double safePriority = std::clamp(
            priority_seconds,
            0.0,
            std::max(0.0, duration - 1.0e-9));

        // Start with the 5-second block containing the playhead, then continue
        // roughly 20 seconds ahead. This makes the listening area useful first.
        const double focusStart = std::floor(safePriority / kStemBlockSeconds) * kStemBlockSeconds;
        const double focusEnd = std::min(duration, focusStart + kPriorityAheadSeconds);
        const double behindStart = std::max(0.0, focusStart - kStemBlockSeconds);

        if (!process_range(focusStart, focusEnd, focusStart > 0.0)) return false;

        // Next fill the immediately preceding block so a small amount of history
        // around the playhead is available before the long background sweep.
        if (behindStart < focusStart) {
            if (!process_range(behindStart, focusStart, true)) return false;
        }

        // Finish everything before the focus area, then everything after it.
        if (behindStart > 0.0) {
            if (!process_range(0.0, behindStart, true)) return false;
        }
        if (focusEnd < duration) {
            if (!process_range(focusEnd, duration, true)) return false;
        }
    }

    vocals_out = std::move(workingVocals);
    instrumental_out = std::move(workingInstrumental);
    return true;
}

} // namespace spectral_waveform
