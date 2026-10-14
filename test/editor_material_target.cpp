#include <cmath>
#include <simplesquirrel/simplesquirrel.hpp>
#include <tuple>
#include "common/Module.h"
#include "editor/EditorAuthority.h"
#include "editor/EditorPersistence.h"
#include "editor/EditorPluginPermissions.h"
#include "editor/EditorProjectSettings.h"
#include "editor/EditorTransactionService.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/material/editing/MaterialTarget.h"
#include "physics/editor/EditorPhysicsTarget.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// editor_material_target.cpp
using namespace eve::material_editing;
using namespace eve::editing;
using eve::editor::LocalTransactionBackend;

namespace {

class NullMaterialAssets final : public IMaterialRuntimeAssetResolver {
public:
    Result<eve::graphics::Texture*> resolveTexture(const std::string&) const override {
        return eve::editing::applied<eve::graphics::Texture*>(&texture);
    }
    Result<eve::graphics::Shader*> resolveShader(const std::string&) const override {
        return eve::editing::applied<eve::graphics::Shader*>(nullptr);
    }

    mutable eve::graphics::Texture texture;
};

class SelectiveMaterialAssets final : public IMaterialRuntimeAssetResolver {
public:
    Result<eve::graphics::Texture*> resolveTexture(const std::string& asset) const override {
        if ((rejectDetail && asset == "asset://tve/detail-albedo") ||
            (rejectColors && asset == "asset://tve/colors-field") ||
            (rejectVertex && asset == "asset://tve/vertex-field") ||
            (rejectMotion && (asset == "asset://tve/motion-field" || asset == "asset://tve/motion-noise")))
            return eve::editing::failed<eve::graphics::Texture*>(EditorStatus::NotFound,
                                                                 RuleId("editor.material.test-missing-detail"),
                                                                 "Requested test texture is unavailable");
        return eve::editing::applied<eve::graphics::Texture*>(&texture);
    }
    Result<eve::graphics::Shader*> resolveShader(const std::string&) const override {
        return eve::editing::applied<eve::graphics::Shader*>(nullptr);
    }

    mutable eve::graphics::Texture texture;
    bool                           rejectDetail = true;
    bool                           rejectColors = false;
    bool                           rejectVertex = false;
    bool                           rejectMotion = false;
};

SelectionSnapshot materialSelection(const MaterialDocumentTarget& target) {
    SelectionSnapshot selection;
    selection.channel = "asset";
    SelectionItem item;
    item.domain = SelectionDomain::Asset;
    item.target = TargetId(target.targetId());
    item.item   = StableId(target.targetId().value());
    item.type   = "graphics.material";
    selection.items.push_back(item);
    selection.primary = item;
    return selection;
}

Result<TransactionReceipt> commitMaterial(MaterialDocumentTarget& target, LocalTransactionBackend& transactions,
                                          const DomainOperation& operation, const char* transactionId) {
    TransactionSpec specification;
    specification.id           = TransactionId(transactionId);
    specification.label        = "Edit material";
    specification.target       = TargetId(target.targetId());
    specification.baseRevision = target.revision();
    auto begun                 = transactions.begin(std::move(specification));
    if (!begun.ok())
        return eve::editing::failed<TransactionReceipt>(begun.code(), RuleId("test.material.begin"),
                                                        "Could not begin material transaction");
    auto appended = transactions.append(operation);
    if (!appended.ok()) {
        [[maybe_unused]] const auto rolledBack = transactions.rollback();
        return eve::editing::failed<TransactionReceipt>(appended.code(), RuleId("test.material.append"),
                                                        "Could not append material operation");
    }
    return transactions.commit();
}

void configureVegetationStages(MaterialDocumentTarget& target, const SelectionSnapshot& selection) {
    const auto set = [&](const char* path, EditorValue assigned) {
        auto operation = target.makeSet(selection, PropertyPath(path), assigned, PropertySetMode::Absolute);
        REQUIRE(operation.ok());
        REQUIRE(target.applyDomainOperation(operation.value()).ok());
    };
    set("surface.mode", "masked");
    set("vegetation.alpha.enabled", true);
    set("vegetation.alpha.global", 0.7);
    set("vegetation.alpha.glancing", 0.3);
    set("vegetation.alpha.camera-min", 12.0);
    set("vegetation.alpha.camera-max", 80.0);
    set("vegetation.emission.enabled", true);
    set("vegetation.emission.minimum", 0.2);
    set("vegetation.emission.maximum", 0.8);
    set("vegetation.emission.phase", 0.6);
    set("vegetation.gradient.enabled", true);
    set("vegetation.gradient.color-one", EditorValue::Array{0.1, 0.2, 0.3, 1.0});
    set("vegetation.gradient.color-two", EditorValue::Array{0.7, 0.8, 0.9, 1.0});
    set("vegetation.gradient.minimum", 0.15);
    set("vegetation.gradient.maximum", 0.85);
    set("textures.orm", "asset://tve/leaf-orm");
    set("textures.emissive", "asset://tve/leaf-emissive");
    set("vegetation.translucency.enabled", true);
    set("vegetation.translucency.color", EditorValue::Array{0.4, 0.6, 0.2, 1.0});
    set("vegetation.translucency.intensity", 0.75);
    set("vegetation.translucency.strength", 3.0);
    set("vegetation.translucency.normal-distortion", 0.25);
    set("vegetation.translucency.scattering", 6.0);
    set("vegetation.translucency.direct", 0.8);
    set("vegetation.translucency.ambient", 0.2);
    set("vegetation.translucency.shadow", 0.65);
    set("vegetation.translucency.mask-amount", 0.5);
    set("vegetation.translucency.mask-minimum", 0.1);
    set("vegetation.translucency.mask-maximum", 0.9);
    set("vegetation.color.enabled", true);
    set("vegetation.color.field", EditorValue::Array{0.2, 0.3, 0.4, 0.6});
    set("vegetation.color.overlay-color", EditorValue::Array{0.8, 0.5, 0.2, 1.0});
    set("vegetation.color.vertex-occlusion-color", EditorValue::Array{0.4, 0.5, 0.6, 1.0});
    set("vegetation.color.overlay", 0.7);
    set("vegetation.color.wetness", 0.4);
    set("vegetation.color.colors-intensity", 1.5);
    set("vegetation.color.alpha-threshold-offset", -0.2);
    set("vegetation.color.invert-vertex-occlusion", true);
    set("vegetation.color.backface-normal", "same");
}

void checkPublishedVegetationStages(const eve::graphics::Material& runtime, const eve::graphics::PbrSurface& surface,
                                    eve::graphics::Texture* texture) {
    CHECK_EQ(runtime.surfaceMode(), eve::graphics::SurfaceMode::Masked);
    CHECK_EQ(surface.vegetationAlpha.global, 0.7f);
    CHECK_EQ(surface.vegetationAlpha.glancing, 0.3f);
    CHECK_EQ(surface.vegetationAlpha.cameraFadeMin, 12.f);
    CHECK_EQ(surface.vegetationAlpha.cameraFadeMax, 80.f);
    CHECK(surface.vegetationEmission.enabled);
    CHECK_EQ(surface.vegetationEmission.minimum, 0.2f);
    CHECK_EQ(surface.vegetationEmission.maximum, 0.8f);
    CHECK_EQ(surface.vegetationEmission.phase, 0.6f);
    CHECK(surface.vegetationGradient.enabled);
    CHECK_EQ(surface.vegetationGradient.colorOne[1], 0.2f);
    CHECK_EQ(surface.vegetationGradient.colorTwo[2], 0.9f);
    CHECK_EQ(surface.vegetationGradient.minimum, 0.15f);
    CHECK_EQ(surface.vegetationGradient.maximum, 0.85f);
    CHECK_EQ(surface.textures[std::size_t(eve::graphics::PbrTextureSlot::MetallicRoughness)].texture, texture);
    CHECK(!surface.textures[std::size_t(eve::graphics::PbrTextureSlot::MetallicRoughness)].srgbDecode);
    CHECK_EQ(surface.textures[std::size_t(eve::graphics::PbrTextureSlot::Emissive)].texture, texture);
    CHECK(surface.translucency.color[1] == 0.6f);
    CHECK_EQ(surface.translucency.intensity, 0.75f);
    CHECK_EQ(surface.translucency.strength, 3.f);
    CHECK_EQ(surface.translucency.scattering, 6.f);
    CHECK_EQ(surface.translucency.maskAmount, 0.5f);
    CHECK_EQ(surface.translucency.maskMinimum, 0.1f);
    CHECK_EQ(surface.translucency.maskMaximum, 0.9f);
    CHECK_EQ(surface.vegetationColor.fieldColor[3], 0.6f);
    CHECK_EQ(surface.vegetationColor.overlayColor[0], 0.8f);
    CHECK_EQ(surface.vegetationColor.vertexOcclusionColor[2], 0.6f);
    CHECK_EQ(surface.vegetationColor.overlay, 0.7f);
    CHECK_EQ(surface.vegetationColor.wetness, 0.4f);
    CHECK_EQ(surface.vegetationColor.colorsIntensity, 1.5f);
    CHECK_EQ(surface.vegetationColor.globalAlphaThresholdOffset, -0.2f);
    CHECK(surface.vegetationColor.invertVertexOcclusion);
    CHECK_EQ(surface.vegetationColor.backfaceNormalMode, eve::graphics::PbrVegetationBackfaceNormalMode::Same);
}

void configureVegetationDetail(MaterialDocumentTarget& target, const SelectionSnapshot& selection) {
    const auto set = [&](const char* path, EditorValue assigned) {
        auto operation = target.makeSet(selection, PropertyPath(path), assigned, PropertySetMode::Absolute);
        REQUIRE(operation.ok());
        REQUIRE(target.applyDomainOperation(operation.value()).ok());
    };
    set("vegetation.detail.enabled", true);
    set("vegetation.detail.albedo", "asset://tve/detail-albedo");
    set("vegetation.detail.normal", "asset://tve/detail-normal");
    set("vegetation.detail.mask", "asset://tve/detail-mask");
    set("vegetation.detail.uv-mode", "world");
    set("vegetation.detail.uv-scale", EditorValue::Array{2.0, 3.0});
    set("vegetation.detail.uv-offset", EditorValue::Array{0.25, 0.5});
    set("vegetation.detail.inverse-uv-scale", true);
    set("vegetation.detail.color", EditorValue::Array{0.1, 0.2, 0.3, 0.4});
    set("vegetation.detail.color-two", EditorValue::Array{0.6, 0.7, 0.8, 0.9});
    set("vegetation.detail.color-mode", "variation");
    set("vegetation.detail.blend-mode", "replace");
    set("vegetation.detail.alpha-mode", "main");
    set("vegetation.detail.value", 0.75);
    set("vegetation.detail.normal-value", 2.0);
    set("vegetation.detail.normal-blend", 0.65);
    set("vegetation.detail.albedo-value", 0.55);
    set("vegetation.detail.metallic", 0.35);
    set("vegetation.detail.occlusion", 0.45);
    set("vegetation.detail.smoothness", 0.85);
    set("vegetation.detail.blend-minimum", 0.15);
    set("vegetation.detail.blend-maximum", 0.8);
    set("vegetation.detail.mask-mode", "inverse");
    set("vegetation.detail.mask-minimum", 0.2);
    set("vegetation.detail.mask-maximum", 0.7);
    set("vegetation.detail.mesh-mode", "inverse");
    set("vegetation.detail.mesh-minimum", 0.3);
    set("vegetation.detail.mesh-maximum", 0.9);
}

void checkVegetationDetail(const eve::graphics::PbrSurface& surface, eve::graphics::Texture* texture) {
    CHECK_EQ(surface.vegetationDetail.textures[0].texture, texture);
    CHECK_EQ(surface.vegetationDetail.textures[1].texture, texture);
    CHECK_EQ(surface.vegetationDetail.textures[2].texture, texture);
    CHECK(surface.vegetationDetail.textures[0].srgbDecode);
    CHECK(!surface.vegetationDetail.textures[1].srgbDecode);
    CHECK(!surface.vegetationDetail.textures[2].srgbDecode);
    CHECK_EQ(surface.vegetationDetail.uvMode, 2u);
    CHECK_EQ(surface.vegetationDetail.colorMode, 1u);
    CHECK_EQ(surface.vegetationDetail.blendMode, 1u);
    CHECK_EQ(surface.vegetationDetail.alphaMode, 0u);
    CHECK_EQ(surface.vegetationDetail.uvScale[1], 3.f);
    CHECK_EQ(surface.vegetationDetail.uvOffset[0], 0.25f);
    CHECK(surface.vegetationDetail.inverseUvScale);
    CHECK_EQ(surface.vegetationDetail.color[2], 0.3f);
    CHECK_EQ(surface.vegetationDetail.colorTwo[3], 0.9f);
    CHECK_EQ(surface.vegetationDetail.value, 0.75f);
    CHECK_EQ(surface.vegetationDetail.normalValue, 2.f);
    CHECK_EQ(surface.vegetationDetail.normalBlendValue, 0.65f);
    CHECK_EQ(surface.vegetationDetail.albedoValue, 0.55f);
    CHECK_EQ(surface.vegetationDetail.metallicValue, 0.35f);
    CHECK_EQ(surface.vegetationDetail.occlusionValue, 0.45f);
    CHECK_EQ(surface.vegetationDetail.smoothnessValue, 0.85f);
    CHECK_EQ(surface.vegetationDetail.blendMinimum, 0.15f);
    CHECK_EQ(surface.vegetationDetail.blendMaximum, 0.8f);
    CHECK_EQ(surface.vegetationDetail.maskMode, 1u);
    CHECK_EQ(surface.vegetationDetail.maskMinimum, 0.2f);
    CHECK_EQ(surface.vegetationDetail.maskMaximum, 0.7f);
    CHECK_EQ(surface.vegetationDetail.meshMode, 1u);
    CHECK_EQ(surface.vegetationDetail.meshMinimum, 0.3f);
    CHECK_EQ(surface.vegetationDetail.meshMaximum, 0.9f);
}

void configureVegetationFields(MaterialDocumentTarget& target, const SelectionSnapshot& selection) {
    const auto set = [&](const char* path, EditorValue assigned) {
        auto operation = target.makeSet(selection, PropertyPath(path), assigned, PropertySetMode::Absolute);
        REQUIRE(operation.ok());
        REQUIRE(target.applyDomainOperation(operation.value()).ok());
    };
    for (const char* group : {"extras", "colors"}) {
        const std::string prefix = std::string("vegetation.") + group;
        set((prefix + ".enabled").c_str(), true);
        set((prefix + ".texture").c_str(), std::string("asset://tve/") + group + "-field");
        set((prefix + ".layer").c_str(), std::int64_t(group == std::string("extras") ? 3 : 7));
        set((prefix + ".coords").c_str(), EditorValue::Array{2.0, 4.0, 0.25, 0.5});
        set((prefix + ".use-pivot-position").c_str(), true);
        for (std::size_t i = 0; i < 9; ++i) set((prefix + ".usage-" + std::to_string(i)).c_str(), double(i + 1) / 10.0);
    }
    set("vegetation.extras.fallback", EditorValue::Array{0.1, 0.2, 0.3, 0.4});
    set("vegetation.colors.fallback", EditorValue::Array{0.5, 0.6, 0.7, 0.8});
}

template <class Field>
void checkVegetationField(const Field& field, eve::graphics::Texture* texture, uint32_t layer, float fallbackFirst) {
    CHECK_EQ(field.texture, texture);
    CHECK_EQ(field.layer, layer);
    CHECK(field.usePivotPosition);
    CHECK_EQ(field.coords[1], 4.f);
    CHECK_EQ(field.coords[3], 0.5f);
    CHECK_EQ(field.fallback[0], fallbackFirst);
    CHECK_EQ(field.usage[0], 0.1f);
    CHECK_EQ(field.usage[8], 0.9f);
}

void configureVegetationVertex(MaterialDocumentTarget& target, const SelectionSnapshot& selection) {
    const auto set = [&](const char* path, EditorValue assigned) {
        auto operation = target.makeSet(selection, PropertyPath(path), assigned, PropertySetMode::Absolute);
        REQUIRE(operation.ok());
        REQUIRE(target.applyDomainOperation(operation.value()).ok());
    };
    set("vegetation.vertex.enabled", true);
    set("vegetation.vertex.texture", "asset://tve/vertex-field");
    set("vegetation.vertex.layer", std::int64_t(6));
    set("vegetation.vertex.fallback", EditorValue::Array{0.1, 0.2, 0.3, 0.8});
    set("vegetation.vertex.coords", EditorValue::Array{3.0, 4.0, 0.25, 0.75});
    for (std::size_t i = 0; i < 9; ++i)
        set(("vegetation.vertex.usage-" + std::to_string(i)).c_str(), double(i + 1) / 10.0);
    set("vegetation.vertex.source", "gpu-fields");
    set("vegetation.vertex.global-size", 0.85);
    set("vegetation.vertex.size-fade-start", 12.0);
    set("vegetation.vertex.size-fade-end", 96.0);
    set("vegetation.vertex.distance-fade-bias", 1.5);
}

void configureVegetationMotion(MaterialDocumentTarget& target, const SelectionSnapshot& selection) {
    const auto set = [&](const char* path, EditorValue assigned) {
        auto operation = target.makeSet(selection, PropertyPath(path), assigned, PropertySetMode::Absolute);
        REQUIRE(operation.ok());
        REQUIRE(target.applyDomainOperation(operation.value()).ok());
    };
    set("vegetation.motion.mode", "object");
    set("vegetation.motion.texture", "asset://tve/motion-field");
    set("vegetation.motion.noise", "asset://tve/motion-noise");
    set("vegetation.motion.layer", std::int64_t(5));
    set("vegetation.motion.fallback", EditorValue::Array{0.1, 0.2, 0.3, 0.4});
    set("vegetation.motion.coords", EditorValue::Array{2.0, 3.0, 0.25, 0.75});
    set("vegetation.motion.global-direction", EditorValue::Array{0.6, 0.8});
    set("vegetation.motion.world-origin", EditorValue::Array{10.0, 20.0, 30.0});
    for (std::size_t i = 0; i < 9; ++i)
        set(("vegetation.motion.usage-" + std::to_string(i)).c_str(), double(i + 1) / 10.0);
    set("vegetation.motion.time", 42.5);
    set("vegetation.motion.dynamic-mode", 0.1);
    set("vegetation.motion.rigidity", 0.2);
    set("vegetation.motion.facing", 0.3);
    set("vegetation.motion.interaction-mask", 0.4);
    const std::array<const char*, 22> names = {
        "bending",           "bending-speed",    "bending-scale",     "bending-variation", "branch",
        "rolling",           "branch-speed",     "branch-scale",      "branch-variation",  "flutter",
        "flutter-speed",     "flutter-scale",    "flutter-variation", "global-bending",    "global-branch",
        "global-flutter",    "noise-tiling",     "interaction",       "fade-distance",     "perspective-push",
        "perspective-noise", "perspective-angle"};
    for (std::size_t i = 0; i < names.size(); ++i)
        set((std::string("vegetation.motion.") + names[i]).c_str(), double(i + 1));
}

}  // namespace

TEST_CASE("editor.material.schema_matches_core_material_authoring_surface") {
    MaterialDocumentTarget  target("metal-panel");
    const SelectionSnapshot selection = materialSelection(target);
    const PropertySchema    schema    = target.schema(selection);
    CHECK_EQ(schema.typeId, std::string("graphics.material"));
    CHECK(schema.find(PropertyPath("shading.metallic")) != nullptr);
    CHECK(schema.find(PropertyPath("shading.roughness")) != nullptr);
    CHECK(schema.find(PropertyPath("textures.normal")) != nullptr);
    CHECK(schema.find(PropertyPath("surface.blend")) != nullptr);
    CHECK(schema.find(PropertyPath("shadow.receive")) != nullptr);
    CHECK(schema.find(PropertyPath("vegetation.alpha.glancing")) != nullptr);
    CHECK(schema.find(PropertyPath("vegetation.alpha.noise")) != nullptr);
    CHECK(schema.find(PropertyPath("vegetation.emission.phase")) != nullptr);
    CHECK(schema.find(PropertyPath("vegetation.gradient.color-two")) != nullptr);
    CHECK(schema.find(PropertyPath("vegetation.translucency.scattering")) != nullptr);
    CHECK(schema.find(PropertyPath("textures.orm")) != nullptr);
    CHECK(schema.find(PropertyPath("textures.emissive")) != nullptr);
    CHECK(schema.find(PropertyPath("vegetation.color.overlay-subsurface")) != nullptr);
    CHECK(schema.find(PropertyPath("vegetation.color.backface-normal")) != nullptr);
    CHECK(schema.find(PropertyPath("vegetation.detail.normal-blend")) != nullptr);
    CHECK(schema.find(PropertyPath("vegetation.detail.uv-mode")) != nullptr);
    CHECK(schema.find(PropertyPath("vegetation.extras.usage-8")) != nullptr);
    CHECK(schema.find(PropertyPath("vegetation.colors.use-pivot-position")) != nullptr);
    CHECK(schema.find(PropertyPath("vegetation.vertex.distance-fade-bias")) != nullptr);
    CHECK(schema.find(PropertyPath("vegetation.motion.perspective-angle")) != nullptr);
    CHECK(target.read(selection, PropertyPath("vegetation.detail.albedo")).value.getIf<std::string>() != nullptr);
    CHECK(target.queryCapability(CapabilityId("eve.editor.target.material-properties")) != nullptr);
}

TEST_CASE("editor.material.vegetation_alpha_properties_are_reversible_and_persistent") {
    MaterialDocumentTarget  target("leaf-alpha");
    LocalWorldAuthority     authority(&target);
    LocalTransactionBackend transactions(&authority);
    const SelectionSnapshot selection = materialSelection(target);

    for (const auto& [path, value, id] :
         {std::tuple{PropertyPath("vegetation.alpha.enabled"), EditorValue(true), "vegetation.alpha.enabled"},
          {PropertyPath("vegetation.alpha.glancing"), EditorValue(0.35), "vegetation.alpha.glancing"},
          {PropertyPath("vegetation.alpha.camera-min"), EditorValue(8.0), "vegetation.alpha.camera-min"},
          {PropertyPath("vegetation.alpha.camera-max"), EditorValue(64.0), "vegetation.alpha.camera-max"},
          {PropertyPath("vegetation.alpha.noise"), EditorValue("asset://tve/noise-volume"),
           "vegetation.alpha.noise"}}) {
        auto operation = target.makeSet(selection, path, value, PropertySetMode::Absolute);
        REQUIRE(operation.ok());
        REQUIRE(commitMaterial(target, transactions, operation.value(), id).ok());
    }
    CHECK(target.validate().empty());
    REQUIRE(transactions.undo().ok());
    CHECK(target.read(selection, PropertyPath("vegetation.alpha.noise")).value == EditorValue(""));
    REQUIRE(transactions.redo().ok());

    MaterialDocumentTarget restored("restored-alpha");
    REQUIRE(restored.loadSnapshot(target.snapshotValue()).ok());
    const SelectionSnapshot restoredSelection = materialSelection(restored);
    CHECK(restored.read(restoredSelection, PropertyPath("vegetation.alpha.glancing")).value == EditorValue(0.35));
    CHECK(restored.read(restoredSelection, PropertyPath("vegetation.alpha.noise")).value ==
          EditorValue("asset://tve/noise-volume"));

    auto invalid = target.makeSet(selection, PropertyPath("vegetation.alpha.constant"), 1.1, PropertySetMode::Absolute);
    CHECK_EQ(static_cast<int>(invalid.code()), static_cast<int>(EditorStatus::Rejected));
}

TEST_CASE("editor.material.legacy_snapshots_migrate_with_vegetation_defaults") {
    for (std::int64_t version : {1, 2, 3, 4, 5, 6, 7, 8}) {
        EditorValue::Object properties;
        properties["shading.metallic"] = 0.2;
        EditorValue::Object snapshot;
        snapshot["schemaVersion"] = version;
        snapshot["properties"]    = std::move(properties);

        MaterialDocumentTarget target("legacy-material");
        REQUIRE(target.loadSnapshot(EditorValue(std::move(snapshot))).ok());
        const SelectionSnapshot selection = materialSelection(target);
        CHECK(target.read(selection, PropertyPath("shading.metallic")).value == EditorValue(0.2));
        CHECK(target.read(selection, PropertyPath("vegetation.alpha.enabled")).value == EditorValue(false));
        CHECK(target.read(selection, PropertyPath("vegetation.emission.enabled")).value == EditorValue(false));
        CHECK(target.read(selection, PropertyPath("vegetation.gradient.enabled")).value == EditorValue(false));
        CHECK(target.read(selection, PropertyPath("vegetation.translucency.enabled")).value == EditorValue(false));
        CHECK(target.read(selection, PropertyPath("vegetation.color.enabled")).value == EditorValue(false));
        CHECK(target.read(selection, PropertyPath("vegetation.detail.enabled")).value == EditorValue(false));
        CHECK(target.read(selection, PropertyPath("vegetation.extras.enabled")).value == EditorValue(false));
        CHECK(target.read(selection, PropertyPath("vegetation.colors.enabled")).value == EditorValue(false));
        CHECK(target.read(selection, PropertyPath("vegetation.vertex.enabled")).value == EditorValue(false));
        CHECK(target.read(selection, PropertyPath("vegetation.motion.mode")).value == EditorValue("disabled"));
        const EditorValue encodedSnapshot = target.snapshotValue();
        const auto*       encoded         = encodedSnapshot.getIf<EditorValue::Object>();
        REQUIRE(encoded != nullptr);
        CHECK(encoded->at("schemaVersion") == EditorValue(9));
    }
}

TEST_CASE("editor.material.translucency_mask_requires_orm_atomically") {
    MaterialDocumentTarget  source("masked-translucency");
    const SelectionSnapshot selection = materialSelection(source);
    for (const auto& [path, value] : {std::pair{PropertyPath("vegetation.translucency.enabled"), EditorValue(true)},
                                      {PropertyPath("vegetation.translucency.intensity"), EditorValue(0.8)},
                                      {PropertyPath("vegetation.translucency.mask-amount"), EditorValue(0.5)}}) {
        auto operation = source.makeSet(selection, path, value, PropertySetMode::Absolute);
        REQUIRE(operation.ok());
        REQUIRE(source.applyDomainOperation(operation.value()).ok());
    }

    auto* renderable = eve::graphics::Renderable3D::create();
    REQUIRE(renderable != nullptr);
    eve::graphics::Material runtime;
    renderable->setMaterial(&runtime);
    NullMaterialAssets              assets;
    Renderable3DMaterialRuntimeSink sink(renderable, &assets);
    MaterialPublishingTarget        target("masked-translucency", &sink);
    const Revision                  before = target.revision();
    CHECK_EQ(static_cast<int>(target.reloadSnapshot(source.snapshotValue()).code()),
             static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(target.revision(), before);
    CHECK(!runtime.hasPbrSurface());
}

TEST_CASE("editor.material.vegetation_stages_snapshot_reaches_bound_runtime_material") {
    MaterialDocumentTarget  source("runtime-leaf");
    const SelectionSnapshot selection = materialSelection(source);
    configureVegetationStages(source, selection);

    auto* renderable = eve::graphics::Renderable3D::create();
    REQUIRE(renderable != nullptr);
    eve::graphics::Material runtime;
    renderable->setMaterial(&runtime);
    NullMaterialAssets              assets;
    Renderable3DMaterialRuntimeSink sink(renderable, &assets);
    MaterialPublishingTarget        target("runtime-leaf", &sink);
    REQUIRE(target.reloadSnapshot(source.snapshotValue()).ok());
    REQUIRE(runtime.hasPbrSurface());
    const auto publishedSurface = std::make_unique<eve::graphics::PbrSurface>(runtime.pbrSurface());
    checkPublishedVegetationStages(runtime, *publishedSurface, &assets.texture);

    MaterialDocumentTarget disabled("runtime-leaf");
    REQUIRE(target.reloadSnapshot(disabled.snapshotValue()).ok());
    const auto clearedSurface = std::make_unique<eve::graphics::PbrSurface>(runtime.pbrSurface());
    CHECK(!clearedSurface->vegetationAlpha.enabled);
    CHECK(!clearedSurface->vegetationEmission.enabled);
    CHECK(!clearedSurface->vegetationGradient.enabled);
    CHECK_EQ(clearedSurface->translucency.intensity, 0.f);
    CHECK_EQ(clearedSurface->translucency.maskAmount, 0.f);
    CHECK_EQ(clearedSurface->vegetationColor.overlay, 0.f);
    CHECK_EQ(clearedSurface->vegetationColor.wetness, 0.f);
    CHECK(!clearedSurface->vegetationColor.invertVertexOcclusion);
    CHECK_EQ(runtime.surfaceMode(), eve::graphics::SurfaceMode::Opaque);

    auto* unbound = eve::graphics::Renderable3D::create();
    REQUIRE(unbound != nullptr);
    Renderable3DMaterialRuntimeSink unboundSink(unbound, &assets);
    MaterialPublishingTarget        unboundTarget("runtime-leaf", &unboundSink);
    const auto                      before = unboundTarget.revision();
    CHECK_EQ(static_cast<int>(unboundTarget.reloadSnapshot(source.snapshotValue()).code()),
             static_cast<int>(EditorStatus::Unsupported));
    CHECK_EQ(unboundTarget.revision(), before);

    for (const auto& [path, value] : {std::pair{PropertyPath("surface.mode"), EditorValue("opaque")},
                                      {PropertyPath("vegetation.alpha.enabled"), EditorValue(false)}}) {
        auto operation = source.makeSet(selection, path, value, PropertySetMode::Absolute);
        REQUIRE(operation.ok());
        REQUIRE(source.applyDomainOperation(operation.value()).ok());
    }
    CHECK_EQ(static_cast<int>(unboundTarget.reloadSnapshot(source.snapshotValue()).code()),
             static_cast<int>(EditorStatus::Unsupported));
    CHECK_EQ(unboundTarget.revision(), before);
}

TEST_CASE("editor.material.missing_vegetation_detail_texture_rejects_atomically") {
    MaterialDocumentTarget  source("missing-runtime-detail");
    const SelectionSnapshot selection = materialSelection(source);
    configureVegetationDetail(source, selection);

    auto* renderable = eve::graphics::Renderable3D::create();
    REQUIRE(renderable != nullptr);
    eve::graphics::Material runtime;
    renderable->setMaterial(&runtime);
    SelectiveMaterialAssets         assets;
    Renderable3DMaterialRuntimeSink sink(renderable, &assets);
    MaterialPublishingTarget        target("missing-runtime-detail", &sink);
    const Revision                  before = target.revision();

    CHECK_EQ(static_cast<int>(target.reloadSnapshot(source.snapshotValue()).code()),
             static_cast<int>(EditorStatus::NotFound));
    CHECK_EQ(target.revision(), before);
    CHECK(!runtime.hasPbrSurface());
}

TEST_CASE("editor.material.vegetation_detail_snapshot_reaches_bound_runtime_material") {
    MaterialDocumentTarget  source("runtime-detail");
    const SelectionSnapshot selection = materialSelection(source);
    configureVegetationDetail(source, selection);

    auto* renderable = eve::graphics::Renderable3D::create();
    REQUIRE(renderable != nullptr);
    eve::graphics::Material runtime;
    renderable->setMaterial(&runtime);
    NullMaterialAssets              assets;
    Renderable3DMaterialRuntimeSink sink(renderable, &assets);
    MaterialPublishingTarget        target("runtime-detail", &sink);
    REQUIRE(target.reloadSnapshot(source.snapshotValue()).ok());
    REQUIRE(runtime.hasPbrSurface());
    const auto published = std::make_unique<eve::graphics::PbrSurface>(runtime.pbrSurface());
    checkVegetationDetail(*published, &assets.texture);

    MaterialDocumentTarget disabled("runtime-detail");
    REQUIRE(target.reloadSnapshot(disabled.snapshotValue()).ok());
    const auto cleared = std::make_unique<eve::graphics::PbrSurface>(runtime.pbrSurface());
    CHECK_EQ(cleared->vegetationDetail.value, 0.f);
    CHECK_EQ(cleared->vegetationDetail.textures[0].texture, nullptr);
}

TEST_CASE("editor.material.vegetation_field_sampling_snapshot_reaches_bound_runtime_material") {
    MaterialDocumentTarget  source("runtime-fields");
    const SelectionSnapshot selection = materialSelection(source);
    configureVegetationFields(source, selection);

    auto* renderable = eve::graphics::Renderable3D::create();
    REQUIRE(renderable != nullptr);
    eve::graphics::Material runtime;
    renderable->setMaterial(&runtime);
    SelectiveMaterialAssets assets;
    assets.rejectDetail = false;
    Renderable3DMaterialRuntimeSink sink(renderable, &assets);
    MaterialPublishingTarget        target("runtime-fields", &sink);
    REQUIRE(target.reloadSnapshot(source.snapshotValue()).ok());
    REQUIRE(runtime.hasPbrSurface());
    const auto published = std::make_unique<eve::graphics::PbrSurface>(runtime.pbrSurface());
    checkVegetationField(published->vegetationExtras, &assets.texture, 3u, 0.1f);
    checkVegetationField(published->vegetationColors, &assets.texture, 7u, 0.5f);

    for (const char* path : {"vegetation.extras.enabled", "vegetation.colors.enabled"}) {
        auto operation = source.makeSet(selection, PropertyPath(path), false, PropertySetMode::Absolute);
        REQUIRE(operation.ok());
        REQUIRE(source.applyDomainOperation(operation.value()).ok());
    }
    assets.rejectColors = true;
    REQUIRE(target.reloadSnapshot(source.snapshotValue()).ok());
    const auto cleared = std::make_unique<eve::graphics::PbrSurface>(runtime.pbrSurface());
    CHECK_EQ(cleared->vegetationExtras.texture, nullptr);
    CHECK_EQ(cleared->vegetationColors.texture, nullptr);
    CHECK_EQ(cleared->vegetationColors.fallback[3], 0.f);
}

TEST_CASE("editor.material.vegetation_vertex_snapshot_reaches_bound_runtime_material") {
    MaterialDocumentTarget  source("runtime-vertex");
    const SelectionSnapshot selection = materialSelection(source);
    configureVegetationVertex(source, selection);

    auto* renderable = eve::graphics::Renderable3D::create();
    REQUIRE(renderable != nullptr);
    eve::graphics::Material runtime;
    renderable->setMaterial(&runtime);
    SelectiveMaterialAssets assets;
    assets.rejectDetail = false;
    Renderable3DMaterialRuntimeSink sink(renderable, &assets);
    MaterialPublishingTarget        target("runtime-vertex", &sink);
    REQUIRE(target.reloadSnapshot(source.snapshotValue()).ok());
    REQUIRE(runtime.hasPbrSurface());
    const auto  published = std::make_unique<eve::graphics::PbrSurface>(runtime.pbrSurface());
    const auto& vertex    = published->vegetationVertex;
    CHECK_EQ(vertex.texture, &assets.texture);
    CHECK_EQ(vertex.layer, 6u);
    CHECK_EQ(vertex.fallback[3], 0.8f);
    CHECK_EQ(vertex.coords[2], 0.25f);
    CHECK_EQ(vertex.usage[8], 0.9f);
    CHECK_EQ(vertex.source, eve::graphics::PbrVegetationDeformationSource::GpuFields);
    CHECK_EQ(vertex.globalSize, 0.85f);
    CHECK_EQ(vertex.sizeFadeStart, 12.f);
    CHECK_EQ(vertex.sizeFadeEnd, 96.f);
    CHECK_EQ(vertex.distanceFadeBias, 1.5f);

    auto disable =
        source.makeSet(selection, PropertyPath("vegetation.vertex.enabled"), false, PropertySetMode::Absolute);
    REQUIRE(disable.ok());
    REQUIRE(source.applyDomainOperation(disable.value()).ok());
    assets.rejectVertex = true;
    REQUIRE(target.reloadSnapshot(source.snapshotValue()).ok());
    const auto cleared = std::make_unique<eve::graphics::PbrSurface>(runtime.pbrSurface());
    CHECK_EQ(cleared->vegetationVertex.texture, nullptr);
    CHECK_EQ(cleared->vegetationVertex.source, eve::graphics::PbrVegetationDeformationSource::RestMesh);
}

TEST_CASE("editor.material.missing_vegetation_vertex_texture_rejects_atomically") {
    MaterialDocumentTarget  source("missing-runtime-vertex");
    const SelectionSnapshot selection = materialSelection(source);
    configureVegetationVertex(source, selection);

    auto* renderable = eve::graphics::Renderable3D::create();
    REQUIRE(renderable != nullptr);
    eve::graphics::Material runtime;
    renderable->setMaterial(&runtime);
    SelectiveMaterialAssets assets;
    assets.rejectDetail = false;
    assets.rejectVertex = true;
    Renderable3DMaterialRuntimeSink sink(renderable, &assets);
    MaterialPublishingTarget        target("missing-runtime-vertex", &sink);
    const Revision                  before = target.revision();

    CHECK_EQ(static_cast<int>(target.reloadSnapshot(source.snapshotValue()).code()),
             static_cast<int>(EditorStatus::NotFound));
    CHECK_EQ(target.revision(), before);
    CHECK(!runtime.hasPbrSurface());
}

TEST_CASE("editor.material.vegetation_motion_snapshot_reaches_bound_runtime_material") {
    MaterialDocumentTarget  source("runtime-motion");
    const SelectionSnapshot selection = materialSelection(source);
    configureVegetationVertex(source, selection);
    configureVegetationMotion(source, selection);

    auto* renderable = eve::graphics::Renderable3D::create();
    REQUIRE(renderable != nullptr);
    eve::graphics::Material runtime;
    renderable->setMaterial(&runtime);
    SelectiveMaterialAssets assets;
    assets.rejectDetail = false;
    Renderable3DMaterialRuntimeSink sink(renderable, &assets);
    MaterialPublishingTarget        target("runtime-motion", &sink);
    REQUIRE(target.reloadSnapshot(source.snapshotValue()).ok());
    REQUIRE(runtime.hasPbrSurface());
    const auto  published = std::make_unique<eve::graphics::PbrSurface>(runtime.pbrSurface());
    const auto& motion    = published->vegetationMotion;
    CHECK_EQ(motion.texture, &assets.texture);
    CHECK_EQ(motion.noise, &assets.texture);
    CHECK_EQ(motion.mode, eve::graphics::PbrVegetationMotionMode::Object);
    CHECK_EQ(motion.layer, 5u);
    CHECK_EQ(motion.fallback[2], 0.3f);
    CHECK_EQ(motion.coords[3], 0.75f);
    CHECK_EQ(motion.globalDirection[1], 0.8f);
    CHECK_EQ(motion.worldOrigin[2], 30.f);
    CHECK_EQ(motion.usage[8], 0.9f);
    CHECK_EQ(motion.time, 42.5);
    CHECK_EQ(motion.dynamicMode, 0.1f);
    CHECK_EQ(motion.rigidity, 0.2f);
    CHECK_EQ(motion.facing, 0.3f);
    CHECK_EQ(motion.interactionMask, 0.4f);
    CHECK_EQ(motion.bending, 1.f);
    CHECK_EQ(motion.bendingSpeed, 2.f);
    CHECK_EQ(motion.bendingScale, 3.f);
    CHECK_EQ(motion.bendingVariation, 4.f);
    CHECK_EQ(motion.branch, 5.f);
    CHECK_EQ(motion.rolling, 6.f);
    CHECK_EQ(motion.branchSpeed, 7.f);
    CHECK_EQ(motion.branchScale, 8.f);
    CHECK_EQ(motion.branchVariation, 9.f);
    CHECK_EQ(motion.flutter, 10.f);
    CHECK_EQ(motion.flutterSpeed, 11.f);
    CHECK_EQ(motion.flutterScale, 12.f);
    CHECK_EQ(motion.flutterVariation, 13.f);
    CHECK_EQ(motion.globalBending, 14.f);
    CHECK_EQ(motion.globalBranch, 15.f);
    CHECK_EQ(motion.globalFlutter, 16.f);
    CHECK_EQ(motion.noiseTiling, 17.f);
    CHECK_EQ(motion.interaction, 18.f);
    CHECK_EQ(motion.fadeDistance, 19.f);
    CHECK_EQ(motion.perspectivePush, 20.f);
    CHECK_EQ(motion.perspectiveNoise, 21.f);
    CHECK_EQ(motion.perspectiveAngle, 22.f);

    auto disable =
        source.makeSet(selection, PropertyPath("vegetation.motion.mode"), "disabled", PropertySetMode::Absolute);
    REQUIRE(disable.ok());
    REQUIRE(source.applyDomainOperation(disable.value()).ok());
    assets.rejectMotion = true;
    REQUIRE(target.reloadSnapshot(source.snapshotValue()).ok());
    const auto cleared = std::make_unique<eve::graphics::PbrSurface>(runtime.pbrSurface());
    CHECK_EQ(cleared->vegetationMotion.texture, nullptr);
    CHECK_EQ(cleared->vegetationMotion.noise, nullptr);
    CHECK_EQ(cleared->vegetationMotion.mode, eve::graphics::PbrVegetationMotionMode::Disabled);
}

TEST_CASE("editor.material.missing_vegetation_motion_texture_rejects_atomically") {
    MaterialDocumentTarget  source("missing-runtime-motion");
    const SelectionSnapshot selection = materialSelection(source);
    configureVegetationVertex(source, selection);
    configureVegetationMotion(source, selection);

    auto* renderable = eve::graphics::Renderable3D::create();
    REQUIRE(renderable != nullptr);
    eve::graphics::Material runtime;
    renderable->setMaterial(&runtime);
    SelectiveMaterialAssets assets;
    assets.rejectDetail = false;
    assets.rejectMotion = true;
    Renderable3DMaterialRuntimeSink sink(renderable, &assets);
    MaterialPublishingTarget        target("missing-runtime-motion", &sink);
    const Revision                  before = target.revision();

    CHECK_EQ(static_cast<int>(target.reloadSnapshot(source.snapshotValue()).code()),
             static_cast<int>(EditorStatus::NotFound));
    CHECK_EQ(target.revision(), before);
    CHECK(!runtime.hasPbrSurface());
}

TEST_CASE("editor.material.properties_are_validated_and_reversible") {
    MaterialDocumentTarget  target("metal-panel");
    LocalWorldAuthority     authority(&target);
    LocalTransactionBackend transactions(&authority);
    const SelectionSnapshot selection = materialSelection(target);

    auto invalid = target.makeSet(selection, PropertyPath("shading.metallic"), 2.0, PropertySetMode::Absolute);
    CHECK_EQ(static_cast<int>(invalid.code()), static_cast<int>(EditorStatus::Rejected));

    auto change = target.makeSet(selection, PropertyPath("shading.metallic"), 0.8, PropertySetMode::Absolute);
    REQUIRE(change.ok());
    REQUIRE(commitMaterial(target, transactions, change.value(), "material.metallic").ok());
    CHECK(target.read(selection, PropertyPath("shading.metallic")).value == EditorValue(0.8));
    REQUIRE(transactions.undo().ok());
    CHECK(target.read(selection, PropertyPath("shading.metallic")).value == EditorValue(0.0));
    REQUIRE(transactions.redo().ok());
    CHECK(target.read(selection, PropertyPath("shading.metallic")).value == EditorValue(0.8));
}

TEST_CASE("editor.material.snapshot_roundtrip_and_cross_field_diagnostics") {
    MaterialDocumentTarget  source("source");
    LocalWorldAuthority     authority(&source);
    LocalTransactionBackend transactions(&authority);
    const SelectionSnapshot selection = materialSelection(source);

    for (const auto& [path, value, id] :
         {std::tuple{PropertyPath("shading.model"), EditorValue("custom"), "material.model"},
          {PropertyPath("surface.mode"), EditorValue("masked"), "material.surface"},
          {PropertyPath("textures.height"), EditorValue("asset://height.png"), "material.height"}}) {
        auto operation = source.makeSet(selection, path, value, PropertySetMode::Absolute);
        REQUIRE(operation.ok());
        REQUIRE(commitMaterial(source, transactions, operation.value(), id).ok());
    }
    CHECK_EQ(source.validate().size(), static_cast<std::size_t>(3));

    MaterialDocumentTarget restored("restored");
    REQUIRE(restored.loadSnapshot(source.snapshotValue()).ok());
    const SelectionSnapshot restoredSelection = materialSelection(restored);
    CHECK(restored.read(restoredSelection, PropertyPath("shading.model")).value == EditorValue("custom"));
    CHECK(restored.read(restoredSelection, PropertyPath("textures.height")).value == EditorValue("asset://height.png"));

    EditorValue::Object invalidRoot;
    invalidRoot["schemaVersion"] = 99;
    invalidRoot["properties"]    = EditorValue::Object{};
    CHECK_EQ(static_cast<int>(restored.loadSnapshot(EditorValue(std::move(invalidRoot))).code()),
             static_cast<int>(EditorStatus::Rejected));
}

// editor_persistence_v2.cpp
using namespace eve::editor;

TEST_CASE("editor.v2.scene_document_and_savegame_share_one_persistence_contract") {
    MemoryAtomicDocumentStore documentStore;
    MemoryAtomicDocumentStore saveGameStore;
    EditorPersistenceAdapter  document(EditorPersistenceKind::SceneDocument, &documentStore,
                                       "content://World/Park.evscene");
    EditorPersistenceAdapter  saveGame(EditorPersistenceKind::SaveGame, &saveGameStore, "savegame://player-1/park");
    auto                      documentBase = document.load();
    auto                      saveBase     = saveGame.load();
    REQUIRE(documentBase.ok());
    REQUIRE(saveBase.ok());

    DomainOperation operation;
    operation.type        = "park.place-tree";
    operation.inverseType = "park.remove-tree";
    operation.target      = TargetId("park-world");
    operation.payload     = EditorValue::Object{{"species", EditorValue("oak")}};
    operation.hasInverse  = true;
    const EditorValue checkpoint(EditorValue::Object{{"treeCount", EditorValue(1)}});
    auto              documentCommit = document.commit(documentBase.value(), checkpoint, {operation});
    auto              saveCommit     = saveGame.commit(saveBase.value(), checkpoint, {operation});
    REQUIRE(documentCommit.ok());
    REQUIRE(saveCommit.ok());
    CHECK(documentCommit.value().content == saveCommit.value().content);
    CHECK_EQ(documentCommit.value().journal.size(), saveCommit.value().journal.size());
    CHECK_EQ(documentCommit.value().journal.front().type, saveCommit.value().journal.front().type);

    auto stale = saveGame.commit(saveBase.value(), checkpoint, {operation});
    CHECK_EQ(static_cast<int>(stale.code()), static_cast<int>(EditorStatus::Conflict));
}

// editor_physics_target.cpp
using namespace eve::editor;

namespace {

SelectionSnapshot colliderSelection(const PhysicsColliderTarget& target) {
    SelectionSnapshot selection;
    selection.channel = "scene";
    SelectionItem item;
    item.domain = SelectionDomain::Scene;
    item.target = TargetId(target.targetId());
    item.item   = StableId(target.targetId().value());
    item.type   = target.describe().type;
    selection.items.push_back(item);
    selection.primary = item;
    return selection;
}

SelectionSnapshot jointSelection(const PhysicsJointTarget& target) {
    SelectionSnapshot selection;
    selection.channel = "scene";
    SelectionItem item;
    item.domain = SelectionDomain::Scene;
    item.target = TargetId(target.targetId());
    item.item   = StableId(target.targetId().value());
    item.type   = target.describe().type;
    selection.items.push_back(item);
    selection.primary = item;
    return selection;
}

Result<TransactionReceipt> commitCollider(PhysicsColliderTarget& target, LocalTransactionBackend& transactions,
                                          const DomainOperation& operation, const char* id) {
    TransactionSpec specification;
    specification.id           = TransactionId(id);
    specification.label        = "Edit collider";
    specification.target       = TargetId(target.targetId());
    specification.baseRevision = target.revision();
    auto begun                 = transactions.begin(std::move(specification));
    if (!begun.ok())
        return eve::editing::failed<TransactionReceipt>(begun.code(), RuleId("test.physics.begin"),
                                                        "Could not begin collider transaction");
    auto appended = transactions.append(operation);
    if (!appended.ok()) {
        [[maybe_unused]] const auto rolledBack = transactions.rollback();
        return eve::editing::failed<TransactionReceipt>(appended.code(), RuleId("test.physics.append"),
                                                        "Could not append collider operation");
    }
    return transactions.commit();
}

}  // namespace

TEST_CASE("editor.physics.collider_schema_tracks_backend_dimensionality") {
    PhysicsColliderTarget target2d("fixture", 2);
    PhysicsColliderTarget target3d("shape", 3);
    const auto            schema2d = target2d.schema(colliderSelection(target2d));
    const auto            schema3d = target3d.schema(colliderSelection(target3d));
    CHECK_EQ(schema2d.typeId, std::string("physics.collider2d"));
    CHECK_EQ(schema3d.typeId, std::string("physics.collider3d"));
    REQUIRE(schema2d.find(PropertyPath("shape.kind")));
    REQUIRE(schema3d.find(PropertyPath("shape.kind")));
    CHECK_EQ(schema2d.find(PropertyPath("shape.kind"))->enumItems.size(), static_cast<std::size_t>(4));
    CHECK_EQ(schema3d.find(PropertyPath("shape.kind"))->enumItems.size(), static_cast<std::size_t>(6));
    CHECK_EQ(target2d.describe().type, std::string("physics-collider-2d"));
    CHECK_EQ(target3d.describe().type, std::string("physics-collider-3d"));
}

TEST_CASE("editor.physics.collider_edits_validate_and_undo") {
    PhysicsColliderTarget   target("shape", 3);
    LocalWorldAuthority     authority(&target);
    LocalTransactionBackend transactions(&authority);
    const SelectionSnapshot selection = colliderSelection(target);

    auto invalid = target.makeSet(selection, PropertyPath("shape.size"), EditorValue::Array{1.0, -2.0, 3.0},
                                  PropertySetMode::Absolute);
    CHECK_EQ(static_cast<int>(invalid.code()), static_cast<int>(EditorStatus::Rejected));
    invalid = target.makeSet(selection, PropertyPath("material.friction"), 1.5, PropertySetMode::Absolute);
    CHECK_EQ(static_cast<int>(invalid.code()), static_cast<int>(EditorStatus::Rejected));

    auto shape = target.makeSet(selection, PropertyPath("shape.kind"), "capsule", PropertySetMode::Absolute);
    REQUIRE(shape.ok());
    REQUIRE(commitCollider(target, transactions, shape.value(), "collider.shape").ok());
    auto radius = target.makeSet(selection, PropertyPath("shape.radius"), 0.75, PropertySetMode::Absolute);
    REQUIRE(radius.ok());
    REQUIRE(commitCollider(target, transactions, radius.value(), "collider.radius").ok());
    CHECK(target.read(selection, PropertyPath("shape.radius")).value == EditorValue(0.75));
    REQUIRE(transactions.undo().ok());
    CHECK(target.read(selection, PropertyPath("shape.radius")).value == EditorValue(0.5));
    REQUIRE(transactions.redo().ok());
    CHECK_EQ(target.validate().size(), static_cast<std::size_t>(1));
}

TEST_CASE("editor.physics.collider_snapshot_is_versioned_atomic_and_diagnostic") {
    PhysicsColliderTarget   source("source", 3);
    const SelectionSnapshot selection = colliderSelection(source);
    auto mesh = source.makeSet(selection, PropertyPath("shape.kind"), "triangle-mesh", PropertySetMode::Absolute);
    REQUIRE(mesh.ok());
    REQUIRE(source.applyDomainOperation(mesh.value()).ok());
    CHECK_EQ(source.validate().size(), static_cast<std::size_t>(1));

    PhysicsColliderTarget restored("restored", 3);
    REQUIRE(restored.loadSnapshot(source.snapshotValue()).ok());
    CHECK(restored.read(colliderSelection(restored), PropertyPath("shape.kind")).value == EditorValue("triangle-mesh"));

    PhysicsColliderTarget wrongDimension("wrong", 2);
    CHECK_EQ(static_cast<int>(wrongDimension.loadSnapshot(source.snapshotValue()).code()),
             static_cast<int>(EditorStatus::Rejected));
    CHECK(wrongDimension.read(colliderSelection(wrongDimension), PropertyPath("shape.kind")).value ==
          EditorValue("box"));

    EditorValue unknownVersion = source.snapshotValue();
    auto*       unknownRoot    = unknownVersion.getIf<EditorValue::Object>();
    REQUIRE(unknownRoot != nullptr);
    unknownRoot->at("schemaVersion")               = int64_t{2};
    const Revision revisionBeforeRejectedMigration = restored.revision();
    CHECK_EQ(static_cast<int>(restored.loadSnapshot(unknownVersion).code()), static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(restored.revision(), revisionBeforeRejectedMigration);
    CHECK(restored.read(colliderSelection(restored), PropertyPath("shape.kind")).value == EditorValue("triangle-mesh"));
}

TEST_CASE("editor.physics.joint_exposes_body_anchor_limit_motor_and_break_authoring") {
    PhysicsJointTarget      target("suspension");
    const SelectionSnapshot selection = jointSelection(target);
    CHECK_EQ(target.schema(selection).typeId, std::string("physics.joint3d"));
    CHECK(target.schema(selection).find(PropertyPath("body.a")) != nullptr);
    CHECK(target.schema(selection).find(PropertyPath("joint.axis")) != nullptr);
    CHECK(target.schema(selection).find(PropertyPath("motor.force")) != nullptr);
    CHECK(target.schema(selection).find(PropertyPath("break.torque")) != nullptr);
    CHECK_EQ(target.validate().size(), static_cast<std::size_t>(1));

    for (const auto& [path, value] : {std::pair{PropertyPath("body.a"), EditorValue("vehicle")},
                                      {PropertyPath("body.b"), EditorValue("wheel")},
                                      {PropertyPath("joint.kind"), EditorValue("wheel")},
                                      {PropertyPath("limit.enabled"), EditorValue(true)},
                                      {PropertyPath("limit.minimum"), EditorValue(2.0)},
                                      {PropertyPath("limit.maximum"), EditorValue(1.0)}}) {
        auto operation = target.makeSet(selection, path, value, PropertySetMode::Absolute);
        REQUIRE(operation.ok());
        REQUIRE(target.applyDomainOperation(operation.value()).ok());
    }
    CHECK_EQ(target.validate().size(), static_cast<std::size_t>(1));

    auto fixLimit = target.makeSet(selection, PropertyPath("limit.maximum"), 3.0, PropertySetMode::Absolute);
    REQUIRE(fixLimit.ok());
    REQUIRE(target.applyDomainOperation(fixLimit.value()).ok());
    CHECK(target.validate().empty());
    CHECK_EQ(static_cast<int>(target.snapshotValue().type()), static_cast<int>(EditorValue::Type::Object));
}

// editor_plugin_permissions.cpp
using namespace eve::editor;

TEST_CASE("editor.plugins.permissions_are_narrow_reversible_and_default_to_ask") {
    PluginPermissionTarget permissions("project-plugins");
    CHECK_EQ(
        static_cast<int>(permissions.makeSet({StableId("broad"), "importer", "filesystem.write", "/", "allow"}).code()),
        static_cast<int>(EditorStatus::Rejected));
    PluginPermissionGrant grant{StableId("assets"), "model-importer", "filesystem.read", "assets/models", "allow"};
    auto                  set = permissions.makeSet(grant);
    REQUIRE(set.ok());
    CHECK(permissions.applyDomainOperation(set.value()).ok());
    CHECK_EQ(permissions.decision("model-importer", "filesystem.read", "assets/models"), std::string("allow"));
    CHECK_EQ(permissions.decision("model-importer", "network.connect", "example.com"), std::string("ask"));
    DomainOperation undo = set.value();
    undo.type            = set.value().inverseType;
    undo.payload         = set.value().inverse;
    CHECK(permissions.applyDomainOperation(undo).ok());
    CHECK(permissions.grants().empty());
}

TEST_CASE("editor.plugins.permission_snapshot_is_atomic_and_rejects_duplicate_semantics") {
    PluginPermissionTarget permissions("project-plugins");
    auto set = permissions.makeSet({StableId("one"), "sync", "network.connect", "api.example.com:443", "ask"});
    REQUIRE(set.ok());
    CHECK(permissions.applyDomainOperation(set.value()).ok());
    EditorValue invalid = permissions.snapshotValue();
    auto*       root    = invalid.getIf<EditorValue::Object>();
    REQUIRE(root);
    auto* grants = root->at("grants").getIf<EditorValue::Array>();
    REQUIRE(grants);
    grants->push_back(EditorValue::Object{{"id", "two"},
                                          {"plugin", "sync"},
                                          {"capability", "network.connect"},
                                          {"scope", "api.example.com:443"},
                                          {"decision", "allow"}});
    CHECK_EQ(static_cast<int>(permissions.loadSnapshot(invalid).code()), static_cast<int>(EditorStatus::Conflict));
    CHECK_EQ(permissions.grants().size(), 1U);
}

// editor_procgen_script.cpp
TEST_CASE("editor.procgen.script_editor_script_loads_schema_and_keeps_failed_preview") {
    ssq::VM vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        editor <- eve.Editor();
        workspace <- editor.newWorkspace("test.procgen", "Test Procgen Editor");
        created <- eve.ProcgenEditorModule().create("test.asset.forest");
        pcg <- created.value;
        configured <- pcg.configureWorkspace(workspace);
        workspacePanelCount <- workspace.getPanelCount();
        panel0 <- workspace.getPanelId(0);
        cap0 <- workspace.getPanelCapability(0);
        ctx3 <- workspace.getPanelContext(3);
        schema <- [
            { key="seed", kind="int", min=1, max=9999, defaultValue=42, label="Seed" },
            { key="density", kind="float", min=0.1, max=1.0, step=0.05, defaultValue=0.8, label="Density" }
        ];
        loaded <- pcg.loadModule("game:/generators/forest.nut", "forest", "Forest", "points", schema);
        procgen <- eve.Procgen();
        pointsResult <- procgen.newPointSet();
        points <- pointsResult.value;
        points.add(1.0, 0.0, 2.0);
        points.add(3.0, 0.0, 4.0);
        published <- pcg.publishPreview(points, "trees", pcg.getRevision());
        beforePoints <- pcg.getPointCount();
        density <- pcg.setFloat("density", 0.4);
        failed <- pcg.failPreview("boom", pcg.getRevision());
        afterFail <- pcg.getPointCount();
        previewRev <- pcg.getPreviewRevision();
        undoResult <- pcg.undo();
        restoredDensity <- pcg.getFloat("density");
        rangeReject <- pcg.setFloat("density", 2.0);
    )"));

    CHECK(vm.find("created").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("created").toTable().get<std::string>("ownership"), std::string("owned"));
    CHECK(vm.find("configured").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("workspacePanelCount").toInt(), 4);
    CHECK_EQ(vm.find("panel0").toString(), std::string("procgen.modules"));
    CHECK_EQ(vm.find("cap0").toString(), std::string("procgen.script"));
    CHECK_EQ(vm.find("ctx3").toString(), std::string("stages"));
    CHECK(vm.find("loaded").toTable().get<bool>("ok"));
    CHECK(vm.find("published").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("beforePoints").toInt(), 2);
    CHECK(vm.find("density").toTable().get<bool>("ok"));
    CHECK(vm.find("failed").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("afterFail").toInt(), 2);
    CHECK(vm.find("undoResult").toTable().get<bool>("ok"));
    CHECK(std::abs(vm.find("restoredDensity").toFloat() - 0.8f) < 0.0001f);
    CHECK(!vm.find("rangeReject").toTable().get<bool>("ok"));
}

// editor_project_settings.cpp
using namespace eve::editor;

namespace {
SelectionSnapshot selection(const ProjectSettingsTarget& target) {
    SelectionSnapshot result;
    SelectionItem     item;
    item.target = TargetId(target.targetId());
    item.item   = StableId(target.targetId().value());
    result.items.push_back(item);
    result.primary = item;
    return result;
}
}  // namespace

TEST_CASE("editor.settings.secret_references_are_redacted_and_raw_values_rejected") {
    ProjectSettingsTarget settings("project", defaultProjectSettingsSchema());
    const auto            selected = selection(settings);
    auto raw = settings.makeSet(selected, PropertyPath("network.auth-token"), "plain-token", PropertySetMode::Absolute);
    CHECK_EQ(static_cast<int>(raw.code()), static_cast<int>(EditorStatus::Rejected));
    auto secret = settings.makeSet(selected, PropertyPath("network.auth-token"), "secret://project/network-token",
                                   PropertySetMode::Absolute);
    REQUIRE(secret.ok());
    CHECK(settings.applyDomainOperation(secret.value()).ok());
    auto read = settings.read(selected, PropertyPath("network.auth-token"));
    CHECK(read.value == EditorValue("secret://redacted"));
    const EditorValue snapshot = settings.snapshotValue();
    const auto*       root     = snapshot.getIf<EditorValue::Object>();
    REQUIRE(root);
    const auto* values = root->at("values").getIf<EditorValue::Object>();
    REQUIRE(values);
    CHECK(values->at("network.auth-token") == EditorValue("secret://project/network-token"));
}

TEST_CASE("editor.settings_changes_are_reversible_restart_aware_and_atomic") {
    ProjectSettingsTarget settings("project", defaultProjectSettingsSchema());
    const auto            selected = selection(settings);
    auto change = settings.makeSet(selected, PropertyPath("content.asset-root"), "content", PropertySetMode::Absolute);
    REQUIRE(change.ok());
    CHECK(settings.applyDomainOperation(change.value()).ok());
    CHECK_EQ(settings.pendingRestart().size(), 1U);
    DomainOperation undo = change.value();
    undo.type            = change.value().inverseType;
    undo.payload         = change.value().inverse;
    CHECK(settings.applyDomainOperation(undo).ok());
    CHECK(settings.read(selected, PropertyPath("content.asset-root")).value == EditorValue("assets"));

    const auto  validSnapshot = settings.snapshotValue();
    EditorValue invalid       = validSnapshot;
    auto*       root          = invalid.getIf<EditorValue::Object>();
    REQUIRE(root);
    auto* values = root->at("values").getIf<EditorValue::Object>();
    REQUIRE(values);
    (*values)["unknown.setting"] = true;
    CHECK_EQ(static_cast<int>(settings.loadSnapshot(invalid).code()), static_cast<int>(EditorStatus::Rejected));
    CHECK(settings.read(selected, PropertyPath("content.asset-root")).value == EditorValue("assets"));
}
