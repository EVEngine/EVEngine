#pragma once

#include "common/Module.h"
#include "animation/Tween.h"

#include <vector>

namespace eve::animation {

/**
 * Animation module — tween factory + per-frame pump.
 * Script: `anim <- eve.Animation();`
 *
 * Tweens own their property tracks; call `anim.update(dt)` (or `tween.update(dt)`)
 * each frame. Relative property changes use `Tween::setDelta` / `setDeltaAngle`.
 */
class Animation : public Module {
public:
    Module_REG(Animation);
    Animation() = default;
    ~Animation() override;

    /** Create a tween (duration in seconds). Returned pointer is owned by script GC. */
    Tween *newTween(float duration = 1.f);

    /** Advance all registered tweens. */
    void update(float dt);

    int getTweenCount() const { return static_cast<int>(tweens_.size()); }
    int getActiveCount() const;

    /** Drop finished/stopped entries from the registry (does not delete Tween objects). */
    void clearFinished();
    /** Detach all tweens from the registry (does not delete Tween objects). */
    void clearAll();

private:
    friend class Tween;

    void registerTween(Tween *t);
    void unregisterTween(Tween *t);

    std::vector<Tween *> tweens_;
};

}  // namespace eve::animation
