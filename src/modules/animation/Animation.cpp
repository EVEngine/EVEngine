#include "animation/Animation.h"

#include <algorithm>
#include <simplesquirrel/simplesquirrel.hpp>

namespace eve::animation {

Module_IMPL(Animation, new Animation());

Animation::~Animation() {
    // Tweens may outlive the module if script GC still holds them; detach.
    for (Tween *t : tweens_) {
        if (t) t->setOwner(nullptr);
    }
    tweens_.clear();
}

Tween *Animation::newTween(float duration) {
    auto *t = new Tween(duration);
    registerTween(t);
    return t;
}

void Animation::registerTween(Tween *t) {
    if (!t) return;
    t->setOwner(this);
    if (std::find(tweens_.begin(), tweens_.end(), t) == tweens_.end()) tweens_.push_back(t);
}

void Animation::unregisterTween(Tween *t) {
    if (!t) return;
    auto it = std::find(tweens_.begin(), tweens_.end(), t);
    if (it != tweens_.end()) tweens_.erase(it);
    if (t->owner() == this) t->setOwner(nullptr);
}

void Animation::update(float dt) {
    // Copy pointer list: a tween destructor during update must not invalidate iteration.
    std::vector<Tween *> snapshot = tweens_;
    for (Tween *t : snapshot) {
        if (!t) continue;
        if (t->isActive()) t->update(dt);
    }
}

int Animation::getActiveCount() const {
    int n = 0;
    for (const Tween *t : tweens_) {
        if (t && t->isActive()) ++n;
    }
    return n;
}

void Animation::clearFinished() {
    auto it = tweens_.begin();
    while (it != tweens_.end()) {
        Tween *t = *it;
        if (!t || t->isFinished() || t->isStopped()) {
            if (t && t->owner() == this) t->setOwner(nullptr);
            it = tweens_.erase(it);
        } else {
            ++it;
        }
    }
}

void Animation::clearAll() {
    for (Tween *t : tweens_) {
        if (t && t->owner() == this) t->setOwner(nullptr);
    }
    tweens_.clear();
}

void Animation::expose(ssq::Table &table) {
    auto cls = table.addClass(name, Animation::create, false);
    expose(cls);

    auto tw = table.addClass<Tween>(
        "Tween", std::function<Tween *()>([]() -> Tween * { return nullptr; }), true);
    tw.addFunc("setFrom", &Tween::setFrom);
    tw.addFunc("setTo", &Tween::setTo);
    tw.addFunc("setDelta", &Tween::setDelta);
    tw.addFunc("setFromAngle", &Tween::setFromAngle);
    tw.addFunc("setToAngle", &Tween::setToAngle);
    tw.addFunc("setDeltaAngle", &Tween::setDeltaAngle);
    tw.addFunc("has", &Tween::has);
    tw.addFunc("get", &Tween::get);
    tw.addFunc("getFrom", &Tween::getFrom);
    tw.addFunc("getTo", &Tween::getTo);
    tw.addFunc("getDelta", &Tween::getDelta);
    tw.addFunc("setDuration", &Tween::setDuration);
    tw.addFunc("getDuration", &Tween::getDuration);
    tw.addFunc("setDelay", &Tween::setDelay);
    tw.addFunc("getDelay", &Tween::getDelay);
    tw.addFunc("setEase", &Tween::setEase);
    tw.addFunc("getEase", &Tween::getEase);
    tw.addFunc("setRepeat", &Tween::setRepeat);
    tw.addFunc("getRepeat", &Tween::getRepeat);
    tw.addFunc("setYoyo", &Tween::setYoyo);
    tw.addFunc("getYoyo", &Tween::getYoyo);
    tw.addFunc("start", &Tween::start);
    tw.addFunc("pause", &Tween::pause);
    tw.addFunc("resume", &Tween::resume);
    tw.addFunc("stop", &Tween::stop);
    tw.addFunc("reset", &Tween::reset);
    tw.addFunc("isRunning", &Tween::isRunning);
    tw.addFunc("isPaused", &Tween::isPaused);
    tw.addFunc("isFinished", &Tween::isFinished);
    tw.addFunc("isStopped", &Tween::isStopped);
    tw.addFunc("isDelayed", &Tween::isDelayed);
    tw.addFunc("isActive", &Tween::isActive);
    tw.addFunc("getElapsed", &Tween::getElapsed);
    tw.addFunc("getProgress", &Tween::getProgress);
    tw.addFunc("getEasedProgress", &Tween::getEasedProgress);
    tw.addFunc("update", &Tween::update);
    tw.addFunc("evaluate", &Tween::evaluate);
    tw.addFunc("getPropertyCount", &Tween::getPropertyCount);
    tw.addFunc("getPropertyName", &Tween::getPropertyName);
}

void Animation::expose(ssq::Class &cls) {
    cls.addFunc("getName", &Animation::getName);
    cls.addFunc("newTween", &Animation::newTween);
    cls.addFunc("update", &Animation::update);
    cls.addFunc("getTweenCount", &Animation::getTweenCount);
    cls.addFunc("getActiveCount", &Animation::getActiveCount);
    cls.addFunc("clearFinished", &Animation::clearFinished);
    cls.addFunc("clearAll", &Animation::clearAll);
}

}  // namespace eve::animation
