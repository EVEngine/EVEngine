// Publishes the event queue as the IMainThreadPost capability, so the thread
// module can hand results back to the main thread without depending on event.

#include "common/MainThreadPost.h"
#include "common/Capability.h"
#include "common/Module.h"
#include "platform_event/PlatformEvent.h"

#include <string>

namespace eve::platform_event {
namespace {

class EventQueuePoster : public eve::caps::IMainThreadPost {
public:
    void prepare() override { resolve(); }

    void postToMainThread(std::string name, std::string data) override {
        // Never touch the module registry here: this runs on a worker thread.
        // prepare() is contracted to have resolved the queue already.
        PlatformEvent *ev = queue_;
        if (!ev) return;
        ev->pushData(std::move(name), std::move(data));
    }

private:
    /** Runs on the submitting thread only; PlatformEvent::pushData itself is locked. */
    void resolve() {
        if (queue_) return;
        PlatformEvent *ev = eve::ModuleManager::getInstance<PlatformEvent>("PlatformEvent");
        if (!ev) ev = PlatformEvent::create();
        queue_ = ev;
    }

    PlatformEvent *queue_ = nullptr;
};

struct Register {
    Register() {
        static EventQueuePoster poster;
        eve::cap::provide<eve::caps::IMainThreadPost>(&poster);
    }
} g_register;

}  // namespace
}  // namespace eve::platform_event
