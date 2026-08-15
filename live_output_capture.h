#pragma once

#include "waveform_data.h"

namespace spectral_waveform::live_output_capture {

// Returns true when a progressive stem-preview point is available for the
// requested playback time. In Original mode this deliberately returns false
// so the untouched original waveform remains the renderer's source.
bool point_at(double seconds, waveform_point& out);

// Aggregates the progressive stem preview in [start_seconds, end_seconds).
// Unprocessed areas fall back to the normal cached original waveform.
bool aggregate(double start_seconds, double end_seconds, waveform_point& out);

// Clears the active stem preview.
void reset();

} // namespace spectral_waveform::live_output_capture
