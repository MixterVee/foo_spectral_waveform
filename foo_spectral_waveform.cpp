#include <foobar2000/SDK/foobar2000.h>
#include "spectral_analyzer.h"
#include "spectral_palette.h"

DECLARE_COMPONENT_VERSION(
    "Spectral Waveform",
    "0.2.0-alpha",
    "Serato-inspired spectral waveform seekbar for foobar2000.\n"
    "\n"
    "v0.2 development build: Default UI element, playback cursor and click-to-seek."
);

VALIDATE_COMPONENT_FILENAME("foo_spectral_waveform.dll");

namespace {

class spectral_initquit : public initquit {
public:
    void on_init() override {
        console::print("foo_spectral_waveform: v0.2.0-alpha loaded");
    }

    void on_quit() override {}
};

static initquit_factory_t<spectral_initquit> g_initquit_factory;

} // namespace
