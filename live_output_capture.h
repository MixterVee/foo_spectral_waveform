#pragma once

#include "waveform_data.h"

namespace spectral_waveform::live_output_capture {

// Returns true when a post-DSP waveform point has been captured for the
// requested playback time. Captured points represent the audio that actually
// reached foobar2000's playback stream after DSP processing.
bool point_at(double seconds, waveform_point& out);

// Clears captured output, normally when a new track begins.
void reset();

} // namespace spectral_waveform::live_output_capture
