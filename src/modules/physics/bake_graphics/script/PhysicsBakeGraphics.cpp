#include "physics/bake_graphics/script/PhysicsBakeGraphics.h"
#include "common/BorrowedRef.h"
#include "common/SquirrelBinding.h"
#include "common/SquirrelOwnership.h"
#include "graphics/Graphics.h"
#include "graphics/Mesh.h"
#include "graphics/RenderSystem.h"
#include "physics/backend/bake/BakedParticlePlayback.h"
#include "physics/backend/bake/DeformPlayback.h"
#include "physics/backend/bake/RigidPlayback.h"
#include "physics/bake_graphics/DeformMeshPresentation.h"
#include "physics/bake_graphics/ParticleBakePresentation.h"
#include "physics/bake_graphics/RigidBakePresentation.h"
namespace eve::physics {
namespace {
Diagnostic invalid(const char* text) {
    return Diagnostic::error(DiagnosticCode::InvalidArgument, text, "physicsBakeGraphics.script");
}
template <class T>
ResultRef<T> argument(const ssq::Object& object) {
    if (object.getType() == ssq::Type::INSTANCE && object.getTypeTag() == script::detail::squirrelTypeHash<T*>()) {
        try {
            if (auto* value = object.to<T*>()) return ResultRef<T>::success(std::ref(*value));
        } catch (const ssq::TypeException&) { /* Invalid native instances fail before target mutation. */
        }
    }
    return ResultRef<T>::failure(invalid("null or wrong native player/target type"));
}
bool isVmOwnedPlayer(HSQUIRRELVM vm, const ssq::Object& object) {
    sq_pushobject(vm, object.getRaw());
    const bool owned = sq_getreleasehook(vm, -1) != nullptr;
    sq_pop(vm, 1);
    return owned;
}
template <class Target>
Result<void> submit(const ssq::Object& playerObject, const std::string& identity, const ssq::Object& targetObject) {
    auto player = argument<RigidPlayback>(playerObject);
    if (!player) return Result<void>::failure(player.status());
    auto target = argument<Target>(targetObject);
    if (!target) return Result<void>::failure(target.status());
    const auto id = PersistentId::parse(identity);
    if (!id || id->isNil()) return Result<void>::failure(invalid("expected non-nil persistent UUID"));
    return submitRigidPlayback(player.value().get(), *id, target.value().get());
}
template <class T>
void exposeControls(ssq::Class& cls) {
    const auto vm = cls.getHandle();
    cls.addFunc("advance",
                [vm](T* self, float seconds) { return script::projectResult(vm, self->player().advance(seconds)); });
    cls.addFunc("seek",
                [vm](T* self, float seconds) { return script::projectResult(vm, self->player().seek(seconds)); });
    cls.addFunc("setRate",
                [vm](T* self, float rate) { return script::projectResult(vm, self->player().setRate(rate)); });
    cls.addFunc("setLoop", [vm](T* self, bool loop) {
        return script::projectResult(vm,
                                     self->player().setWrap(loop ? BakePlaybackWrap::Loop : BakePlaybackWrap::Clamp));
    });
    cls.addFunc("setPaused", [](T* self, bool paused) { self->player().setPaused(paused); });
    cls.addFunc("paused", [](T* self) { return self->player().paused(); });
    cls.addFunc("time", [](T* self) { return static_cast<float>(self->player().time()); });
    cls.addFunc("rate", [](T* self) { return static_cast<float>(self->player().rate()); });
    cls.addFunc("duration", [](T* self) { return self->player().track().duration(); });
    cls.addFunc("setPlacement", [vm](T* self, float x, float y, float z, float qx, float qy, float qz, float qw) {
        return script::projectResult(vm, self->player().setPlacement({x, y, z, qx, qy, qz, qw}));
    });
}
// Root the supplied VM player; presentation controls and drawing use that same state.
struct ScriptDeformPresentation {
    ssq::Object                             playerOwner;
    std::unique_ptr<DeformMeshPresentation> presentation;
    DeformPlayback&                         player() { return *playerOwner.to<DeformPlayback*>(); }
};
void exposeDeformPresentation(ssq::Table& table) {
    const auto vm  = table.getHandle();
    auto       cls = table.addClass<ScriptDeformPresentation>(
        "BakedDeformPresentation",
        std::function<ScriptDeformPresentation*()>([]() -> ScriptDeformPresentation* { return nullptr; }), true);
    exposeControls<ScriptDeformPresentation>(cls);
    cls.addFunc("createMesh", [vm](ScriptDeformPresentation* self, const ssq::Object& graphicsObject) {
        auto graphics = argument<graphics::Graphics>(graphicsObject);
        if (!graphics) return script::projectStatusResult(vm, graphics.status());
        auto mesh = self->presentation->createMesh(self->player(), graphics.value().get());
        if (!mesh) return script::projectStatusResult(vm, mesh.status());
        // Mesh remains owned by Graphics, exactly as for Graphics.newMeshFromArrays.
        // No presentation or VM finalizer may delete this borrowed facade.
        auto result = script::projectStatusResult(vm, mesh.status());
        result.set("value", mesh.value());
        script::markResultHasValue(result);
        result.set("ownership", std::string("graphics-owned"));
        return result;
    });
    cls.addFunc("upload",
                [vm](ScriptDeformPresentation* self, const ssq::Object& graphicsObject, const ssq::Object& meshObject) {
                    auto graphics = argument<graphics::Graphics>(graphicsObject);
                    if (!graphics) return script::projectStatusResult(vm, graphics.status());
                    auto mesh = argument<graphics::Mesh>(meshObject);
                    if (!mesh) return script::projectStatusResult(vm, mesh.status());
                    return script::projectResult(
                        vm, self->presentation->upload(self->player(), graphics.value().get(), mesh.value().get()));
                });
}

// Only values and prepared CPU buffers survive calls. Material resources are
// borrowed afresh on draw and cleared before returning, including error paths.
struct ScriptParticlePresentation {
    ssq::Object                               playerOwner;
    std::unique_ptr<ParticleBakePresentation> presentation;
    std::vector<PersistentId>                 identities;
    std::vector<graphics::DrawItem2D>         templates;
    std::vector<ParticleBakeMaterial>         bindings;
    BakedParticlePlayback&                    player() { return *playerOwner.to<BakedParticlePlayback*>(); }
};
Result<std::size_t> drawParticles(ScriptParticlePresentation& self, const ssq::Object& graphicsObject,
                                  const ssq::Object& materialObjects) {
    auto graphics = argument<graphics::Graphics>(graphicsObject);
    if (!graphics) return Result<std::size_t>::failure(graphics.status());
    if (materialObjects.getType() != ssq::Type::ARRAY)
        return Result<std::size_t>::failure(invalid("expected an array of live material sprites"));
    struct Clear {
        ScriptParticlePresentation& self;
        ~Clear() {
            self.bindings.clear();
            self.templates.clear();
        }
    } clear{self};
    auto materials = materialObjects.toArray();
    if (materials.size() != self.identities.size())
        return Result<std::size_t>::failure(invalid("material count must match prepared emitter identities"));
    for (std::size_t i = 0; i < self.identities.size(); ++i) {
        auto target = argument<graphics::Renderable2D>(materials.get<ssq::Object>(i));
        if (!target) return Result<std::size_t>::failure(target.status());
        auto                 sprite = target.value().get().sprite();
        graphics::DrawItem2D material;
        material.texture      = sprite->texture;
        material.normal       = sprite->normalTexture;
        material.shader       = sprite->shader;
        material.canvas       = sprite->canvas;
        material.camera       = sprite->camera;
        material.layer        = sprite->layer;
        material.blend        = sprite->blend;
        material.receiveLight = sprite->receiveLight;
        material.litPath      = sprite->receiveLight && sprite->normalTexture && sprite->texture && !sprite->shader;
        self.templates.push_back(material);
        self.bindings.push_back({self.identities[i], self.templates.back()});
    }
    return self.presentation->draw(self.player(), graphics.value().get(), self.bindings);
}
void exposeParticlePresentation(ssq::Table& table) {
    const auto vm  = table.getHandle();
    auto       cls = table.addClass<ScriptParticlePresentation>(
        "BakedParticlePresentation",
        std::function<ScriptParticlePresentation*()>([]() -> ScriptParticlePresentation* { return nullptr; }), true);
    exposeControls<ScriptParticlePresentation>(cls);
    cls.addFunc("draw",
                [vm](ScriptParticlePresentation* self, const ssq::Object& graphics, const ssq::Object& materials) {
                    return script::projectResult(vm, drawParticles(*self, graphics, materials), [](std::size_t count) {
                        return Value::integer(static_cast<std::int64_t>(count));
                    });
                });
}
ssq::Table prepareParticles(HSQUIRRELVM vm, const ssq::Object& playerObject, const ssq::Object& identitiesObject) {
    auto player = argument<BakedParticlePlayback>(playerObject);
    if (!player) return script::projectStatusResult(vm, player.status());
    if (!isVmOwnedPlayer(vm, playerObject))
        return script::projectStatusResult(
            vm, Status::failure(invalid("presentation requires a VM-owned player; use prepareInstance")));
    if (identitiesObject.getType() != ssq::Type::ARRAY)
        return script::projectStatusResult(vm, Status::failure(invalid("expected emitter UUID array")));
    try {
        auto identities = identitiesObject.toArray();
        if (identities.size() > 65536)
            return script::projectStatusResult(vm, Status::failure(invalid("emitter identity budget exceeded")));
        auto result = std::make_unique<ScriptParticlePresentation>();
        result->identities.reserve(identities.size());
        for (std::size_t i = 0; i < identities.size(); ++i) {
            auto value = identities.get<ssq::Object>(i);
            if (value.getType() != ssq::Type::STRING)
                return script::projectStatusResult(vm, Status::failure(invalid("emitter UUID must be a string")));
            const auto id = PersistentId::parse(value.to<std::string>());
            if (!id || id->isNil() || (!result->identities.empty() && !(result->identities.back() < *id)))
                return script::projectStatusResult(
                    vm, Status::failure(invalid("emitter UUIDs must be sorted, unique and non-nil")));
            result->identities.push_back(*id);
        }
        auto prepared = ParticleBakePresentation::prepare(player.value().get().track());
        if (!prepared) return script::projectStatusResult(vm, prepared.status());
        result->playerOwner  = playerObject;
        result->presentation = std::move(prepared).takeValue();
        result->templates.reserve(result->identities.size());
        result->bindings.reserve(result->identities.size());
        auto object = script::makeOwnedSquirrelInstance<ScriptParticlePresentation>(vm, std::move(result));
        if (!object) return script::projectStatusResult(vm, object.status());
        return script::projectStatusResult(vm, Status::success(), std::move(object).takeValue());
    } catch (const std::bad_alloc&) {
        return script::projectStatusResult(
            vm, Status::failure(
                    Diagnostic::error(DiagnosticCode::Failed, "particle script presentation allocation failed")));
    }
}
}  // namespace
Module_IMPL(PhysicsBakeGraphics, new PhysicsBakeGraphics());
void PhysicsBakeGraphics::expose(ssq::Table& table) {
    exposeDeformPresentation(table);
    exposeParticlePresentation(table);
    auto cls = table.addClass(name, PhysicsBakeGraphics::create, false);
    expose(cls);
}
void PhysicsBakeGraphics::expose(ssq::Class& cls) {
    const auto vm = cls.getHandle();
    cls.addFunc("prepareParticles",
                [vm](PhysicsBakeGraphics*, const ssq::Object& player, const ssq::Object& identities) {
                    return prepareParticles(vm, player, identities);
                });
    cls.addFunc("prepareDeform", [vm](PhysicsBakeGraphics*, const ssq::Object& playerObject, float ropeRadius) {
        try {
            auto player = argument<DeformPlayback>(playerObject);
            if (!player) return script::projectStatusResult(vm, player.status());
            if (!isVmOwnedPlayer(vm, playerObject))
                return script::projectStatusResult(
                    vm, Status::failure(invalid("presentation requires a VM-owned player; use prepareInstance")));
            auto prepared = DeformMeshPresentation::prepare(player.value().get().track(), ropeRadius);
            if (!prepared) return script::projectStatusResult(vm, prepared.status());
            auto wrapper          = std::make_unique<ScriptDeformPresentation>();
            wrapper->playerOwner  = playerObject;
            wrapper->presentation = std::move(prepared).takeValue();
            auto object           = script::makeOwnedSquirrelInstance<ScriptDeformPresentation>(vm, std::move(wrapper));
            if (!object) return script::projectStatusResult(vm, object.status());
            return script::projectStatusResult(vm, Status::success(), std::move(object).takeValue());
        } catch (const std::bad_alloc&) {
            return script::projectStatusResult(
                vm,
                Status::failure(Diagnostic::error(DiagnosticCode::Failed, "deformation presentation allocation failed",
                                                  "physicsBakeGraphics.script")));
        }
    });
    cls.addFunc("submitRigid2D", [vm](PhysicsBakeGraphics*, const ssq::Object& player, const std::string& identity,
                                      const ssq::Object& target) {
        return script::projectResult(vm, submit<graphics::Renderable2D>(player, identity, target));
    });
    cls.addFunc("submitRigid3D", [vm](PhysicsBakeGraphics*, const ssq::Object& player, const std::string& identity,
                                      const ssq::Object& target) {
        return script::projectResult(vm, submit<graphics::Renderable3D>(player, identity, target));
    });
}
}  // namespace eve::physics
