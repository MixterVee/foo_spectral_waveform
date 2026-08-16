#pragma once

#include <foobar2000/SDK/foobar2000.h>
#include "waveform_data.h"

namespace spectral_waveform {

// Loads/saves versioned waveform analysis in the foobar2000 profile folder.
// Cache validity is tied to the track path/subsong plus source file stats when available.
bool load_waveform_cache(metadb_handle_ptr track, waveform_data& out, abort_callback& aborter);
void save_waveform_cache(metadb_handle_ptr track, const waveform_data& data, abort_callback& aborter);

// Separate persistent caches for the two Spleeter waveform results.
// mode: 1 = Vocals, 2 = Instrumental.
bool load_stem_waveform_cache(
    metadb_handle_ptr track,
    int mode,
    waveform_data& out,
    abort_callback& aborter);

void save_stem_waveform_cache(
    metadb_handle_ptr track,
    int mode,
    const waveform_data& data,
    abort_callback& aborter);

// Persists the separated PCM used only for scrub/reverse transport. The on-disk
// representation stores the two stems as scaled packed 24-bit samples so it is
// much smaller than keeping three float streams while preserving ample fidelity.
void save_transport_pcm_block(
    metadb_handle_ptr track,
    double start_seconds,
    const float* vocals,
    const float* instrumental,
    t_size frames,
    unsigned channels,
    unsigned sample_rate,
    abort_callback& aborter);

// Re-publishes persistent transport blocks to the companion Stem Separator.
// Blocks nearest priority_seconds are restored first.
bool rehydrate_transport_pcm_cache(
    metadb_handle_ptr track,
    double priority_seconds,
    abort_callback& aborter);

// Removes Original, Vocals, Instrumental and persistent transport PCM caches
// for one track. Missing/unwritable cache files are ignored.
void remove_waveform_caches(metadb_handle_ptr track, abort_callback& aborter);

} // namespace spectral_waveform
