// Drives streaming-source refills once per event pump.
//
// Audio::pump has nothing to do with platform events, but it does need a
// per-frame tick and the event pump was the only one available -- so the event
// module called into audio. Hanging it off the sink's end-of-pump hook keeps
// the timing identical without the dependency. The Audio module binds itself as
// owner at construction so the hot path never looks up ModuleManager.

#include "audio/Audio.h"
#include "audio/EventSinkOwner.h"
#include "common/Capability.h"
#include "platform_event/PlatformEventSink.h"

namespace eve::audio {
namespace {

Audio* g_owner = nullptr;

class AudioPumpSink : public eve::platform_event::IPlatformEventSink {
public:
    void onPumpFinished() override {
        if (g_owner) g_owner->pump();
    }
};

struct Register {
    Register() {
        static AudioPumpSink sink;
        eve::cap::addListener<eve::platform_event::IPlatformEventSink>(&sink);
    }
} g_register;

}  // namespace

void bindAudioEventSinkOwner(Audio* owner) noexcept { g_owner = owner; }

}  // namespace eve::audio
