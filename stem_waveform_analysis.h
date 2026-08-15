#pragma once

#include "waveform_data.h"
#include <foobar2000/SDK/foobar2000.h>
#include <functional>

namespace spectral_waveform {

// Returns -1 when the Stem Separator provider is unavailable.
int current_stem_mode();

// Replaces the original waveform with the selected stem in sequential blocks.
// The callback is invoked after each completed block so the UI can show the
// stem waveform filling ahead without waiting for the whole track.
bool analyze_stem_progressive(
    metadb_handle_ptr track,
    int mode,
    const waveform_data& original,
    abort_callback& aborter,
    const std::function<void(const waveform_data&)>& on_update);

} // namespace spectral_waveform
