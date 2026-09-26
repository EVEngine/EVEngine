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

TEST_CASE("graphics.displayOutput.packNitsRoundTrip") {
    float paper = 0.f, peak = 0.f;
    unpackNits(packNits(200.f, 1000.f), paper, peak);
    CHECK(std::abs(paper - 200.f) < 0.15f);
    CHECK(std::abs(peak - 1000.f) < 1.f);

    unpackNits(packNits(80.f, 200.f), paper, peak);
    CHECK(std::abs(paper - 80.f) < 0.15f);
    CHECK(peak >= paper);
}

TEST_CASE("graphics.displayOutput.encodeHdr10PaperWhiteNearMidCode") {
    float r = 1.f, g = 1.f, b = 1.f;
    encodeHdr10(r, g, b, 200.f, 1000.f);
    // Paper white at 200 nits is a mid PQ code, not near 0 or 1.
    CHECK(r > 0.4f);
    CHECK(r < 0.7f);
    CHECK(std::abs(r - g) < 1e-4f);
    CHECK(std::abs(g - b) < 1e-4f);
    CHECK(linearToPq(0.f) == 0.f);
    CHECK(linearToPq(1.f) > 0.9f);
}

TEST_CASE("graphics.displayOutput.acesPreservesHdrHeadroom") {
    float r = 8.f, g = 8.f, b = 8.f;
    acesToDisplayLinear(r, g, b, 200.f, 1000.f);
    CHECK(r > 1.f);
    CHECK(r <= 1000.f / 200.f + 1e-3f);
}

TEST_CASE("graphics.displayOutput.resolveTintModes") {
    const Color sdr = sceneResolveTint(true, ActiveColorSpace::Sdr, false, 200.f, 1000.f);
    CHECK(sdr.r >= 0.5f);
    CHECK(std::abs(sdr.g - 0.f) < 1e-5f);
    CHECK(sdr.a >= 65536.f);

    const Color hdr10 = sceneResolveTint(true, ActiveColorSpace::Hdr10, false, 200.f, 1000.f);
    CHECK(std::abs(hdr10.g - 2.f) < 1e-5f);
    CHECK(hdr10.a < 1.f);

    const Color ui = uiResolveTint(ActiveColorSpace::ScRgb, 200.f, 1000.f);
    CHECK(ui.r < 0.f);
    CHECK(std::abs(ui.g - 1.f) < 1e-5f);
}

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
