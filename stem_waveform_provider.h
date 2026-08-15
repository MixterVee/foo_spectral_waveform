#pragma once

#include <foobar2000/SDK/foobar2000.h>

class NOVTABLE stem_waveform_provider : public service_base {
    FB2K_MAKE_SERVICE_INTERFACE_ENTRYPOINT(stem_waveform_provider);
public:
    virtual int get_mode() = 0;
    virtual bool process_both(
        const float* input,
        t_size frames,
        unsigned channels,
        unsigned sample_rate,
        pfc::array_t<float>& vocals,
        pfc::array_t<float>& instrumental,
        abort_callback& aborter) = 0;
};
