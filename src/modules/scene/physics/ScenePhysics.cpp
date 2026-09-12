#include "scene/physics/ScenePhysics.h"

#include "physics/ArtifactProvider.h"
#include "physics/World3D.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <stdexcept>

namespace eve::scene_physics {

Module_IMPL(ScenePhysics, new ScenePhysics());

eve::Result<void> ScenePhysics::bindGeneratedColliders(eve::physics::World3D& world) {
    auto result = eve::physics::physicsArtifactProvider().bindWorld(world);
    if (!result) return eve::Result<void>::failure(result.status());
    return result;
}

eve::Result<void> ScenePhysics::unbindGeneratedColliders() {
    const auto bound = eve::physics::physicsArtifactProvider().boundWorld();
    if (bound.isInvalid())
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::NoOp));
    auto result = eve::physics::physicsArtifactProvider().unbindWorld(bound);
    if (!result) return eve::Result<void>::failure(result.status());
    return result;
}

eve::physics::PhysicsWorldHandle ScenePhysics::boundWorld() const noexcept {
    return eve::physics::physicsArtifactProvider().boundWorld();
}

void ScenePhysics::expose(ssq::Table& table) {
    auto module = table.addClass(name, ScenePhysics::create, false);
    expose(module);
}

void ScenePhysics::expose(ssq::Class& cls) {
    cls.addFunc("bindGeneratedColliders", [](ScenePhysics* self, eve::physics::World3D* world) {
        if (!self || !world) throw std::runtime_error("shared collider binding requires a live World3D");
        auto result = self->bindGeneratedColliders(*world);
        if (!result) throw std::runtime_error(result.status().describe());
    });
    cls.addFunc("unbindGeneratedColliders", [](ScenePhysics* self) {
        if (!self) throw std::runtime_error("scene physics module is unavailable");
        auto result = self->unbindGeneratedColliders();
        if (!result) throw std::runtime_error(result.status().describe());
    });
    cls.addFunc("isGeneratedColliderWorldBound", [](ScenePhysics* self) {
        return self && !self->boundWorld().isInvalid();
    });
}

}  // namespace eve::scene_physics
