#include <foobar2000/SDK/foobar2000.h>
#include "spectral_analyzer.h"
#include "spectral_palette.h"

DECLARE_COMPONENT_VERSION(
    "Spectral Waveform",
    "0.1.0-alpha",
    "Serato-inspired spectral waveform seekbar for foobar2000.\n"
    "\n"
    "Initial development build: component skeleton + spectral analysis core."
);

VALIDATE_COMPONENT_FILENAME("foo_spectral_waveform.dll");

namespace {

class spectral_initquit final : public initquit {
public:
    void on_init() override {
        console::print("foo_spectral_waveform: v0.1.0-alpha loaded");
    }

    void on_quit() override {}
};

static initquit_factory_t<spectral_initquit> g_initquit_factory;

} // namespace
