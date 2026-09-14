#include "stylize/MeshVfxAsset.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::stylize;

TEST_CASE("stylize.mesh_vfx_asset parses layered version one assets") {
    auto parsed = MeshVfxAsset::fromJson(R"({
        "schema":"eve.stylize.mesh-vfx","schemaVersion":1,
        "layers":[
          {"style":"slash","playback":{"fadeIn":0.02,"hold":0.1,"fadeOut":0.2},"parameters":{"intensity":2}},
          {"style":"rim","playback":{"loop":true}}
        ],
        "trail":{"maxSamples":48,"lifetime":0.25,"minDistance":0.01,"teleportDistance":3}
    })");
    REQUIRE(parsed.ok());
    auto asset = std::move(parsed).takeValue();
    REQUIRE_EQ(asset.layers.size(), 2u);
    REQUIRE(asset.trail.has_value());
    auto created = MeshVfxAssetInstance::create(asset);
    REQUIRE(created.ok());
    auto instance = std::move(created).takeValue();
    REQUIRE_EQ(instance->layerCount(), 2u);
    REQUIRE(instance->hasTrail());
}

TEST_CASE("stylize.mesh_vfx_asset migrates legacy version zero") {
    auto parsed = MeshVfxAsset::fromJson(
        R"({"schemaVersion":0,"style":"aura","playback":{"hold":0.5},"parameters":{"power":1.5}})");
    REQUIRE(parsed.ok());
    REQUIRE_EQ(parsed.value().layers.size(), 1u);
    REQUIRE_EQ(parsed.value().layers.front().style, "aura");
}

TEST_CASE("stylize.mesh_vfx_asset rejects unknown and invalid fields") {
    auto unknown = MeshVfxAsset::fromJson(
        R"({"schema":"eve.stylize.mesh-vfx","schemaVersion":1,"layers":[{"style":"rim","typo":1}]})");
    REQUIRE(!unknown.ok());
    auto invalid = MeshVfxAsset::fromJson(
        R"({"schema":"eve.stylize.mesh-vfx","schemaVersion":1,"layers":[{"style":"rim","playback":{"hold":-1}}]})");
    REQUIRE(!invalid.ok());
}

TEST_CASE("stylize.mesh_vfx_asset reload is transactional") {
    auto parsed = MeshVfxAsset::fromJson(
        R"({"schema":"eve.stylize.mesh-vfx","schemaVersion":1,"layers":[{"style":"rim"}]})");
    REQUIRE(parsed.ok());
    MeshVfxAssetSlot slot(std::move(parsed).takeValue());
    auto failed = slot.reload(R"({"schemaVersion":99})");
    REQUIRE(!failed.ok());
    REQUIRE_EQ(slot.revision(), 1u);
    REQUIRE_EQ(slot.asset().layers.front().style, "rim");
    auto applied = slot.reload(
        R"({"schema":"eve.stylize.mesh-vfx","schemaVersion":1,"layers":[{"style":"ember"}]})");
    REQUIRE(applied.ok());
    REQUIRE_EQ(applied.value(), 2u);
    REQUIRE_EQ(slot.asset().layers.front().style, "ember");
}

TEST_CASE("stylize.mesh_vfx_asset evaluates authored parameter curves") {
    MeshVfxFloatCurve curve{{{0.f, 0.f}, {0.5f, 2.f}, {1.f, 0.f}}};
    REQUIRE_EQ(curve.evaluate(0.25f), 1.f);
    REQUIRE_EQ(curve.evaluate(0.75f), 1.f);
    auto invalid = MeshVfxAsset::fromJson(
        R"({"schema":"eve.stylize.mesh-vfx","schemaVersion":1,"layers":[{"style":"rim","curves":{"intensity":[[0.5,1],[0.25,2]]}}]})");
    REQUIRE(!invalid.ok());
}

TEST_CASE("stylize.mesh_vfx_asset emits ordered timeline events without callbacks") {
    auto parsed = MeshVfxAsset::fromJson(R"({
      "schema":"eve.stylize.mesh-vfx","schemaVersion":1,
      "layers":[{"style":"rim","playback":{"fadeIn":0,"hold":1,"fadeOut":0}}],
      "events":[{"time":0,"name":"cast"},{"time":0.25,"name":"hit"},{"time":0.75,"name":"recover"}]
    })");
    REQUIRE(parsed.ok());
    auto created = MeshVfxAssetInstance::create(parsed.value());
    REQUIRE(created.ok());
    auto instance = std::move(created).takeValue();
    instance->play();
    auto events = instance->drainEvents();
    REQUIRE_EQ(events.size(), 1u);
    REQUIRE_EQ(events.front(), "cast");
    instance->update(0.3f);
    events = instance->drainEvents();
    REQUIRE_EQ(events.size(), 1u);
    REQUIRE_EQ(events.front(), "hit");
    instance->update(0.5f);
    events = instance->drainEvents();
    REQUIRE_EQ(events.size(), 1u);
    REQUIRE_EQ(events.front(), "recover");
}

TEST_CASE("stylize.mesh_vfx_asset serializes migrated assets in canonical schema") {
    auto legacy = MeshVfxAsset::fromJson(
        R"({"schemaVersion":0,"style":"aura","playback":{"hold":0.5},"events":[{"time":0.5,"name":"pulse"}]})");
    REQUIRE(legacy.ok());
    auto encoded = legacy.value().toJson();
    REQUIRE(encoded.ok());
    auto decoded = MeshVfxAsset::fromJson(encoded.value());
    REQUIRE(decoded.ok());
    REQUIRE_EQ(decoded.value().layers.front().style, "aura");
    REQUIRE_EQ(decoded.value().events.front().name, "pulse");
}

namespace {
class MeshVfxAttachmentSource final : public eve::IAttachmentPointSource {
public:
    eve::Result<eve::AttachmentPoint> sampleAttachmentPoint(
        std::string_view name, eve::AttachmentPoint offset) const override {
        if (name == "weapon.root") return eve::Result<eve::AttachmentPoint>::success({offset.x, offset.y, offset.z});
        if (name == "weapon.tip") return eve::Result<eve::AttachmentPoint>::success({offset.x + 1.f, offset.y, offset.z});
        return eve::Result<eve::AttachmentPoint>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "missing test attachment"));
    }
};
}

TEST_CASE("stylize.mesh_vfx_asset samples authored weapon attachments into its trail") {
    auto parsed = MeshVfxAsset::fromJson(R"({
      "schema":"eve.stylize.mesh-vfx","schemaVersion":1,
      "layers":[{"style":"slash"}],
      "trail":{"maxSamples":8,"lifetime":0.2,"minDistance":0,"teleportDistance":5,
               "rootAttachment":"weapon.root","tipAttachment":"weapon.tip",
               "rootOffset":[0,0.25,0],"tipOffset":[0,0.25,0]}
    })");
    REQUIRE(parsed.ok());
    auto created = MeshVfxAssetInstance::create(parsed.value());
    REQUIRE(created.ok());
    auto instance = std::move(created).takeValue();
    MeshVfxAttachmentSource source;
    auto sampled = instance->sampleTrail(source);
    REQUIRE(sampled.ok());
    REQUIRE(instance->trail().sampleCount() > 0u);
    auto encoded = parsed.value().toJson();
    REQUIRE(encoded.ok());
    REQUIRE(MeshVfxAsset::fromJson(encoded.value()).ok());
}

namespace {
class MeshVfxAnimationEvents final : public eve::IAnimationEventSource {
public:
    std::vector<std::string> names;
    std::size_t animationEventCount() const noexcept override { return names.size(); }
    std::string animationEventName(std::size_t index) const override {
        return index < names.size() ? names[index] : std::string{};
    }
};
}

TEST_CASE("stylize.mesh_vfx_asset maps animation notifications to lifecycle actions") {
    auto parsed = MeshVfxAsset::fromJson(R"({
      "schema":"eve.stylize.mesh-vfx","schemaVersion":1,
      "layers":[{"style":"rim"}],
      "trail":{"rootAttachment":"root","tipAttachment":"tip"},
      "animationTriggers":{"play":["attack.start"],"stop":["attack.end"],"trailBreak":["attack.cancel"]}
    })");
    REQUIRE(parsed.ok());
    auto created = MeshVfxAssetInstance::create(parsed.value());
    REQUIRE(created.ok());
    auto instance = std::move(created).takeValue();
    MeshVfxAnimationEvents events;
    events.names = {"ignored", "attack.start"};
    auto dispatch = instance->processAnimationEvents(events);
    REQUIRE_EQ(dispatch.handled, 1u);
    REQUIRE_EQ(dispatch.played, 1u);
    events.names = {"attack.cancel", "attack.end"};
    dispatch = instance->processAnimationEvents(events);
    REQUIRE_EQ(dispatch.trailBreaks, 1u);
    REQUIRE_EQ(dispatch.stopped, 1u);

    auto ambiguous = MeshVfxAsset::fromJson(R"({
      "schema":"eve.stylize.mesh-vfx","schemaVersion":1,"layers":[{"style":"rim"}],
      "animationTriggers":{"play":["same"],"stop":["same"]}
    })");
    REQUIRE(!ambiguous.ok());
}
