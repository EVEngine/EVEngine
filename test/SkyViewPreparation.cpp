#include <vector>
#include "Fixtures.h"
#include "graphics/Canvas.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/ViewPreparation.h"
#include "zeroerr/unittest.h"

TEST_CASE("graphics.view preparation failures cancel dispatch and registrations detach") {
    using namespace eve::graphics;
    GfxFixture fixture(16, 16, true);
    auto&      gfx = *fixture.gfx;
    REQUIRE(!addViewPreparation({}).ok());
    REQUIRE(detail::prepareViewResources(gfx, glm::mat4(1), glm::vec3(0)).ok());
    std::vector<int> order;
    auto             first = addViewPreparation([&](Graphics&, const glm::mat4&, const glm::vec3&) {
        order.push_back(1);
        auto recursive = detail::prepareViewResources(gfx, glm::mat4(1), glm::vec3(0));
        REQUIRE(!recursive.ok());
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::Failed, "injected"));
    });
    REQUIRE(first.ok());
    auto second = addViewPreparation([&](Graphics&, const glm::mat4&, const glm::vec3&) {
        order.push_back(2);
        return eve::Result<void>::success();
    });
    REQUIRE(second.ok());
    auto failed = detail::prepareViewResources(gfx, glm::mat4(1), glm::vec3(0));
    REQUIRE(!failed.ok());
    REQUIRE(order.size() == 1);
    REQUIRE(order.front() == 1);
    removeViewPreparation(first.value());
    order.clear();
    REQUIRE(detail::prepareViewResources(gfx, glm::mat4(1), glm::vec3(0)).ok());
    REQUIRE(order.size() == 1);
    REQUIRE(order.front() == 2);
    removeViewPreparation(second.value());
    removeViewPreparation(second.value());
    order.clear();
    REQUIRE(detail::prepareViewResources(gfx, glm::mat4(1), glm::vec3(0)).ok());
    REQUIRE(order.empty());
}

TEST_CASE("graphics.view preparation runs before offscreen target opens") {
    using namespace eve::graphics;
    GfxFixture fixture(16, 16, true);
    auto*      gfx            = fixture.gfx;
    auto*      preparedTarget = gfx->newHDRCanvas(8, 8);
    auto*      destination    = gfx->newHDRCanvas(16, 16);
    auto*      camera         = Camera3D::createCamera();
    camera->setEye(0, 2, 0);
    camera->setTarget(1, 2, 0);
    int  calls        = 0;
    auto registration = addViewPreparation([&](Graphics& provider, const glm::mat4&, const glm::vec3& eye) {
        REQUIRE(&provider == gfx);
        REQUIRE(eye.y == 2);
        // A nested destination would throw: this preparation must precede it.
        provider.begin3DFrameToCanvas(preparedTarget);
        provider.end3DFrameToCanvas();
        ++calls;
        return eve::Result<void>::success();
    });
    REQUIRE(registration.ok());
    RenderSystem3D::renderToCanvas(*gfx, destination, camera);
    REQUIRE(calls == 1);
    removeViewPreparation(registration.value());
    RenderSystem3D::renderToCanvas(*gfx, destination, camera);
    REQUIRE(calls == 1);
}
