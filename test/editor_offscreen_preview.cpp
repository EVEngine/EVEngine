#include <memory>
#include <stdexcept>
#include "graphics/Canvas.h"
#include "graphics/ICanvasFactory.h"
#include "graphics/ICanvasTarget.h"
#include "graphics/editing/OffscreenPreview.h"
#include "image/ImageData.h"
#include "spritestack/editing/SpriteStackTarget.h"
#include "virtualgeometry/editing/VirtualGeometryTarget.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// editor_offscreen_preview.cpp
using namespace eve::graphics_editing;
using namespace eve::editing;

namespace {
class FakeCanvas final : public eve::graphics::Canvas {
public:
    FakeCanvas(int width, int height) : width_(width), height_(height) {}
    int                     getWidth() const override { return width_; }
    int                     getHeight() const override { return height_; }
    eve::graphics::Texture* getTexture() override { return nullptr; }
    void clear(std::optional<eve::graphics::Color>, std::optional<int>, std::optional<double>) override {}
    eve::graphics::Color   getPixel(int, int) override { return {}; }
    eve::image::ImageData* newImageData() override { return new eve::image::ImageData(width_, height_); }
    void                   draw(eve::graphics::Graphics*, const glm::mat4&) const override {}
    void                   draw(eve::graphics::Canvas*, const glm::mat4&) const override {}

private:
    int width_  = 0;
    int height_ = 0;
};

class FakeCanvasFactory final : public eve::graphics::ICanvasFactory {
public:
    eve::graphics::Canvas* newCanvas(int width, int height) override {
        canvas = std::make_unique<FakeCanvas>(width, height);
        return canvas.get();
    }
    std::unique_ptr<FakeCanvas> canvas;
};
class FakeCanvasTarget final : public eve::graphics::ICanvasTarget {
public:
    void setCanvas(eve::graphics::Canvas* value) override {
        current = value;
        ++bindings;
    }
    eve::graphics::Canvas* getCanvas() const override { return current; }
    eve::graphics::Canvas* current  = nullptr;
    int                    bindings = 0;
};
}  // namespace

TEST_CASE("editor.preview.offscreen_artifacts_are_bounded_revision_safe_and_releasable") {
    FakeCanvasFactory               factory;
    FakeCanvasTarget                targets;
    auto*                           graphicsToken = reinterpret_cast<eve::graphics::Graphics*>(&factory);
    GraphicsOffscreenPreviewService service(graphicsToken, &factory, &targets);
    int                             draws    = 0;
    auto                            rendered = service.render({StableId("material"), 5, 64, 32, {0.0, 0.0, 0.0, 1.0}},
                                                              [&](eve::graphics::Graphics* graphics, eve::graphics::Canvas* canvas) {
                                       REQUIRE(graphics);
                                       REQUIRE(canvas);
                                       ++draws;
                                       return eve::editing::applied<void>();
                                   });
    REQUIRE(rendered.ok());
    CHECK_EQ(draws, 1);
    CHECK_EQ(rendered.value().width, 64);
    CHECK_EQ(targets.bindings, 2);
    CHECK(targets.current == nullptr);
    auto pixels = service.image(rendered.value().handle, 5);
    REQUIRE(pixels.ok());
    REQUIRE(pixels.value());
    CHECK_EQ(pixels.value()->getWidth(), 64);
    CHECK_EQ(static_cast<int>(service.image(rendered.value().handle, 6).code()),
             static_cast<int>(EditorStatus::Conflict));
    CHECK(service.release(rendered.value().handle).ok());
    CHECK_EQ(static_cast<int>(service.image(rendered.value().handle, 5).code()),
             static_cast<int>(EditorStatus::NotFound));
    CHECK_EQ(
        static_cast<int>(
            service.render({StableId("huge"), 1, 5000, 1}, [](auto*, auto*) { return eve::editing::applied<void>(); })
                .code()),
        static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(static_cast<int>(service
                                  .render({StableId("throw"), 2, 32, 32},
                                          [](auto*, auto*) -> Result<void> { throw std::runtime_error("draw failed"); })
                                  .code()),
             static_cast<int>(EditorStatus::Failed));
    CHECK(targets.current == nullptr);
    CHECK_EQ(targets.bindings, 4);
}

// editor_spritestack_target.cpp
using namespace eve::spritestack_editing;
using namespace eve::editing;

namespace {
SelectionSnapshot select(const SpriteStackDocumentTarget& target) {
    SelectionSnapshot s;
    s.channel = "spritestack";
    s.items.push_back({SelectionDomain::Asset, TargetId(target.targetId()), StableId(target.targetId().value()),
                       "spritestack.asset"});
    return s;
}
void apply(SpriteStackDocumentTarget& target, Result<DomainOperation> operation) {
    REQUIRE(operation.ok());
    REQUIRE(target.applyDomainOperation(operation.value()).ok());
}
class MissingModelResolver final : public ISpriteStackModelResolver {
public:
    Result<eve::model3d::ModelData*> resolveModel(const std::string&) const override {
        return eve::editing::failed<eve::model3d::ModelData*>(EditorStatus::NotFound, RuleId("test.model"),
                                                              "missing model");
    }
};
}  // namespace

TEST_CASE("editor.spritestack.preset_is_reversible_and_atomic") {
    SpriteStackDocumentTarget target("stack");
    const auto                selection = select(target);
    auto layers = target.makeSet(selection, PropertyPath("bake.layers"), int64_t{8}, PropertySetMode::Absolute);
    REQUIRE(layers.ok());
    REQUIRE(target.applyDomainOperation(layers.value()).ok());
    CHECK_EQ(target.value().bake.layerCount, 8);
    DomainOperation undo = layers.value();
    undo.payload         = layers.value().inverse;
    REQUIRE(target.applyDomainOperation(undo).ok());
    CHECK_EQ(target.value().bake.layerCount, 16);
    const auto  before  = target.snapshotValue();
    EditorValue invalid = before;
    auto*       root    = invalid.getIf<EditorValue::Object>();
    auto*       content = (*root)["content"].getIf<EditorValue::Object>();
    (*content)["width"] = int64_t{100000};
    CHECK_EQ(static_cast<int>(target.loadSnapshot(invalid).code()), static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(target.snapshotValue(), before);
}

TEST_CASE("editor.spritestack.bake_is_deterministic_and_reports_layer_artifacts") {
    SpriteStackDocumentTarget target("stack");
    const auto                selection = select(target);
    apply(target, target.makeSet(selection, PropertyPath("bake.layers"), int64_t{6}, PropertySetMode::Absolute));
    apply(target, target.makeSet(selection, PropertyPath("bake.width"), int64_t{32}, PropertySetMode::Absolute));
    apply(target, target.makeSet(selection, PropertyPath("bake.height"), int64_t{24}, PropertySetMode::Absolute));
    SpriteStackBakeRuntime runtime;
    auto                   first = runtime.bake(target);
    REQUIRE(first.ok());
    CHECK_EQ(first.value().size(), static_cast<std::size_t>(6));
    for (const auto& layer : first.value()) {
        CHECK_EQ(layer.width, 32);
        CHECK_EQ(layer.height, 24);
        CHECK(layer.checksum != 0);
        CHECK(layer.alphaCoverage > 0);
    }
    const auto artifacts = first.value();
    auto       second    = runtime.bake(target);
    REQUIRE(second.ok());
    for (std::size_t i = 0; i < artifacts.size(); ++i) CHECK_EQ(second.value()[i].checksum, artifacts[i].checksum);
}

TEST_CASE("editor.spritestack.failed_rebake_preserves_generation_and_stale_publish_fails_first") {
    SpriteStackDocumentTarget target("stack");
    SpriteStackBakeRuntime    runtime;
    auto                      baked = runtime.bake(target);
    REQUIRE(baked.ok());
    const auto revision  = runtime.revision();
    const auto checksum  = baked.value().front().checksum;
    const auto selection = select(target);
    apply(target, target.makeSet(selection, PropertyPath("source.kind"), "model", PropertySetMode::Absolute));
    apply(target, target.makeSet(selection, PropertyPath("source.value"), "missing.glb", PropertySetMode::Absolute));
    MissingModelResolver resolver;
    CHECK_EQ(static_cast<int>(runtime.bake(target, &resolver).code()), static_cast<int>(EditorStatus::NotFound));
    CHECK_EQ(runtime.revision(), revision);
    CHECK_EQ(runtime.layers().front()->getWidth(), 128);
    CHECK(checksum != 0);
    CHECK_EQ(static_cast<int>(runtime.publish(nullptr, target.revision()).code()),
             static_cast<int>(EditorStatus::Conflict));
}

// editor_virtualgeometry_target.cpp
using namespace eve::virtualgeometry_editing;
using namespace eve::editing;

namespace {
SelectionSnapshot select(const VirtualGeometryDocumentTarget& target) {
    SelectionSnapshot s;
    s.channel = "virtualgeometry";
    s.items.push_back({SelectionDomain::Asset, TargetId(target.targetId()), StableId(target.targetId().value()),
                       "virtualgeometry.import"});
    return s;
}
void apply(VirtualGeometryDocumentTarget& target, Result<DomainOperation> operation) {
    REQUIRE(operation.ok());
    REQUIRE(target.applyDomainOperation(operation.value()).ok());
}
class GridResolver final : public IVirtualGeometryMeshResolver {
public:
    Result<VirtualGeometryMeshData> resolve(const std::string& id) const override {
        if (id != "grid")
            return eve::editing::failed<VirtualGeometryMeshData>(EditorStatus::NotFound, RuleId("test.mesh"),
                                                                 "missing mesh");
        VirtualGeometryMeshData mesh;
        constexpr int           size = 20;
        for (int y = 0; y <= size; ++y)
            for (int x = 0; x <= size; ++x)
                mesh.positions.insert(mesh.positions.end(), {static_cast<float>(x), 0.f, static_cast<float>(y)});
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x) {
                const std::uint32_t a = y * (size + 1) + x, b = a + 1, c = a + size + 1, d = c + 1;
                mesh.indices.insert(mesh.indices.end(), {a, c, b, b, c, d});
            }
        return eve::editing::applied<VirtualGeometryMeshData>(std::move(mesh));
    }
};
}  // namespace

TEST_CASE("editor.virtualgeometry.import_preset_is_reversible_and_atomic") {
    VirtualGeometryDocumentTarget target("vg");
    const auto                    selection = select(target);
    auto edit = target.makeSet(selection, PropertyPath("cluster.maxTriangles"), int64_t{64}, PropertySetMode::Absolute);
    REQUIRE(edit.ok());
    REQUIRE(target.applyDomainOperation(edit.value()).ok());
    CHECK_EQ(target.value().builder.maxTrianglesPerCluster, 64);
    DomainOperation undo = edit.value();
    undo.payload         = edit.value().inverse;
    REQUIRE(target.applyDomainOperation(undo).ok());
    CHECK_EQ(target.value().builder.maxTrianglesPerCluster, 124);
    const auto  before        = target.snapshotValue();
    EditorValue invalid       = before;
    auto*       root          = invalid.getIf<EditorValue::Object>();
    auto*       content       = (*root)["content"].getIf<EditorValue::Object>();
    (*content)["mergeFactor"] = int64_t{20};
    CHECK_EQ(static_cast<int>(target.loadSnapshot(invalid).code()), static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(target.snapshotValue(), before);
}

TEST_CASE("editor.virtualgeometry.build_is_deterministic_and_emits_lod_cost_curve") {
    VirtualGeometryDocumentTarget target("vg");
    const auto                    selection = select(target);
    apply(target, target.makeSet(selection, PropertyPath("source.asset"), "grid", PropertySetMode::Absolute));
    apply(target, target.makeSet(selection, PropertyPath("preview.samples"), int64_t{12}, PropertySetMode::Absolute));
    GridResolver                resolver;
    VirtualGeometryBuildRuntime runtime;
    auto                        first = runtime.build(target, resolver);
    REQUIRE(first.ok());
    CHECK(first.value().clusters > 0);
    CHECK(first.value().maxLod > 0);
    CHECK(first.value().roots > 0);
    CHECK_EQ(first.value().lodCurve.size(), static_cast<std::size_t>(12));
    CHECK(first.value().lodCurve.front().distance < first.value().lodCurve.back().distance);
    CHECK(first.value().lodCurve.front().triangles >= first.value().lodCurve.back().triangles);
    const auto checksum = first.value().checksum;
    auto       second   = runtime.build(target, resolver);
    REQUIRE(second.ok());
    CHECK_EQ(second.value().checksum, checksum);
}

TEST_CASE("editor.virtualgeometry.failed_rebuild_preserves_previous_generation") {
    VirtualGeometryDocumentTarget target("vg");
    const auto                    selection = select(target);
    apply(target, target.makeSet(selection, PropertyPath("source.asset"), "grid", PropertySetMode::Absolute));
    GridResolver                resolver;
    VirtualGeometryBuildRuntime runtime;
    REQUIRE(runtime.build(target, resolver).ok());
    const auto* asset    = runtime.asset();
    const auto  revision = runtime.revision();
    apply(target, target.makeSet(selection, PropertyPath("source.asset"), "missing", PropertySetMode::Absolute));
    CHECK_EQ(static_cast<int>(runtime.build(target, resolver).code()), static_cast<int>(EditorStatus::NotFound));
    CHECK(runtime.asset() == asset);
    CHECK_EQ(runtime.revision(), revision);
}
