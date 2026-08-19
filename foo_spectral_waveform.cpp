#include <foobar2000/SDK/foobar2000.h>
#include "spectral_analyzer.h"
#include "spectral_palette.h"

DECLARE_COMPONENT_VERSION(
    "Spectral Waveform",
    "0.5.0 Stable Stem Blend Menu",
    "Serato-inspired spectral waveform for foobar2000.\n"
    "Bass / mids / treble coloring, zoom and follow modes, time markers, persistent analysis cache, and stem-aware post-DSP updates.\n"
    "Mirrors Stem Separator controls including cache settings, exports, pre-cache and backend benchmark; drag/hold audition is intentionally muted and the seek is committed on release."
);

VALIDATE_COMPONENT_FILENAME("foo_spectral_waveform.dll");

namespace {

class spectral_initquit : public initquit {
public:
    void on_init() override {
        console::print("foo_spectral_waveform: v0.5.0 loaded");
    }

    void on_quit() override {}
};

static initquit_factory_t<spectral_initquit> g_initquit_factory;

} // namespace