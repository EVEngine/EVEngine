// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "module": "graphics_multiscale",
  "default": "render-window"
}
*/
// clang-format on
#include "common/config.h"
#include "graphics/multiscale/AngularView.h"
#include "graphics/multiscale/SphereView.h"
#include "spatial/multiscale/CellPosition.h"
#ifdef EVENGINE_WEBGPU
#include "graphics/webgpu/Graphics.h"
#else
#include "graphics/vulkan/Graphics.h"
#endif
#include <memory>
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

TEST_CASE("graphics.multiscale.providerFirstDestructionRevokesPreparedView") {
    using namespace eve::graphics::multiscale;
#ifdef EVENGINE_WEBGPU
    auto graphics    = std::make_unique<eve::graphics::webgpu::Graphics>();
    auto unsupported = SphereView::build(*graphics);
    REQUIRE(!unsupported.ok());
#else
    // Own this provider directly; never delete the ModuleManager singleton.
    auto graphics = std::make_unique<eve::graphics::vulkan::Graphics>();
    graphics->initHeadless(64, 64);
    auto view = SphereView::build(*graphics);
    REQUIRE(view.ok());
    auto frame = SphereFrame::build({}, {}, {}, AngularView{}, 1);
    REQUIRE(frame.ok());
    auto batch = SphereBatch::prepare(*graphics, frame.value());
    REQUIRE(batch.ok());
    REQUIRE(!batch.value()->draw().ok());
    REQUIRE(!view.value()->draw(frame.value()).ok());
    auto lifetime = graphics->resourceLifetime();
    REQUIRE(!lifetime.expired());
    graphics.reset();
    REQUIRE(lifetime.expired());
    auto staleDraw = view.value()->draw(frame.value());
    REQUIRE(!staleDraw.ok());
    auto staleRender = view.value()->render(frame.value());
    REQUIRE(!staleRender.ok());
    REQUIRE(!batch.value()->draw().ok());
    // Neither failure nor consumer destruction may touch the freed provider/resources.
    REQUIRE(!batch.value()->update(frame.value()).ok());
    view.value().reset();
    batch.value().reset();
#endif
}
