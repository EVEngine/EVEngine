#include "physics/backend/bake/script/PhysicsBake.h"
#include <charconv>
#include <functional>
#include "CollapseBakeBuildIdentity.h"
#include "common/BorrowedRef.h"
#include "common/SquirrelBinding.h"
#include "common/SquirrelOwnership.h"
#include "physics/backend/bake/BakeRecipe.h"
#include "physics/backend/bake/BakedParticlePlayback.h"
#include "physics/backend/bake/DeformPlayback.h"
#include "physics/backend/bake/RigidPlayback.h"
namespace eve::physics {
namespace {
// A distinct VM type avoids competing with the legacy CollapseBakeRecipe alias registration.
// All validation, state and encoding remain owned by the canonical recipe value.
struct ScriptBakeRecipe {
    BakeRecipe value;
};
Diagnostic invalid(const char* message) {
    return Diagnostic::error(DiagnosticCode::InvalidArgument, message, "physicsBake.script");
}
ResultRef<ScriptBakeRecipe> recipeArgument(const ssq::Object& object) {
    if (object.getType() == ssq::Type::INSTANCE &&
        object.getTypeTag() == script::detail::squirrelTypeHash<ScriptBakeRecipe*>()) {
        try {
            if (auto* value = object.to<ScriptBakeRecipe*>())
                return ResultRef<ScriptBakeRecipe>::success(std::ref(*value));
        } catch (const ssq::TypeException&) {
            // Invalid native wrappers are rejected through the checked boundary.
        }
    }
    return ResultRef<ScriptBakeRecipe>::failure(invalid("expected a live BakeRecipe wrapper"));
}
ssq::Table ownPlayer(HSQUIRRELVM vm, RigidPlayback player) {
    auto object =
        script::makeOwnedSquirrelInstance<RigidPlayback>(vm, std::make_unique<RigidPlayback>(std::move(player)));
    if (!object) return script::projectStatusResult(vm, object.status());
    return script::projectStatusResult(vm, Status::success(), std::move(object).takeValue());
}
// Immutable owning sample handle: large meshes do not become unbounded script tables.
struct ScriptDeformSample {
    DeformTopology           topology;
    std::vector<DeformPoint> points;
    explicit ScriptDeformSample(DeformTopology value) : topology(std::move(value)), points(topology.vertexCount()) {}
};
template <class T>
ssq::Table ownValue(HSQUIRRELVM vm, std::unique_ptr<T> value) {
    auto object = script::makeOwnedSquirrelInstance<T>(vm, std::move(value));
    if (!object) return script::projectStatusResult(vm, object.status());
    return script::projectStatusResult(vm, Status::success(), std::move(object).takeValue());
}
ssq::Table prepareDeformPlayer(HSQUIRRELVM vm, DeformTrack track) {
    auto prepared = DeformPlayback::prepare(std::move(track));
    if (!prepared) return script::projectStatusResult(vm, prepared.status());
    return ownValue(vm, std::make_unique<DeformPlayback>(std::move(prepared).takeValue()));
}
void exposeDeformPlayer(ssq::Table& table) {
    const auto vm    = table.getHandle();
    auto       frame = table.addClass<ScriptDeformSample>(
        "BakedDeformSample", std::function<ScriptDeformSample*()>([]() -> ScriptDeformSample* { return nullptr; }),
        true);
    frame.addFunc("vertexCount", [](ScriptDeformSample* self) { return static_cast<int>(self->points.size()); });
    frame.addFunc("indexCount",
                  [](ScriptDeformSample* self) { return static_cast<int>(self->topology.indices().size()); });
    frame.addFunc("primitive", [](ScriptDeformSample* self) {
        return std::string(self->topology.primitive() == DeformPrimitive::Triangles ? "triangles" : "segments");
    });
    frame.addFunc("topologyKey", [](ScriptDeformSample* self) { return std::string(self->topology.key()); });
    frame.addFunc("point", [vm](ScriptDeformSample* self, int index) {
        if (index < 0 || static_cast<std::size_t>(index) >= self->points.size())
            return script::projectStatusResult(vm, Status::failure(invalid("vertex index out of range")));
        const auto value = self->points[static_cast<std::size_t>(index)];
        return script::projectStatusResult(
            vm, Status::success(),
            Value::object(
                {{"x", Value::number(value.x)}, {"y", Value::number(value.y)}, {"z", Value::number(value.z)}}));
    });
    frame.addFunc("index", [vm](ScriptDeformSample* self, int index) {
        const auto indices = self->topology.indices();
        if (index < 0 || static_cast<std::size_t>(index) >= indices.size())
            return script::projectStatusResult(vm, Status::failure(invalid("connectivity index out of range")));
        return script::projectStatusResult(vm, Status::success(),
                                           Value::integer(indices[static_cast<std::size_t>(index)]));
    });
    frame.addFunc("hasUV", [](ScriptDeformSample* self) { return !self->topology.uv().empty(); });
    frame.addFunc("uv", [vm](ScriptDeformSample* self, int index) {
        const auto values = self->topology.uv();
        if (index < 0 || static_cast<std::size_t>(index) >= values.size())
            return script::projectStatusResult(vm, Status::failure(invalid("UV missing or index out of range")));
        const auto value = values[static_cast<std::size_t>(index)];
        return script::projectStatusResult(
            vm, Status::success(), Value::object({{"u", Value::number(value.u)}, {"v", Value::number(value.v)}}));
    });
    auto cls = table.addClass<DeformPlayback>(
        "BakedDeformPlayback", std::function<DeformPlayback*()>([]() -> DeformPlayback* { return nullptr; }), true);
    cls.addFunc("prepareInstance", [vm](DeformPlayback* self) { return prepareDeformPlayer(vm, self->track()); });
    cls.addFunc("advance", [vm](DeformPlayback* self, float seconds) {
        return script::projectResult(vm, self->advance(seconds));
    });
    cls.addFunc("seek",
                [vm](DeformPlayback* self, float seconds) { return script::projectResult(vm, self->seek(seconds)); });
    cls.addFunc("setRate",
                [vm](DeformPlayback* self, float rate) { return script::projectResult(vm, self->setRate(rate)); });
    cls.addFunc("setLoop", [vm](DeformPlayback* self, bool loop) {
        return script::projectResult(vm, self->setWrap(loop ? BakePlaybackWrap::Loop : BakePlaybackWrap::Clamp));
    });
    cls.addFunc("setPaused", [](DeformPlayback* self, bool paused) { self->setPaused(paused); });
    cls.addFunc("paused", [](DeformPlayback* self) { return self->paused(); });
    cls.addFunc("time", [](DeformPlayback* self) { return static_cast<float>(self->time()); });
    cls.addFunc("rate", [](DeformPlayback* self) { return static_cast<float>(self->rate()); });
    cls.addFunc("duration", [](DeformPlayback* self) { return self->track().duration(); });
    cls.addFunc("setPlacement",
                [vm](DeformPlayback* self, float x, float y, float z, float qx, float qy, float qz, float qw) {
                    return script::projectResult(vm, self->setPlacement({x, y, z, qx, qy, qz, qw}));
                });
    cls.addFunc("captureSample", [vm](DeformPlayback* self) {
        try {
            auto result  = std::make_unique<ScriptDeformSample>(self->track().topology());
            auto sampled = self->sample(result->points);
            if (!sampled) return script::projectStatusResult(vm, sampled.status());
            return ownValue(vm, std::move(result));
        } catch (const std::bad_alloc&) {
            return script::projectStatusResult(
                vm, Status::failure(Diagnostic::error(DiagnosticCode::Failed, "deformation sample allocation failed",
                                                      "physicsBake.script")));
        }
    });
}
struct ScriptParticleSample {
    std::vector<BakedParticleSample> particles;
    explicit ScriptParticleSample(std::size_t capacity) : particles(capacity) {}
};
ssq::Table prepareParticlePlayer(HSQUIRRELVM vm, ParticleTrack track) {
    auto prepared = BakedParticlePlayback::prepare(std::move(track));
    if (!prepared) return script::projectStatusResult(vm, prepared.status());
    return ownValue(vm, std::make_unique<BakedParticlePlayback>(std::move(prepared).takeValue()));
}
void exposeParticlePlayer(ssq::Table& table) {
    const auto vm    = table.getHandle();
    auto       frame = table.addClass<ScriptParticleSample>(
        "BakedParticleSample",
        std::function<ScriptParticleSample*()>([]() -> ScriptParticleSample* { return nullptr; }), true);
    frame.addFunc("count", [](ScriptParticleSample* self) { return static_cast<int>(self->particles.size()); });
    frame.addFunc("particle", [vm](ScriptParticleSample* self, int index) {
        if (index < 0 || static_cast<std::size_t>(index) >= self->particles.size())
            return script::projectStatusResult(vm, Status::failure(invalid("visible particle index out of range")));
        const auto& sample = self->particles[static_cast<std::size_t>(index)];
        const auto& pose   = sample.pose;
        return script::projectStatusResult(
            vm, Status::success(),
            Value::object({{"emitter", Value::string(sample.emitter.format())},
                           {"birthSerial", Value::string(std::to_string(sample.birthSerial))},
                           {"x", Value::number(pose.x)},
                           {"y", Value::number(pose.y)},
                           {"w", Value::number(pose.w)},
                           {"h", Value::number(pose.h)},
                           {"rotation", Value::number(pose.rotation)},
                           {"r", Value::number(pose.r)},
                           {"g", Value::number(pose.g)},
                           {"b", Value::number(pose.b)},
                           {"a", Value::number(pose.a)},
                           {"u0", Value::number(pose.u0)},
                           {"v0", Value::number(pose.v0)},
                           {"u1", Value::number(pose.u1)},
                           {"v1", Value::number(pose.v1)}}));
    });
    auto cls = table.addClass<BakedParticlePlayback>(
        "BakedParticlePlayback",
        std::function<BakedParticlePlayback*()>([]() -> BakedParticlePlayback* { return nullptr; }), true);
    cls.addFunc("prepareInstance",
                [vm](BakedParticlePlayback* self) { return prepareParticlePlayer(vm, self->track()); });
    cls.addFunc("advance", [vm](BakedParticlePlayback* self, float seconds) {
        return script::projectResult(vm, self->advance(seconds));
    });
    cls.addFunc("seek", [vm](BakedParticlePlayback* self, float seconds) {
        return script::projectResult(vm, self->seek(seconds));
    });
    cls.addFunc("setRate", [vm](BakedParticlePlayback* self, float rate) {
        return script::projectResult(vm, self->setRate(rate));
    });
    cls.addFunc("setLoop", [vm](BakedParticlePlayback* self, bool loop) {
        return script::projectResult(vm, self->setWrap(loop ? BakePlaybackWrap::Loop : BakePlaybackWrap::Clamp));
    });
    cls.addFunc("setPaused", [](BakedParticlePlayback* self, bool paused) { self->setPaused(paused); });
    cls.addFunc("paused", [](BakedParticlePlayback* self) { return self->paused(); });
    cls.addFunc("time", [](BakedParticlePlayback* self) { return static_cast<float>(self->time()); });
    cls.addFunc("rate", [](BakedParticlePlayback* self) { return static_cast<float>(self->rate()); });
    cls.addFunc("duration", [](BakedParticlePlayback* self) { return self->track().duration(); });
    cls.addFunc("setPlacement",
                [vm](BakedParticlePlayback* self, float x, float y, float z, float qx, float qy, float qz, float qw) {
                    return script::projectResult(vm, self->setPlacement({x, y, z, qx, qy, qz, qw}));
                });
    cls.addFunc("captureSample", [vm](BakedParticlePlayback* self) {
        try {
            auto result  = std::make_unique<ScriptParticleSample>(self->track().capacity());
            auto sampled = self->sample(result->particles);
            if (!sampled) return script::projectStatusResult(vm, sampled.status());
            result->particles.resize(sampled.value());
            return ownValue(vm, std::move(result));
        } catch (const std::bad_alloc&) {
            return script::projectStatusResult(
                vm, Status::failure(Diagnostic::error(DiagnosticCode::Failed, "particle sample allocation failed",
                                                      "physicsBake.script")));
        }
    });
}
void exposeRecipe(ssq::Table& table) {
    const auto vm  = table.getHandle();
    auto       cls = table.addClass<ScriptBakeRecipe>("BakeRecipe");
    cls.addFunc("producerBuildRevision",
                [](ScriptBakeRecipe*) { return std::string(EVENGINE_COLLAPSE_PRODUCER_REVISION); });
    cls.addFunc("box3DBackendRevision",
                [](ScriptBakeRecipe*) { return std::string(EVENGINE_COLLAPSE_BOX3D_REVISION); });
    cls.addFunc("setCoordinates", [vm](ScriptBakeRecipe* self, const std::string& coordinates) {
        if (coordinates != "metres3d" && coordinates != "pixels2d")
            return script::projectResult(vm,
                                         Result<void>::failure(invalid("coordinates must be metres3d or pixels2d")));
        return script::projectResult(
            vm, self->value.setCoordinates(coordinates == "pixels2d" ? BakeCoordinates::PlanarPixels
                                                                     : BakeCoordinates::MetresRightHandedYUp));
    });
    cls.addFunc("setProducer", [vm](ScriptBakeRecipe* self, const std::string& revision) {
        return script::projectResult(vm, self->value.setProducer(revision));
    });
    cls.addFunc("setBackend", [vm](ScriptBakeRecipe* self, const std::string& revision) {
        return script::projectResult(vm, self->value.setBackend(revision));
    });
    cls.addFunc("setParameters", [vm](ScriptBakeRecipe* self, const std::string& bytes) {
        return script::projectResult(vm, self->value.setParameters(bytes));
    });
    cls.addFunc("setSource", [vm](ScriptBakeRecipe* self, const std::string& name, const std::string& bytes) {
        return script::projectResult(vm, self->value.setSource(name, bytes));
    });
    cls.addFunc("setTiming", [vm](ScriptBakeRecipe* self, float seconds, int stride) {
        if (stride <= 0)
            return script::projectResult(vm, Result<void>::failure(invalid("sample stride must be positive")));
        return script::projectResult(vm, self->value.setTiming(seconds, static_cast<std::uint32_t>(stride)));
    });
    cls.addFunc("setRandomStream", [vm](ScriptBakeRecipe* self, const std::string& name,
                                        const std::string& decimalSeed) {
        std::uint64_t seed   = 0;
        const auto    parsed = std::from_chars(decimalSeed.data(), decimalSeed.data() + decimalSeed.size(), seed);
        if (decimalSeed.empty() || parsed.ec != std::errc{} || parsed.ptr != decimalSeed.data() + decimalSeed.size())
            return script::projectResult(
                vm, Result<void>::failure(invalid("seed must be an unsigned 64-bit decimal string")));
        return script::projectResult(vm, self->value.setRandomStream(name, seed));
    });
    cls.addFunc("key", [vm](ScriptBakeRecipe* self) {
        return script::projectResult(vm, self->value.key(), [](const std::string& key) { return Value::string(key); });
    });
}
void exposeRigidPlayer(ssq::Table& table) {
    const auto vm  = table.getHandle();
    auto       cls = table.addClass<RigidPlayback>(
        "BakedRigidPlayback", std::function<RigidPlayback*()>([]() -> RigidPlayback* { return nullptr; }), true);
    cls.addFunc("copy", [vm](RigidPlayback* self) { return ownPlayer(vm, *self); });
    cls.addFunc("advance",
                [vm](RigidPlayback* self, float seconds) { return script::projectResult(vm, self->advance(seconds)); });
    cls.addFunc("seek",
                [vm](RigidPlayback* self, float seconds) { return script::projectResult(vm, self->seek(seconds)); });
    cls.addFunc("setRate",
                [vm](RigidPlayback* self, float rate) { return script::projectResult(vm, self->setRate(rate)); });
    cls.addFunc("setLoop", [vm](RigidPlayback* self, bool loop) {
        return script::projectResult(vm, self->setWrap(loop ? BakePlaybackWrap::Loop : BakePlaybackWrap::Clamp));
    });
    cls.addFunc("setPaused", [](RigidPlayback* self, bool paused) { self->setPaused(paused); });
    cls.addFunc("paused", [](RigidPlayback* self) { return self->paused(); });
    cls.addFunc("time", [](RigidPlayback* self) { return static_cast<float>(self->time()); });
    cls.addFunc("rate", [](RigidPlayback* self) { return static_cast<float>(self->rate()); });
    cls.addFunc("duration", [](RigidPlayback* self) { return self->duration(); });
    cls.addFunc("trackCount", [](RigidPlayback* self) { return static_cast<int>(self->track().trackCount()); });
    cls.addFunc("trackId", [vm](RigidPlayback* self, int index) {
        if (index < 0) return script::projectStatusResult(vm, Status::failure(invalid("negative track index")));
        return script::projectResult(vm, self->track().trackId(static_cast<std::size_t>(index)),
                                     [](PersistentId id) { return Value::string(id.format()); });
    });
    cls.addFunc("setPlacement",
                [vm](RigidPlayback* self, float x, float y, float z, float qx, float qy, float qz, float qw) {
                    return script::projectResult(vm, self->setPlacement({x, y, z, qx, qy, qz, qw}));
                });
    cls.addFunc("sample", [vm](RigidPlayback* self, const std::string& identity) {
        auto id = PersistentId::parse(identity);
        if (!id)
            return script::projectResult(vm, Result<RigidTrackPose>::failure(invalid("invalid persistent identity")),
                                         [](const RigidTrackPose&) { return Value::null(); });
        return script::projectResult(vm, self->sample(*id), [](const RigidTrackPose& pose) {
            return Value::object({{"x", Value::number(pose.x)},
                                  {"y", Value::number(pose.y)},
                                  {"z", Value::number(pose.z)},
                                  {"qx", Value::number(pose.qx)},
                                  {"qy", Value::number(pose.qy)},
                                  {"qz", Value::number(pose.qz)},
                                  {"qw", Value::number(pose.qw)}});
        });
    });
}
}  // namespace
Result<BakeRecipe> copyScriptBakeRecipe(const ssq::Object& object) {
    auto recipe = recipeArgument(object);
    if (!recipe) return Result<BakeRecipe>::failure(recipe.status());
    try {
        return Result<BakeRecipe>::success(recipe.value().get().value);
    } catch (const std::bad_alloc&) {
        return Result<BakeRecipe>::failure(
            Diagnostic::error(DiagnosticCode::Failed, "script recipe copy allocation failed", "physicsBake.script"));
    }
}
const char*  PhysicsBake::name = "PhysicsBake";
PhysicsBake* PhysicsBake::create() {
    static PhysicsBake facade;
    return &facade;
}
void PhysicsBake::expose(ssq::Table& table) {
    try {
        table.findClass(name);
        return;
    } catch (const ssq::NotFoundException&) {
    }
    exposeRecipe(table);
    exposeRigidPlayer(table);
    exposeDeformPlayer(table);
    exposeParticlePlayer(table);
    auto cls = table.addClass("PhysicsBake", PhysicsBake::create, false);
    expose(cls);
}
void PhysicsBake::expose(ssq::Class& cls) {
    const auto vm = cls.getHandle();
    cls.addFunc("loadParticles", [vm](PhysicsBake*, const std::string& path, const ssq::Object& recipeObject) {
        auto recipe = recipeArgument(recipeObject);
        if (!recipe) return script::projectStatusResult(vm, recipe.status());
        auto loaded = ParticleTrack::loadForRecipe(path, recipe.value().get().value);
        if (!loaded) return script::projectStatusResult(vm, loaded.status());
        return prepareParticlePlayer(vm, std::move(loaded).takeValue());
    });
    cls.addFunc("loadDeform", [vm](PhysicsBake*, const std::string& path, const ssq::Object& recipeObject) {
        auto recipe = recipeArgument(recipeObject);
        if (!recipe) return script::projectStatusResult(vm, recipe.status());
        auto loaded = DeformTrack::loadForRecipe(path, recipe.value().get().value);
        if (!loaded) return script::projectStatusResult(vm, loaded.status());
        return prepareDeformPlayer(vm, std::move(loaded).takeValue());
    });
    cls.addFunc("loadRigid", [vm](PhysicsBake*, const std::string& path, const ssq::Object& recipeObject) {
        auto recipe = recipeArgument(recipeObject);
        if (!recipe) return script::projectStatusResult(vm, recipe.status());
        auto loaded = RigidPoseTrack::loadForRecipe(path, recipe.value().get().value);
        if (!loaded) return script::projectStatusResult(vm, loaded.status());
        return ownPlayer(vm, RigidPlayback(std::move(loaded).takeValue()));
    });
}
}  // namespace eve::physics
