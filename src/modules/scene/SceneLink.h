#pragma once

// Attaching things to scene nodes.
//
// A link keeps an external object -- a renderable, a physics body, a camera, an
// audio source -- in step with the node it hangs off. The set of kinds used to
// be a closed enum with a switch in TransformSystem, so scene had to include
// graphics, physics and audio, and no other module could ever attach to the
// tree without editing this file.
//
// Kinds are registered instead. The module that owns the target type supplies
// the three operations, keeps the casts on its own side where the type is
// complete, and scene never learns what a Renderable3D is.
//
// Dispatch is through a function-pointer table, not virtual calls: link sync
// runs for every dirty node every frame.

#include "common/Export.h"

#include <cstddef>

namespace eve::scene {

struct SceneNode;

/** Behaviour for one kind of link target, supplied by the owning module. */
struct LinkOps {
    /** Stable name, e.g. "renderable3d". Registration is idempotent by name. */
    const char* kind = nullptr;

    /** Node -> target, after the node's world matrix is recomputed. */
    void (*pushWorld)(const SceneNode& node, void* target) = nullptr;

    /**
     * Target -> node, before world propagation, for links whose target is
     * authoritative (syncMode 1). Null when the kind cannot drive a node.
     */
    void (*pullWorld)(SceneNode& node, void* target) = nullptr;

    /**
     * Whether the target is still alive. Dead links are dropped rather than
     * followed into freed memory. Null means always alive.
     */
    bool (*alive)(const void* target) = nullptr;
};

/**
 * Register a link kind and return its id. Registering the same name twice
 * returns the first id and leaves the original operations in place, so a
 * module reloaded as a plugin cannot silently swap them out.
 */
EVENGINE_API int registerLinkKind(const LinkOps& ops);

/** Id for a previously registered kind, or -1. */
EVENGINE_API int findLinkKind(const char* kind);

/** Operations for an id, or nullptr when the id is unknown. */
EVENGINE_API const LinkOps* linkOps(int kindId);

/** Name for an id, or "" when the id is unknown. */
EVENGINE_API const char* linkKindName(int kindId);

/**
 * One link between a scene node and an external object. The node may hold
 * several links of different kinds; re-linking the same kind replaces the
 * target. syncMode: 0 = node drives target, 1 = target drives node.
 */
struct SceneLink {
    int kind = -1;
    void* target = nullptr;
    int syncMode = 0;
};

}  // namespace eve::scene
