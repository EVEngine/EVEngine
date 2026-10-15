#include <map>
#include <memory>
#include <string>
#include "camera/editing/CameraTarget.h"
#include "daynight/DayNight.h"
#include "decal/DecalManager.h"
#include "decal/editing/DecalTarget.h"
#include "fluids/editor/EditorSurfaceFluidPreview.h"
#include "graphics/Canvas.h"
#include "graphics/ICanvasFactory.h"
#include "graphics/ICanvasTarget.h"
#include "graphics/ISolidRectRenderer.h"
#include "graphics/Light.h"
#include "graphics/lighting/editing/LightingTarget.h"
#include "graphics/material/editing/MaterialGraph.h"
#include "graphics/material/editor/EditorMaterialOffscreen.h"
#include "hd2d/editing/Hd2dTarget.h"
#include "image/ImageData.h"
#include "particles/editor/EditorParticleOffscreen.h"
#include "ui/editor/EditorUiOffscreen.h"
#include "weather/Weather.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// editor_camera_target.cpp
using namespace eve::camera_editing;
using namespace eve::editing;

namespace {
void apply(CameraDocumentTarget& target, Result<DomainOperation> operation) {
    REQUIRE(operation.ok());
    REQUIRE(target.applyDomainOperation(operation.value()).ok());
}
SelectionSnapshot select(const CameraDocumentTarget& target, const char* id) {
    SelectionSnapshot selection;
    selection.channel = "camera";
    selection.items.push_back({SelectionDomain::Asset, TargetId(target.targetId()), StableId(id), "camera.rig"});
    return selection;
}
CameraRigValue rig(const char* id, const char* mode = "follow") {
    CameraRigValue value;
    value.id   = ObjectId(id);
    value.name = id;
    value.mode = mode;
    return value;
}
}  // namespace

TEST_CASE("editor.camera.rigs_are_property_editable_reversible_and_persistent") {
    CameraDocumentTarget target("shot");
    apply(target, target.makeCreateRig(rig("hero", "orbit")));
    const auto selection = select(target, "hero");
    auto       edit      = target.makeSet(selection, PropertyPath("rig.fov"), 72.0, PropertySetMode::Absolute);
    REQUIRE(edit.ok());
    REQUIRE(target.applyDomainOperation(edit.value()).ok());
    CHECK_EQ(*target.read(selection, PropertyPath("rig.fov")).value.getIf<double>(), 72.0);
    DomainOperation undo = edit.value();
    undo.payload         = edit.value().inverse;
    REQUIRE(target.applyDomainOperation(undo).ok());
    CHECK_EQ(*target.read(selection, PropertyPath("rig.fov")).value.getIf<double>(), 60.0);
    CameraDocumentTarget restored("copy");
    REQUIRE(restored.loadSnapshot(target.snapshotValue()).ok());
    CHECK_EQ(restored.rigs().front().id.value(), std::string("hero"));
}

TEST_CASE("editor.camera.timeline_has_stable_keys_reference_safety_and_scrub") {
    CameraDocumentTarget target("shot");
    auto                 hero = rig("hero", "follow");
    hero.offset               = {0, 3, 8};
    apply(target, target.makeCreateRig(hero));
    CameraTimelineKeyValue cut;
    cut.id    = ObjectId("cut-1");
    cut.kind  = "cut";
    cut.time  = 1;
    cut.rig   = ObjectId("hero");
    cut.blend = .5f;
    apply(target, target.makeCreateKey(cut));
    CameraTimelineKeyValue fov0;
    fov0.id       = ObjectId("fov-0");
    fov0.kind     = "float";
    fov0.time     = 1;
    fov0.property = "fov";
    fov0.value    = 60;
    apply(target, target.makeCreateKey(fov0));
    CameraTimelineKeyValue fov;
    fov.id       = ObjectId("fov-1");
    fov.kind     = "float";
    fov.time     = 3;
    fov.property = "fov";
    fov.value    = 80;
    apply(target, target.makeCreateKey(fov));
    CHECK_EQ(static_cast<int>(target.makeDeleteRig(ObjectId("hero")).code()), static_cast<int>(EditorStatus::Conflict));
    CameraPreview preview;
    auto          pose = preview.evaluate(target, 2);
    REQUIRE(pose.ok());
    CHECK_EQ(pose.value().rig.value(), std::string("hero"));
    CHECK_EQ(pose.value().fov, 70.f);
    auto overlay = preview.gizmo(target, 2);
    REQUIRE(overlay.ok());
    CHECK_EQ(overlay.value().primitives.size(), static_cast<std::size_t>(3));
}

TEST_CASE("editor.camera.invalid_snapshot_and_runtime_generation_are_atomic") {
    CameraDocumentTarget target("shot");
    apply(target, target.makeCreateRig(rig("hero")));
    const auto  before  = target.snapshotValue();
    EditorValue invalid = before;
    auto*       root    = invalid.getIf<EditorValue::Object>();
    auto*       content = (*root)["content"].getIf<EditorValue::Object>();
    auto*       rigs    = (*content)["rigs"].getIf<EditorValue::Array>();
    auto*       first   = (*rigs)[0].getIf<EditorValue::Object>();
    (*first)["fov"]     = 200.0;
    CHECK_EQ(static_cast<int>(target.loadSnapshot(invalid).code()), static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(target.snapshotValue(), before);
    CameraDocumentRuntime runtime;
    REQUIRE(runtime.publish(target, nullptr).ok());
    auto*                generation = runtime.controller();
    const auto           revision   = runtime.revision();
    CameraDocumentTarget empty("empty");
    REQUIRE(runtime.publish(empty, nullptr).ok());
    CHECK(runtime.controller() != generation);
    CHECK(runtime.revision() != revision);
}

// editor_decal_target.cpp
using namespace eve::decal_editing;
using namespace eve::editing;

namespace {
SelectionSnapshot selection(const char* target) {
    SelectionSnapshot value;
    value.items.push_back({SelectionDomain::Asset, TargetId(target), StableId("decal"), "decal.instance"});
    return value;
}
class Assets final : public IDecalRuntimeAssetResolver {
public:
    std::map<std::string, eve::graphics::Texture*> values;
    Result<eve::graphics::Texture*>                texture(const std::string& asset) const override {
        auto found = values.find(asset);
        if (found == values.end())
            return eve::editing::failed<eve::graphics::Texture*>(EditorStatus::NotFound, RuleId("test.decal.texture"),
                                                                                "missing texture");
        return eve::editing::applied<eve::graphics::Texture*>(found->second);
    }
};
Result<void> set(DecalDocumentTarget& target, const char* path, EditorValue value) {
    auto op = target.makeSet(selection(target.targetId().value().c_str()), PropertyPath(path), value,
                             PropertySetMode::Absolute);
    if (!op.ok()) return Result<void>::failure(op.status());
    return target.applyDomainOperation(op.value());
}
}  // namespace

TEST_CASE("editor.decal.properties_are_reversible_persistent_and_gizmo_ready") {
    DecalDocumentTarget target("scorch");
    auto                operation =
        target.makeSet(selection("scorch"), PropertyPath("projection.size"), 2.5, PropertySetMode::Absolute);
    REQUIRE(operation.ok());
    REQUIRE(target.applyDomainOperation(operation.value()).ok());
    CHECK_EQ(*target.value("projection.size")->getIf<double>(), 2.5);
    DomainOperation undo = operation.value();
    undo.payload         = operation.value().inverse;
    REQUIRE(target.applyDomainOperation(undo).ok());
    CHECK_EQ(*target.value("projection.size")->getIf<double>(), 0.5);
    REQUIRE(set(target, "texture.albedo", "textures/scorch.png").ok());
    auto gizmo = DecalGizmoPreviewService().build(target);
    CHECK_EQ(static_cast<int>(gizmo.status), static_cast<int>(EditorStatus::Applied));
    REQUIRE_EQ(gizmo.primitives.size(), static_cast<std::size_t>(2));
    CHECK_EQ(gizmo.primitives[0].kind, std::string("oriented-wire-box"));
    DecalDocumentTarget restored("copy");
    REQUIRE(restored.loadSnapshot(target.snapshotValue()).ok());
    CHECK_EQ(*restored.value("texture.albedo")->getIf<std::string>(), std::string("textures/scorch.png"));
}

TEST_CASE("editor.decal.snapshot_is_atomic_and_rejects_projection_rules") {
    DecalDocumentTarget target("scorch");
    const auto          before    = target.snapshotValue();
    EditorValue         invalid   = before;
    auto*               root      = invalid.getIf<EditorValue::Object>();
    auto*               values    = (*root)["values"].getIf<EditorValue::Object>();
    (*values)["transform.normal"] = EditorValue::Array{0.0, 0.0, 0.0};
    CHECK_EQ(static_cast<int>(target.loadSnapshot(invalid).code()), static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(target.snapshotValue(), before);
    CHECK_EQ(static_cast<int>(target
                                  .makeSet(selection("scorch"), PropertyPath("texture.uvRect"),
                                           EditorValue::Array{0.8, 0.0, 0.4, 1.0}, PropertySetMode::Absolute)
                                  .code()),
             static_cast<int>(EditorStatus::Rejected));
}

TEST_CASE("editor.decal.v1_snapshot_migrates_with_pom_disabled") {
    DecalDocumentTarget original("legacy");
    EditorValue         legacy = original.snapshotValue();
    auto*               root   = legacy.getIf<EditorValue::Object>();
    (*root)["schemaVersion"]   = int64_t{1};
    auto* values               = (*root)["values"].getIf<EditorValue::Object>();
    values->erase("projection.parallaxScale");
    values->erase("projection.parallaxMinLayers");
    values->erase("projection.parallaxMaxLayers");
    DecalDocumentTarget restored("restored");
    REQUIRE(restored.loadSnapshot(legacy).ok());
    CHECK_EQ(*restored.value("projection.parallaxScale")->getIf<double>(), 0.0);
    CHECK_EQ(*restored.value("projection.parallaxMinLayers")->getIf<double>(), 8.0);
    CHECK_EQ(*restored.value("projection.parallaxMaxLayers")->getIf<double>(), 24.0);
    CHECK_EQ(*restored.value("projection.edgeFadeWidth")->getIf<double>(), 0.06);
}

TEST_CASE("editor.decal.v2_snapshot_accepts_world_projection_upgrade") {
    DecalDocumentTarget source("v2");
    EditorValue         snapshot = source.snapshotValue();
    auto*               root     = snapshot.getIf<EditorValue::Object>();
    (*root)["schemaVersion"]     = int64_t{2};
    DecalDocumentTarget restored("restored-v2");
    REQUIRE(restored.loadSnapshot(snapshot).ok());
    CHECK_EQ(*restored.value("projection.edgeFadeWidth")->getIf<double>(), 0.06);
    REQUIRE(set(restored, "projection.mode", "world").ok());
}

TEST_CASE("editor.decal.v2_and_v3_snapshots_fill_missing_pom_fields") {
    for (const int64_t version : {int64_t{2}, int64_t{3}}) {
        DecalDocumentTarget source("legacy-pom");
        EditorValue         snapshot = source.snapshotValue();
        auto*               root     = snapshot.getIf<EditorValue::Object>();
        (*root)["schemaVersion"]     = version;
        auto* values                 = (*root)["values"].getIf<EditorValue::Object>();
        values->erase("projection.parallaxScale");
        values->erase("projection.parallaxMinLayers");
        values->erase("projection.parallaxMaxLayers");
        values->erase("projection.edgeFadeWidth");
        DecalDocumentTarget restored("restored-legacy-pom");
        REQUIRE(restored.loadSnapshot(snapshot).ok());
        CHECK_EQ(*restored.value("projection.parallaxScale")->getIf<double>(), 0.0);
        CHECK_EQ(*restored.value("projection.parallaxMinLayers")->getIf<double>(), 8.0);
        CHECK_EQ(*restored.value("projection.parallaxMaxLayers")->getIf<double>(), 24.0);
        CHECK_EQ(*restored.value("projection.edgeFadeWidth")->getIf<double>(), 0.06);
    }
}

TEST_CASE("editor.decal.runtime_replacement_resolves_assets_before_atomic_generation_swap") {
    auto& manager = eve::decal::DecalManager::inst();
    manager.clearAll();
    manager.setLimit("scorch", 1);
    DecalDocumentTarget target("scorch");
    REQUIRE(set(target, "decal.kind", "scorch").ok());
    REQUIRE(set(target, "texture.albedo", "textures/a.png").ok());
    Assets assets;
    auto*  texture                  = reinterpret_cast<eve::graphics::Texture*>(0x1);
    assets.values["textures/a.png"] = texture;
    DecalRuntimeBinding binding(&manager, &assets);
    REQUIRE(binding.publish(target).ok());
    const int first = binding.runtimeId();
    REQUIRE(first > 0);
    CHECK_EQ(manager.count(), 1);
    REQUIRE(set(target, "texture.albedo", "textures/missing.png").ok());
    CHECK_EQ(static_cast<int>(binding.publish(target).code()), static_cast<int>(EditorStatus::NotFound));
    CHECK_EQ(binding.runtimeId(), first);
    CHECK_EQ(manager.count(), 1);
    CHECK_EQ(manager.instances()[0].id, first);
    REQUIRE(set(target, "texture.albedo", "textures/a.png").ok());
    REQUIRE(set(target, "projection.depth", 0.7).ok());
    REQUIRE(set(target, "projection.mode", "world").ok());
    REQUIRE(set(target, "projection.parallaxScale", 0.05).ok());
    REQUIRE(set(target, "projection.edgeFadeWidth", 0.12).ok());
    REQUIRE(binding.publish(target).ok());
    CHECK(binding.runtimeId() != first);
    CHECK_EQ(manager.count(), 1);
    CHECK_EQ(manager.instances()[0].depth, 0.7f);
    CHECK_EQ(manager.instances()[0].projectionMode, 3);
    CHECK_EQ(manager.instances()[0].parallaxScale, 0.05f);
    CHECK_EQ(manager.instances()[0].edgeFadeWidth, 0.12f);
    REQUIRE(binding.clear().ok());
    CHECK_EQ(manager.count(), 0);
}

TEST_CASE("editor.decal.publishing_target_preserves_author_and_runtime_on_rejection") {
    auto& manager = eve::decal::DecalManager::inst();
    manager.clearAll();
    Assets assets;
    assets.values["textures/a.png"] = reinterpret_cast<eve::graphics::Texture*>(0x1);
    DecalRuntimeBinding   runtime(&manager, &assets);
    DecalPublishingTarget target("live", &runtime);
    auto first = target.authoringTarget().makeSet(selection("live"), PropertyPath("texture.albedo"), "textures/a.png",
                                                  PropertySetMode::Absolute);
    REQUIRE(first.ok());
    REQUIRE(target.applyDomainOperation(first.value()).ok());
    const int generation = runtime.runtimeId();
    REQUIRE(generation > 0);
    auto missing = target.authoringTarget().makeSet(selection("live"), PropertyPath("texture.albedo"),
                                                    "textures/missing.png", PropertySetMode::Absolute);
    REQUIRE(missing.ok());
    CHECK_EQ(static_cast<int>(target.applyDomainOperation(missing.value()).code()),
             static_cast<int>(EditorStatus::NotFound));
    CHECK_EQ(*target.authoringTarget().value("texture.albedo")->getIf<std::string>(), std::string("textures/a.png"));
    CHECK_EQ(runtime.runtimeId(), generation);
    CHECK_EQ(manager.instances()[0].id, generation);
    REQUIRE(runtime.clear().ok());
}

// editor_domain_offscreen.cpp
using namespace eve::editor;
using namespace eve::editing;

namespace {
class Canvas final : public eve::graphics::Canvas {
public:
    Canvas(int width, int height) : width_(width), height_(height) {}
    int                     getWidth() const override { return width_; }
    int                     getHeight() const override { return height_; }
    eve::graphics::Texture* getTexture() override { return nullptr; }
    void clear(std::optional<eve::graphics::Color>, std::optional<int>, std::optional<double>) override {}
    eve::graphics::Color   getPixel(int, int) override { return {}; }
    eve::image::ImageData* newImageData() override { return new eve::image::ImageData(width_, height_); }
    void                   draw(eve::graphics::Graphics*, const glm::mat4&) const override {}
    void                   draw(eve::graphics::Canvas*, const glm::mat4&) const override {}

private:
    int width_, height_;
};
class Backend final : public eve::graphics::ICanvasFactory,
                      public eve::graphics::ICanvasTarget,
                      public eve::graphics::ISolidRectRenderer {
public:
    eve::graphics::Canvas* newCanvas(int width, int height) override {
        canvas = std::make_unique<Canvas>(width, height);
        return canvas.get();
    }
    void                   setCanvas(eve::graphics::Canvas* value) override { current = value; }
    eve::graphics::Canvas* getCanvas() const override { return current; }
    void drawSolidRectRGBA(float, float, float, float, float, float, float, float) override { ++rectangles; }
    std::unique_ptr<Canvas> canvas;
    eve::graphics::Canvas*  current    = nullptr;
    int                     rectangles = 0;
};

GraphDocumentData particleGraph() {
    eve::particles_editing::ParticleGraphDomain domain;
    GraphDocument                               document;
    for (const char* type : {"emission", "motion", "collision", "renderer", "output"}) {
        auto node = domain.makeNode(GraphNodeId(type), type);
        REQUIRE(node.ok());
        CHECK(document.createNode(node.value()).ok());
    }
    int sequence = 0;
    for (const auto& [from, to] : std::vector<std::pair<const char*, const char*>>{{"emission.out", "motion.in"},
                                                                                   {"motion.out", "collision.in"},
                                                                                   {"collision.out", "renderer.in"},
                                                                                   {"renderer.out", "output.in"}}) {
        const auto* output = document.findPin(GraphPinId(from));
        const auto* input  = document.findPin(GraphPinId(to));
        REQUIRE(output);
        REQUIRE(input);
        CHECK(document
                  .connect({StableId("edge-" + std::to_string(sequence++)), output->id, input->id},
                           domain.canConnect(*output, *input))
                  .ok());
    }
    return document.snapshot(domain.domain());
}
}  // namespace

TEST_CASE("editor.material.offscreen_adapter_produces_publishable_artifact") {
    Backend                          backend;
    auto*                            token = reinterpret_cast<eve::graphics::Graphics*>(&backend);
    GraphicsOffscreenPreviewService  offscreen(token, &backend, &backend);
    int                              draws = 0;
    OffscreenMaterialPreviewRenderer renderer(&offscreen,
                                              [&](const MaterialPreviewRenderRequest& request, auto*, auto*) {
                                                  CHECK(request.material.getIf<EditorValue::Object>() != nullptr);
                                                  ++draws;
                                                  return eve::editing::applied<void>();
                                              });
    MaterialDocumentTarget           material("material");
    MaterialPreviewService           service;
    auto                             task = service.render(DocumentId("doc"), material, {}, renderer);
    REQUIRE(task.ok());
    CHECK_EQ(draws, 1);
    CHECK(service.publish(DocumentId("doc"), material.revision(), task.value()).ok());
    CHECK(service.publishedArtifact(DocumentId("doc")).starts_with("preview://"));
}

TEST_CASE("editor.ui.offscreen_renderer_rasterizes_visible_widget_boxes") {
    Backend                         backend;
    auto*                           token = reinterpret_cast<eve::graphics::Graphics*>(&backend);
    GraphicsOffscreenPreviewService offscreen(token, &backend, &backend);
    UiDocumentTarget                document("hud");
    UiLayoutValue                   layout;
    layout.width  = 80;
    layout.height = 40;
    auto create   = document.makeCreate({ObjectId("panel"), {}, "panel", "Panel", layout});
    REQUIRE(create.ok());
    CHECK(document.applyDomainOperation(create.value()).ok());
    UiOffscreenPreviewRenderer renderer(&offscreen, &backend);
    auto                       artifact = renderer.render(document, 320, 200);
    REQUIRE(artifact.ok());
    CHECK_EQ(backend.rectangles, 1);
    CHECK_EQ(artifact.value().sourceRevision, document.revision());
    CHECK(backend.current == nullptr);
}

TEST_CASE("editor.particles.offscreen_preview_compiles_and_budget_checks_before_draw") {
    Backend                         backend;
    auto*                           token = reinterpret_cast<eve::graphics::Graphics*>(&backend);
    GraphicsOffscreenPreviewService offscreen(token, &backend, &backend);
    int                             draws = 0;
    ParticleOffscreenPreviewService renderer(
        &offscreen, [&](const eve::particles_editing::ParticleGraphCompileResult& compiled,
                        const eve::particles_editing::ParticleGraphPreviewResult& estimate, auto*, auto*) {
            CHECK_EQ(compiled.documentRevision, estimate.documentRevision);
            ++draws;
            return eve::editing::applied<void>();
        });
    ParticleOffscreenPreviewRequest request;
    request.previewId = StableId("particles");
    request.graph     = particleGraph();
    auto artifact     = renderer.render(request);
    REQUIRE(artifact.ok());
    CHECK_EQ(draws, 1);
    request.seconds = -1.0;
    CHECK_EQ(static_cast<int>(renderer.render(request).code()), static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(draws, 1);
}

namespace {
class ParticlePresenter final : public IParticleOffscreenPresenter {
public:
    int          draws = 0;
    Result<void> draw(const ParticleOffscreenPreviewRequest&                    request,
                      const eve::particles_editing::ParticleGraphCompileResult& compiled,
                      const eve::particles_editing::ParticleGraphPreviewResult& estimate, eve::graphics::Graphics*,
                      eve::graphics::Canvas*) override {
        CHECK_EQ(request.graph.revision, compiled.documentRevision);
        CHECK_EQ(compiled.documentRevision, estimate.documentRevision);
        ++draws;
        return eve::editing::applied<void>();
    }
};
}  // namespace

TEST_CASE("editor.particles.offscreen_preview_dispatches_isolated_presenter_contract") {
    Backend                         backend;
    auto*                           token = reinterpret_cast<eve::graphics::Graphics*>(&backend);
    GraphicsOffscreenPreviewService offscreen(token, &backend, &backend);
    ParticlePresenter               presenter;
    ParticleOffscreenPreviewService renderer(&offscreen, &presenter);
    ParticleOffscreenPreviewRequest request;
    request.previewId = StableId("isolated-particles");
    request.graph     = particleGraph();
    auto artifact     = renderer.render(request);
    REQUIRE(artifact.ok());
    CHECK_EQ(presenter.draws, 1);
    ParticleEmitterOffscreenPresenter           runtime;
    eve::particles_editing::ParticleGraphDomain domain;
    const auto                                  compiled = domain.compile(request.graph);
    const auto estimate = domain.preview(request.graph, request.seconds, request.fixedStep, request.particleBudget);
    request.graph.revision++;
    CHECK_EQ(static_cast<int>(runtime.draw(request, compiled, estimate, nullptr, nullptr).code()),
             static_cast<int>(EditorStatus::Conflict));
}

namespace {
class SurfaceFluidRenderer final : public ISurfaceFluidPreviewRenderer {
public:
    int          draws = 0;
    Result<void> draw(const SurfaceFluidPreviewSnapshot& snapshot) override {
        CHECK_EQ(static_cast<int>(snapshot.status), static_cast<int>(EditorStatus::Applied));
        ++draws;
        return eve::editing::applied<void>();
    }
};
}  // namespace

TEST_CASE("editor.surface_fluid.offscreen_preview_replays_before_renderer_dispatch") {
    Backend                             backend;
    auto*                               token = reinterpret_cast<eve::graphics::Graphics*>(&backend);
    GraphicsOffscreenPreviewService     offscreen(token, &backend, &backend);
    SurfaceFluidTarget                  target("waterfall");
    SurfaceFluidRenderer                presenter;
    SurfaceFluidOffscreenPreviewService renderer(&offscreen, &presenter);
    SurfaceFluidPreviewRequest          request;
    request.previewId        = StableId("surface-fluid");
    request.documentRevision = target.revision();
    request.seconds          = 0.1;
    request.positions        = {{{0.0, 0.0, 0.0}}, {{1.0, 0.0, 0.0}}, {{0.0, 0.0, 1.0}}};
    request.indices          = {0, 1, 2};
    request.seeds.push_back({});
    auto artifact = renderer.render(target, request);
    REQUIRE(artifact.ok());
    CHECK_EQ(presenter.draws, 1);
    CHECK_EQ(artifact.value().sourceRevision, target.revision());
    request.documentRevision++;
    CHECK_EQ(static_cast<int>(renderer.render(target, request).code()), static_cast<int>(EditorStatus::Conflict));
    CHECK_EQ(presenter.draws, 1);
}

// editor_graph_material_v2.cpp
using namespace eve::material_editing;
using namespace eve::editing;

namespace {

GraphNodeRecord constantNode(const char* id) {
    GraphNodeRecord node;
    node.id         = GraphNodeId(id);
    node.type       = "material.constant";
    node.properties = EditorValue::Object{{"value", EditorValue(1.0)}};
    node.pins.push_back({GraphPinId(std::string(id) + ".out"), node.id, "float", GraphPinDirection::Output});
    return node;
}

GraphNodeRecord outputNode(const char* id) {
    GraphNodeRecord node;
    node.id         = GraphNodeId(id);
    node.type       = "material.output";
    node.properties = EditorValue::Object{};
    node.pins.push_back({GraphPinId(std::string(id) + ".roughness"), node.id, "float", GraphPinDirection::Input});
    return node;
}

}  // namespace

TEST_CASE("editor.v2.graph_core_is_domain_neutral_and_revisioned") {
    GraphDocument       graph;
    MaterialGraphDomain material;
    CHECK(graph.createNode(constantNode("constant")).ok());
    CHECK(graph.createNode(outputNode("output")).ok());
    CHECK_EQ(graph.revision(), static_cast<Revision>(2));

    const GraphPinRecord* from = graph.findPin(GraphPinId("constant.out"));
    const GraphPinRecord* to   = graph.findPin(GraphPinId("output.roughness"));
    CHECK(from != nullptr);
    CHECK(to != nullptr);
    GraphConnectionDecision allowed = material.canConnect(*from, *to);
    CHECK(allowed.allowed);
    CHECK(graph.connect({StableId("edge-1"), from->id, to->id}, allowed).ok());
    CHECK_EQ(graph.revision(), static_cast<Revision>(3));
    CHECK_EQ(graph.snapshot(material.domain()).edges.size(), static_cast<std::size_t>(1));

    GraphConnectionDecision rejected = material.canConnect(*to, *from);
    CHECK(!rejected.allowed);
    CHECK_EQ(static_cast<int>(graph.connect({StableId("edge-2"), to->id, from->id}, rejected).code()),
             static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(graph.revision(), static_cast<Revision>(3));
}

TEST_CASE("editor.v2.material_compile_failure_keeps_last_successful_preview") {
    GraphDocument         graph;
    MaterialGraphDomain   domain;
    MaterialEditorService materials;
    DocumentId            document("document:material");
    CHECK(graph.createNode(outputNode("output")).ok());

    auto firstTask = materials.compile(document, graph.snapshot(domain.domain()), domain);
    CHECK(firstTask.ok());
    auto firstResult = materials.result(firstTask.value());
    CHECK(firstResult.ok());
    CHECK_EQ(static_cast<int>(firstResult.value().status), static_cast<int>(EditorStatus::Applied));
    CHECK(materials.publishPreview(document, graph.revision(), firstTask.value()).ok());
    const std::string firstPreview = materials.previewArtifact(document);
    CHECK(!firstPreview.empty());

    GraphNodeRecord invalid;
    invalid.id         = GraphNodeId("error");
    invalid.type       = "material.error";
    invalid.properties = EditorValue::Object{};
    CHECK(graph.createNode(std::move(invalid)).ok());
    auto failedTask = materials.compile(document, graph.snapshot(domain.domain()), domain);
    CHECK(failedTask.ok());
    CHECK_EQ(static_cast<int>(materials.result(failedTask.value()).value().status),
             static_cast<int>(EditorStatus::Failed));
    CHECK_EQ(static_cast<int>(materials.publishPreview(document, graph.revision(), failedTask.value()).code()),
             static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(materials.previewArtifact(document), firstPreview);
}

TEST_CASE("editor.v2.material_stale_compile_cannot_replace_preview") {
    GraphDocument         graph;
    MaterialGraphDomain   domain;
    MaterialEditorService materials;
    DocumentId            document("document:material-stale");
    CHECK(graph.createNode(outputNode("output")).ok());
    auto task = materials.compile(document, graph.snapshot(domain.domain()), domain);
    CHECK(task.ok());

    CHECK(graph.setNodeProperties(GraphNodeId("output"), EditorValue::Object{{"opacity", EditorValue(0.5)}}).ok());
    auto stale = materials.publishPreview(document, graph.revision(), task.value());
    CHECK_EQ(static_cast<int>(stale.code()), static_cast<int>(EditorStatus::Conflict));
    CHECK(materials.previewArtifact(document).empty());

    auto currentTask = materials.compile(document, graph.snapshot(domain.domain()), domain);
    CHECK(materials.publishPreview(document, graph.revision(), currentTask.value()).ok());
    CHECK(materials.previewArtifact(document).find("r2") != std::string::npos);
}

// editor_hd2d_target.cpp
using namespace eve::hd2d_editing;
using namespace eve::editing;
namespace {
SelectionSnapshot select(const Hd2dDocumentTarget& t) {
    SelectionSnapshot s;
    s.channel = "hd2d";
    s.items.push_back({SelectionDomain::Asset, TargetId(t.targetId()), StableId(t.targetId().value()), "hd2d.asset"});
    return s;
}
void apply(Hd2dDocumentTarget& t, Result<DomainOperation> op) {
    REQUIRE(op.ok());
    REQUIRE(t.applyDomainOperation(op.value()).ok());
}
class MissingTexture final : public IHd2dTextureResolver {
public:
    Result<eve::graphics::Texture*> texture(const std::string&) const override {
        return eve::editing::failed<eve::graphics::Texture*>(EditorStatus::NotFound, RuleId("test.texture"),
                                                             "missing texture");
    }
};
}  // namespace
TEST_CASE("editor.hd2d.sprite_preset_is_reversible_and_scrubs_frames") {
    Hd2dDocumentTarget target("hero");
    const auto         selection = select(target);
    apply(target, target.makeSet(selection, PropertyPath("asset.source"), "hero.png", PropertySetMode::Absolute));
    apply(target, target.makeSet(selection, PropertyPath("sprite.grid"), EditorValue::Array{4.0, 2.0},
                                 PropertySetMode::Absolute));
    apply(target, target.makeSet(selection, PropertyPath("sprite.animation"), EditorValue::Array{2.0, 5.0},
                                 PropertySetMode::Absolute));
    apply(target, target.makeSet(selection, PropertyPath("sprite.fps"), 2.0, PropertySetMode::Absolute));
    Hd2dFramePreviewService preview;
    auto                    p = preview.evaluate(target, 1);
    REQUIRE(p.ok());
    CHECK_EQ(p.value().frame, 4);
    CHECK_EQ(p.value().uv[0], 0.f);
    CHECK_EQ(p.value().uv[1], .5f);
    auto flip = target.makeSet(selection, PropertyPath("sprite.flipX"), true, PropertySetMode::Absolute);
    REQUIRE(flip.ok());
    REQUIRE(target.applyDomainOperation(flip.value()).ok());
    DomainOperation undo = flip.value();
    undo.payload         = flip.value().inverse;
    REQUIRE(target.applyDomainOperation(undo).ok());
    CHECK(!target.value().flipX);
}
TEST_CASE("editor.hd2d.rejects_invalid_frame_ranges_and_loads_atomically") {
    Hd2dDocumentTarget target("hero");
    const auto         selection = select(target);
    CHECK_EQ(static_cast<int>(target
                                  .makeSet(selection, PropertyPath("sprite.animation"), EditorValue::Array{0.0, 2.0},
                                           PropertySetMode::Absolute)
                                  .code()),
             static_cast<int>(EditorStatus::Rejected));
    const auto  before   = target.snapshotValue();
    EditorValue invalid  = before;
    auto*       root     = invalid.getIf<EditorValue::Object>();
    auto*       content  = (*root)["content"].getIf<EditorValue::Object>();
    (*content)["wallUv"] = EditorValue::Array{.8, .8, .2, .2};
    CHECK_EQ(static_cast<int>(target.loadSnapshot(invalid).code()), static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(target.snapshotValue(), before);
}
TEST_CASE("editor.hd2d.runtime_resolves_assets_before_live_creation") {
    Hd2dDocumentTarget target("hero");
    const auto         selection = select(target);
    apply(target, target.makeSet(selection, PropertyPath("asset.source"), "missing.png", PropertySetMode::Absolute));
    Hd2dDocumentRuntime runtime;
    MissingTexture      textures;
    auto*               module   = reinterpret_cast<eve::hd2d::Hd2D*>(std::uintptr_t{1});
    auto*               graphics = reinterpret_cast<eve::graphics::Graphics*>(std::uintptr_t{1});
    CHECK_EQ(static_cast<int>(runtime.publishSprite(target, module, graphics, nullptr, textures).code()),
             static_cast<int>(EditorStatus::NotFound));
    CHECK_EQ(runtime.revision(), Revision(0));
}

// editor_lighting_target.cpp
using namespace eve::lighting_editing;
using namespace eve::editing;

namespace {

SelectionSnapshot selection(const LightingPropertyTargetBase& target) {
    SelectionSnapshot result;
    result.items.push_back(
        {SelectionDomain::Scene, TargetId(target.targetId()), StableId("lighting"), target.describe().type});
    return result;
}

void set(LightingPropertyTargetBase& target, const char* path, EditorValue value) {
    auto operation = target.makeSet(selection(target), PropertyPath(path), value, PropertySetMode::Absolute);
    REQUIRE(operation.ok());
    REQUIRE(target.applyDomainOperation(operation.value()).ok());
}

}  // namespace

TEST_CASE("editor.lighting.light3d_properties_validate_snapshot_and_apply_runtime") {
    Light3DDocumentTarget document("key-light");
    set(document, "light.type", "dir");
    set(document, "transform.direction", EditorValue::Array{0.0, -1.0, 0.0});
    set(document, "light.color", EditorValue::Array{1.0, 0.8, 0.6, 1.0});
    set(document, "light.intensity", 4.0);
    set(document, "shadow.cast", true);
    set(document, "shadow.method", "csm");
    set(document, "shadow.strength", 0.75);
    CHECK(document.validate().empty());

    eve::graphics::Light3D* light = eve::graphics::Light3D::createLight();
    REQUIRE(light);
    Light3DRuntimeApplier applier;
    REQUIRE(applier.apply(document, light).ok());
    CHECK_EQ(light->getType(), "dir");
    CHECK_EQ(light->getDirY(), -1.f);
    CHECK_EQ(light->data()->intensity, 4.f);
    CHECK(light->getCastShadow());
    CHECK_EQ(light->getShadowMethod(), "csm");
    CHECK_EQ(light->getShadowStrength(), 0.75f);

    const EditorValue     snapshot = document.snapshotValue();
    Light3DDocumentTarget restored("restored-light");
    REQUIRE(restored.loadSnapshot(snapshot).ok());
    CHECK_EQ(restored.snapshotValue(), snapshot);
}

TEST_CASE("editor.lighting.light3d_spot_applies_cone_and_perspective_shadow") {
    Light3DDocumentTarget document("spot-light");
    set(document, "light.type", "spot");
    set(document, "transform.position", EditorValue::Array{1.0, 3.0, -2.0});
    set(document, "transform.direction", EditorValue::Array{0.0, -1.0, 0.2});
    set(document, "light.intensity", 5.0);
    set(document, "light.spot-angle", 28.0);
    set(document, "light.spot-softness", 0.4);
    set(document, "shadow.cast", true);
    set(document, "shadow.method", "perspective");
    CHECK(document.validate().empty());

    eve::graphics::Light3D* light = eve::graphics::Light3D::createLight();
    REQUIRE(light);
    REQUIRE(Light3DRuntimeApplier{}.apply(document, light).ok());
    CHECK_EQ(light->getType(), "spot");
    CHECK_EQ(light->getSpotAngle(), 28.f);
    CHECK_EQ(light->getSpotSoftness(), 0.4f);
    CHECK_EQ(light->getShadowMethod(), "perspective");
    CHECK(light->getCastShadow());
}

TEST_CASE("editor.lighting.light3d_point_shadow_warns_when_cube_reserved") {
    Light3DDocumentTarget document("point-light");
    set(document, "light.type", "point");
    set(document, "shadow.cast", true);
    set(document, "shadow.method", "auto");
    const auto diagnostics = document.validate();
    REQUIRE(!diagnostics.empty());
    CHECK_EQ(diagnosticRule(diagnostics.front()).value(), "editor.light.point-shadow-reserved");
}

TEST_CASE("editor.lighting.light3d_rejects_zero_direction_before_runtime_mutation") {
    Light3DDocumentTarget document("invalid-light");
    set(document, "transform.direction", EditorValue::Array{0.0, 0.0, 0.0});
    REQUIRE(!document.validate().empty());
    eve::graphics::Light3D* light = eve::graphics::Light3D::createLight();
    REQUIRE(light);
    light->setPosition(9.f, 8.f, 7.f);
    const auto rejected = Light3DRuntimeApplier{}.apply(document, light);
    CHECK_EQ(static_cast<int>(rejected.code()), static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(light->getX(), 9.f);
}

TEST_CASE("editor.lighting.environment_applies_daynight_and_weather_modes") {
    EnvironmentDocumentTarget daynightDocument("sky");
    set(daynightDocument, "environment.mode", "daynight");
    set(daynightDocument, "daynight.time", 18.5);
    set(daynightDocument, "daynight.speed", 2.0);
    set(daynightDocument, "daynight.turbidity", 6.0);
    set(daynightDocument, "environment.exposure", 1.8);
    eve::daynight::DayNight   daynight;
    EnvironmentRuntimeApplier applier;
    REQUIRE(applier.applyDayNight(daynightDocument, &daynight).ok());
    CHECK_EQ(daynight.getTimeOfDay(), 18.5f);
    CHECK_EQ(daynight.getSpeed(), 2.f);
    CHECK_EQ(daynight.getTurbidity(), 6.f);
    CHECK_EQ(daynight.getSkyExposure(), 1.8f);

    EnvironmentDocumentTarget weatherDocument("weather");
    set(weatherDocument, "environment.mode", "weather");
    set(weatherDocument, "weather.preset", "storm");
    set(weatherDocument, "weather.intensity", 0.9);
    set(weatherDocument, "weather.wind-speed", 20.0);
    set(weatherDocument, "weather.lightning", true);
    eve::weather::Weather weather;
    REQUIRE(applier.applyWeather(weatherDocument, &weather).ok());
    CHECK_EQ(weather.getPreset(), "storm");
    CHECK_EQ(weather.getIntensity(), 0.9f);
    CHECK_EQ(weather.getWindSpeed(), 20.f);
    CHECK(weather.isLightningEnabled());
    CHECK_EQ(static_cast<int>(applier.applyDayNight(weatherDocument, &daynight).code()),
             static_cast<int>(EditorStatus::Rejected));
}

TEST_CASE("editor.lighting.environment_detects_multiple_environment_owners") {
    EnvironmentDocumentTarget document("owners");
    set(document, "environment.mode", "daynight");
    set(document, "weather.environment-enabled", true);
    const auto diagnostics = document.validate();
    REQUIRE_EQ(diagnostics.size(), size_t{1});
    CHECK_EQ(diagnosticRule(diagnostics.front()).value(), "editor.environment.multiple-owners");
}
