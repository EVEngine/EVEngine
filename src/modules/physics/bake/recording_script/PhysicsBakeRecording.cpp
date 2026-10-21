#include "physics/bake/recording_script/PhysicsBakeRecording.h"
#include <algorithm>
#include <functional>
#include "common/BorrowedRef.h"
#include "common/SquirrelBinding.h"
#include "common/SquirrelOwnership.h"
#include "physics/Body.h"
#include "physics/Body3D.h"
#include "physics/World.h"
#include "physics/World3D.h"
#include "physics/bake/BakeRecipe.h"
#include "physics/bake/DeformRecording.h"
#include "physics/bake/DeformTrack.h"
#include "physics/bake/RigidPoseTrack.h"
#include "physics/bake/RigidRecording.h"
#include "physics/bake/capture/RigidBakeCapture.h"
#include "physics/bake/deform_capture/DeformBakeCapture.h"
#include "physics/bake/script/PhysicsBake.h"
#include "physics/cloth/Cloth3D.h"
#include "physics/rope/Rope3D.h"
namespace eve::physics {
namespace {
Diagnostic invalid(const char* message) {
    return Diagnostic::error(DiagnosticCode::InvalidArgument, message, "physicsBakeRecording.script");
}
template <class T>
ResultRef<T> sourceArgument(const ssq::Object& object) {
    if (object.getType() == ssq::Type::INSTANCE && object.getTypeTag() == script::detail::squirrelTypeHash<T*>()) {
        try {
            if (auto* source = object.to<T*>()) return ResultRef<T>::success(std::ref(*source));
        } catch (const ssq::TypeException&) {
            // Malformed wrappers are rejected before any frame admission.
        }
    }
    return ResultRef<T>::failure(invalid("expected a live source of the requested native type"));
}
template <class T>
ssq::Table capture(HSQUIRRELVM vm, DeformRecording& recording, const ssq::Object& object, float seconds) {
    auto source = sourceArgument<T>(object);
    if (!source) return script::projectStatusResult(vm, source.status());
    auto frame = captureDeformFrame(source.value().get(), seconds);
    if (!frame) return script::projectStatusResult(vm, frame.status());
    return script::projectResult(vm, recording.append(std::move(frame).takeValue()));
}
struct ScriptRigidRecording {
    RigidRecording                recording;
    std::vector<RigidBakeBinding> bindings;
    explicit ScriptRigidRecording(RigidRecording value) : recording(std::move(value)) {}
};
template <class WorldType, class BodyType>
Result<void> bindRigid(ScriptRigidRecording& session, const ssq::Object& worldObject, const ssq::Object& bodyObject,
                       const std::string& identity, RigidTrackSpace expectedSpace) {
    if (session.recording.space() != expectedSpace || session.recording.frameCount() != 0 ||
        session.bindings.size() >= 16384)
        return Result<void>::failure(invalid("wrong coordinate space, locked roster or binding budget exceeded"));
    auto world = sourceArgument<WorldType>(worldObject);
    if (!world) return Result<void>::failure(world.status());
    auto body = sourceArgument<BodyType>(bodyObject);
    if (!body) return Result<void>::failure(body.status());
    const auto id = PersistentId::parse(identity);
    if (!id || id->isNil()) return Result<void>::failure(invalid("expected a non-nil persistent UUID"));
    const auto& sourceWorld = world.value().get();
    const auto& sourceBody  = body.value().get();
    if (!sourceWorld.isValid() || !sourceBody.isValid() || sourceBody.getWorld() != &sourceWorld ||
        sourceWorld.findBody(sourceBody.runtimeHandle()) != &sourceBody)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::StaleHandle,
                                                       "source body is stale or belongs to another world",
                                                       "physicsBakeRecording.script"));
    const RigidBakeBinding candidate{*id, sourceWorld.runtimeHandle(), sourceBody.runtimeHandle()};
    if ((!session.bindings.empty() && session.bindings.front().world != candidate.world) ||
        std::any_of(session.bindings.begin(), session.bindings.end(),
                    [&](const auto& binding) { return binding.id == candidate.id || binding.body == candidate.body; }))
        return Result<void>::failure(invalid("mixed worlds or duplicate body/identity binding"));
    try {
        session.bindings.push_back(candidate);
    } catch (const std::bad_alloc&) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::Failed, "rigid binding allocation failed",
                                                       "physicsBakeRecording.script"));
    }
    return Result<void>::success();
}
template <class WorldType>
ssq::Table captureRigid(HSQUIRRELVM vm, ScriptRigidRecording& session, const ssq::Object& worldObject, float seconds,
                        RigidTrackSpace expectedSpace) {
    if (session.recording.space() != expectedSpace)
        return script::projectStatusResult(vm, Status::failure(invalid("wrong capture coordinate space")));
    auto world = sourceArgument<WorldType>(worldObject);
    if (!world) return script::projectStatusResult(vm, world.status());
    auto frame = captureRigidFrame(world.value().get(), session.bindings, seconds);
    if (!frame) return script::projectStatusResult(vm, frame.status());
    return script::projectResult(vm, session.recording.append(std::move(frame).takeValue()));
}
void exposeRigidRecording(ssq::Table& table) {
    const auto vm  = table.getHandle();
    auto       cls = table.addClass<ScriptRigidRecording>(
        "RigidBakeRecording", std::function<ScriptRigidRecording*()>([]() -> ScriptRigidRecording* { return nullptr; }),
        true);
    cls.addFunc("bindBody2D", [vm](ScriptRigidRecording* self, const ssq::Object& world, const ssq::Object& body,
                                   const std::string& id) {
        return script::projectResult(vm, bindRigid<World, Body>(*self, world, body, id, RigidTrackSpace::PlanarPixels));
    });
    cls.addFunc("bindBody3D", [vm](ScriptRigidRecording* self, const ssq::Object& world, const ssq::Object& body,
                                   const std::string& id) {
        return script::projectResult(vm, bindRigid<World3D, Body3D>(*self, world, body, id, RigidTrackSpace::Metres3D));
    });
    cls.addFunc("capture2D", [vm](ScriptRigidRecording* self, const ssq::Object& world, float seconds) {
        return captureRigid<World>(vm, *self, world, seconds, RigidTrackSpace::PlanarPixels);
    });
    cls.addFunc("capture3D", [vm](ScriptRigidRecording* self, const ssq::Object& world, float seconds) {
        return captureRigid<World3D>(vm, *self, world, seconds, RigidTrackSpace::Metres3D);
    });
    cls.addFunc("bindingCount", [](ScriptRigidRecording* self) { return static_cast<int>(self->bindings.size()); });
    cls.addFunc("frameCount",
                [](ScriptRigidRecording* self) { return static_cast<int>(self->recording.frameCount()); });
    cls.addFunc("sampleCount",
                [](ScriptRigidRecording* self) { return static_cast<int>(self->recording.sampleCount()); });
    cls.addFunc("save", [vm](ScriptRigidRecording* self, const std::string& path) {
        auto track = self->recording.freeze();
        if (!track) return script::projectStatusResult(vm, track.status());
        return script::projectResult(vm, track.value().save(path));
    });
}
}  // namespace
Module_IMPL(PhysicsBakeRecording, new PhysicsBakeRecording());
void PhysicsBakeRecording::expose(ssq::Table& table) {
    exposeRigidRecording(table);
    const auto vm        = table.getHandle();
    auto       recording = table.addClass<DeformRecording>(
        "DeformBakeRecording", std::function<DeformRecording*()>([]() -> DeformRecording* { return nullptr; }), true);
    recording.addFunc("captureCloth", [vm](DeformRecording* self, const ssq::Object& source, float seconds) {
        return capture<Cloth3D>(vm, *self, source, seconds);
    });
    recording.addFunc("captureRope", [vm](DeformRecording* self, const ssq::Object& source, float seconds) {
        return capture<Rope3D>(vm, *self, source, seconds);
    });
    recording.addFunc("frameCount", [](DeformRecording* self) { return static_cast<int>(self->frameCount()); });
    recording.addFunc("sampleCount", [](DeformRecording* self) { return static_cast<int>(self->sampleCount()); });
    recording.addFunc("save", [vm](DeformRecording* self, const std::string& path) {
        auto track = self->freeze();
        if (!track) return script::projectStatusResult(vm, track.status());
        return script::projectResult(vm, track.value().save(path));
    });
    auto cls = table.addClass(name, PhysicsBakeRecording::create, false);
    expose(cls);
}
void PhysicsBakeRecording::expose(ssq::Class& cls) {
    const auto vm = cls.getHandle();
    cls.addFunc("beginRigid", [vm](PhysicsBakeRecording*, const ssq::Object& object, int sampleBudget) {
        if (sampleBudget <= 0)
            return script::projectStatusResult(vm, Status::failure(invalid("sample budget must be positive")));
        auto recipe = copyScriptBakeRecipe(object);
        if (!recipe) return script::projectStatusResult(vm, recipe.status());
        auto prepared = RigidRecording::prepare(recipe.value(), static_cast<std::size_t>(sampleBudget));
        if (!prepared) return script::projectStatusResult(vm, prepared.status());
        auto instance = script::makeOwnedSquirrelInstance<ScriptRigidRecording>(
            vm, std::make_unique<ScriptRigidRecording>(std::move(prepared).takeValue()));
        if (!instance) return script::projectStatusResult(vm, instance.status());
        return script::projectStatusResult(vm, Status::success(), std::move(instance).takeValue());
    });
    cls.addFunc("beginDeform", [vm](PhysicsBakeRecording*, const ssq::Object& object, int sampleBudget) {
        if (sampleBudget <= 0)
            return script::projectStatusResult(vm, Status::failure(invalid("sample budget must be positive")));
        auto recipe = copyScriptBakeRecipe(object);
        if (!recipe) return script::projectStatusResult(vm, recipe.status());
        auto prepared = DeformRecording::prepare(recipe.value(), static_cast<std::size_t>(sampleBudget));
        if (!prepared) return script::projectStatusResult(vm, prepared.status());
        auto instance = script::makeOwnedSquirrelInstance<DeformRecording>(
            vm, std::make_unique<DeformRecording>(std::move(prepared).takeValue()));
        if (!instance) return script::projectStatusResult(vm, instance.status());
        return script::projectStatusResult(vm, Status::success(), std::move(instance).takeValue());
    });
}
}  // namespace eve::physics
