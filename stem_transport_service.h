#pragma once

#include <foobar2000/SDK/foobar2000.h>
#include <cstdint>

enum stem_transport_state : int {
    stem_transport_normal = 0,
    stem_transport_hold = 1,
    stem_transport_scrub = 2,
    stem_transport_reverse = 3,
    stem_transport_release_wait = 4,
};

enum stem_transport_debug_source : int {
    stem_debug_source_none = 0,
    stem_debug_source_live = 1,
    stem_debug_source_cache = 2,
    stem_debug_source_miss = 3,
};

struct stem_transport_debug_status {
    int state = stem_transport_normal;
    int mode = 0;
    int last_render_source = stem_debug_source_none;
    int last_render_ok = 0;

    double position_seconds = 0.0;
    double render_seconds = 0.0;
    double scrub_velocity = 0.0;
    double last_render_start_seconds = 0.0;
    double last_source_rate = 0.0;
    double live_start_seconds = -1.0;
    double live_end_seconds = -1.0;

    uint64_t render_attempts = 0;
    uint64_t render_successes = 0;
    uint64_t live_hits = 0;
    uint64_t cache_hits = 0;
    uint64_t render_misses = 0;
    uint64_t scrub_audio_writes = 0;
    uint64_t scrub_silence_writes = 0;
};

class NOVTABLE stem_transport_service : public service_base {
    FB2K_MAKE_SERVICE_INTERFACE_ENTRYPOINT(stem_transport_service);
public:
    virtual void set_hold(double position_seconds) = 0;
    virtual void set_scrub(double position_seconds) = 0;
    virtual void set_reverse(double position_seconds) = 0;
    virtual void release_transport(double position_seconds) = 0;
    virtual void cancel_transport() = 0;

    virtual int get_state() = 0;
    virtual double get_position_seconds() = 0;
    virtual bool is_position_ready(double position_seconds) = 0;
    virtual bool get_debug_status(stem_transport_debug_status& out) = 0;

    virtual bool publish_cache_block(
        const char* track_path_utf8,
        double start_seconds,
        const float* original,
        const float* vocals,
        const float* instrumental,
        t_size frames,
        unsigned channels,
        unsigned sample_rate) = 0;
};
