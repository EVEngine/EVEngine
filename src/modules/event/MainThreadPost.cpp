// Publishes the event queue as the IMainThreadPost capability, so the thread
// module can hand results back to the main thread without depending on event.

#include "common/Capability.h"
#include "common/MainThreadPost.h"
#include "common/Module.h"
#include "event/Event.h"

#include <string>

namespace eve::event {
namespace {

class EventQueuePoster : public eve::caps::IMainThreadPost {
public:
    void prepare() override { resolve(); }

    void postToMainThread(std::string name, std::string data) override {
        // Never touch the module registry here: this runs on a worker thread.
        // prepare() is contracted to have resolved the queue already.
        Event *ev = queue_;
        if (!ev) return;
        ev->pushData(std::move(name), std::move(data));
    }

private:
    /** Runs on the submitting thread only; Event::pushData itself is locked. */
    void resolve() {
        if (queue_) return;
        Event *ev = eve::ModuleManager::getInstance<Event>("Event");
        if (!ev) ev = Event::create();
        queue_ = ev;
    }

    Event *queue_ = nullptr;
};

struct Register {
    Register() {
        static EventQueuePoster poster;
        eve::cap::provide<eve::caps::IMainThreadPost>(&poster);
    }
} g_register;

}  // namespace
}  // namespace eve::event
