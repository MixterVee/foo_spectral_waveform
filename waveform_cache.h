#pragma once

#include <foobar2000/SDK/foobar2000.h>
#include "waveform_data.h"

namespace spectral_waveform {

// Loads/saves versioned waveform analysis in the foobar2000 profile folder.
// Cache validity is tied to the track path/subsong plus source file stats when available.
bool load_waveform_cache(metadb_handle_ptr track, waveform_data& out, abort_callback& aborter);
void save_waveform_cache(metadb_handle_ptr track, const waveform_data& data, abort_callback& aborter);

} // namespace spectral_waveform
