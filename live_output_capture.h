#pragma once

#include "waveform_data.h"

namespace spectral_waveform::live_output_capture {

struct stem_analysis_status {
    int mode = 0;
    bool active = false;
    bool ready = false;
    int progress_percent = 0;
};

// Returns true when a progressive stem-preview point is available for the
// requested playback time. In Original mode this deliberately returns false
// so the untouched original waveform remains the renderer's source.
bool point_at(double seconds, waveform_point& out);

// Aggregates the progressive stem preview in [start_seconds, end_seconds).
// Newly completed analysis blocks are visually crossfaded from the previous
// preview so progressive stem generation morphs smoothly rather than snapping.
bool aggregate(double start_seconds, double end_seconds, waveform_point& out);

// True for the short interval after a progressive preview update. The UI uses
// this to repaint at its normal timer cadence while the visual crossfade runs.
bool animation_active();

// Snapshot of the current progressive Vocals/Instrumental waveform build.
// progress_percent counts completed 5-second stem-analysis targets, not an
// invented percentage inside one Spleeter inference call.
stem_analysis_status analysis_status();

// Re-read the current Stem Separator mode and publish/select its preview now.
// Used by controls embedded in the waveform so paused playback updates instantly.
void refresh_mode();

// Clears the active previews and invalidates the current stem-analysis
// generation so stale workers cannot recreate caches during manual recovery.
void reset();

} // namespace spectral_waveform::live_output_capture
