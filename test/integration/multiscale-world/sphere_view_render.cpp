// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "default": "render-window",
  "module": "graphics_multiscale"
}
*/
// clang-format on
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include "graphics/Graphics.h"
#include "graphics/multiscale/AngularView.h"
#include "graphics/multiscale/SphereView.h"
#include "image/ImageData.h"
#include "spatial/multiscale/CellPosition.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

namespace {
void captureSphereView(eve::graphics::Graphics& graphics, const char* name) {
    const char* directory = std::getenv("EVE_MULTISCALE_CAPTURE_DIR");
    if (!directory || !*directory) return;
    const auto path = std::filesystem::path(directory) / name;
    std::filesystem::create_directories(path.parent_path());
    std::unique_ptr<eve::image::ImageData> frame(graphics.newImageData());
    REQUIRE(frame != nullptr);
    std::ofstream out(path, std::ios::binary);
    REQUIRE(out.good());
    out << "P6\n" << frame->getWidth() << ' ' << frame->getHeight() << "\n255\n";
    const auto* bytes = static_cast<const char*>(frame->getData());
    for (int y = 0; y < frame->getHeight(); ++y)
        for (int x = 0; x < frame->getWidth(); ++x)
            out.write(bytes + (static_cast<std::size_t>(y) * frame->getWidth() + x) * 4, 3);
    out.close();
    REQUIRE(out.good());
}
}  // namespace
TEST_CASE("graphics.multiscale.nativeSphereDepthIsIndependentOfDrawOrderAcrossScales") {
    using namespace eve::graphics::multiscale;
    using eve::spatial::CellPosition;
    auto*                       window   = eve::window::Window::create();
    auto*                       graphics = eve::graphics::Graphics::create();
    eve::window::WindowSettings settings;
    settings.width  = 320;
    settings.height = 180;
    REQUIRE(window->setWindowSettings(settings));
    graphics->setScreenReadbackEnabled(true);
    graphics->setBackgroundColor({0.02f, 0.03f, 0.05f, 1.f});
    auto renderer = SphereView::build(*graphics);
    REQUIRE(renderer.ok());
    std::array<CellPosition, 2> centers{};
    centers[0].cell[2] = 10;
    centers[1].cell[2] = 1000000000000000000LL;
    std::array<SphereAppearance, 2> appearance{{{2, 900, {0.8f, 0.1f, 0.1f}}, {2e17, 900, {0.1f, 0.1f, 0.8f}}}};
    AngularView                     view;
    view.cellExponent = 900;
    view.aspect       = 320.0 / 180.0;
    auto first        = SphereFrame::build(centers, appearance, {}, view, 2);
    REQUIRE(first.ok());
    for (int i = 0; i < 3; ++i) REQUIRE(renderer.value()->render(first.value()).ok());
    const auto nearColor = graphics->getPixel(160, 90);
    REQUIRE(nearColor.r > 0.7f);
    REQUIRE(nearColor.r > nearColor.b + 0.3f);
    REQUIRE(graphics->getPixel(20, 20).r < nearColor.r * 0.5f);
    captureSphereView(*graphics, "sphere-near-occludes-far.ppm");
    std::swap(centers[0], centers[1]);
    std::swap(appearance[0], appearance[1]);
    auto reverse = SphereFrame::build(centers, appearance, {}, view, 2);
    REQUIRE(reverse.ok());
    REQUIRE(renderer.value()->render(reverse.value()).ok());
    REQUIRE(std::abs(graphics->getPixel(160, 90).r - nearColor.r) < 0.02f);
    auto farOnly = SphereFrame::build(std::span(centers).first(1), std::span(appearance).first(1), {}, view, 2);
    REQUIRE(farOnly.ok());
    // The script loop owns present; exercise the same split lifecycle here.
    REQUIRE(renderer.value()->draw(farOnly.value()).ok());
    graphics->present();
    REQUIRE(graphics->getPixel(160, 90).b > 0.7f);
    REQUIRE(graphics->getPixel(160, 90).b > graphics->getPixel(160, 90).r + 0.3f);
    captureSphereView(*graphics, "sphere-far-revealed.ppm");
    auto invalid = SphereFrame::build(centers, appearance, {}, view, 1);
    REQUIRE(!invalid.ok());
    REQUIRE(renderer.value()->render(first.value()).ok());
    REQUIRE(graphics->getPixel(160, 90).r > 0.7f);
    auto moved = std::move(first.value());
    REQUIRE(!renderer.value()->render(first.value()).ok());
    REQUIRE(!renderer.value()->draw(first.value()).ok());
    REQUIRE(renderer.value()->render(moved).ok());
    // Check both viewport edges, both Y directions and camera-inside full coverage.
    // Compare away from a declared 0.001-unit silhouette uncertainty band.
    for (const auto& center : std::array<std::array<int64_t, 3>, 3>{{{15, 6, 10}, {-15, -6, 10}, {0, 0, 0}}}) {
        const std::array<CellPosition, 1>     sampleCenters{{{center, {}}}};
        const std::array<SphereAppearance, 1> sampleAppearance{{{2, 900, {0.8f, 0.1f, 0.1f}}}};
        auto sampleFrame = SphereFrame::build(sampleCenters, sampleAppearance, {}, view, 1);
        REQUIRE(sampleFrame.ok());
        REQUIRE(renderer.value()->render(sampleFrame.value()).ok());
        std::unique_ptr<eve::image::ImageData> pixels(graphics->newImageData());
        REQUIRE(pixels != nullptr);
        const auto* bytes   = static_cast<const unsigned char*>(pixels->getData());
        std::size_t checked = 0;
        for (int y = 0; y < 180; ++y)
            for (int x = 0; x < 320; ++x) {
                std::array<double, 3> ray{(2.0 * (x + 0.5) / 320 - 1) * view.aspect, -(2.0 * (y + 0.5) / 180 - 1), 1};
                const double          norm = std::hypot(ray[0], ray[1], ray[2]);
                for (double& component : ray) component /= norm;
                const double perpendicular =
                    std::hypot(center[1] * ray[2] - center[2] * ray[1], center[2] * ray[0] - center[0] * ray[2],
                               center[0] * ray[1] - center[1] * ray[0]);
                if (std::abs(perpendicular - 2) < 0.001) continue;
                const double along = center[0] * ray[0] + center[1] * ray[1] + center[2] * ray[2];
                const bool   expected =
                    perpendicular < 2 && along + std::sqrt(std::max(0.0, 4 - perpendicular * perpendicular)) > 0;
                const bool actual = bytes[(static_cast<std::size_t>(y) * 320 + x) * 4] > 150;
                REQUIRE_EQ(actual, expected);
                ++checked;
            }
        REQUIRE(checked > 56000);
    }
    renderer.value().reset();
    window->close();
}
