#pragma once

// A module's hook into the raw platform event stream.
//
// The SDL pump used to translate every event itself, which meant it had to know
// about keyboard, joystick, touch, window, graphics, audio and ui -- eight
// upward dependencies from what should be one of the lowest modules, and the
// last dependency cycle in the engine.
//
// Now each module registers a sink for the events it owns. The pump drains the
// platform queue and offers each event to the sinks in priority order; whoever
// recognises it translates it. Modules the build does not contain simply have
// no sink, so the pump keeps working with any subset linked in.
//
// Providers depend on event (downwards, which is fine); event depends on none
// of them.
//
// `nativeEvent` is the backend's own event object -- SDL_Event* for the SDL
// backend. Only sinks built against the active backend are ever registered, so
// the cast is safe; the sinks in question are themselves backend code
// (keyboard/sdl, touch/sdl, ...).

#include "common/Capability.h"
#include "common/Export.h"

namespace eve::event {

class Message;

class EVENGINE_API IPlatformEventSink {
public:
    /** Dispatch order; lower runs first. */
    enum Priority {
        kObserver = 0,    // sees everything before anyone claims it (UI)
        kSurface = 10,    // window / swapchain lifecycle
        kInput = 20,      // keyboard, joystick, touch
    };

    static constexpr const char* capabilityName = "eve.event.IPlatformEventSink";
    virtual ~IPlatformEventSink() = default;

    /**
     * Inspect an event before translation. Return true to consume it, so no
     * later sink sees it and no Message is queued.
     */
    virtual bool observePlatformEvent(const void* nativeEvent) { return false; }

    /**
     * Translate an event into a queued Message, updating any module state it
     * implies. Return nullptr to leave the event to the next sink. Ownership of
     * a returned Message passes to the caller.
     */
    virtual Message* translatePlatformEvent(const void* nativeEvent) { return nullptr; }

    /** Called once per pump() after the platform queue drains. */
    virtual void onPumpFinished() {}
};

}  // namespace eve::event
