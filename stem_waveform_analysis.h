#pragma once

#include "waveform_data.h"
#include <foobar2000/SDK/foobar2000.h>
#include <functional>

namespace spectral_waveform {

// Returns -1 when the Stem Separator provider is unavailable.
int current_stem_mode();

// Runs one progressive separation pass and builds both Vocals and Instrumental
// waveforms from the same Spleeter inference blocks. For seekable inputs, the
// block containing priority_seconds and a short window ahead are analyzed first,
// then the remainder of the track is filled in. The callback is invoked after
// each completed block so either stem can be displayed immediately.
bool analyze_stems_progressive(
    metadb_handle_ptr track,
    const waveform_data& original,
    double priority_seconds,
    abort_callback& aborter,
    const std::function<void(const waveform_data&, const waveform_data&)>& on_update,
    waveform_data& vocals_out,
    waveform_data& instrumental_out);

} // namespace spectral_waveform
