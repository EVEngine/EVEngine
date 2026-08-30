#include "editor/EditorMaterialOffscreen.h"
#include "editor/EditorParticleOffscreen.h"
#include "editor/EditorUiOffscreen.h"
#include "editor/EditorSurfaceFluidPreview.h"

#include "graphics/Canvas.h"
#include "graphics/ICanvasFactory.h"
#include "graphics/ICanvasTarget.h"
#include "graphics/ISolidRectRenderer.h"
#include "image/ImageData.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <memory>

using namespace eve::editor;

namespace {
class Canvas final : public eve::graphics::Canvas {
public:
    Canvas(int width, int height) : width_(width), height_(height) {}
    int getWidth() const override { return width_; } int getHeight() const override { return height_; }
    eve::graphics::Texture* getTexture() override { return nullptr; }
    void clear(std::optional<eve::graphics::Color>, std::optional<int>, std::optional<double>) override {}
    eve::graphics::Color getPixel(int, int) override { return {}; }
    eve::image::ImageData* newImageData() override { return new eve::image::ImageData(width_, height_); }
    void draw(eve::graphics::Graphics*, const glm::mat4&) const override {}
    void draw(eve::graphics::Canvas*, const glm::mat4&) const override {}
private: int width_, height_;
};
class Backend final : public eve::graphics::ICanvasFactory,
                      public eve::graphics::ICanvasTarget,
                      public eve::graphics::ISolidRectRenderer {
public:
    eve::graphics::Canvas* newCanvas(int width, int height) override { canvas=std::make_unique<Canvas>(width,height);return canvas.get(); }
    void setCanvas(eve::graphics::Canvas* value) override { current=value; }
    eve::graphics::Canvas* getCanvas() const override { return current; }
    void drawSolidRectRGBA(float,float,float,float,float,float,float,float) override { ++rectangles; }
    std::unique_ptr<Canvas> canvas; eve::graphics::Canvas* current=nullptr; int rectangles=0;
};

GraphDocumentData particleGraph() {
    ParticleGraphDomain domain;
    GraphDocument document;
    for (const char* type : {"emission", "motion", "collision", "renderer", "output"}) {
        auto node = domain.makeNode(GraphNodeId(type), type); REQUIRE(node.value);
        CHECK(document.createNode(*node.value).isAccepted());
    }
    int sequence = 0;
    for (const auto& [from, to] : std::vector<std::pair<const char*, const char*>>{
             {"emission.out","motion.in"},{"motion.out","collision.in"},
             {"collision.out","renderer.in"},{"renderer.out","output.in"}}) {
        const auto* output = document.findPin(GraphPinId(from)); const auto* input = document.findPin(GraphPinId(to));
        REQUIRE(output); REQUIRE(input);
        CHECK(document.connect({StableId("edge-" + std::to_string(sequence++)), output->id, input->id},
                               domain.canConnect(*output, *input)).isAccepted());
    }
    return document.snapshot(domain.domain());
}
}

TEST_CASE("editor.material.offscreen_adapter_produces_publishable_artifact") {
    Backend backend; auto* token=reinterpret_cast<eve::graphics::Graphics*>(&backend);
    GraphicsOffscreenPreviewService offscreen(token,&backend,&backend);
    int draws=0;
    OffscreenMaterialPreviewRenderer renderer(&offscreen,
        [&](const MaterialPreviewRenderRequest& request,auto*,auto*) { CHECK(request.material.getIf<EditorValue::Object>() != nullptr);++draws;return EditorResult<void>::applied(); });
    MaterialDocumentTarget material("material"); MaterialPreviewService service;
    auto task=service.render(DocumentId("doc"),material,{},renderer); REQUIRE(task.value);
    CHECK_EQ(draws,1); CHECK(service.publish(DocumentId("doc"),material.revision(),*task.value).isAccepted());
    CHECK(service.publishedArtifact(DocumentId("doc")).starts_with("preview://"));
}

TEST_CASE("editor.ui.offscreen_renderer_rasterizes_visible_widget_boxes") {
    Backend backend; auto* token=reinterpret_cast<eve::graphics::Graphics*>(&backend);
    GraphicsOffscreenPreviewService offscreen(token,&backend,&backend);
    UiDocumentTarget document("hud"); UiLayoutValue layout;layout.width=80;layout.height=40;
    auto create=document.makeCreate({ObjectId("panel"),{},"panel","Panel",layout});REQUIRE(create.value);
    CHECK(document.applyDomainOperation(*create.value).isAccepted());
    UiOffscreenPreviewRenderer renderer(&offscreen,&backend);
    auto artifact=renderer.render(document,320,200);REQUIRE(artifact.value);
    CHECK_EQ(backend.rectangles,1);CHECK_EQ(artifact.value->sourceRevision,document.revision());
    CHECK(backend.current==nullptr);
}

TEST_CASE("editor.particles.offscreen_preview_compiles_and_budget_checks_before_draw") {
    Backend backend; auto* token=reinterpret_cast<eve::graphics::Graphics*>(&backend);
    GraphicsOffscreenPreviewService offscreen(token,&backend,&backend); int draws=0;
    ParticleOffscreenPreviewService renderer(&offscreen,
        [&](const ParticleGraphCompileResult& compiled,const ParticleGraphPreviewResult& estimate,auto*,auto*) {
            CHECK_EQ(compiled.documentRevision,estimate.documentRevision);++draws;return EditorResult<void>::applied(); });
    ParticleOffscreenPreviewRequest request;request.previewId=StableId("particles");request.graph=particleGraph();
    auto artifact=renderer.render(request);REQUIRE(artifact.value);CHECK_EQ(draws,1);
    request.seconds=-1.0;
    CHECK_EQ(static_cast<int>(renderer.render(request).status),static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(draws,1);
}

namespace {
class ParticlePresenter final : public IParticleOffscreenPresenter {
public:
    int draws = 0;
    EditorResult<void> draw(const ParticleOffscreenPreviewRequest& request,
                            const ParticleGraphCompileResult& compiled,
                            const ParticleGraphPreviewResult& estimate,
                            eve::graphics::Graphics*, eve::graphics::Canvas*) override {
        CHECK_EQ(request.graph.revision, compiled.documentRevision);
        CHECK_EQ(compiled.documentRevision, estimate.documentRevision);
        ++draws; return EditorResult<void>::applied();
    }
};
}

TEST_CASE("editor.particles.offscreen_preview_dispatches_isolated_presenter_contract") {
    Backend backend; auto* token=reinterpret_cast<eve::graphics::Graphics*>(&backend);
    GraphicsOffscreenPreviewService offscreen(token,&backend,&backend);
    ParticlePresenter presenter; ParticleOffscreenPreviewService renderer(&offscreen,&presenter);
    ParticleOffscreenPreviewRequest request; request.previewId=StableId("isolated-particles");
    request.graph=particleGraph();
    auto artifact=renderer.render(request); REQUIRE(artifact.value);
    CHECK_EQ(presenter.draws,1);
    ParticleEmitterOffscreenPresenter runtime;
    ParticleGraphDomain domain; const auto compiled=domain.compile(request.graph);
    const auto estimate=domain.preview(request.graph,request.seconds,request.fixedStep,request.particleBudget);
    request.graph.revision++;
    CHECK_EQ(static_cast<int>(runtime.draw(request,compiled,estimate,nullptr,nullptr).status),
             static_cast<int>(EditorStatus::Conflict));
}

namespace {
class SurfaceFluidRenderer final : public ISurfaceFluidPreviewRenderer {
public:
    int draws = 0;
    EditorResult<void> draw(const SurfaceFluidPreviewSnapshot& snapshot) override {
        CHECK_EQ(static_cast<int>(snapshot.status), static_cast<int>(EditorStatus::Applied));
        ++draws; return EditorResult<void>::applied();
    }
};
}

TEST_CASE("editor.surface_fluid.offscreen_preview_replays_before_renderer_dispatch") {
    Backend backend; auto* token=reinterpret_cast<eve::graphics::Graphics*>(&backend);
    GraphicsOffscreenPreviewService offscreen(token,&backend,&backend);
    SurfaceFluidTarget target("waterfall"); SurfaceFluidRenderer presenter;
    SurfaceFluidOffscreenPreviewService renderer(&offscreen,&presenter);
    SurfaceFluidPreviewRequest request; request.previewId=StableId("surface-fluid");
    request.documentRevision=target.revision(); request.seconds=0.1;
    request.positions={{{0.0,0.0,0.0}},{{1.0,0.0,0.0}},{{0.0,0.0,1.0}}};
    request.indices={0,1,2}; request.seeds.push_back({});
    auto artifact=renderer.render(target,request); REQUIRE(artifact.value);
    CHECK_EQ(presenter.draws,1); CHECK_EQ(artifact.value->sourceRevision,target.revision());
    request.documentRevision++;
    CHECK_EQ(static_cast<int>(renderer.render(target,request).status),
             static_cast<int>(EditorStatus::Conflict));
    CHECK_EQ(presenter.draws,1);
}
