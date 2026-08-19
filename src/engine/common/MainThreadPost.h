#pragma once

// Hand a message back to the main thread from a worker.
//
// The thread module's postMain / submitPost need to enqueue onto the event
// queue, which would make the thread pool depend on the event module. Going
// through a capability keeps that edge out and lets a build without the event
// module still use the pool for pure compute.

#include "common/Export.h"

#include <string>

namespace eve::caps {

class EVENGINE_API IMainThreadPost {
public:
    static constexpr const char* capabilityName = "IMainThreadPost";
    virtual ~IMainThreadPost() = default;

    /**
     * Resolve any lazily created backing state. Must be called on the
     * submitting thread before a worker uses this poster: the provider may need
     * the module registry, which is not thread-safe.
     */
    virtual void prepare() = 0;

    /** Queue a named message for the main thread. Safe from any thread once prepare() has run. */
    virtual void postToMainThread(std::string name, std::string data) = 0;
};

}  // namespace eve::caps
