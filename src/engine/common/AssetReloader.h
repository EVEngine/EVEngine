#pragma once

// Hot reload: let each module react to its own asset files.
//
// The filesystem watcher knows a path changed but nothing about what lives at
// that path. It used to hard-code the answer -- reaching into graphics for
// textures, particles for emitter configs and map for tile layers -- which made
// the lowest-level module in the engine depend on three of the highest.
//
// Instead every module that owns an asset kind registers a reloader, and the
// watcher just dispatches. Modules the build does not contain simply are not
// registered.
//
// Ordering matters when several reloaders react to one file: an image change
// must refresh the GPU texture before the emitters and tile layers that sample
// it re-bind. Register with a priority to express that.

#include "common/Export.h"

#include <string>

namespace eve::caps {

class EVENGINE_API IAssetReloader {
public:
    /** Suggested priorities; lower runs first. */
    enum Priority {
        kTexture = 10,   // refresh GPU resources before their consumers re-bind
        kConsumer = 20,  // things that reference a reloaded resource
    };

    static constexpr const char* capabilityName = "IAssetReloader";
    virtual ~IAssetReloader() = default;

    /**
     * Stable name for this reloader ("texture" / "particle" / "tilemap").
     * A path bound to this kind is offered here regardless of its extension.
     */
    virtual const char* reloadKind() const = 0;

    /** Whether this reloader claims `normPath` during automatic dispatch. */
    virtual bool handlesPath(const std::string& normPath) const = 0;

    /** Reload everything referencing `normPath`. Returns true if anything changed. */
    virtual bool reload(const std::string& normPath) = 0;
};

}  // namespace eve::caps
