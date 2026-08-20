#include "common/Capability.h"
#include "common/PhysicsQuery.h"
#include "physics/Physics.h"
#include "physics/World.h"

#include <string>
#include <vector>

namespace eve::physics {
namespace {

class PhysicsQueryImpl final : public eve::IPhysicsQuery {
public:
    int newWorld(float gravityX, float gravityY) override {
        auto *ph = eve::ModuleManager::getInstance<Physics>("Physics");
        if (!ph) return -1;
        auto *w = ph->newWorld(gravityX, gravityY);
        if (!w) return -1;
        worlds_.push_back(w);
        return static_cast<int>(worlds_.size()) - 1;
    }

    int worldCount() override {
        int n = 0;
        for (auto *w : worlds_)
            if (w) ++n;
        return n;
    }

    bool worldGravity(int id, float *gx, float *gy) override {
        auto *w = world(id);
        if (!w) return false;
        if (gx) *gx = w->getGravityX();
        if (gy) *gy = w->getGravityY();
        return true;
    }

    bool rayCast(int id, float x1, float y1, float x2, float y2, RayHitInfo *out) override {
        auto *w = world(id);
        if (!w || !out) return false;
        w->rayCast(x1, y1, x2, y2);
        out->hit = w->hasRayHit();
        if (out->hit) {
            out->bodyId = w->getRayHitBodyId();
            out->x = w->getRayHitX();
            out->y = w->getRayHitY();
            out->normalX = w->getRayHitNormalX();
            out->normalY = w->getRayHitNormalY();
            out->fraction = w->getRayHitFraction();
        }
        return true;
    }

    bool removeWorld(int id) override {
        if (id < 0 || static_cast<size_t>(id) >= worlds_.size()) return false;
        delete worlds_[static_cast<size_t>(id)];
        worlds_[static_cast<size_t>(id)] = nullptr;
        return true;
    }

private:
    World *world(int id) const {
        if (id < 0 || static_cast<size_t>(id) >= worlds_.size()) return nullptr;
        return worlds_[static_cast<size_t>(id)];
    }

    std::vector<World *> worlds_;
};

}  // namespace

void registerPhysicsCapabilities() {
    static PhysicsQueryImpl impl;
    eve::cap::provide<eve::IPhysicsQuery>(&impl);
}

}  // namespace eve::physics
