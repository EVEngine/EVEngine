#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "editor/EditorAuthority.h"
#include "graphics/material/editing/MaterialTarget.h"
#include "editor/EditorTransactionService.h"
#include "graphics/RenderSystem3D.h"

#include <tuple>

using namespace eve::material_editing;
using namespace eve::editing;
using eve::editor::LocalTransactionBackend;

namespace {

class NullMaterialAssets final : public IMaterialRuntimeAssetResolver {
public:
    EditorResult<eve::graphics::Texture*> resolveTexture(const std::string&) const override {
        return eve::editing::applied<eve::graphics::Texture*>(&texture);
    }
    EditorResult<eve::graphics::Shader*> resolveShader(const std::string&) const override {
        return eve::editing::applied<eve::graphics::Shader*>(nullptr);
    }

    mutable eve::graphics::Texture texture;
};

class SelectiveMaterialAssets final : public IMaterialRuntimeAssetResolver {
public:
    EditorResult<eve::graphics::Texture*> resolveTexture(const std::string& asset) const override {
        if ((rejectDetail && asset == "asset://tve/detail-albedo") ||
            (rejectColors && asset == "asset://tve/colors-field") ||
            (rejectVertex && asset == "asset://tve/vertex-field") ||
            (rejectMotion && (asset == "asset://tve/motion-field" || asset == "asset://tve/motion-noise")))
            return eve::editing::failed<eve::graphics::Texture*>(
                EditorStatus::NotFound, RuleId("editor.material.test-missing-detail"),
                "Requested test texture is unavailable");
        return eve::editing::applied<eve::graphics::Texture*>(&texture);
    }
    EditorResult<eve::graphics::Shader*> resolveShader(const std::string&) const override {
        return eve::editing::applied<eve::graphics::Shader*>(nullptr);
    }

    mutable eve::graphics::Texture texture;
    bool rejectDetail = true;
    bool rejectColors = false;
    bool rejectVertex = false;
    bool rejectMotion = false;
};

SelectionSnapshot materialSelection(const MaterialDocumentTarget& target) {
    SelectionSnapshot selection;
    selection.channel = "asset";
    SelectionItem item;
    item.domain = SelectionDomain::Asset;
    item.target = TargetId(target.targetId());
    item.item = StableId(target.targetId().value());
    item.type = "graphics.material";
    selection.items.push_back(item);
    selection.primary = item;
    return selection;
}

EditorResult<TransactionReceipt> commitMaterial(MaterialDocumentTarget& target,
                                                LocalTransactionBackend& transactions,
                                                const DomainOperation& operation,
                                                const char* transactionId) {
    TransactionSpec specification;
    specification.id = TransactionId(transactionId);
    specification.label = "Edit material";
    specification.target = TargetId(target.targetId());
    specification.baseRevision = target.revision();
    auto begun = transactions.begin(std::move(specification));
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

void checkPublishedVegetationStages(const eve::graphics::Material& runtime,
                                    const eve::graphics::PbrSurface& surface,
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
    CHECK_EQ(surface.textures[std::size_t(eve::graphics::PbrTextureSlot::MetallicRoughness)].texture,
             texture);
    CHECK(!surface.textures[std::size_t(eve::graphics::PbrTextureSlot::MetallicRoughness)].srgbDecode);
    CHECK_EQ(surface.textures[std::size_t(eve::graphics::PbrTextureSlot::Emissive)].texture,
             texture);
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
    CHECK_EQ(surface.vegetationColor.backfaceNormalMode,
             eve::graphics::PbrVegetationBackfaceNormalMode::Same);
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
        for (std::size_t i = 0; i < 9; ++i)
            set((prefix + ".usage-" + std::to_string(i)).c_str(), double(i + 1) / 10.0);
    }
    set("vegetation.extras.fallback", EditorValue::Array{0.1, 0.2, 0.3, 0.4});
    set("vegetation.colors.fallback", EditorValue::Array{0.5, 0.6, 0.7, 0.8});
}

template <class Field>
void checkVegetationField(const Field& field, eve::graphics::Texture* texture, uint32_t layer,
                          float fallbackFirst) {
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
        "bending",          "bending-speed",    "bending-scale",      "bending-variation",
        "branch",           "rolling",          "branch-speed",       "branch-scale",
        "branch-variation", "flutter",          "flutter-speed",      "flutter-scale",
        "flutter-variation", "global-bending",  "global-branch",      "global-flutter",
        "noise-tiling",     "interaction",      "fade-distance",      "perspective-push",
        "perspective-noise", "perspective-angle"};
    for (std::size_t i = 0; i < names.size(); ++i)
        set((std::string("vegetation.motion.") + names[i]).c_str(), double(i + 1));
}

}  // namespace

TEST_CASE("editor.material.schema_matches_core_material_authoring_surface") {
    MaterialDocumentTarget target("metal-panel");
    const SelectionSnapshot selection = materialSelection(target);
    const PropertySchema schema = target.schema(selection);
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
    MaterialDocumentTarget target("leaf-alpha");
    LocalWorldAuthority authority(&target);
    LocalTransactionBackend transactions(&authority);
    const SelectionSnapshot selection = materialSelection(target);

    for (const auto& [path, value, id] : {
             std::tuple{PropertyPath("vegetation.alpha.enabled"), EditorValue(true), "vegetation.alpha.enabled"},
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

    auto invalid = target.makeSet(selection, PropertyPath("vegetation.alpha.constant"), 1.1,
                                  PropertySetMode::Absolute);
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
        const auto* encoded = encodedSnapshot.getIf<EditorValue::Object>();
        REQUIRE(encoded != nullptr);
        CHECK(encoded->at("schemaVersion") == EditorValue(9));
    }
}

TEST_CASE("editor.material.translucency_mask_requires_orm_atomically") {
    MaterialDocumentTarget source("masked-translucency");
    const SelectionSnapshot selection = materialSelection(source);
    for (const auto& [path, value] : {
             std::pair{PropertyPath("vegetation.translucency.enabled"), EditorValue(true)},
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
    NullMaterialAssets assets;
    Renderable3DMaterialRuntimeSink sink(renderable, &assets);
    MaterialPublishingTarget target("masked-translucency", &sink);
    const Revision before = target.revision();
    CHECK_EQ(static_cast<int>(target.reloadSnapshot(source.snapshotValue()).code()),
             static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(target.revision(), before);
    CHECK(!runtime.hasPbrSurface());
}

TEST_CASE("editor.material.vegetation_stages_snapshot_reaches_bound_runtime_material") {
    MaterialDocumentTarget source("runtime-leaf");
    const SelectionSnapshot selection = materialSelection(source);
    configureVegetationStages(source, selection);

    auto* renderable = eve::graphics::Renderable3D::create();
    REQUIRE(renderable != nullptr);
    eve::graphics::Material runtime;
    renderable->setMaterial(&runtime);
    NullMaterialAssets assets;
    Renderable3DMaterialRuntimeSink sink(renderable, &assets);
    MaterialPublishingTarget target("runtime-leaf", &sink);
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
    MaterialPublishingTarget unboundTarget("runtime-leaf", &unboundSink);
    const auto before = unboundTarget.revision();
    CHECK_EQ(static_cast<int>(unboundTarget.reloadSnapshot(source.snapshotValue()).code()),
             static_cast<int>(EditorStatus::Unsupported));
    CHECK_EQ(unboundTarget.revision(), before);

    for (const auto& [path, value] : {
             std::pair{PropertyPath("surface.mode"), EditorValue("opaque")},
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
    MaterialDocumentTarget source("missing-runtime-detail");
    const SelectionSnapshot selection = materialSelection(source);
    configureVegetationDetail(source, selection);

    auto* renderable = eve::graphics::Renderable3D::create();
    REQUIRE(renderable != nullptr);
    eve::graphics::Material runtime;
    renderable->setMaterial(&runtime);
    SelectiveMaterialAssets assets;
    Renderable3DMaterialRuntimeSink sink(renderable, &assets);
    MaterialPublishingTarget target("missing-runtime-detail", &sink);
    const Revision before = target.revision();

    CHECK_EQ(static_cast<int>(target.reloadSnapshot(source.snapshotValue()).code()),
             static_cast<int>(EditorStatus::NotFound));
    CHECK_EQ(target.revision(), before);
    CHECK(!runtime.hasPbrSurface());
}

TEST_CASE("editor.material.vegetation_detail_snapshot_reaches_bound_runtime_material") {
    MaterialDocumentTarget source("runtime-detail");
    const SelectionSnapshot selection = materialSelection(source);
    configureVegetationDetail(source, selection);

    auto* renderable = eve::graphics::Renderable3D::create();
    REQUIRE(renderable != nullptr);
    eve::graphics::Material runtime;
    renderable->setMaterial(&runtime);
    NullMaterialAssets assets;
    Renderable3DMaterialRuntimeSink sink(renderable, &assets);
    MaterialPublishingTarget target("runtime-detail", &sink);
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
    MaterialDocumentTarget source("runtime-fields");
    const SelectionSnapshot selection = materialSelection(source);
    configureVegetationFields(source, selection);

    auto* renderable = eve::graphics::Renderable3D::create();
    REQUIRE(renderable != nullptr);
    eve::graphics::Material runtime;
    renderable->setMaterial(&runtime);
    SelectiveMaterialAssets assets;
    assets.rejectDetail = false;
    Renderable3DMaterialRuntimeSink sink(renderable, &assets);
    MaterialPublishingTarget target("runtime-fields", &sink);
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
    MaterialDocumentTarget source("runtime-vertex");
    const SelectionSnapshot selection = materialSelection(source);
    configureVegetationVertex(source, selection);

    auto* renderable = eve::graphics::Renderable3D::create();
    REQUIRE(renderable != nullptr);
    eve::graphics::Material runtime;
    renderable->setMaterial(&runtime);
    SelectiveMaterialAssets assets;
    assets.rejectDetail = false;
    Renderable3DMaterialRuntimeSink sink(renderable, &assets);
    MaterialPublishingTarget target("runtime-vertex", &sink);
    REQUIRE(target.reloadSnapshot(source.snapshotValue()).ok());
    REQUIRE(runtime.hasPbrSurface());
    const auto published = std::make_unique<eve::graphics::PbrSurface>(runtime.pbrSurface());
    const auto& vertex = published->vegetationVertex;
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

    auto disable = source.makeSet(selection, PropertyPath("vegetation.vertex.enabled"), false,
                                  PropertySetMode::Absolute);
    REQUIRE(disable.ok());
    REQUIRE(source.applyDomainOperation(disable.value()).ok());
    assets.rejectVertex = true;
    REQUIRE(target.reloadSnapshot(source.snapshotValue()).ok());
    const auto cleared = std::make_unique<eve::graphics::PbrSurface>(runtime.pbrSurface());
    CHECK_EQ(cleared->vegetationVertex.texture, nullptr);
    CHECK_EQ(cleared->vegetationVertex.source,
             eve::graphics::PbrVegetationDeformationSource::RestMesh);
}

TEST_CASE("editor.material.missing_vegetation_vertex_texture_rejects_atomically") {
    MaterialDocumentTarget source("missing-runtime-vertex");
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
    MaterialPublishingTarget target("missing-runtime-vertex", &sink);
    const Revision before = target.revision();

    CHECK_EQ(static_cast<int>(target.reloadSnapshot(source.snapshotValue()).code()),
             static_cast<int>(EditorStatus::NotFound));
    CHECK_EQ(target.revision(), before);
    CHECK(!runtime.hasPbrSurface());
}

TEST_CASE("editor.material.vegetation_motion_snapshot_reaches_bound_runtime_material") {
    MaterialDocumentTarget source("runtime-motion");
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
    MaterialPublishingTarget target("runtime-motion", &sink);
    REQUIRE(target.reloadSnapshot(source.snapshotValue()).ok());
    REQUIRE(runtime.hasPbrSurface());
    const auto published = std::make_unique<eve::graphics::PbrSurface>(runtime.pbrSurface());
    const auto& motion = published->vegetationMotion;
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

    auto disable = source.makeSet(selection, PropertyPath("vegetation.motion.mode"), "disabled",
                                  PropertySetMode::Absolute);
    REQUIRE(disable.ok());
    REQUIRE(source.applyDomainOperation(disable.value()).ok());
    assets.rejectMotion = true;
    REQUIRE(target.reloadSnapshot(source.snapshotValue()).ok());
    const auto cleared = std::make_unique<eve::graphics::PbrSurface>(runtime.pbrSurface());
    CHECK_EQ(cleared->vegetationMotion.texture, nullptr);
    CHECK_EQ(cleared->vegetationMotion.noise, nullptr);
    CHECK_EQ(cleared->vegetationMotion.mode,
             eve::graphics::PbrVegetationMotionMode::Disabled);
}

TEST_CASE("editor.material.missing_vegetation_motion_texture_rejects_atomically") {
    MaterialDocumentTarget source("missing-runtime-motion");
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
    MaterialPublishingTarget target("missing-runtime-motion", &sink);
    const Revision before = target.revision();

    CHECK_EQ(static_cast<int>(target.reloadSnapshot(source.snapshotValue()).code()),
             static_cast<int>(EditorStatus::NotFound));
    CHECK_EQ(target.revision(), before);
    CHECK(!runtime.hasPbrSurface());
}

TEST_CASE("editor.material.properties_are_validated_and_reversible") {
    MaterialDocumentTarget target("metal-panel");
    LocalWorldAuthority authority(&target);
    LocalTransactionBackend transactions(&authority);
    const SelectionSnapshot selection = materialSelection(target);

    auto invalid = target.makeSet(selection, PropertyPath("shading.metallic"), 2.0,
                                  PropertySetMode::Absolute);
    CHECK_EQ(static_cast<int>(invalid.code()), static_cast<int>(EditorStatus::Rejected));

    auto change = target.makeSet(selection, PropertyPath("shading.metallic"), 0.8,
                                 PropertySetMode::Absolute);
    REQUIRE(change.ok());
    REQUIRE(commitMaterial(target, transactions, change.value(), "material.metallic").ok());
    CHECK(target.read(selection, PropertyPath("shading.metallic")).value == EditorValue(0.8));
    REQUIRE(transactions.undo().ok());
    CHECK(target.read(selection, PropertyPath("shading.metallic")).value == EditorValue(0.0));
    REQUIRE(transactions.redo().ok());
    CHECK(target.read(selection, PropertyPath("shading.metallic")).value == EditorValue(0.8));
}

TEST_CASE("editor.material.snapshot_roundtrip_and_cross_field_diagnostics") {
    MaterialDocumentTarget source("source");
    LocalWorldAuthority authority(&source);
    LocalTransactionBackend transactions(&authority);
    const SelectionSnapshot selection = materialSelection(source);

    for (const auto& [path, value, id] : {
             std::tuple{PropertyPath("shading.model"), EditorValue("custom"), "material.model"},
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
    CHECK(restored.read(restoredSelection, PropertyPath("textures.height")).value ==
          EditorValue("asset://height.png"));

    EditorValue::Object invalidRoot;
    invalidRoot["schemaVersion"] = 99;
    invalidRoot["properties"] = EditorValue::Object{};
    CHECK_EQ(static_cast<int>(restored.loadSnapshot(EditorValue(std::move(invalidRoot))).code()),
             static_cast<int>(EditorStatus::Rejected));
}
