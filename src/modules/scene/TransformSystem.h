#pragma once

namespace eve::scene {

class SceneHost;

/**
 * Propagates local TRS → world matrices for all SceneHost trees (or one host).
 * Call after mount/reconcile or local transform edits.
 */
class TransformSystem {
public:
    static void updateAll();
    static void updateHost(SceneHost *host);
};

}  // namespace eve::scene
