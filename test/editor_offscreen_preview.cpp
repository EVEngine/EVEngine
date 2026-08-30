#include "editor/EditorOffscreenPreview.h"

#include "graphics/Canvas.h"
#include "graphics/ICanvasFactory.h"
#include "graphics/ICanvasTarget.h"
#include "image/ImageData.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <memory>
#include <stdexcept>

using namespace eve::editor;

namespace {
class FakeCanvas final : public eve::graphics::Canvas {
public:
    FakeCanvas(int width, int height) : width_(width), height_(height) {}
    int getWidth() const override { return width_; }
    int getHeight() const override { return height_; }
    eve::graphics::Texture* getTexture() override { return nullptr; }
    void clear(std::optional<eve::graphics::Color>, std::optional<int>, std::optional<double>) override {}
    eve::graphics::Color getPixel(int, int) override { return {}; }
    eve::image::ImageData* newImageData() override { return new eve::image::ImageData(width_, height_); }
    void draw(eve::graphics::Graphics*, const glm::mat4&) const override {}
    void draw(eve::graphics::Canvas*, const glm::mat4&) const override {}
private:
    int width_ = 0;
    int height_ = 0;
};

class FakeCanvasFactory final : public eve::graphics::ICanvasFactory {
public:
    eve::graphics::Canvas* newCanvas(int width, int height) override {
        canvas = std::make_unique<FakeCanvas>(width, height); return canvas.get();
    }
    std::unique_ptr<FakeCanvas> canvas;
};
class FakeCanvasTarget final : public eve::graphics::ICanvasTarget {
public:
    void setCanvas(eve::graphics::Canvas* value) override { current = value; ++bindings; }
    eve::graphics::Canvas* getCanvas() const override { return current; }
    eve::graphics::Canvas* current = nullptr;
    int bindings = 0;
};
}

TEST_CASE("editor.preview.offscreen_artifacts_are_bounded_revision_safe_and_releasable") {
    FakeCanvasFactory factory;
    FakeCanvasTarget targets;
    auto* graphicsToken = reinterpret_cast<eve::graphics::Graphics*>(&factory);
    GraphicsOffscreenPreviewService service(graphicsToken, &factory, &targets);
    int draws = 0;
    auto rendered = service.render({StableId("material"), 5, 64, 32, {0.0, 0.0, 0.0, 1.0}},
        [&](eve::graphics::Graphics* graphics, eve::graphics::Canvas* canvas) {
            REQUIRE(graphics); REQUIRE(canvas); ++draws; return EditorResult<void>::applied();
        });
    REQUIRE(rendered.value); CHECK_EQ(draws, 1); CHECK_EQ(rendered.value->width, 64);
    CHECK_EQ(targets.bindings, 2); CHECK(targets.current == nullptr);
    auto pixels = service.image(rendered.value->handle, 5); REQUIRE(pixels.value); REQUIRE(*pixels.value);
    CHECK_EQ((*pixels.value)->getWidth(), 64);
    CHECK_EQ(static_cast<int>(service.image(rendered.value->handle, 6).status), static_cast<int>(EditorStatus::Conflict));
    CHECK(service.release(rendered.value->handle).isAccepted());
    CHECK_EQ(static_cast<int>(service.image(rendered.value->handle, 5).status), static_cast<int>(EditorStatus::NotFound));
    CHECK_EQ(static_cast<int>(service.render({StableId("huge"), 1, 5000, 1},
        [](auto*, auto*) { return EditorResult<void>::applied(); }).status), static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(static_cast<int>(service.render({StableId("throw"), 2, 32, 32},
        [](auto*, auto*) -> EditorResult<void> { throw std::runtime_error("draw failed"); }).status),
        static_cast<int>(EditorStatus::Failed));
    CHECK(targets.current == nullptr); CHECK_EQ(targets.bindings, 4);
}
