#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "devtools/RenderFlow.hpp"
#include "common/RenderTrace.h"
#include "common/Exception.h"

#include <string>

using namespace eve::dev;

TEST_CASE("devtools.renderflow.passAndBindSlice") {
    RenderFlow flow;
    flow.frameBegin();
    flow.passBegin("RenderSystem2D");
    flow.target("screen");
    flow.bind("texture", "hero");
    flow.draw("drawTexturedRectShaderUV", "hero");
    flow.error("draw failed: bad descriptor");
    flow.passEnd("RenderSystem2D");
    flow.frameEnd();

    RenderSliceResult slice = flow.sliceBackward({});
    CHECK(!slice.passes.empty());
    CHECK_EQ(slice.passes.back(), std::string("RenderSystem2D"));

    bool sawBind = false, sawDraw = false, sawError = false;
    for (const auto& e : slice.path) {
        if (e.kind == RenderEventKind::Bind && e.detail == "hero") sawBind = true;
        if (e.kind == RenderEventKind::Draw) sawDraw = true;
        if (e.kind == RenderEventKind::Error) sawError = true;
    }
    CHECK(sawBind);
    CHECK(sawDraw);
    CHECK(sawError);

    const std::string report = flow.formatErrorReport("draw failed: bad descriptor");
    CHECK(report.find("Render Pipeline Trace") != std::string::npos);
    CHECK(report.find("RenderSystem2D") != std::string::npos);
    CHECK(report.find("hero") != std::string::npos);
}

TEST_CASE("devtools.renderflow.ringBuffer") {
    RenderFlow flow;
    flow.setMaxEvents(16);
    for (int i = 0; i < 40; ++i) flow.draw("drawSolidRect", "solid");
    CHECK_EQ(flow.eventCount(), 16u);
    CHECK(flow.event(flow.events().front().id - 1) == nullptr);
}

TEST_CASE("devtools.renderflow.hookViaException") {
    RenderFlow flow;
    eve::debug::setRenderTracer(&flow);
    eve::debug::rtFrameBegin();
    eve::debug::rtPassBegin("RenderSystem3D");
    eve::debug::rtBind("mesh", "rock");
    eve::debug::rtDraw("drawMeshShader", "default");
    try {
        throw eve::Exception("failed to create mesh3d-style pipeline");
    } catch (const eve::Exception& e) {
        // Exception ctor already called rtError via the active tracer.
        const std::string report = flow.formatErrorReport(e.what());
        CHECK(report.find("Render Pipeline Trace") != std::string::npos);
        CHECK(report.find("RenderSystem3D") != std::string::npos);
        const bool hasDrawOrBind =
            report.find("drawMeshShader") != std::string::npos ||
            report.find("rock") != std::string::npos;
        CHECK(hasDrawOrBind);
    }
    eve::debug::setRenderTracer(nullptr);
    CHECK(eve::debug::renderTracer() == nullptr);
}
