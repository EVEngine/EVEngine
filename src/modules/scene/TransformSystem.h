#pragma once
#include "common/Export.h"


namespace eve::scene {

class SceneHost;

/**
 * @brief Propagates local TRS → world matrices for all SceneHost trees (or one host).
 * Call after mount/reconcile or local transform edits.
 */
class EVENGINE_API TransformSystem {
public:
    static void updateAll();
    static void updateHost(SceneHost *host);
};

}  // namespace eve::scene
