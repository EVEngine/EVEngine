#include "physics/destruction/Destruction.h"

#include "common/Exception.h"
#include "common/Json.h"
#include "common/Value.h"
#include "physics/World3D.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <functional>
#include <utility>

namespace eve::physics {

Module_IMPL(Destruction, new Destruction());

eve::Result<void> Destruction::registerGeometryCollectionSchema() {
    return GeometryCollectionAsset::ensureSchemaRegistered();
}

eve::Result<GeometryCollectionAsset> Destruction::assetFromJson(const std::string& json) {
    auto parsed = eve::Value::fromJson(json);
    if (!parsed) return eve::Result<GeometryCollectionAsset>::failure(parsed.status());
    return GeometryCollectionAsset::fromValue(parsed.value());
}

eve::Result<GeometryCollectionAsset> Destruction::makeWeldedBoxesFixture(float strainThreshold) {
    return GeometryCollectionAsset::makeWeldedBoxesFixture(strainThreshold);
}

eve::Result<std::unique_ptr<GeometryCollectionInstance>> Destruction::createInstance(
    World3D* world, const GeometryCollectionAsset& asset, float originX, float originY, float originZ) {
    if (!world)
        return eve::Result<std::unique_ptr<GeometryCollectionInstance>>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "geometry-collection create requires a World3D", "world"));
    return GeometryCollectionInstance::create(*world, asset, originX, originY, originZ);
}

GeometryCollectionAsset* Destruction::newWeldedBoxesFixtureScript(float strainThreshold) {
    auto created = makeWeldedBoxesFixture(strainThreshold);
    if (!created.ok()) {
        const auto* diagnostic = created.status().primaryDiagnostic();
        throw Exception("Destruction.newWeldedBoxesFixture: %s",
                        diagnostic ? diagnostic->message().c_str() : "creation failed");
    }
    return new GeometryCollectionAsset(std::move(created.value()));
}

GeometryCollectionInstance* Destruction::createInstanceScript(World3D* world, GeometryCollectionAsset* asset,
                                                              float originX, float originY, float originZ) {
    if (!asset) throw Exception("Destruction.createInstance: asset is null");
    auto created = createInstance(world, *asset, originX, originY, originZ);
    if (!created.ok()) {
        const auto* diagnostic = created.status().primaryDiagnostic();
        throw Exception("Destruction.createInstance: %s",
                        diagnostic ? diagnostic->message().c_str() : "creation failed");
    }
    return created.value().release();
}

namespace {

DestructionField makeStrainField(float x, float y, float z, float radius, float magnitude) {
    DestructionField field;
    field.kind = DestructionFieldKind::Strain;
    field.falloff = DestructionFieldFalloff::Linear;
    field.centerX = x;
    field.centerY = y;
    field.centerZ = z;
    field.radius = radius;
    field.magnitude = magnitude;
    return field;
}

DestructionField makeAnchorField(float x, float y, float z, float radius) {
    DestructionField field;
    field.kind = DestructionFieldKind::Anchor;
    field.falloff = DestructionFieldFalloff::None;
    field.centerX = x;
    field.centerY = y;
    field.centerZ = z;
    field.radius = radius;
    field.magnitude = 1.f;
    return field;
}

DestructionField makeSleepField(float x, float y, float z, float radius) {
    DestructionField field;
    field.kind = DestructionFieldKind::Sleep;
    field.falloff = DestructionFieldFalloff::None;
    field.centerX = x;
    field.centerY = y;
    field.centerZ = z;
    field.radius = radius;
    field.magnitude = 1.f;
    return field;
}

void applyStrainFieldScript(GeometryCollectionInstance* self, float x, float y, float z, float radius,
                            float magnitude) {
    if (!self) throw Exception("GeometryCollectionInstance.applyStrainField: null");
    auto result = self->applyField(makeStrainField(x, y, z, radius, magnitude));
    if (!result.ok()) {
        const auto* diagnostic = result.status().primaryDiagnostic();
        throw Exception("GeometryCollectionInstance.applyStrainField: %s",
                        diagnostic ? diagnostic->message().c_str() : "failed");
    }
}

void applyAnchorFieldScript(GeometryCollectionInstance* self, float x, float y, float z, float radius) {
    if (!self) throw Exception("GeometryCollectionInstance.applyAnchorField: null");
    auto result = self->applyField(makeAnchorField(x, y, z, radius));
    if (!result.ok()) {
        const auto* diagnostic = result.status().primaryDiagnostic();
        throw Exception("GeometryCollectionInstance.applyAnchorField: %s",
                        diagnostic ? diagnostic->message().c_str() : "failed");
    }
}

void applySleepFieldScript(GeometryCollectionInstance* self, float x, float y, float z, float radius) {
    if (!self) throw Exception("GeometryCollectionInstance.applySleepField: null");
    auto result = self->applyField(makeSleepField(x, y, z, radius));
    if (!result.ok()) {
        const auto* diagnostic = result.status().primaryDiagnostic();
        throw Exception("GeometryCollectionInstance.applySleepField: %s",
                        diagnostic ? diagnostic->message().c_str() : "failed");
    }
}

void stepScript(GeometryCollectionInstance* self, int tick, float dtSeconds) {
    if (!self) throw Exception("GeometryCollectionInstance.step: null");
    auto delta = eve::Duration::fromSeconds(dtSeconds);
    if (!delta.ok()) throw Exception("GeometryCollectionInstance.step: invalid dt");
    eve::SimulationStep step{eve::SimulationTick{static_cast<std::uint64_t>(tick)}, delta.value()};
    auto result = self->step(step);
    if (!result.ok()) {
        const auto* diagnostic = result.status().primaryDiagnostic();
        throw Exception("GeometryCollectionInstance.step: %s",
                        diagnostic ? diagnostic->message().c_str() : "failed");
    }
}

int boneStateScript(GeometryCollectionInstance* self, int boneIndex) {
    if (!self) throw Exception("GeometryCollectionInstance.boneState: null");
    return static_cast<int>(self->boneState(boneIndex));
}

}  // namespace

void Destruction::expose(ssq::Table& table) {
    auto cls = table.addClass(name, Destruction::create, false);
    expose(cls);

    auto asset = table.addClass<GeometryCollectionAsset>(
        "GeometryCollectionAsset", std::function<GeometryCollectionAsset*()>([]() -> GeometryCollectionAsset* {
            return nullptr;
        }),
        true);
    (void)asset;

    auto instance = table.addClass<GeometryCollectionInstance>(
        "GeometryCollectionInstance",
        std::function<GeometryCollectionInstance*()>([]() -> GeometryCollectionInstance* { return nullptr; }), true);
    instance.addFunc("applyStrainField", applyStrainFieldScript);
    instance.addFunc("applyAnchorField", applyAnchorFieldScript);
    instance.addFunc("applySleepField", applySleepFieldScript);
    instance.addFunc("step", stepScript);
    instance.addFunc("boneCount", [](GeometryCollectionInstance* self) { return self->boneCount(); });
    instance.addFunc("edgeCount", [](GeometryCollectionInstance* self) { return self->edgeCount(); });
    instance.addFunc("boneState", boneStateScript);
    instance.addFunc("edgeStrain", [](GeometryCollectionInstance* self, int edgeIndex) {
        return self->edgeStrain(edgeIndex);
    });
    instance.addFunc("edgeBroken", [](GeometryCollectionInstance* self, int edgeIndex) {
        return self->edgeBroken(edgeIndex);
    });
    instance.addFunc("detachEventCount", [](GeometryCollectionInstance* self) {
        return self->detachEventCount();
    });
    instance.addFunc("hasLiveWorld", [](GeometryCollectionInstance* self) { return self->hasLiveWorld(); });
    instance.addFunc("releaseBodies", [](GeometryCollectionInstance* self) { self->releaseBodies(); });
}

void Destruction::expose(ssq::Class& cls) {
    cls.addFunc("registerGeometryCollectionSchema", [](Destruction* self) {
        auto result = self->registerGeometryCollectionSchema();
        if (!result.ok()) {
            const auto* diagnostic = result.status().primaryDiagnostic();
            throw Exception("Destruction.registerGeometryCollectionSchema: %s",
                            diagnostic ? diagnostic->message().c_str() : "failed");
        }
    });
    cls.addFunc("newWeldedBoxesFixture", &Destruction::newWeldedBoxesFixtureScript);
    cls.addFunc("createInstance", &Destruction::createInstanceScript);
}

}  // namespace eve::physics
