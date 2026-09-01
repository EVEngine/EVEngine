// Drives streaming-source refills once per event pump.
//
// Audio::pump has nothing to do with platform events, but it does need a
// per-frame tick and the event pump was the only one available -- so the event
// module called into audio. Hanging it off the sink's end-of-pump hook keeps
// the timing identical without the dependency.

#include "audio/Audio.h"
#include "common/Capability.h"
#include "common/Module.h"
#include "platform_event/PlatformEventSink.h"

namespace eve::audio {
namespace {

class AudioPumpSink : public eve::platform_event::IPlatformEventSink {
public:
    void onPumpFinished() override {
        if (auto *audio = eve::ModuleManager::getInstance<Audio>("Audio")) audio->pump();
    }
};

struct Register {
    Register() {
        static AudioPumpSink sink;
        eve::cap::addListener<eve::platform_event::IPlatformEventSink>(&sink);
    }
} g_register;

}  // namespace
}  // namespace eve::audio
