#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cmath>

#include "graphics/DisplayOutputEncoding.h"
#include "graphics/Graphics.h"

using eve::graphics::Color;
using eve::graphics::Graphics;
using eve::graphics::display::ActiveColorSpace;
using eve::graphics::display::acesToDisplayLinear;
using eve::graphics::display::encodeHdr10;
using eve::graphics::display::linearToPq;
using eve::graphics::display::packNits;
using eve::graphics::display::sceneResolveTint;
using eve::graphics::display::uiResolveTint;
using eve::graphics::display::unpackNits;


TEST_CASE("graphics.displayOutput.apiValidation") {
    auto *gfx = Graphics::create();
    REQUIRE(gfx != nullptr);
    if (!gfx->isHeadless()) gfx->initHeadless(64, 64);

    CHECK(gfx->setDisplayOutputMode(Graphics::DisplayOutputMode::Sdr).ok());
    CHECK(gfx->setDisplayOutputMode(Graphics::DisplayOutputMode::Auto).ok());
    CHECK_EQ(gfx->getDisplayOutputMode(), Graphics::DisplayOutputMode::Auto);

    // Vulkan accepts the preference even when the surface cannot present HDR.
    // WebGPU rejects strict HDR modes as Unsupported.
    const auto hdr10 = gfx->setDisplayOutputMode(Graphics::DisplayOutputMode::Hdr10);
    if (gfx->getBackendName() == "vulkan") {
        CHECK(hdr10.ok());
        CHECK_EQ(gfx->getDisplayOutputMode(), Graphics::DisplayOutputMode::Hdr10);
    } else {
        CHECK(!hdr10.ok());
        CHECK_EQ(gfx->getDisplayOutputMode(), Graphics::DisplayOutputMode::Auto);
    }
    // Headless has no swapchain, so active present stays SDR.
    CHECK(!gfx->isDisplayHdrActive());
    CHECK_EQ(gfx->getActiveDisplayColorSpace(), Graphics::DisplayColorSpace::Sdr);

    CHECK(gfx->setDisplayHdrCalibration(180.f, 800.f).ok());
    CHECK(std::abs(gfx->getDisplayPaperWhiteNits() - 180.f) < 1e-3f);
    CHECK(std::abs(gfx->getDisplayPeakNits() - 800.f) < 1e-3f);
    CHECK(!gfx->setDisplayHdrCalibration(NAN, 1000.f).ok());

    auto support = gfx->queryDisplayOutputSupport();
    // Headless has no present surface; windowed backends report at least SDR.
    if (gfx->isHeadless()) {
        CHECK(!support.ok());
    } else {
        REQUIRE(support.ok());
        CHECK(support.value().sdr);
    }
}
