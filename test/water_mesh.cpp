#include "zeroerr/unittest.h"

#include "graphics/Water.h"
#include "graphics/Graphics.h"
#include "graphics/Texture.h"
#include "window/Window.h"

using namespace eve::graphics;

TEST_CASE("graphics.WaterMesh.pcgPlaneAndCircleCounts") {
    WaterMeshSettings plane;
    plane.sizeX = 8.0F; plane.sizeZ = 4.0F; plane.densityX = 40.0F; plane.densityY = 40.0F;
    auto planeTriangles = calculateWaterMeshTriangles(plane);
    REQUIRE(static_cast<bool>(planeTriangles));
    CHECK(planeTriangles.value() == 64);

    WaterMeshSettings circle;
    circle.type = WaterMeshType::Circle;
    circle.sizeX = 8.0F; circle.sizeZ = 8.0F; circle.densityX = 40.0F; circle.densityY = 40.0F;
    auto circleTriangles = calculateWaterMeshTriangles(circle);
    REQUIRE(static_cast<bool>(circleTriangles));
    CHECK(circleTriangles.value() == 96);
}

TEST_CASE("graphics.WaterMesh.rejectsInvalidAndExcessiveSettings") {
    WaterMeshSettings settings;
    settings.sizeX = 0.0F;
    CHECK(!calculateWaterMeshTriangles(settings));
    settings.sizeX = 100000.0F;
    CHECK(!calculateWaterMeshTriangles(settings));
    settings.sizeX = 8.0F;
    settings.type = static_cast<WaterMeshType>(9);
    CHECK(!calculateWaterMeshTriangles(settings));
}

TEST_CASE("graphics.WaterGradient.bakesAndPreservesTextureOnFailure") {
    auto* window = eve::window::Window::create();
    auto* graphics = eve::graphics::Graphics::create();
    REQUIRE(window != nullptr); REQUIRE(graphics != nullptr);
    eve::window::WindowSettings windowSettings; windowSettings.width = 64; windowSettings.height = 64;
    REQUIRE(window->setWindowSettings(windowSettings));
    Water* water = graphics->newWater(); REQUIRE(water != nullptr);
    WaterDepthGradient gradient;
    REQUIRE(static_cast<bool>(gradient.addColorStop(0.0F, 0.05F, 0.5F, 0.7F)));
    REQUIRE(static_cast<bool>(gradient.addColorStop(1.0F, 0.0F, 0.04F, 0.12F)));
    REQUIRE(static_cast<bool>(gradient.addAlphaStop(0.0F, 0.35F)));
    REQUIRE(static_cast<bool>(gradient.addAlphaStop(1.0F, 1.0F)));
    auto baked = water->setDepthGradient(gradient, 8); REQUIRE(static_cast<bool>(baked));
    CHECK(baked.value() == 64);
    auto* published = water->getDepthGradientTexture(); REQUIRE(published != nullptr);
    CHECK(published->getWidth() == 8); CHECK(published->getHeight() == 8);
    auto failed = water->setDepthGradient(gradient, 1); CHECK(!failed);
    CHECK(water->getDepthGradientTexture() == published);
    delete water; window->close();
}
